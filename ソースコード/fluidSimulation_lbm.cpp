#include "fluidSimulation.h"
#include "renderer.h"
#include <vector>
#include <cassert>
#include <cmath>

void FluidSimulation::LBM_Init()
{
    mGravity = Vector3(0.0f, -0.01f, 0.0f);
    mTau = 0.55f;

    mWeights[0] = 1.0f / 3.0f;
    mWeights[1] = mWeights[2] = mWeights[3] = mWeights[4] = mWeights[5] = mWeights[6] = 1.0f / 18.0f;
    for (int i = 7; i < Q; ++i) mWeights[i] = 1.0f / 36.0f;

    int temp_dirs[Q][3] = {
        {0,0,0},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},
        {1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},{1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},
        {0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1}
    };
    memcpy(mDirs, temp_dirs, sizeof(mDirs));

    size_t gridSize = (size_t)NX * NY * NZ;
    mF.assign(gridSize * Q, 0.0f);
    mF_new.assign(gridSize * Q, 0.0f);
    mLbmRho.assign(gridSize, 1.0f);
    mLbmVel.assign(gridSize, Vector3(0.0f,0.0f,0.0f));
    mIsSolid.assign(gridSize, 0);

    for (int z = 0; z < NZ; ++z) for (int y = 0; y < NY; ++y) for (int x = 0; x < NX; ++x) {
        size_t index = (size_t)z * NY * NX + (size_t)y * NX + x;
        mLbmVel[index] = Vector3(0,0,0);
        mLbmRho[index] = 1.0f;
        mIsSolid[index] = (x == 0 || x == NX-1 || y == 0 || y == NY-1 || z == 0 || z == NZ-1) ? 1 : 0;
        float u_sq = mLbmVel[index].lengthSq();
        for (int i = 0; i < Q; ++i) {
            float u_dot_c = mLbmVel[index].x * mDirs[i][0] + mLbmVel[index].y * mDirs[i][1] + mLbmVel[index].z * mDirs[i][2];
            float feq = mWeights[i] * mLbmRho[index] * (1.0f + 3.0f * u_dot_c + 4.5f * u_dot_c * u_dot_c - 1.5f * u_sq);
            mF[index * Q + i] = feq;
        }
    }

    // GPU ƒŠƒ\[ƒXì¬
    ID3D11Device* device = Renderer::GetDevice();
    HRESULT hr = S_OK;

    Renderer::CreateComputeShader(&m_pLBM_CS, "lbm_step.cso");
    Renderer::CreateComputeShader(&m_pAdvectParticles_CS, "advect_particles.cso");
    Renderer::CreateComputeShader(&m_pParticleCollideBuild_CS, "particle_collide_build.cso");
    Renderer::CreateComputeShader(&m_pParticleCollideResolve_CS, "particle_collide_resolve.cso");

    // collision grid resolution based on current smoothing radius
    {
        float cellSize = mCollideCellSize;
        int gx = std::max(1, static_cast<int>(std::ceil(mBoundsSize.x / cellSize)));
        int gy = std::max(1, static_cast<int>(std::ceil(mBoundsSize.y / cellSize)));
        int gz = std::max(1, static_cast<int>(std::ceil(mBoundsSize.z / cellSize)));
        mCollideGridX = gx; mCollideGridY = gy; mCollideGridZ = gz;
        UINT cellCount = gx * gy * gz;
        UINT maxPerCell = (UINT)mCollideMaxPerCell;

        // cellCount buffer
        {
            D3D11_BUFFER_DESC cbd = {};
            cbd.Usage = D3D11_USAGE_DEFAULT;
            cbd.ByteWidth = sizeof(UINT) * cellCount;
            cbd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
            cbd.CPUAccessFlags = 0;
            cbd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            cbd.StructureByteStride = sizeof(UINT);

            std::vector<UINT> zeros(cellCount, 0u);
            D3D11_SUBRESOURCE_DATA cinit = { zeros.data(), 0, 0 };
            hr = device->CreateBuffer(&cbd, &cinit, &m_pCellCountBuffer);
            assert(SUCCEEDED(hr));

            D3D11_UNORDERED_ACCESS_VIEW_DESC cuavd = {};
            cuavd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            cuavd.Format = DXGI_FORMAT_UNKNOWN;
            cuavd.Buffer.NumElements = cellCount;
            hr = device->CreateUnorderedAccessView(m_pCellCountBuffer, &cuavd, &m_pCellCount_UAV);
            assert(SUCCEEDED(hr));

            D3D11_SHADER_RESOURCE_VIEW_DESC csrvd = {};
            csrvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            csrvd.Format = DXGI_FORMAT_UNKNOWN;
            csrvd.Buffer.FirstElement = 0;
            csrvd.Buffer.NumElements = cellCount;
            hr = device->CreateShaderResourceView(m_pCellCountBuffer, &csrvd, &m_pCellCount_SRV);
            assert(SUCCEEDED(hr));
        }

        // cellList buffer
        {
            UINT totalSlots = cellCount * maxPerCell;
            D3D11_BUFFER_DESC lbd = {};
            lbd.Usage = D3D11_USAGE_DEFAULT;
            lbd.ByteWidth = sizeof(UINT) * totalSlots;
            lbd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
            lbd.CPUAccessFlags = 0;
            lbd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            lbd.StructureByteStride = sizeof(UINT);

            std::vector<UINT> lzeros(totalSlots, 0xffffffffu);
            D3D11_SUBRESOURCE_DATA linit = { lzeros.data(), 0, 0 };
            hr = device->CreateBuffer(&lbd, &linit, &m_pCellListBuffer);
            assert(SUCCEEDED(hr));

            D3D11_UNORDERED_ACCESS_VIEW_DESC luavd = {};
            luavd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            luavd.Format = DXGI_FORMAT_UNKNOWN;
            luavd.Buffer.NumElements = totalSlots;
            hr = device->CreateUnorderedAccessView(m_pCellListBuffer, &luavd, &m_pCellList_UAV);
            assert(SUCCEEDED(hr));

            D3D11_SHADER_RESOURCE_VIEW_DESC lsrvd = {};
            lsrvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            lsrvd.Format = DXGI_FORMAT_UNKNOWN;
            lsrvd.Buffer.FirstElement = 0;
            lsrvd.Buffer.NumElements = totalSlots;
            hr = device->CreateShaderResourceView(m_pCellListBuffer, &lsrvd, &m_pCellList_SRV);
            assert(SUCCEEDED(hr));
        }

        // collide constants buffer
        {
            D3D11_BUFFER_DESC cbDesc = {};
            cbDesc.Usage = D3D11_USAGE_DYNAMIC;
            cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            cbDesc.ByteWidth = (UINT)sizeof(CollideConstants);
            cbDesc.MiscFlags = 0;
            hr = device->CreateBuffer(&cbDesc, nullptr, &m_pCollide_ConstantBuffer);
            assert(SUCCEEDED(hr));
        }
    }

    // generic buffer descs for LBM buffers
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;

    // F buffers (ping-pong)
    bufferDesc.ByteWidth = (UINT)(sizeof(float) * mF.size());
    bufferDesc.StructureByteStride = sizeof(float);
    uavDesc.Buffer.NumElements = (UINT)mF.size();
    srvDesc.Buffer.NumElements = (UINT)mF.size();
    D3D11_SUBRESOURCE_DATA initData = { mF.data(), 0, 0 };
    for (int i = 0; i < 2; ++i) {
        hr = device->CreateBuffer(&bufferDesc, (i == 0) ? &initData : nullptr, &m_pLBM_F_Buffer[i]);
        assert(SUCCEEDED(hr));
        hr = device->CreateUnorderedAccessView(m_pLBM_F_Buffer[i], &uavDesc, &m_pLBM_F_UAV[i]);
        assert(SUCCEEDED(hr));
        hr = device->CreateShaderResourceView(m_pLBM_F_Buffer[i], &srvDesc, &m_pLBM_F_SRV[i]);
        assert(SUCCEEDED(hr));
    }

    // Rho
    bufferDesc.ByteWidth = (UINT)(sizeof(float) * mLbmRho.size());
    bufferDesc.StructureByteStride = sizeof(float);
    uavDesc.Buffer.NumElements = (UINT)mLbmRho.size();
    initData.pSysMem = mLbmRho.data();
    hr = device->CreateBuffer(&bufferDesc, &initData, &m_pLBM_Rho_Buffer);
    assert(SUCCEEDED(hr));
    hr = device->CreateUnorderedAccessView(m_pLBM_Rho_Buffer, &uavDesc, &m_pLBM_Rho_UAV);
    assert(SUCCEEDED(hr));
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC rhoSrvDesc = {};
        rhoSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        rhoSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
        rhoSrvDesc.Buffer.NumElements = (UINT)mLbmRho.size();
        hr = device->CreateShaderResourceView(m_pLBM_Rho_Buffer, &rhoSrvDesc, &m_pLBM_Rho_SRV);
        assert(SUCCEEDED(hr));
    }

    // Vel
    bufferDesc.ByteWidth = (UINT)(sizeof(Vector3) * mLbmVel.size());
    bufferDesc.StructureByteStride = sizeof(Vector3);
    uavDesc.Buffer.NumElements = (UINT)mLbmVel.size();
    srvDesc.Buffer.NumElements = (UINT)mLbmVel.size();
    initData.pSysMem = mLbmVel.data();
    hr = device->CreateBuffer(&bufferDesc, &initData, &m_pLBM_Vel_Buffer);
    assert(SUCCEEDED(hr));
    hr = device->CreateUnorderedAccessView(m_pLBM_Vel_Buffer, &uavDesc, &m_pLBM_Vel_UAV);
    assert(SUCCEEDED(hr));
    hr = device->CreateShaderResourceView(m_pLBM_Vel_Buffer, &srvDesc, &m_pLBM_Vel_SRV);
    assert(SUCCEEDED(hr));

    // IsSolid
    bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.ByteWidth = (UINT)(sizeof(int) * mIsSolid.size());
    bufferDesc.StructureByteStride = sizeof(int);
    srvDesc.Buffer.NumElements = (UINT)mIsSolid.size();
    initData.pSysMem = mIsSolid.data();
    hr = device->CreateBuffer(&bufferDesc, &initData, &m_pLBM_IsSolid_Buffer);
    assert(SUCCEEDED(hr));
    hr = device->CreateShaderResourceView(m_pLBM_IsSolid_Buffer, &srvDesc, &m_pLBM_IsSolid_SRV);
    assert(SUCCEEDED(hr));

    // constant buffers
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.ByteWidth = sizeof(LBMConstants);
    hr = device->CreateBuffer(&cbDesc, nullptr, &m_pLBM_ConstantBuffer);
    assert(SUCCEEDED(hr));
    cbDesc.ByteWidth = sizeof(AdvectConstants);
    hr = device->CreateBuffer(&cbDesc, nullptr, &m_pAdvect_ConstantBuffer);
    assert(SUCCEEDED(hr));
    cbDesc.ByteWidth = sizeof(BuildInstanceConstants);
    hr = device->CreateBuffer(&cbDesc, nullptr, &m_pBuildInstance_ConstantBuffer);
    assert(SUCCEEDED(hr));

    // visualize CB
    Renderer::CreateComputeShader(&m_pVisualizeSlice_CS, "visualize_lbm_slice.cso");
    D3D11_BUFFER_DESC vizCbDesc = {};
    vizCbDesc.Usage = D3D11_USAGE_DYNAMIC;
    vizCbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    vizCbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    // The HLSL VizCB requires 64 bytes (matches shader layout). Use 64 to avoid "constant buffer too small" warnings.
    vizCbDesc.ByteWidth = 64;
    hr = device->CreateBuffer(&vizCbDesc, nullptr, &m_pVizCB);
    assert(SUCCEEDED(hr));
}