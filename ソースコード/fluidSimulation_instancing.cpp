#include "fluidSimulation.h"
#include "renderer.h"
#include "manager.h"
#include "camera.h"
#include <vector>
#include <cassert>
#include "scene.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

struct CircleInstanceData
{
    DirectX::XMFLOAT4 color;
    DirectX::XMFLOAT4X4 world;
};

void FluidSimulation::InitInstancing()
{
    ID3D11Device* device = Renderer::GetDevice();
    HRESULT hr; 

    Renderer::CreateComputeShader(&m_pBuildInstance_CS, "build_instance_buffer.cso");

    const int kCircleSegments = 8;
    std::vector<VERTEX_3D> vertices(kCircleSegments * 3);
    mCircleVertexCount = kCircleSegments * 3;
    int vertexIndex = 0;
    for (int i = 0; i < kCircleSegments; ++i) {
        float angle1 = (float)i / kCircleSegments * 2.0f * DirectX::XM_PI;
        float x1 = cosf(angle1), y1 = sinf(angle1);
        float angle2 = (float)(i + 1) / kCircleSegments * 2.0f * DirectX::XM_PI;
        float x2 = cosf(angle2), y2 = sinf(angle2);

        vertices[vertexIndex++] = { {0.0f,0.0f,0.0f}, {0.0f,0.0f,-1.0f}, {1.0f,1.0f,1.0f,1.0f}, {0.5f, 0.5f} };
        vertices[vertexIndex++] = { {x2,y2,0.0f},      {0.0f,0.0f,-1.0f}, {1.0f,1.0f,1.0f,1.0f}, {x2 * 0.5f + 0.5f, -y2 * 0.5f + 0.5f} };
        vertices[vertexIndex++] = { {x1,y1,0.0f},      {0.0f,0.0f,-1.0f}, {1.0f,1.0f,1.0f,1.0f}, {x1 * 0.5f + 0.5f, -y1 * 0.5f + 0.5f} };
    }

    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(VERTEX_3D) * mCircleVertexCount;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem = vertices.data();
    hr = device->CreateBuffer(&bd, &sd, &mCircleVertexBuffer);
    assert(SUCCEEDED(hr));

    UINT instanceCount = static_cast<UINT>(mParticles.count);

    bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(CircleInstanceData) * instanceCount;
    bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(CircleInstanceData);
    hr = device->CreateBuffer(&bd, NULL, &m_pCircleInstanceBuffer_Structured);
    assert(SUCCEEDED(hr));

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.Buffer.NumElements = instanceCount;
    hr = device->CreateUnorderedAccessView(m_pCircleInstanceBuffer_Structured, &uavDesc, &m_pCircleInstanceBuffer_UAV);
    assert(SUCCEEDED(hr));

    bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(CircleInstanceData) * instanceCount;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.MiscFlags = 0;
    bd.StructureByteStride = 0;
    hr = device->CreateBuffer(&bd, NULL, &mCircleInstanceBuffer);
    assert(SUCCEEDED(hr));

    Renderer::CreatePixelShader(&mCirclePixelShader, "particlePS.cso");

    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,   0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA,   0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,   0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA,   0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA,   0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,      0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA,   0 },

        { "COLOR",    1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 }
    };
    Renderer::CreateVertexShader(&mCircleVertexShader, &mCircleInputLayout, "shaderInstancing.cso", layout, ARRAYSIZE(layout));

    // obstacle buffer
    {
        D3D11_BUFFER_DESC obDesc = {};
        obDesc.Usage = D3D11_USAGE_DEFAULT;
        obDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        obDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        obDesc.ByteWidth = sizeof(GPUObstacle) * MAX_OBSTACLES;
        obDesc.StructureByteStride = sizeof(GPUObstacle);
        hr = device->CreateBuffer(&obDesc, nullptr, &m_pObstacle_Buffer);
        assert(SUCCEEDED(hr));
        D3D11_SHADER_RESOURCE_VIEW_DESC obSrvDesc = {};
        obSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        obSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
        obSrvDesc.Buffer.FirstElement = 0;
        obSrvDesc.Buffer.NumElements = MAX_OBSTACLES;
        hr = device->CreateShaderResourceView(m_pObstacle_Buffer, &obSrvDesc, &m_pObstacle_SRV);
        assert(SUCCEEDED(hr));
    }

    // particle position/vel/type buffers creation (kept minimal)
    {
        std::vector<DirectX::XMFLOAT3> posData(instanceCount);
        std::vector<DirectX::XMFLOAT3> velData(instanceCount);
        std::vector<UINT>              typeData(instanceCount);
        size_t useCount = std::min<size_t>(instanceCount, mParticles.count);
        for (size_t i = 0; i < useCount; ++i) {
            posData[i] = DirectX::XMFLOAT3(mParticles.positions[i].x, mParticles.positions[i].y, mParticles.positions[i].z);
            velData[i] = DirectX::XMFLOAT3(mParticles.velocities[i].x, mParticles.velocities[i].y, mParticles.velocities[i].z);
            typeData[i] = static_cast<UINT>(mParticles.fluidTypes[i]);
        }

        D3D11_BUFFER_DESC pbDesc = {};
        pbDesc.Usage = D3D11_USAGE_DEFAULT;
        pbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        pbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

        pbDesc.ByteWidth = sizeof(DirectX::XMFLOAT3) * instanceCount;
        pbDesc.StructureByteStride = sizeof(DirectX::XMFLOAT3);
        D3D11_SUBRESOURCE_DATA initPos = { posData.data(), 0, 0 };
        for (int i = 0; i < 2; ++i)
        {
            HRESULT hr2 = device->CreateBuffer(&pbDesc, &initPos, &m_pParticlePos_Buffer[i]);
            assert(SUCCEEDED(hr2));
            D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
            srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            srvd.Format = DXGI_FORMAT_UNKNOWN;
            srvd.Buffer.FirstElement = 0;
            srvd.Buffer.NumElements = instanceCount;
            hr2 = device->CreateShaderResourceView(m_pParticlePos_Buffer[i], &srvd, &m_pParticlePos_SRV[i]);
            assert(SUCCEEDED(hr2));
            D3D11_UNORDERED_ACCESS_VIEW_DESC uavd = {};
            uavd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavd.Format = DXGI_FORMAT_UNKNOWN;
            uavd.Buffer.NumElements = instanceCount;
            hr2 = device->CreateUnorderedAccessView(m_pParticlePos_Buffer[i], &uavd, &m_pParticlePos_UAV[i]);
            assert(SUCCEEDED(hr2));
        }

        D3D11_SUBRESOURCE_DATA initVel = { velData.data(), 0, 0 };
        pbDesc.ByteWidth = sizeof(DirectX::XMFLOAT3) * instanceCount;
        pbDesc.StructureByteStride = sizeof(DirectX::XMFLOAT3);
        for (int i = 0; i < 2; ++i)
        {
            HRESULT hrV = device->CreateBuffer(&pbDesc, &initVel, &m_pParticleVel_Buffer[i]);
            assert(SUCCEEDED(hrV));
            D3D11_SHADER_RESOURCE_VIEW_DESC srvdV = {};
            srvdV.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            srvdV.Format = DXGI_FORMAT_UNKNOWN;
            srvdV.Buffer.FirstElement = 0;
            srvdV.Buffer.NumElements = instanceCount;
            hrV = device->CreateShaderResourceView(m_pParticleVel_Buffer[i], &srvdV, &m_pParticleVel_SRV[i]);
            assert(SUCCEEDED(hrV));
            D3D11_UNORDERED_ACCESS_VIEW_DESC uavdV = {};
            uavdV.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavdV.Format = DXGI_FORMAT_UNKNOWN;
            uavdV.Buffer.NumElements = instanceCount;
            hrV = device->CreateUnorderedAccessView(m_pParticleVel_Buffer[i], &uavdV, &m_pParticleVel_UAV[i]);
            assert(SUCCEEDED(hrV));
        }

        D3D11_BUFFER_DESC tbDesc = {};
        tbDesc.Usage = D3D11_USAGE_DEFAULT;
        tbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        tbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        tbDesc.ByteWidth = sizeof(UINT) * instanceCount;
        tbDesc.StructureByteStride = sizeof(UINT);
        D3D11_SUBRESOURCE_DATA initType = { typeData.data(), 0, 0 };
        HRESULT hrt = device->CreateBuffer(&tbDesc, &initType, &m_pParticleType_Buffer);
        assert(SUCCEEDED(hrt));
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
            srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            srvd.Format = DXGI_FORMAT_UNKNOWN;
            srvd.Buffer.FirstElement = 0;
            srvd.Buffer.NumElements = instanceCount;
            hrt = device->CreateShaderResourceView(m_pParticleType_Buffer, &srvd, &m_pParticleType_SRV);
            assert(SUCCEEDED(hrt));
        }
    }
}

void FluidSimulation::UninitInstancing()
{
    if (mCircleVertexBuffer) { mCircleVertexBuffer->Release(); mCircleVertexBuffer = nullptr; }
    if (mCircleInstanceBuffer) { mCircleInstanceBuffer->Release(); mCircleInstanceBuffer = nullptr; }
    if (mCircleVertexShader) { mCircleVertexShader->Release(); mCircleVertexShader = nullptr; }
    if (mCirclePixelShader) { mCirclePixelShader->Release(); mCirclePixelShader = nullptr; }
    if (mCircleInputLayout) { mCircleInputLayout->Release(); mCircleInputLayout = nullptr; }

    if (m_pBuildInstance_CS) { m_pBuildInstance_CS->Release(); m_pBuildInstance_CS = nullptr; }
    if (m_pBuildInstance_ConstantBuffer) { m_pBuildInstance_ConstantBuffer->Release(); m_pBuildInstance_ConstantBuffer = nullptr; }
    if (m_pCircleInstanceBuffer_Structured) { m_pCircleInstanceBuffer_Structured->Release(); m_pCircleInstanceBuffer_Structured = nullptr; }
    if (m_pCircleInstanceBuffer_UAV) { m_pCircleInstanceBuffer_UAV->Release(); m_pCircleInstanceBuffer_UAV = nullptr; }
}

void FluidSimulation::DrawInstancing()
{
    size_t instanceCount = mParticles.count;
    if (instanceCount == 0) return;

    ID3D11DeviceContext* dc = Renderer::GetDeviceContext();
    if (!dc) return;

    if (!mCircleVertexBuffer || !mCircleInstanceBuffer || !mCircleVertexShader || !mCirclePixelShader || !mCircleInputLayout)
        return;

    if (m_pBuildInstance_CS && m_pCircleInstanceBuffer_UAV && m_pBuildInstance_ConstantBuffer && m_pCircleInstanceBuffer_Structured)
    {
        DirectX::XMMATRIX invView = DirectX::XMMatrixIdentity();
        camera* cam = Manager::GetScene()->GetGameObject<camera>();
        if (cam) {
            DirectX::XMMATRIX view = cam->GetViewMatrix();
            invView = DirectX::XMMatrixInverse(nullptr, view);
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(dc->Map(m_pBuildInstance_ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            BuildInstanceConstants* bc = (BuildInstanceConstants*)mapped.pData;
            bc->numParticles = static_cast<UINT>(instanceCount);
            if (mFluidProperties.size() > static_cast<size_t>(FluidType::Water)) {
                const Vector3& c = mFluidProperties[static_cast<int>(FluidType::Water)].color;
                bc->color_water = DirectX::XMFLOAT4(c.x, c.y, c.z, 1.0f);
            }
            else {
                bc->color_water = DirectX::XMFLOAT4(0.2f, 0.5f, 1.0f, 1.0f);
            }
            if (mFluidProperties.size() > static_cast<size_t>(FluidType::Air)) {
                const Vector3& c2 = mFluidProperties[static_cast<int>(FluidType::Air)].color;
                bc->color_air = DirectX::XMFLOAT4(c2.x, c2.y, c2.z, 1.0f);
            }
            else {
                bc->color_air = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            }
            bc->invView = invView;
            dc->Unmap(m_pBuildInstance_ConstantBuffer, 0);
        }

        UINT readIndex = static_cast<UINT>(m_Particle_PingPong_Index);
        ID3D11ShaderResourceView* cs_srvs[3] = {
            m_pParticlePos_SRV[readIndex],
            m_pParticleType_SRV,
            m_pParticleVel_SRV[readIndex]
        };

        dc->CSSetShader(m_pBuildInstance_CS, nullptr, 0);
        dc->CSSetConstantBuffers(0, 1, &m_pBuildInstance_ConstantBuffer);
        dc->CSSetShaderResources(0, 3, cs_srvs);
        ID3D11UnorderedAccessView* uavs[1] = { m_pCircleInstanceBuffer_UAV };
        dc->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

        UINT groups = static_cast<UINT>((instanceCount + 255u) / 256u);
        {
            char buf[256];
            sprintf_s(buf, sizeof(buf), "Dispatching BuildInstance CS ptr=%p groups=%u\n", (void*)m_pBuildInstance_CS, groups);
            OutputDebugStringA(buf);
        }
        dc->Dispatch(groups, 1, 1);

        ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
        dc->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
        ID3D11ShaderResourceView* nullSRVs[3] = { nullptr, nullptr, nullptr };
        dc->CSSetShaderResources(0, 3, nullSRVs);
        ID3D11Buffer* nullCBs[1] = { nullptr };
        dc->CSSetConstantBuffers(0, 1, nullCBs);
        dc->CSSetShader(nullptr, nullptr, 0);

        if (mCircleInstanceBuffer && m_pCircleInstanceBuffer_Structured) {
            dc->CopyResource(mCircleInstanceBuffer, m_pCircleInstanceBuffer_Structured);
        }
    }

    UINT strides[2] = { sizeof(VERTEX_3D), sizeof(CircleInstanceData) };
    UINT offsets[2] = { 0, 0 };
    ID3D11Buffer* buffers[2] = { mCircleVertexBuffer, mCircleInstanceBuffer };

    dc->IASetInputLayout(mCircleInputLayout);
    dc->IASetVertexBuffers(0, 2, buffers, strides, offsets);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    dc->VSSetShader(mCircleVertexShader, nullptr, 0);
    dc->PSSetShader(mCirclePixelShader, nullptr, 0);

    dc->DrawInstanced(mCircleVertexCount, static_cast<UINT>(instanceCount), 0, 0);

    dc->VSSetShader(nullptr, nullptr, 0);
    dc->PSSetShader(nullptr, nullptr, 0);
    ID3D11Buffer* nullBufs[2] = { nullptr, nullptr };
    dc->IASetVertexBuffers(0, 2, nullBufs, strides, offsets);
    dc->IASetInputLayout(nullptr);
}

void FluidSimulation::InitTriangleInstancing() { /* ‹óŽÀ‘• */ }
void FluidSimulation::UninitTriangleInstancing() { /* ‹óŽÀ‘• */ }
void FluidSimulation::DrawTriangleInstancing() { /* ‹óŽÀ‘• */ }