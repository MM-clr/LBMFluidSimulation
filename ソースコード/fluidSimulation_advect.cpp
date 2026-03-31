#include "fluidSimulation.h"
#include "renderer.h"
#include <algorithm>
#include <cassert>

void FluidSimulation::AdvectCombine()
{
    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    const size_t particleCount = mParticles.count;
    
    {
        UINT readIndex = static_cast<UINT>(m_Particle_PingPong_Index);
        if (!m_pParticlePos_SRV[readIndex] || !m_pParticleVel_SRV[readIndex]) {
            char buf[256];
            sprintf_s(buf, sizeof(buf), "AdvectCombine: NULL SRV detected readIndex=%u posSRV=%p velSRV=%p - skipping Advect\n",
                      readIndex, (void*)m_pParticlePos_SRV[readIndex], (void*)m_pParticleVel_SRV[readIndex]);
            OutputDebugStringA(buf);
            return;
        }
    }
    if (m_pAdvect_ConstantBuffer) {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        if (SUCCEEDED(context->Map(m_pAdvect_ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource))) {
            AdvectConstants* constants = (AdvectConstants*)mappedResource.pData;
            constants->numParticles = static_cast<UINT>(particleCount);
            constants->timeStep = mTimeStep;
            constants->boundaryDamping = mBoundaryDamping >= 0.0f ? mBoundaryDamping : 0.0f;
            constants->numObstacles = static_cast<UINT>(std::min<size_t>(mObstacles.size(), FluidSimulation::MAX_OBSTACLES));
            constants->boundsMin = DirectX::XMFLOAT3(mMinBounds.x, mMinBounds.y, mMinBounds.z);
            constants->boundsMax = DirectX::XMFLOAT3(mMaxBounds.x, mMaxBounds.y, mMaxBounds.z);
            constants->boundsSize = DirectX::XMFLOAT3(mBoundsSize.x, mBoundsSize.y, mBoundsSize.z);
            constants->gridSize.x = (UINT)NX; constants->gridSize.y = (UINT)NY; constants->gridSize.z = (UINT)NZ;
            constants->initialVelocity = DirectX::XMFLOAT3(mInitialVelocity.x, mInitialVelocity.y, mInitialVelocity.z);
            constants->wind = DirectX::XMFLOAT3(mWind.x, mWind.y, mWind.z);
            if (constants->boundsSize.x == 0.0f) constants->boundsSize.x = 1e-6f;
            if (constants->boundsSize.y == 0.0f) constants->boundsSize.y = 1e-6f;
            if (constants->boundsSize.z == 0.0f) constants->boundsSize.z = 1e-6f;
            context->Unmap(m_pAdvect_ConstantBuffer, 0);
        }
    }

    int readIndex = m_Particle_PingPong_Index;
    int writeIndex = 1 - m_Particle_PingPong_Index;

    context->CSSetShader(m_pAdvectParticles_CS, nullptr, 0);
    context->CSSetConstantBuffers(0, 1, &m_pAdvect_ConstantBuffer);

    ID3D11ShaderResourceView* srvs[] = {
        m_pLBM_Vel_SRV,
        m_pParticlePos_SRV[readIndex],
        m_pParticleVel_SRV[readIndex],
        m_pObstacle_SRV
    };
    context->CSSetShaderResources(0, 4, srvs);

    ID3D11UnorderedAccessView* uavs[] = {
        m_pParticlePos_UAV[writeIndex],
        m_pParticleVel_UAV[writeIndex]
    };
    context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);

    Renderer::DebugCheckSRVStride(m_pLBM_Vel_SRV, "Advect LBM Vel (t0)", sizeof(DirectX::XMFLOAT3));
    Renderer::DebugCheckSRVStride(m_pParticlePos_SRV[readIndex], "Advect particlePos (t1)", sizeof(DirectX::XMFLOAT3));
    Renderer::DebugCheckSRVStride(m_pParticleVel_SRV[readIndex], "Advect particleVel (t2)", sizeof(DirectX::XMFLOAT3));
    Renderer::DebugCheckSRVStride(m_pObstacle_SRV, "Advect obstacles (t3)", sizeof(GPUObstacle)); 

    context->Dispatch((UINT((particleCount + 255) / 256)), 1, 1);

    ID3D11UnorderedAccessView* nullUAVs[] = { nullptr, nullptr };
    context->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
    ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr, nullptr, nullptr };
    context->CSSetShaderResources(0, 4, nullSRVs);
    context->CSSetShader(nullptr, nullptr, 0);

    // í«â¡: ã≠êßâèú
    Renderer::UnbindAllShaderResources();
}