#include "fluidSimulation.h"
#include "renderer.h"
#include "manager.h"
#include "camera.h"
#include "scene.h"
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cmath>
#include <limits>
#include <DirectXMath.h>
#include <d3dcompiler.h>

void FluidSimulation::VisualizeLBMSlice(UINT sliceZ)
{
    // スライスインデックスを軸に応じてクランプ（安全策）
    UINT maxIndex = 0;
    switch (mLbmSliceAxis) {
    case AxisX: maxIndex = (NX > 0) ? (UINT)NX - 1 : 0; break;
    case AxisY: maxIndex = (NY > 0) ? (UINT)NY - 1 : 0; break;
    default:    maxIndex = (NZ > 0) ? (UINT)NZ - 1 : 0; break;
    }
    if (sliceZ > maxIndex) sliceZ = maxIndex;

    // デバッグログ（呼び出し状況）
    {
        char tmp[512];
        sprintf_s(tmp, sizeof(tmp),
            "VisualizeLBMSlice: slice=%u axis=%u m_pLBM_Rho_Buffer=%p m_pLBM_Vel_Buffer=%p m_pLbmVizTexture=%p mode=%u\n",
            sliceZ, (unsigned)mLbmSliceAxis, (void*)m_pLBM_Rho_Buffer, (void*)m_pLBM_Vel_Buffer, (void*)m_pLbmVizTexture, (unsigned)mLbmVizMode);
        OutputDebugStringA(tmp);
    }

    ID3D11Device* device = Renderer::GetDevice();
    ID3D11DeviceContext* ctx = Renderer::GetDeviceContext();
    // Ensure SRVs are up-to-date: if existing SRVs might reference old buffers, recreate from buffers.
    if (m_pLBM_Rho_SRV) { m_pLBM_Rho_SRV->Release(); m_pLBM_Rho_SRV = nullptr; }
    if (m_pLBM_Vel_SRV) { m_pLBM_Vel_SRV->Release(); m_pLBM_Vel_SRV = nullptr; }
    // Ensure SRVs are up-to-date: if existing SRVs might reference old buffers, recreate from buffers.
    if (!device || !ctx) {
        OutputDebugStringA("VisualizeLBMSlice: device or context is null.\n");
        return;
    }

    // 出力幅／高さをスライス軸に応じて決定
    UINT outW = 0, outH = 0;
    switch (mLbmSliceAxis) {
    case AxisX: outW = (UINT)NY; outH = (UINT)NZ; break;
    case AxisY: outW = (UINT)NX; outH = (UINT)NZ; break;
    default:    outW = (UINT)NX; outH = (UINT)NY; break;
    }
    if (outW == 0 || outH == 0) {
        OutputDebugStringA("VisualizeLBMSlice: invalid output size.\n");
        return;
    }

    // 出力テクスチャがなければ作る（SRV/UAV も）
    if (!m_pLbmVizTexture || m_vizWidth != outW || m_vizHeight != outH) {
        if (m_pLbmVizSRV) { m_pLbmVizSRV->Release(); m_pLbmVizSRV = nullptr; }
        if (m_pLbmVizUAV) { m_pLbmVizUAV->Release(); m_pLbmVizUAV = nullptr; }
        if (m_pLbmVizTexture) { m_pLbmVizTexture->Release(); m_pLbmVizTexture = nullptr; }

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = outW;
        td.Height = outH;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        HRESULT hr = device->CreateTexture2D(&td, nullptr, &m_pLbmVizTexture);
        if (FAILED(hr) || !m_pLbmVizTexture) {
            OutputDebugStringA("VisualizeLBMSlice: CreateTexture2D failed.\n");
            return;
        }

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        hr = device->CreateUnorderedAccessView(m_pLbmVizTexture, &uavDesc, &m_pLbmVizUAV);
        if (FAILED(hr) || !m_pLbmVizUAV) {
            OutputDebugStringA("VisualizeLBMSlice: CreateUnorderedAccessView failed.\n");
            m_pLbmVizTexture->Release(); m_pLbmVizTexture = nullptr;
            return;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        srvDesc.Texture2D.MipLevels = 1;
        hr = device->CreateShaderResourceView(m_pLbmVizTexture, &srvDesc, &m_pLbmVizSRV);
        if (FAILED(hr) || !m_pLbmVizSRV) {
            OutputDebugStringA("VisualizeLBMSlice: CreateShaderResourceView failed.\n");
            m_pLbmVizUAV->Release(); m_pLbmVizUAV = nullptr;
            m_pLbmVizTexture->Release(); m_pLbmVizTexture = nullptr;
            return;
        }

        m_vizWidth = outW;
        m_vizHeight = outH;
    }

    // GPU 可視化パス：LBM の GPU リソースが SRV として存在する場合は ComputeShader を使う
    bool haveRhoSRV = (m_pLBM_Rho_SRV != nullptr);
    bool haveVelSRV = (m_pLBM_Vel_SRV != nullptr);

    // SRV が無ければ、遅延で作る（バッファは structured buffer で作られている前提）
    const size_t cellCount = static_cast<size_t>(NX) * static_cast<size_t>(NY) * static_cast<size_t>(NZ);
    if (!haveRhoSRV && m_pLBM_Rho_Buffer) {
        D3D11_BUFFER_DESC bd = {};
        m_pLBM_Rho_Buffer->GetDesc(&bd);
        if (bd.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
            srvd.Format = DXGI_FORMAT_UNKNOWN;
            srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            srvd.Buffer.ElementOffset = 0;
            srvd.Buffer.ElementWidth = (UINT)(bd.ByteWidth / sizeof(float));
            if (SUCCEEDED(device->CreateShaderResourceView(m_pLBM_Rho_Buffer, &srvd, &m_pLBM_Rho_SRV))) {
                haveRhoSRV = true;
            } else {
                OutputDebugStringA("VisualizeLBMSlice: Create SRV for rho buffer failed.\n");
            }
        }
    }
    if (!haveVelSRV && m_pLBM_Vel_Buffer) {
        D3D11_BUFFER_DESC bd = {};
        m_pLBM_Vel_Buffer->GetDesc(&bd);
        if (bd.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
            srvd.Format = DXGI_FORMAT_UNKNOWN;
            srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            srvd.Buffer.ElementOffset = 0;
            srvd.Buffer.ElementWidth = (UINT)(bd.ByteWidth / sizeof(DirectX::XMFLOAT3));
            if (SUCCEEDED(device->CreateShaderResourceView(m_pLBM_Vel_Buffer, &srvd, &m_pLBM_Vel_SRV))) {
                haveVelSRV = true;
            } else {
                OutputDebugStringA("VisualizeLBMSlice: Create SRV for vel buffer failed.\n");
            }
        }
    }

    // GPU 経路: rho/vel 両方の SRV が揃っていれば ComputeShader 経路を使う
    if (haveRhoSRV && haveVelSRV && m_pVisualizeSlice_CS && m_pVizCB) {
        // Compute rhoMin/rhoMax from CPU data if available
        float rhoMin = 0.0f, rhoMax = 1.0f;
        if (!mLbmRho.empty()) {
            rhoMin = std::numeric_limits<float>::infinity();
            rhoMax = -std::numeric_limits<float>::infinity();
            for (size_t i = 0; i < mLbmRho.size(); ++i) {
                float vv = mLbmRho[i];
                if (!isnan(vv) && !isinf(vv)) {
                    if (vv < rhoMin) rhoMin = vv;
                    if (vv > rhoMax) rhoMax = vv;
                }
            }
            if (rhoMin == std::numeric_limits<float>::infinity() || rhoMax == -std::numeric_limits<float>::infinity()) {
                rhoMin = 0.0f; rhoMax = 1.0f;
            }
            if (fabsf(rhoMax - rhoMin) < 1e-6f) {
                rhoMin -= 0.5f; rhoMax += 0.5f;
            }
        }

        struct VizCB {
            UINT gridX;        // uint3 gridDim.x
            UINT gridY;        // uint3 gridDim.y
            UINT gridZ;        // uint3 gridDim.z
            UINT axis;         // uint axis
            UINT slice;        // uint slice
            UINT mode;         // uint mode
            float rhoMin;      // float rhoMin
            float rhoMax;      // float rhoMax
            float pad0;        // float pad0
            float velBlendLow; // float velBlendLow
            float velBlendHigh;// float velBlendHigh
            float vizVelScale; // float vizVelScale
            float pad1;        // float pad1
            UINT licSamples;   // uint licSamples
            float licStep;     // float licStep
            float licNoiseScale; // float licNoiseScale
            float pad2;        // float pad2
        };

        VizCB cb = {};
        cb.gridX = (UINT)NX;
        cb.gridY = (UINT)NY;
        cb.gridZ = (UINT)NZ;
        cb.axis = (UINT)mLbmSliceAxis;
        cb.slice = sliceZ;
        cb.mode = (UINT)mLbmVizMode;
        cb.rhoMin = rhoMin;
        cb.rhoMax = rhoMax;
        const float userVizVelScale = 10.0f;
        cb.pad0 = 0.0f;
        cb.velBlendLow = 0.6f;
        cb.velBlendHigh = 0.95f;
        cb.vizVelScale = userVizVelScale;
        cb.pad1 = 0.0f;
        cb.licSamples = 8;
        cb.licStep = 0.5f;
        cb.licNoiseScale = 8.0f;
        cb.pad2 = 0.0f;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx->Map(m_pVizCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            memcpy(mapped.pData, &cb, sizeof(cb));
            ctx->Unmap(m_pVizCB, 0);
        }

        // Bind and dispatch compute shader
        ctx->CSSetShader(m_pVisualizeSlice_CS, nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, &m_pVizCB);
        ID3D11ShaderResourceView* srvs[2] = { m_pLBM_Rho_SRV, m_pLBM_Vel_SRV };
        ctx->CSSetShaderResources(0, 2, srvs);
        ID3D11UnorderedAccessView* uavs[1] = { m_pLbmVizUAV };
        ctx->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

        UINT groupsX = (outW + 7) / 8;
        UINT groupsY = (outH + 7) / 8;
        ctx->Dispatch(groupsX, groupsY, 1);

        // Unbind
        ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
        ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
        ctx->CSSetShaderResources(0, 2, nullSRVs);
        ID3D11Buffer* nullCB = nullptr;
        ctx->CSSetConstantBuffers(0, 1, &nullCB);
        ctx->CSSetShader(nullptr, nullptr, 0);

        ctx->Flush();
        return;
    }

    //
    // GPU 経路が使えない場合の CPU フォールバック（既存コード）
    //
    if (mLbmRho.empty() && m_pLBM_Rho_Buffer) {
        D3D11_BUFFER_DESC bd = {};
        m_pLBM_Rho_Buffer->GetDesc(&bd);
        D3D11_BUFFER_DESC sd = {};
        sd.Usage = D3D11_USAGE_STAGING;
        sd.ByteWidth = bd.ByteWidth;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        sd.BindFlags = 0;
        sd.MiscFlags = bd.MiscFlags;
        sd.StructureByteStride = bd.StructureByteStride;
        ID3D11Buffer* staging = nullptr;
        if (SUCCEEDED(device->CreateBuffer(&sd, nullptr, &staging)) && staging) {
            ctx->CopyResource(staging, m_pLBM_Rho_Buffer);
            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
                size_t floats = sd.ByteWidth / sizeof(float);
                mLbmRho.resize(floats);
                memcpy(mLbmRho.data(), mapped.pData, sd.ByteWidth);
                ctx->Unmap(staging, 0);
            }
            staging->Release();
        }
    }

    if (mLbmVel.empty() && m_pLBM_Vel_Buffer) {
        D3D11_BUFFER_DESC bd = {};
        m_pLBM_Vel_Buffer->GetDesc(&bd);
        D3D11_BUFFER_DESC sd = {};
        sd.Usage = D3D11_USAGE_STAGING;
        sd.ByteWidth = bd.ByteWidth;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        sd.BindFlags = 0;
        sd.MiscFlags = bd.MiscFlags;
        sd.StructureByteStride = bd.StructureByteStride;
        ID3D11Buffer* staging = nullptr;
        if (SUCCEEDED(device->CreateBuffer(&sd, nullptr, &staging)) && staging) {
            ctx->CopyResource(staging, m_pLBM_Vel_Buffer);
            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
                size_t count = sd.ByteWidth / sizeof(DirectX::XMFLOAT3);
                mLbmVel.resize(count);
                DirectX::XMFLOAT3* src = reinterpret_cast<DirectX::XMFLOAT3*>(mapped.pData);
                for (size_t i = 0; i < count; ++i) {
                    mLbmVel[i].x = src[i].x;
                    mLbmVel[i].y = src[i].y;
                    mLbmVel[i].z = src[i].z;
                }
                ctx->Unmap(staging, 0);
            }
            staging->Release();
        }
    }

    if (mLbmRho.empty()) {
        OutputDebugStringA("VisualizeLBMSlice: no CPU rho data to fallback to CPU path.\n");
        return;
    }

    // rho の min/max を CPU バッファから確実に計算
    float rhoMinCPU = std::numeric_limits<float>::infinity();
    float rhoMaxCPU = -std::numeric_limits<float>::infinity();
    for (size_t i = 0; i < mLbmRho.size(); ++i) {
        float v = mLbmRho[i];
        if (!isnan(v) && !isinf(v)) {
            if (v < rhoMinCPU) rhoMinCPU = v;
            if (v > rhoMaxCPU) rhoMaxCPU = v;
        }
    }
    if (rhoMinCPU == std::numeric_limits<float>::infinity() || rhoMaxCPU == -std::numeric_limits<float>::infinity()) {
        rhoMinCPU = 0.0f; rhoMaxCPU = 1.0f;
    }
    if (fabsf(rhoMaxCPU - rhoMinCPU) < 1e-6f) {
        rhoMinCPU -= 0.5f; rhoMaxCPU += 0.5f;
    }

    const float userVizVelScale = 10.0f;
    float velScale = (mMaxLbmSpeed > 1e-6f ? mMaxLbmSpeed : 0.1f) * userVizVelScale;
    velScale = std::max(velScale, 1e-6f);

    std::vector<float> pixels;
    pixels.resize((size_t)outW * (size_t)outH * 4);

    auto idx_from_xyz = [&](int x, int y, int z)->size_t {
        return (size_t)z * (size_t)NX * (size_t)NY + (size_t)y * (size_t)NX + (size_t)x;
    };

    for (UINT vy = 0; vy < outH; ++vy) {
        for (UINT vx = 0; vx < outW; ++vx) {
            int x = 0, y = 0, z = 0;
            if (mLbmSliceAxis == AxisZ) { x = (int)vx; y = (int)vy; z = (int)sliceZ; }
            else if (mLbmSliceAxis == AxisY) { x = (int)vx; y = (int)sliceZ; z = (int)vy; }
            else { x = (int)sliceZ; y = (int)vx; z = (int)vy; }

            if (x < 0 || x >= NX || y < 0 || y >= NY || z < 0 || z >= NZ) {
                size_t base = ((size_t)vy * outW + vx) * 4;
                pixels[base + 0] = 0.0f; pixels[base + 1] = 0.0f; pixels[base + 2] = 0.0f; pixels[base + 3] = 1.0f;
                continue;
            }

            size_t idx = idx_from_xyz(x, y, z);
            float rho = mLbmRho[idx];
            float v = (rho - rhoMinCPU) / std::max(1e-6f, rhoMaxCPU - rhoMinCPU);
            v = (v < 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v);

            Vector3 vel3 = (idx < mLbmVel.size()) ? mLbmVel[idx] : Vector3{ 0.0f,0.0f,0.0f };
            float vMag = sqrtf(vel3.x * vel3.x + vel3.y * vel3.y + vel3.z * vel3.z);
            float vVel = vMag / velScale;
            vVel = (vVel < 0.0f) ? 0.0f : (vVel > 1.0f ? 1.0f : vVel);

            // rho カラーマップ
            Vector3 colRho;
            colRho.x = 0.0f;
            colRho.y = v;
            colRho.z = 1.0f - v;
            float t = (v <= 0.7f) ? 0.0f : ((v - 0.7f) / (1.0f - 0.7f));
            t = (t < 0.0f) ? 0.0f : (t > 1.0f ? 1.0f : t);
            colRho.x = colRho.x * (1.0f - t) + 1.0f * t;
            colRho.y = colRho.y * (1.0f - t) + 0.0f * t;
            colRho.z = colRho.z * (1.0f - t) + 0.0f * t;

            // velocity magnitude カラーマップ
            Vector3 colVelMag;
            colVelMag.x = vVel; colVelMag.y = vVel; colVelMag.z = 0.0f;

            // velocity vector 表示
            Vector3 colVelVec;
            if (vMag > 1e-6f) {
                float nx = vel3.x / vMag;
                float ny = vel3.y / vMag;
                float nz = vel3.z / vMag;
                colVelVec.x = nx * 0.5f + 0.5f;
                colVelVec.y = ny * 0.5f + 0.5f;
                colVelVec.z = nz * 0.5f + 0.5f;
            } else {
                colVelVec.x = 0.5f; colVelVec.y = 0.5f; colVelVec.z = 0.5f;
            }

            Vector3 finalCol;
            if (mLbmVizMode == VizRho) {
                Vector3 colVel = colVelMag;
                const float velBlendLow = 0.6f;
                const float velBlendHigh = 0.95f;
                float blend = 0.0f;
                if (vVel <= velBlendLow) blend = 0.0f;
                else if (vVel >= velBlendHigh) blend = 1.0f;
                else {
                    float t2 = (vVel - velBlendLow) / (velBlendHigh - velBlendLow);
                    blend = t2 * t2 * (3.0f - 2.0f * t2);
                }
                finalCol.x = colRho.x * (1.0f - blend) + colVel.x * blend;
                finalCol.y = colRho.y * (1.0f - blend) + colVel.y * blend;
                finalCol.z = colRho.z * (1.0f - blend) + colVel.z * blend;
            } else if (mLbmVizMode == VizVelMag) {
                finalCol = colVelMag;
            } else { // VizVelVec
                finalCol = colVelVec;
            }

            size_t base = ((size_t)vy * outW + vx) * 4;
            pixels[base + 0] = finalCol.x;
            pixels[base + 1] = finalCol.y;
            pixels[base + 2] = finalCol.z;
            pixels[base + 3] = 1.0f;
        }
    }

    UINT srcRowPitch = outW * sizeof(float) * 4;
    ctx->UpdateSubresource(m_pLbmVizTexture, 0, nullptr, pixels.data(), srcRowPitch, 0);

    // デバッグログ（先頭の数ピクセル）
    {
        char dbg[512];
        sprintf_s(dbg, sizeof(dbg), "VisualizeLBMSlice(CPU fallback): wrote texture %ux%u slice=%u rhoMin=%f rhoMax=%f velScale=%f mode=%u\n",
            outW, outH, sliceZ, rhoMinCPU, rhoMaxCPU, velScale, (unsigned)mLbmVizMode);
        OutputDebugStringA(dbg);
    }
}

void FluidSimulation::SetDebugVisualizeCollisions(bool enable)
{
    mDebugVisualizeCollisions = enable;
}

void FluidSimulation::DrawCollisionDebug(float /*radius*/, size_t /*maxPairs*/)
{
    if (!mDebugVisualizeCollisions) return;
    OutputDebugStringA("DrawCollisionDebug: stub called (no-op in minimal build).\n");
}

void FluidSimulation::DrawBoundsGrid(int /*divisions*/, DirectX::XMFLOAT4 /*color*/)
{
    // スタブ: 何もしない（必要なら元実装を戻してください）
}

