#include "fluidSimulation.h"
#include "threadPool.h"
#include "renderer.h"
#include "manager.h"
#include "scene.h"
#include "vector3.h"
#include <ctime>
#include <cstdlib>
#include <cassert>
#include <cmath>
#include <algorithm> // for std::min/std::max
#include <DirectXMath.h> // XMFLOAT3
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")
#include "input.h"

// Toggle for verbose debug output printed to OutputDebugStringA / DebugCheckSRVStride.
// Set to true temporarily if you need the debug messages again.
static const bool sEnableFluidDebugOutput = true;

// 基本的な初期化 / 更新 / 解放のオーケストレーションのみを残す

void FluidSimulation::Init()
{
    const int defaultParticleCount = 100000;
    Init(defaultParticleCount);
}

void FluidSimulation::Init(int particleCount)
{
    mThreadPool = new ThreadPool(std::max(1u, std::thread::hardware_concurrency()));

    mFluidProperties.resize(static_cast<size_t>(FluidType::Count));
    mFluidProperties[static_cast<int>(FluidType::Air)] = { 1.2f, 0.0f, Vector3(1.0f,1.0f,1.0f) };
    mFluidProperties[static_cast<int>(FluidType::Water)] = { 1000.0f, 0.0f, Vector3(0.2f,0.5f,1.0f) };

    SetBounds(Vector3(-5.0f, 0.0f, -5.0f), Vector3(5.0f, 5.0f, 5.0f));

    mParticles.resize(particleCount);
    mPreviousPositions.resize(particleCount);
    mParticles.count = particleCount;

    srand(static_cast<unsigned>(time(nullptr)));
    float margin = 0.1f;
    float rangeX = mBoundsSize.x - 2.0f * margin;
    float rangeY = mBoundsSize.y - 2.0f * margin;
    float rangeZ = mBoundsSize.z - 2.0f * margin;
    float volume = rangeX * rangeY * rangeZ;
    float particleSpacing = powf(volume / static_cast<float>(particleCount), 1.0f / 3.0f);

    int countX = std::max(1, static_cast<int>(rangeX / particleSpacing));
    int countY = std::max(1, static_cast<int>(rangeY / particleSpacing));
    int countZ = std::max(1, static_cast<int>(rangeZ / particleSpacing));

    float spacingX = rangeX / static_cast<float>(countX);
    float spacingY = rangeY / static_cast<float>(countY);
    float spacingZ = rangeZ / static_cast<float>(countZ);

    int index = 0;
    for (int z = 0; z < countZ && index < particleCount; ++z) {
        for (int y = 0; y < countY && index < particleCount; ++y) {
            for (int x = 0; x < countX && index < particleCount; ++x) {
                mParticles.positions[index] = Vector3(
                    mMinBounds.x + margin + (static_cast<float>(x) + 0.5f) * spacingX,
                    mMinBounds.y + margin + (static_cast<float>(y) + 0.5f) * spacingY,
                    mMinBounds.z + margin + (static_cast<float>(z) + 0.5f) * spacingZ
                );
                mParticles.velocities[index] = Vector3(0.0f, 0.0f, 0.0f);
                mParticles.densities[index] = 1.0f;
                mParticles.pressures[index] = 0.0f;
                mParticles.temperatures[index] = 300.0f;
                mParticles.fluidTypes[index] = FluidType::Air;
                mPreviousPositions[index] = mParticles.positions[index];
                ++index;
            }
        }
    }

    if (index < (int)mParticles.count) {
        for (; index < (int)mParticles.count; ++index) {
            mParticles.positions[index] = mMinBounds;
            mParticles.velocities[index] = Vector3(0.0f,0.0f,0.0f);
            mParticles.densities[index] = 1.0f;
            mParticles.pressures[index] = 0.0f;
            mParticles.temperatures[index] = 300.0f;
            mParticles.fluidTypes[index] = FluidType::Air;
            mPreviousPositions[index] = mParticles.positions[index];
        }
    }

    mOctreeNeedsRebuild = true;

    SetSmoothingRadius(1.0f);
    mTimeStep = 0.01f;
    mGasConstant = 2000.0f;
    mBoundaryDamping = 0.5f;

    // LBM 初期化（実体は fluidSimulation_lbm.cpp に移動）
    LBM_Init();

    // インスタンシング初期化（実体は fluidSimulation_instancing.cpp に移動）
    InitInstancing();
    InitTriangleInstancing();

    if (sEnableFluidDebugOutput) {
        char buf[256];
        sprintf_s(buf, sizeof(buf), "Initial particles[0..2]: p0=(%f,%f,%f) p1=(%f,%f,%f) p2=(%f,%f,%f)\n",
                  mParticles.positions.size() > 0 ? mParticles.positions[0].x : 0.0f,
                  mParticles.positions.size() > 0 ? mParticles.positions[0].y : 0.0f,
                  mParticles.positions.size() > 0 ? mParticles.positions[0].z : 0.0f,
                  mParticles.positions.size() > 1 ? mParticles.positions[1].x : 0.0f,
                  mParticles.positions.size() > 1 ? mParticles.positions[1].y : 0.0f,
                  mParticles.positions.size() > 1 ? mParticles.positions[1].z : 0.0f,
                  mParticles.positions.size() > 2 ? mParticles.positions[2].x : 0.0f,
                  mParticles.positions.size() > 2 ? mParticles.positions[2].y : 0.0f,
                  mParticles.positions.size() > 2 ? mParticles.positions[2].z : 0.0f);
        OutputDebugStringA(buf);
    }
}

void FluidSimulation::Uninit()
{
    if (mOctree) { delete mOctree; mOctree = nullptr; }

    mParticles.clear();
    mTriangles.clear();
    mCircles.clear();

    if (mThreadPool) { delete mThreadPool; mThreadPool = nullptr; }

    // GPU 解放（インスタンシング / LBM / Advect / Collide 等は各モジュールが所有）
    if (m_pLBM_CS) m_pLBM_CS->Release();
    for (int i = 0; i < 2; ++i) {
        if (m_pLBM_F_Buffer[i]) { m_pLBM_F_Buffer[i]->Release(); m_pLBM_F_Buffer[i] = nullptr; }
        if (m_pLBM_F_SRV[i]) { m_pLBM_F_SRV[i]->Release(); m_pLBM_F_SRV[i] = nullptr; }
        if (m_pLBM_F_UAV[i]) { m_pLBM_F_UAV[i]->Release(); m_pLBM_F_UAV[i] = nullptr; }
    }
    if (m_pLBM_Rho_Buffer) { m_pLBM_Rho_Buffer->Release(); m_pLBM_Rho_Buffer = nullptr; }
    if (m_pLBM_Rho_UAV) { m_pLBM_Rho_UAV->Release(); m_pLBM_Rho_UAV = nullptr; }
    if (m_pLBM_Rho_SRV) { m_pLBM_Rho_SRV->Release(); m_pLBM_Rho_SRV = nullptr; }
    if (m_pLBM_Vel_Buffer) { m_pLBM_Vel_Buffer->Release(); m_pLBM_Vel_Buffer = nullptr; }
    if (m_pLBM_Vel_SRV) { m_pLBM_Vel_SRV->Release(); m_pLBM_Vel_SRV = nullptr; }
    if (m_pLBM_Vel_UAV) { m_pLBM_Vel_UAV->Release(); m_pLBM_Vel_UAV = nullptr; }
    if (m_pLBM_IsSolid_Buffer) { m_pLBM_IsSolid_Buffer->Release(); m_pLBM_IsSolid_Buffer = nullptr; }
    if (m_pLBM_IsSolid_SRV) { m_pLBM_IsSolid_SRV->Release(); m_pLBM_IsSolid_SRV = nullptr; }
    if (m_pLBM_ConstantBuffer) { m_pLBM_ConstantBuffer->Release(); m_pLBM_ConstantBuffer = nullptr; }

    if (m_pAdvectParticles_CS) { m_pAdvectParticles_CS->Release(); m_pAdvectParticles_CS = nullptr; }
    for (int i = 0; i < 2; ++i) {
        if (m_pParticlePos_Buffer[i]) { m_pParticlePos_Buffer[i]->Release(); m_pParticlePos_Buffer[i] = nullptr; }
        if (m_pParticlePos_SRV[i]) { m_pParticlePos_SRV[i]->Release(); m_pParticlePos_SRV[i] = nullptr; }
        if (m_pParticlePos_UAV[i]) { m_pParticlePos_UAV[i]->Release(); m_pParticlePos_UAV[i] = nullptr; }
        if (m_pParticleVel_Buffer[i]) { m_pParticleVel_Buffer[i]->Release(); m_pParticleVel_Buffer[i] = nullptr; }
        if (m_pParticleVel_SRV[i]) { m_pParticleVel_SRV[i]->Release(); m_pParticleVel_SRV[i] = nullptr; }
        if (m_pParticleVel_UAV[i]) { m_pParticleVel_UAV[i]->Release(); m_pParticleVel_UAV[i] = nullptr; }
    }
    if (m_pParticleType_Buffer) { m_pParticleType_Buffer->Release(); m_pParticleType_Buffer = nullptr; }
    if (m_pParticleType_SRV) { m_pParticleType_SRV->Release(); m_pParticleType_SRV = nullptr; }
    if (m_pAdvect_ConstantBuffer) { m_pAdvect_ConstantBuffer->Release(); m_pAdvect_ConstantBuffer = nullptr; }

    UninitInstancing();
    UninitTriangleInstancing();

    if (m_pVisualizeSlice_CS) { m_pVisualizeSlice_CS->Release(); m_pVisualizeSlice_CS = nullptr; }
    if (m_pVizCB) { m_pVizCB->Release(); m_pVizCB = nullptr; }
    if (m_pLbmVizSRV) { m_pLbmVizSRV->Release(); m_pLbmVizSRV = nullptr; }
    if (m_pLbmVizUAV) { m_pLbmVizUAV->Release(); m_pLbmVizUAV = nullptr; }
    if (m_pLbmVizTexture) { m_pLbmVizTexture->Release(); m_pLbmVizTexture = nullptr; }

    if (m_pCellCount_SRV) { m_pCellCount_SRV->Release(); m_pCellCount_SRV = nullptr; }
    if (m_pCellCount_UAV) { m_pCellCount_UAV->Release(); m_pCellCount_UAV = nullptr; }
    if (m_pCellCountBuffer) { m_pCellCountBuffer->Release(); m_pCellCountBuffer = nullptr; }

    if (m_pCellList_SRV) { m_pCellList_SRV->Release(); m_pCellList_SRV = nullptr; }
    if (m_pCellList_UAV) { m_pCellList_UAV->Release(); m_pCellList_UAV = nullptr; }
    if (m_pCellListBuffer) { m_pCellListBuffer->Release(); m_pCellListBuffer = nullptr; }

    if (m_pCollide_ConstantBuffer) { m_pCollide_ConstantBuffer->Release(); m_pCollide_ConstantBuffer = nullptr; }

    if (m_pDriveParticles_CS) { m_pDriveParticles_CS->Release(); m_pDriveParticles_CS = nullptr; }
    if (m_pDriveParticles_CB) { m_pDriveParticles_CB->Release(); m_pDriveParticles_CB = nullptr; }
}

// --- 以降は元の Update() 等の実装 ---
// （Update の中でセグメント風追加処理を修正しました）
void FluidSimulation::Update()
{
    SetObstaclesFromScene();

    // 追加: 時変風を時間刻みで更新（mTimeStep を使用）
    UpdateWind(mTimeStep);

    // --- Backspace 押下で粒子位置をリセット（押下エッジで一度だけ実行） ---
    if (Input::GetKeyTrigger(VK_F1) || !mPerformedInitialReset) {
        SetInitialParticlePositions();
        mPerformedInitialReset = true;
        if (sEnableFluidDebugOutput) {
            OutputDebugStringA("FluidSimulation: Backspace pressed or initial auto-reset - particle positions reset.\n");
        }
    }

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    const size_t particleCount = mParticles.count;

    // LBM ステップ（CB 更新と Dispatch）
    if (m_pLBM_ConstantBuffer && m_pLBM_CS) {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        if (SUCCEEDED(context->Map(m_pLBM_ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource))) {
            LBMConstants* constants = (LBMConstants*)mappedResource.pData;
            for (int i = 0; i < Q; ++i) {
                constants->weights[i].x = mWeights[i];
                constants->weights[i].y = 0.0f;
                constants->weights[i].z = 0.0f;
                constants->weights[i].w = 0.0f;
                constants->dirs[i].x = mDirs[i][0];
                constants->dirs[i].y = mDirs[i][1];
                constants->dirs[i].z = mDirs[i][2];
                constants->dirs[i].w = 0;
            }
            constants->tau = mTau;
            // gravity を GUI フラグに従って書き込む
            if (mEnableGravity) {
                constants->gravity.x = mGravity.x;
                constants->gravity.y = mGravity.y;
                constants->gravity.z = mGravity.z;
            } else {
                constants->gravity.x = 0.0f;
                constants->gravity.y = 0.0f;
                constants->gravity.z = 0.0f;
            }
            constants->gridSize.x = NX;
            constants->gridSize.y = NY;
            constants->gridSize.z = NZ;
            // mWind は UpdateWind により更新済み（ベース + ガスト）
            constants->wind.x = mWind.x;
            constants->wind.y = mWind.y;
            constants->wind.z = mWind.z;
            // 追加: UI で設定した forceScale / maxLbmSpeed を定数バッファに書き出す（既存）
            // forceScale は "単位時間当たりの力" を表すため、LBM ステップ幅 mTimeStep でスケーリングする。
            // これによりフレームレートや内部タイムステップに依存した過大な加速を抑制する。
            constants->forceScale = mForceScale * mTimeStep;
            constants->maxLbmSpeed = mMaxLbmSpeed;
            // boundaryMode を定数バッファへ渡す（0=periodic,1=wall）
            // デフォルトは periodic (0)。ユーザは SetBoundaryMode を呼べるので mBoundaryMode を反映
            constants->boundaryMode = mBoundaryMode;

            // 追加: Smagorinsky パラメータを定数バッファへ渡す
            constants->Cs = mSmagorinskyCs;
            constants->delta = mSmagorinskyDelta;
            constants->tau_min = mTauMin;
            constants->tau_max = mTauMax;

            context->Unmap(m_pLBM_ConstantBuffer, 0);
            if (sEnableFluidDebugOutput) {
                char dbgMode[128];
                sprintf_s(dbgMode, sizeof(dbgMode), "LBMConstants.boundaryMode (written) = %u\n", constants->boundaryMode);
                OutputDebugStringA(dbgMode);
            }
        }

        {
            // --- LocalWind GPU バッファ作成（遅延作成） ---
            ID3D11Device* device = Renderer::GetDevice();
            ID3D11DeviceContext* ctx = Renderer::GetDeviceContext();
            if (device && !m_pLocalWindBuffer)
            {
                D3D11_BUFFER_DESC bd = {};
                bd.Usage = D3D11_USAGE_DYNAMIC;
                bd.ByteWidth = sizeof(LocalWind) * MAX_LOCAL_WINDS;
                bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
                bd.StructureByteStride = sizeof(LocalWind);
                HRESULT hr = device->CreateBuffer(&bd, nullptr, &m_pLocalWindBuffer);
                if (FAILED(hr)) {
                    if (sEnableFluidDebugOutput) {
                        OutputDebugStringA("CreateBuffer(LocalWind) failed\n");
                    }
                } else {
                    D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
                    srvd.Format = DXGI_FORMAT_UNKNOWN;
                    srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
                    srvd.Buffer.ElementWidth = MAX_LOCAL_WINDS;
                    device->CreateShaderResourceView(m_pLocalWindBuffer, &srvd, &m_pLocalWind_SRV);
                }
            }

            // LocalWind count / mode / sigma constant buffer 作成（遅延）
            if (device && !m_pLocalWindCountCB)
            {
                D3D11_BUFFER_DESC cbd = {};
                cbd.Usage = D3D11_USAGE_DYNAMIC;
                cbd.ByteWidth = 16; // 4 * 4 bytes: num(uint), mode(uint), sigma(float), pad
                cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                device->CreateBuffer(&cbd, nullptr, &m_pLocalWindCountCB);
            }

            // LocalWind データを GPU に書き込む（存在する場合のみ）
            if (m_pLocalWindBuffer && ctx)
            {
                // 合成リストを作る: ユーザ追加の mLocalWinds に加え、
                // オプションで現在のセグメント風を末尾に追加する（最大 MAX_LOCAL_WINDS）
                std::vector<LocalWind> combined;
                combined.reserve(MAX_LOCAL_WINDS);

                for (size_t i = 0; i < mLocalWinds.size() && combined.size() < MAX_LOCAL_WINDS; ++i) {
                    combined.push_back(mLocalWinds[i]);
                }

                // セグメント風を追加する（grid 座標変換を行う）
                if (mSegmentWindEnabled && combined.size() < MAX_LOCAL_WINDS) {
                    Vector3 dir = mSegmentWindP1 - mSegmentWindP0;
                    float segLen = sqrtf(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
                    if (segLen > 1e-6f) {
                        Vector3 dirN = Vector3(dir.x/segLen, dir.y/segLen, dir.z/segLen);
                        // 現在位置 (world)
                        float t = mSegmentWindProgress;
                        if (t < 0.0f) t = 0.0f;
                        if (t > 1.0f) t = 1.0f;
                        Vector3 pos = mSegmentWindP0 + dir * t;

                        // world -> grid (0..NX-1, 0..NY-1, 0..NZ-1)
                        float gx = (mBoundsSize.x > 0.0f) ? (pos.x - mMinBounds.x) * (float)(NX - 1) / mBoundsSize.x : 0.0f;
                        float gy = (mBoundsSize.y > 0.0f) ? (pos.y - mMinBounds.y) * (float)(NY - 1) / mBoundsSize.y : 0.0f;
                        float gz = (mBoundsSize.z > 0.0f) ? (pos.z - mMinBounds.z) * (float)(NZ - 1) / mBoundsSize.z : 0.0f;

                        // world radius -> grid radius (平均スケール)
                        float sx = (mBoundsSize.x > 0.0f) ? (float)(NX - 1) / mBoundsSize.x : 0.0f;
                        float sy = (mBoundsSize.y > 0.0f) ? (float)(NY - 1) / mBoundsSize.y : 0.0f;
                        float sz = (mBoundsSize.z > 0.0f) ? (float)(NZ - 1) / mBoundsSize.z : 0.0f;
                        float avgScale = (sx + sy + sz) / 3.0f;
                        float radiusGrid = mSegmentWindRadius * avgScale;

                        // Convert world direction to grid-space direction and normalize.
                        // This ensures the direction used in GPU (which treats coords in grid units)
                        // has correct magnitude and orientation even if world->grid scales differ.
                        float dirGridX = dirN.x * sx;
                        float dirGridY = dirN.y * sy;
                        float dirGridZ = dirN.z * sz;
                        float dirGridLen = sqrtf(dirGridX*dirGridX + dirGridY*dirGridY + dirGridZ*dirGridZ);
                        if (dirGridLen < 1e-6f) {
                            // fallback: use world-normalized direction (less ideal)
                            dirGridX = dirN.x; dirGridY = dirN.y; dirGridZ = dirN.z;
                            dirGridLen = sqrtf(dirGridX*dirGridX + dirGridY*dirGridY + dirGridZ*dirGridZ);
                        }
                        if (dirGridLen > 1e-6f) {
                            dirGridX /= dirGridLen; dirGridY /= dirGridLen; dirGridZ /= dirGridLen;
                        } else {
                            dirGridX = 0.0f; dirGridY = 0.0f; dirGridZ = 0.0f;
                        }

                        // Skip if center is well outside the grid extents (beyond radius influence).
                        // This prevents pushing a LocalWind whose center lies far outside the grid,
                        // which produces no effect but may confuse debugging.
                        bool outsideByRadius =
                            (gx < -radiusGrid) || (gx > (float)(NX - 1) + radiusGrid) ||
                            (gy < -radiusGrid) || (gy > (float)(NY - 1) + radiusGrid) ||
                            (gz < -radiusGrid) || (gz > (float)(NZ - 1) + radiusGrid);

                        if (outsideByRadius) {
                            if (sEnableFluidDebugOutput) {
                                char dbgSkip[256];
                                sprintf_s(dbgSkip, sizeof(dbgSkip),
                                          "SegmentWind skipped: center outside grid+radius: center=(%.3f,%.3f,%.3f) radiusGrid=%.3f\n",
                                          gx, gy, gz, radiusGrid);
                                OutputDebugStringA(dbgSkip);
                            }
                        } else {
                            // Clamp center to grid boundaries so shader receives valid in-range coords.
                            float cgx = std::min(std::max(gx, 0.0f), (float)(NX - 1));
                            float cgy = std::min(std::max(gy, 0.0f), (float)(NY - 1));
                            float cgz = std::min(std::max(gz, 0.0f), (float)(NZ - 1));

                            LocalWind lw = {};
                            lw.center = DirectX::XMFLOAT3(cgx, cgy, cgz);
                            lw.radius = radiusGrid;
                            lw.direction = DirectX::XMFLOAT3(dirGridX, dirGridY, dirGridZ);
                            lw.strength = mSegmentWindStrength;

                            combined.push_back(lw);

                            // Debug: print segment wind values if enabled
                            if (sEnableFluidDebugOutput) {
                                char dbgSeg[256];
                                sprintf_s(dbgSeg, sizeof(dbgSeg),
                                          "SegmentWind added: center=(%.3f,%.3f,%.3f) radiusGrid=%.3f dirGrid=(%.3f,%.3f,%.3f) strength=%.3f progress=%.3f\n",
                                          cgx, cgy, cgz, radiusGrid, dirGridX, dirGridY, dirGridZ, mSegmentWindStrength, mSegmentWindProgress);
                                OutputDebugStringA(dbgSeg);
                            }
                        }
                    }
                }

                D3D11_MAPPED_SUBRESOURCE mapped;
                if (SUCCEEDED(ctx->Map(m_pLocalWindBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                    size_t copyCount = std::min<size_t>(combined.size(), MAX_LOCAL_WINDS);
                    if (copyCount > 0) {
                        memcpy(mapped.pData, combined.data(), sizeof(LocalWind) * copyCount);
                    }
                    ctx->Unmap(m_pLocalWindBuffer, 0);
                }
            }

            if (m_pLocalWindCountCB && ctx) {
                struct LocalWindCBData { UINT num; UINT mode; float sigma; UINT pad; };
                D3D11_MAPPED_SUBRESOURCE mappedCnt;
                if (SUCCEEDED(ctx->Map(m_pLocalWindCountCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedCnt))) {
                    LocalWindCBData* p = (LocalWindCBData*)mappedCnt.pData;
                    // GPU に送った実際の数を算出: mLocalWinds + (セグメント風 ? 1 : 0), clamped
                    UINT gpuNum = static_cast<UINT>(mLocalWinds.size());
                    if (mSegmentWindEnabled && gpuNum < (UINT)MAX_LOCAL_WINDS) gpuNum++;
                    if (gpuNum > (UINT)MAX_LOCAL_WINDS) gpuNum = (UINT)MAX_LOCAL_WINDS;

                    p->num = gpuNum;
                    p->mode = mLocalWindMode;
                    p->sigma = mLocalWindSigma;
                    p->pad = 0;
                    ctx->Unmap(m_pLocalWindCountCB, 0);

                    if (sEnableFluidDebugOutput) {
                        char dbgCnt[128];
                        sprintf_s(dbgCnt, sizeof(dbgCnt), "LocalWind CB: gpuNum=%u mode=%u sigma=%f\n", p->num, p->mode, p->sigma);
                        OutputDebugStringA(dbgCnt);
                    }
                }
            }
        }

        // Bind compute shader, constants and input SRVs (important)
        context->CSSetShader(m_pLBM_CS, nullptr, 0);
        context->CSSetConstantBuffers(0, 1, &m_pLBM_ConstantBuffer);

        // --- Bind LocalWind count CB to b1 if present (fix: ensure shader sees gNumLocalWinds / mode / sigma) ---
        if (m_pLocalWindCountCB) {
            context->CSSetConstantBuffers(1, 1, &m_pLocalWindCountCB);
        }

        // --- Bind input SRVs required by lbm_step.hlsl: t0=f_in, t1=is_solid, t2=gLocalWinds ---
        {
            ID3D11ShaderResourceView* lbm_srvs[3] = { nullptr, nullptr, nullptr };
            lbm_srvs[0] = (m_LBM_PingPong_Index >= 0 && m_LBM_PingPong_Index < 2) ? m_pLBM_F_SRV[m_LBM_PingPong_Index] : nullptr;
            lbm_srvs[1] = m_pLBM_IsSolid_SRV;
            lbm_srvs[2] = m_pLocalWind_SRV; // may be null
            context->CSSetShaderResources(0, 3, lbm_srvs);
        }

        ID3D11UnorderedAccessView* lbm_uavs[] = { m_pLBM_F_UAV[1 - m_LBM_PingPong_Index], m_pLBM_Rho_UAV, m_pLBM_Vel_UAV };
        context->CSSetUnorderedAccessViews(0, 3, lbm_uavs, nullptr);

        if (sEnableFluidDebugOutput) {
            Renderer::DebugCheckSRVStride(m_pLBM_F_SRV[m_LBM_PingPong_Index], "LBM F (t0)", sizeof(float));
            Renderer::DebugCheckSRVStride(m_pLBM_IsSolid_SRV, "LBM IsSolid (t1)", sizeof(int));

            {
                char buf[256];
                sprintf_s(buf, sizeof(buf), "Dispatching LBM CS ptr=%p (m_pLBM_CS)\n", (void*)m_pLBM_CS);
                OutputDebugStringA(buf);
            }
            // Log current boundary mode (from CPU) so we can trace whether GUI change propagated
            {
                char dbgBM[128]; sprintf_s(dbgBM, sizeof(dbgBM), "Dispatch: mBoundaryMode=%u m_pLBM_CS=%p\n", mBoundaryMode, (void*)m_pLBM_CS); OutputDebugStringA(dbgBM);
            }
        }
        context->Dispatch((NX + 7) / 8, (NY + 7) / 8, (NZ + 7) / 8);

        // 既存の解除に加え、念のため全リソースをヌルバインド（安全策）
        ID3D11UnorderedAccessView* nullUAVs[] = { nullptr, nullptr, nullptr };
        context->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
        ID3D11ShaderResourceView* nullSRVs[3] = { nullptr, nullptr, nullptr };
        context->CSSetShaderResources(0, 3, nullSRVs);

        // Unbind LocalWind count CB (b1) to avoid accidental reuse
        ID3D11Buffer* nullCB = nullptr;
        context->CSSetConstantBuffers(1, 1, &nullCB);

        context->CSSetShader(nullptr, nullptr, 0);

        // 追加：Renderer のユーティリティで PS/VS/CS の SRV/UAV を全解除
        if (sEnableFluidDebugOutput) {
            char buf[512];
            sprintf_s(buf, sizeof(buf),
                "Before Renderer::UnbindAllShaderResources() -> m_pLbmVizSRV=%p m_pLBM_Rho_SRV=%p m_pLBM_Vel_SRV=%p\n",
                (void*)m_pLbmVizSRV, (void*)m_pLBM_Rho_SRV, (void*)m_pLBM_Vel_SRV);
            OutputDebugStringA(buf);
        }
        Renderer::UnbindAllShaderResources();
        if (sEnableFluidDebugOutput) {
            char buf[256];
            sprintf_s(buf, sizeof(buf), "After Renderer::UnbindAllShaderResources()\n");
            OutputDebugStringA(buf);
        }

        // GPU -> CPU readback for LBM rho (updates mLbmRho for VizCB rhoMin/rhoMax computation)
        // Must run BEFORE VisualizeLBMSlice so rhoMin/rhoMax are up-to-date.
        {
            ID3D11Device* device = Renderer::GetDevice();
            ID3D11DeviceContext* ctx = Renderer::GetDeviceContext();
            if (device && ctx && m_pLBM_Rho_Buffer) {
                const UINT cellCount = (UINT)NX * (UINT)NY * (UINT)NZ;
                // Match the source buffer desc exactly for CopyResource compatibility
                D3D11_BUFFER_DESC srcDesc = {};
                m_pLBM_Rho_Buffer->GetDesc(&srcDesc);
                D3D11_BUFFER_DESC sd = {};
                sd.Usage = D3D11_USAGE_STAGING;
                sd.ByteWidth = srcDesc.ByteWidth;
                sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                sd.BindFlags = 0;
                sd.MiscFlags = srcDesc.MiscFlags;
                sd.StructureByteStride = srcDesc.StructureByteStride;
                ID3D11Buffer* staging = nullptr;
                HRESULT hr = device->CreateBuffer(&sd, nullptr, &staging);
                if (SUCCEEDED(hr) && staging) {
                    ctx->CopyResource(staging, m_pLBM_Rho_Buffer);
                    // Ensure copy is submitted to GPU before mapping the staging resource
                    ctx->Flush();
                    D3D11_MAPPED_SUBRESOURCE mapped;
                    hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
                    if (SUCCEEDED(hr)) {
                        float* data = reinterpret_cast<float*>(mapped.pData);

                        // Update mLbmRho so VisualizeLBMSlice can compute correct rhoMin/rhoMax
                        mLbmRho.resize(cellCount);
                        memcpy(mLbmRho.data(), data, cellCount * sizeof(float));

                        if (sEnableFluidDebugOutput) {
                            size_t center = (size_t)(NZ / 2) * NY * NX + (size_t)(NY / 2) * NX + (size_t)(NX / 2);
                            size_t i0 = center;
                            size_t i1 = (center + 1 < cellCount) ? center + 1 : center;
                            size_t i2 = (center > 0) ? center - 1 : center;
                            char buf[256];
                            sprintf_s(buf, sizeof(buf), "Readback LBM rho samples: center=%f +1=%f -1=%f\n",
                                data[i0], data[i1], data[i2]);
                            OutputDebugStringA(buf);
                        }
                        ctx->Unmap(staging, 0);
                    }
                    else {
                        OutputDebugStringA("Readback: Map failed\n");
                    }
                    staging->Release();
                }
                else {
                    OutputDebugStringA("Readback: Create staging failed\n");
                }
            }
        }

        // --- 追加: LBM Dispatch 直後に自動可視化を行う（ping-pong 整合を確保） ---
        if (mShowLbmSlice && mAutoVisualizeLBM) {
            // SRV が古いバッファを参照する恐れがあるため、一旦既存 SRV を破棄して Visualize 側で再作成させる
            if (m_pLBM_Rho_SRV) { m_pLBM_Rho_SRV->Release(); m_pLBM_Rho_SRV = nullptr; }
            if (m_pLBM_Vel_SRV) { m_pLBM_Vel_SRV->Release(); m_pLBM_Vel_SRV = nullptr; }

            // VisualizeLBMSlice は GPU 経路を使用する場合に内部で SRV を (再)作成します。
            VisualizeLBMSlice(mLbmSliceIndex);
        }
    }

    if (particleCount > 0 && m_pAdvectParticles_CS) {
        AdvectCombine();
    }

    // GPU 衝突処理 (Build/Resolve)
    if (particleCount > 0 && m_pParticleCollideBuild_CS && m_pParticleCollideResolve_CS && m_pCollide_ConstantBuffer) {
        UINT adv_readIndex = (UINT)m_Particle_PingPong_Index;
        UINT adv_writeIndex = 1u - adv_readIndex;

        {
            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(context->Map(m_pCollide_ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                CollideConstants* cc = (CollideConstants*)mapped.pData;
                cc->numParticles = static_cast<UINT>(particleCount);
                cc->boundsMin.x = mMinBounds.x; cc->boundsMin.y = mMinBounds.y; cc->boundsMin.z = mMinBounds.z;
                cc->boundsSize.x = mBoundsSize.x; cc->boundsSize.y = mBoundsSize.y; cc->boundsSize.z = mBoundsSize.z;
                cc->gridDim.x = (UINT)mCollideGridX; cc->gridDim.y = (UINT)mCollideGridY; cc->gridDim.z = (UINT)mCollideGridZ;
                cc->maxPerCell = (UINT)mCollideMaxPerCell;
                cc->cellSize = mCollideCellSize;
                context->Unmap(m_pCollide_ConstantBuffer, 0);
            }
        }

        UINT clearZero[4] = { 0u, 0u, 0u, 0u };
        UINT clearInv[4]  = { 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu };
        if (m_pCellCount_UAV) context->ClearUnorderedAccessViewUint(m_pCellCount_UAV, clearZero);
        if (m_pCellList_UAV)  context->ClearUnorderedAccessViewUint(m_pCellList_UAV,  clearInv);

        context->CSSetShader(m_pParticleCollideBuild_CS, nullptr, 0);
        context->CSSetConstantBuffers(0, 1, &m_pCollide_ConstantBuffer);
        ID3D11ShaderResourceView* build_srvs[] = { m_pParticlePos_SRV[adv_writeIndex], m_pParticleVel_SRV[adv_writeIndex] };
        context->CSSetShaderResources(0, 2, build_srvs);
        ID3D11UnorderedAccessView* build_uavs[] = { m_pCellCount_UAV, m_pCellList_UAV };
        context->CSSetUnorderedAccessViews(0, 2, build_uavs, nullptr);
        context->Dispatch((static_cast<UINT>(particleCount) + 255u) / 256u, 1, 1);

        {
            ID3D11UnorderedAccessView* nullUAVs2[] = { nullptr, nullptr };
            context->CSSetUnorderedAccessViews(0, 2, nullUAVs2, nullptr);
            ID3D11ShaderResourceView* nullSRVs2[] = { nullptr, nullptr };
            context->CSSetShaderResources(0, 2, nullSRVs2);
            context->CSSetShader(nullptr, nullptr, 0);
        }
        Renderer::UnbindAllShaderResources();

        context->CSSetShader(m_pParticleCollideResolve_CS, nullptr, 0);
        context->CSSetConstantBuffers(0, 1, &m_pCollide_ConstantBuffer);
        ID3D11ShaderResourceView* res_srvs_main[] = {
            m_pParticlePos_SRV[adv_writeIndex],
            m_pParticleVel_SRV[adv_writeIndex],
            m_pCellCount_SRV,
            m_pCellList_SRV
        };
        context->CSSetShaderResources(0, 4, res_srvs_main);
        ID3D11UnorderedAccessView* res_uavs[4] = { nullptr, nullptr, nullptr, nullptr };
        res_uavs[2] = m_pParticlePos_UAV[adv_readIndex];
        res_uavs[3] = m_pParticleVel_UAV[adv_readIndex];
        context->CSSetUnorderedAccessViews(0, 4, res_uavs, nullptr);
        context->Dispatch((static_cast<UINT>(particleCount) + 255u) / 256u, 1, 1);

        {
            ID3D11UnorderedAccessView* nullAllUAVs[4] = { nullptr, nullptr, nullptr, nullptr };
            context->CSSetUnorderedAccessViews(0, 4, nullAllUAVs, nullptr);
            ID3D11ShaderResourceView* nullAllSRVs[4] = { nullptr, nullptr, nullptr, nullptr };
            context->CSSetShaderResources(0, 4, nullAllSRVs);
            context->CSSetShader(nullptr, nullptr, 0);
        }
        Renderer::UnbindAllShaderResources();

        // Resolve の出力先は adv_readIndex。両バッファを同期する。
        if (m_pParticlePos_Buffer[adv_readIndex] && m_pParticlePos_Buffer[adv_writeIndex] &&
            m_pParticlePos_Buffer[adv_readIndex] != m_pParticlePos_Buffer[adv_writeIndex]) {
            context->CopyResource(m_pParticlePos_Buffer[adv_writeIndex], m_pParticlePos_Buffer[adv_readIndex]);
        }
        if (m_pParticleVel_Buffer[adv_readIndex] && m_pParticleVel_Buffer[adv_writeIndex] &&
            m_pParticleVel_Buffer[adv_readIndex] != m_pParticleVel_Buffer[adv_writeIndex]) {
            context->CopyResource(m_pParticleVel_Buffer[adv_writeIndex], m_pParticleVel_Buffer[adv_readIndex]);
        }
    }

    m_LBM_PingPong_Index = 1 - m_LBM_PingPong_Index;
    m_Particle_PingPong_Index = 1 - m_Particle_PingPong_Index;

    // 一時デバッグ呼び出し（Update() 内に挿入）
    if (Input::GetKeyTrigger('V')) {
        bool ok = TestVisualizeSingleCell(2u, 4u, 3u, 2u, 4u, 10.0f);
        char buf[128];
        sprintf_s(buf, sizeof(buf), "TestVisualizeSingleCell invoked -> %s\n", ok ? "PASS" : "FAIL");
        OutputDebugStringA(buf);
    }
}

void FluidSimulation::SetWind(const Vector3& wind)
{
    mWind = wind;
}

void FluidSimulation::SetBoundaryMode(UINT mode)
{
    if (mode > 1u) mode = 0u;
    mBoundaryMode = mode;
}

// Overload: set + optionally recreate shader
void FluidSimulation::SetBoundaryModeAndRecreate(UINT mode)
{
    if (mode > 1u) mode = 0u;
    mBoundaryMode = mode;
    if (sEnableFluidDebugOutput) {
        char dbg[128]; sprintf_s(dbg, sizeof(dbg), "SetBoundaryModeAndRecreate: mode=%u\n", mBoundaryMode); OutputDebugStringA(dbg);
    }
    // Recreate LBM CS so any compiled-in behavior is reloaded (useful during development)
    if (m_pLBM_CS) {
        m_pLBM_CS->Release(); m_pLBM_CS = nullptr;
    }
    // Recreate LBM CS: prefer runtime compile from HLSL so latest boundary logic is guaranteed to be used.
    {
        ID3D11Device* device = Renderer::GetDevice();
        ID3DBlob* csBlob = nullptr;
        ID3DBlob* errBlob = nullptr;
        HRESULT hr = E_FAIL;
        if (device) {
            hr = D3DCompileFromFile(
                L"shader\\lbm_step.hlsl",
                nullptr,
                D3D_COMPILE_STANDARD_FILE_INCLUDE,
                "main",
                "cs_5_0",
                D3DCOMPILE_OPTIMIZATION_LEVEL3,
                0,
                &csBlob,
                &errBlob);

            if (SUCCEEDED(hr) && csBlob) {
                hr = device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_pLBM_CS);
            }
        }

        if (FAILED(hr) || !m_pLBM_CS) {
            if (errBlob) {
                OutputDebugStringA((const char*)errBlob->GetBufferPointer());
            }
            OutputDebugStringA("SetBoundaryModeAndRecreate: runtime compile failed, fallback to lbm_step.cso\n");
            Renderer::CreateComputeShader(&m_pLBM_CS, "lbm_step.cso");
        }

        if (csBlob) csBlob->Release();
        if (errBlob) errBlob->Release();
    }
}

UINT FluidSimulation::GetBoundaryMode() const
{
    return mBoundaryMode;
}

void FluidSimulation::SetForceScale(float fs)
{
    mForceScale = fs;
}

float FluidSimulation::GetForceScale() const
{
    return mForceScale;
}

void FluidSimulation::SetMaxLbmSpeed(float s)
{
    mMaxLbmSpeed = s;
}

float FluidSimulation::GetMaxLbmSpeed() const
{
    return mMaxLbmSpeed;
}

void FluidSimulation::SetSmoothingRadius(float smoothingRadius)
{
    mSmoothingRadius = smoothingRadius;
    mSR2 = mSmoothingRadius * mSmoothingRadius;
    mSR6 = mSR2 * mSR2 * mSR2;
    mSR9 = mSR6 * mSR2 * mSmoothingRadius;

    mPoly6Factor = 315.0f / (64.0f * (float)M_PI * mSR9);
    mSpikyFactor = -45.0f / ((float)M_PI * mSR6);
    mViscosityFactor = 45.0f / ((float)M_PI * mSR6);

    mCollideCellSize = mSmoothingRadius * mCollideCellScale;
    if (mCollideCellSize <= 1e-6f) mCollideCellSize = 1e-6f;
}

void FluidSimulation::SetBounds(const Vector3& minBounds, const Vector3& maxBounds)
{
    mMinBounds = minBounds;
    mMaxBounds = maxBounds;
    mBoundsSize = mMaxBounds - mMinBounds;
    mBoundsCenter = (mMinBounds + mMaxBounds) * 0.5f;
}

void FluidSimulation::SetInitialParticlePositions()
{
    srand(static_cast<unsigned>(time(nullptr)));
    float margin = 0.1f;
    float rangeX = mBoundsSize.x - 2.0f * margin;
    float rangeY = mBoundsSize.y - 2.0f * margin;
    float rangeZ = mBoundsSize.z - 2.0f * margin;
    float volume = rangeX * rangeY * rangeZ;
    if (mParticles.count == 0) return;
    float particleSpacing = powf(volume / static_cast<float>(mParticles.count), 1.0f / 3.0f);

    int countX = std::max(1, static_cast<int>(rangeX / particleSpacing));
    int countY = std::max(1, static_cast<int>(rangeY / particleSpacing));
    int countZ = std::max(1, static_cast<int>(rangeZ / particleSpacing));

    float spacingX = rangeX / static_cast<float>(countX);
    float spacingY = rangeY / static_cast<float>(countY);
    float spacingZ = rangeZ / static_cast<float>(countZ);

    int index = 0;
    for (int z = 0; z < countZ && index < (int)mParticles.count; ++z) {
        for (int y = 0; y < countY && index < (int)mParticles.count; ++y) {
            for (int x = 0; x < countX && index < (int)mParticles.count; ++x) {
                mParticles.positions[index] = Vector3(
                    mMinBounds.x + margin + (static_cast<float>(x) + 0.5f) * spacingX,
                    mMinBounds.y + margin + (static_cast<float>(y) + 0.5f) * spacingY,
                    mMinBounds.z + margin + (static_cast<float>(z) + 0.5f) * spacingZ
                );
                ++index;
            }
        }
    }

    if (index < (int)mParticles.count) {
        for (; index < (int)mParticles.count; ++index) {
            mParticles.positions[index] = mMinBounds;
            mParticles.velocities[index] = Vector3(0.0f,0.0f,0.0f);
            mParticles.densities[index] = 1.0f;
            mParticles.pressures[index] = 0.0f;
            mParticles.temperatures[index] = 300.0f;
            mParticles.fluidTypes[index] = FluidType::Air;
            mPreviousPositions[index] = mParticles.positions[index];
        }
    }

    mOctreeNeedsRebuild = true;

    SetSmoothingRadius(1.0f);
    mTimeStep = 0.01f;
    mGasConstant = 2000.0f;
    mBoundaryDamping = 0.5f;

    LBM_Init();
    InitInstancing();
    InitTriangleInstancing();
}

// --- 統一された setter/getter と Draw を一箇所にまとめました ---

void FluidSimulation::SetShowLbmSlice(bool show)
{
    mShowLbmSlice = show;
}

void FluidSimulation::SetLbmSliceIndex(UINT slice)
{
    if (slice >= static_cast<UINT>(NZ)) slice = NZ > 0 ? NZ - 1 : 0;
    mLbmSliceIndex = slice;
}

bool FluidSimulation::GetShowLbmSlice() const
{
    return mShowLbmSlice;
}

UINT FluidSimulation::GetLbmSliceIndex() const
{
    return mLbmSliceIndex;
}

void FluidSimulation::SetShowParticles(bool show)
{
    mShowParticles = show;
}

bool FluidSimulation::GetShowParticles() const
{
    return mShowParticles;
}

void FluidSimulation::Draw()
{
    // Safety: ensure a vertex shader is bound before any ctx->Draw calls.
    // Some compute dispatch paths may unbind shaders; this guard binds a minimal
    // unlit VS/PS if none is present to avoid D3D11 ERROR #341.
    {
        ID3D11DeviceContext* ctx = Renderer::GetDeviceContext();
        if (ctx) {
            ID3D11VertexShader* curVS = nullptr;
            ID3D11ClassInstance* cls = nullptr;
            UINT numCls = 0;
            ctx->VSGetShader(&curVS, &cls, &numCls);
            if (!curVS) {
                // Bind lightweight unlit shaders (create via Renderer helpers).
                ID3D11VertexShader* vs = nullptr;
                ID3D11InputLayout* il = nullptr;
                ID3D11PixelShader* ps = nullptr;
                Renderer::CreateVertexShader(&vs, &il, "unlitTextureVS.cso");
                Renderer::CreatePixelShader(&ps, "unlitTexturePS.cso");
                if (il) ctx->IASetInputLayout(il);
                if (vs) ctx->VSSetShader(vs, nullptr, 0);
                if (ps) ctx->PSSetShader(ps, nullptr, 0);
                // release local refs (Create* returns addref'd objects)
                if (vs) vs->Release();
                if (ps) ps->Release();
                if (il) il->Release();
            } else {
                // release the ref from VSGetShader
                curVS->Release();
            }
            if (cls) {
                // VSGetShader returned class instances pointer if any - release if set
                // According to API, VSGetShader returns class instances array pointer (not AddRef),
                // but to be safe we don't attempt to release cls here.
            }
        }
    }

    if (mShowParticles) {
        DrawInstancing();
    }

    DrawTriangleInstancing();
    DrawBoundsGrid(16, DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));
    if (mDebugVisualizeCollisions) {
        DrawCollisionDebug(mSmoothingRadius, 10000);
    }

    // LBM slice display: do NOT automatically regenerate visualization every frame
    // unless auto-update flag is enabled. VisualizeLBMSlice is invoked explicitly
    // by the ImGui "Visualize" button (or when mAutoVisualizeLBM==true).
    if (mShowLbmSlice) {
        // only regenerate if auto-update is enabled
        if (mAutoVisualizeLBM) {
            VisualizeLBMSlice(mLbmSliceIndex);
        }

        // If visualization texture exists, draw it scaled up for readability.
        if (m_pLbmVizSRV) {
            if (sEnableFluidDebugOutput) {
                char buf[256];
                sprintf_s(buf, sizeof(buf), "Drawing LBM viz SRV=%p size=(%u,%u) m_pLBM_Rho_SRV=%p m_pLBM_Vel_SRV=%p\n",
                          (void*)m_pLbmVizSRV, m_vizWidth, m_vizHeight, (void*)m_pLBM_Rho_SRV, (void*)m_pLBM_Vel_SRV);
                OutputDebugStringA(buf);
            }

            // preserve aspect ratio, ensure min dimension ~512
            UINT srcW = (m_vizWidth > 0) ? m_vizWidth : 1;
            UINT srcH = (m_vizHeight > 0) ? m_vizHeight : 1;
            const UINT minDim = 512;
            float aspect = static_cast<float>(srcW) / static_cast<float>(srcH);
            UINT dispW = srcW;
            UINT dispH = srcH;

            if (dispW < minDim && dispH < minDim) {
                if (aspect >= 1.0f) {
                    dispW = minDim;
                    dispH = static_cast<UINT>(std::max(1.0f, std::round(minDim / aspect)));
                } else {
                    dispH = minDim;
                    dispW = static_cast<UINT>(std::max(1.0f, std::round(minDim * aspect)));
                }
            } else {
                if (dispW < minDim) {
                    dispW = minDim;
                    dispH = static_cast<UINT>(std::max(1.0f, std::round(dispW / aspect)));
                }
                if (dispH < minDim) {
                    dispH = minDim;
                    dispW = static_cast<UINT>(std::max(1.0f, std::round(dispH * aspect)));
                }
            }

            // Debug: ensure DrawTextureRect gets called with a valid SRV
            if (sEnableFluidDebugOutput) {
                char buf2[256];
                sprintf_s(buf2, sizeof(buf2), "Calling Renderer::DrawTextureRect with SRV=%p\n", (void*)m_pLbmVizSRV);
                OutputDebugStringA(buf2);
            }

            Renderer::DrawTextureRect(m_pLbmVizSRV, 16, 48, dispW, dispH);

            if (sEnableFluidDebugOutput) {
                char buf3[256];
                sprintf_s(buf3, sizeof(buf3), "After DrawTextureRect SRV=%p\n", (void*)m_pLbmVizSRV);
                OutputDebugStringA(buf3);
            }
        } else {
            if (sEnableFluidDebugOutput) {
                OutputDebugStringA("m_pLbmVizSRV is null -> nothing to draw for LBM slice\n");
            }
        }
    }

    DrawDebugUI();
}

void FluidSimulation::DriveParticlesWithLbmVel(float blend)
{
    if (blend <= 0.0f) return; // 何もしない
    if (!m_pLBM_Vel_Buffer) return; // LBM Vel バッファが存在しない
    if (mParticles.count == 0) return;

    ID3D11Device* device = Renderer::GetDevice();
    ID3D11DeviceContext* ctx = Renderer::GetDeviceContext();
    if (!device || !ctx) return;

    const UINT NX = FluidSimulation::NX;
    const UINT NY = FluidSimulation::NY;
    const UINT NZ = FluidSimulation::NZ;
    const UINT cellCount = NX * NY * NZ;
    if (cellCount == 0) return;

    // ステージングバッファを作成（読み取り用）— ソースバッファの MiscFlags/StructureByteStride を合わせる
    D3D11_BUFFER_DESC srcDesc = {};
    m_pLBM_Vel_Buffer->GetDesc(&srcDesc);
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = srcDesc.ByteWidth;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = srcDesc.MiscFlags;
    desc.StructureByteStride = srcDesc.StructureByteStride;

    ID3D11Buffer* staging = nullptr;
    HRESULT hr = device->CreateBuffer(&desc, nullptr, &staging);
    if (FAILED(hr) || !staging) {
        if (sEnableFluidDebugOutput) {
            OutputDebugStringA("DriveParticlesWithLbmVel: CreateBuffer(staging) failed\n");
        }
        return;
    }

    // GPU -> ステージングへコピー
    ctx->CopyResource(staging, m_pLBM_Vel_Buffer);
    ctx->Flush();

    // マップして読み取り
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        staging->Release();
        if (sEnableFluidDebugOutput) {
            OutputDebugStringA("DriveParticlesWithLbmVel: Map(staging) failed\n");
        }
        return;
    }

    const DirectX::XMFLOAT3* velData = reinterpret_cast<const DirectX::XMFLOAT3*>(mapped.pData);

    // 用途: グリッド空間 (x:[0..NX-1], y:[0..NY-1], z:[0..NZ-1])
    auto clamp01 = [](float v) {
        if (v < 0.0f) return 0.0f;
        if (v > 1.0f) return 1.0f;
        return v;
    };

    const Vector3 boundsMin = mMinBounds;
    const Vector3 boundsSize = mBoundsSize;
    const float invX = (boundsSize.x > 0.0f) ? (float)(NX - 1) / boundsSize.x : 0.0f;
    const float invY = (boundsSize.y > 0.0f) ? (float)(NY - 1) / boundsSize.y : 0.0f;
    const float invZ = (boundsSize.z > 0.0f) ? (float)(NZ - 1) / boundsSize.z : 0.0f;

    const size_t pcount = mParticles.count;
    for (size_t i = 0; i < pcount; ++i)
    {
        Vector3 pos = mParticles.positions[i];

        // 正規化して格子座標へ
        float fx = (pos.x - boundsMin.x) * invX;
        float fy = (pos.y - boundsMin.y) * invY;
        float fz = (pos.z - boundsMin.z) * invZ;

        fx = clamp01(fx);
        fy = clamp01(fy);
        fz = clamp01(fz);

        float gx = fx * (float)(NX - 1);
        float gy = fy * (float)(NY - 1);
        float gz = fz * (float)(NZ - 1);

        int x0 = static_cast<int>(floorf(gx));
        int y0 = static_cast<int>(floorf(gy));
        int z0 = static_cast<int>(floorf(gz));
        int x1 = std::min<int>(x0 + 1, (int)NX - 1);
        int y1 = std::min<int>(y0 + 1, (int)NY - 1);
        int z1 = std::min<int>(z0 + 1, (int)NZ - 1);

        float wx = gx - (float)x0;
        float wy = gy - (float)y0;
        float wz = gz - (float)z0;

        // index helper: (z * NY + y) * NX + x
        auto idx = [&](int xi, int yi, int zi) -> size_t {
            return static_cast<size_t>((zi * (int)NY + yi) * (int)NX + xi);
        };

        // 8点サンプリング
        DirectX::XMFLOAT3 c000 = velData[idx(x0,y0,z0)];
        DirectX::XMFLOAT3 c100 = velData[idx(x1,y0,z0)];
        DirectX::XMFLOAT3 c010 = velData[idx(x0,y1,z0)];
        DirectX::XMFLOAT3 c110 = velData[idx(x1,y1,z0)];
        DirectX::XMFLOAT3 c001 = velData[idx(x0,y0,z1)];
        DirectX::XMFLOAT3 c101 = velData[idx(x1,y0,z1)];
        DirectX::XMFLOAT3 c011 = velData[idx(x0,y1,z1)];
        DirectX::XMFLOAT3 c111 = velData[idx(x1,y1,z1)];

        // トリリニア補間
        auto lerpf = [](float a, float b, float t) { return a + (b - a) * t; };
        DirectX::XMFLOAT3 c00 = { lerpf(c000.x, c100.x, wx), lerpf(c000.y, c100.y, wx), lerpf(c000.z, c100.z, wx) };
        DirectX::XMFLOAT3 c10 = { lerpf(c010.x, c110.x, wx), lerpf(c010.y, c110.y, wx), lerpf(c010.z, c110.z, wx) };
        DirectX::XMFLOAT3 c01 = { lerpf(c001.x, c101.x, wx), lerpf(c001.y, c101.y, wx), lerpf(c001.z, c101.z, wx) };
        DirectX::XMFLOAT3 c11 = { lerpf(c011.x, c111.x, wx), lerpf(c011.y, c111.y, wx), lerpf(c011.z, c111.z, wx) };

        DirectX::XMFLOAT3 c0 = { lerpf(c00.x, c10.x, wy), lerpf(c00.y, c10.y, wy), lerpf(c00.z, c10.z, wy) };
        DirectX::XMFLOAT3 c1 = { lerpf(c01.x, c11.x, wy), lerpf(c01.y, c11.y, wy), lerpf(c01.z, c11.z, wy) };

        DirectX::XMFLOAT3 sampled = { lerpf(c0.x, c1.x, wz), lerpf(c0.y, c1.y, wz), lerpf(c0.z, c1.z, wz) };

        // 現在の粒子速度と混合して適用
        Vector3 lbmVel(sampled.x, sampled.y, sampled.z);
        Vector3 oldVel = mParticles.velocities[i];
        Vector3 newVel = Vector3(
            oldVel.x * (1.0f - blend) + lbmVel.x * blend,
            oldVel.y * (1.0f - blend) + lbmVel.y * blend,
            oldVel.z * (1.0f - blend) + lbmVel.z * blend
        );
        mParticles.velocities[i] = newVel;
    }

    // 解放
    ctx->Unmap(staging, 0);
    staging->Release();
}

void FluidSimulation::DriveParticlesWithLbmVelGPU(float blend)
{
    if (blend <= 0.0f) return;
    if (!m_pLBM_Vel_SRV || mParticles.count == 0) return;

    ID3D11Device* device = Renderer::GetDevice();
    ID3D11DeviceContext* ctx = Renderer::GetDeviceContext();
    if (!device || !ctx) return;

    // ラジック: read particle positions from current pos SRV, write velocities to write UAV
    UINT adv_readIndex = static_cast<UINT>(m_Particle_PingPong_Index);
    UINT adv_writeIndex = 1u - adv_readIndex;

    // Lazy compile shader if not yet created
    if (!m_pDriveParticles_CS) {
        HRESULT hr = S_OK;
        ID3DBlob* csBlob = nullptr;
        ID3DBlob* errBlob = nullptr;
        // Compile from file (ensure shader file exists in project)
        hr = D3DCompileFromFile(L"shader\\drive_particles_with_lbm.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                 "CSMain", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &csBlob, &errBlob);
        if (FAILED(hr) || !csBlob) {
            if (errBlob) {
                OutputDebugStringA((const char*)errBlob->GetBufferPointer());
                errBlob->Release();
            }
            OutputDebugStringA("DriveParticlesWithLbmVelGPU: D3DCompileFromFile failed\n");
            return;
        }
        hr = device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_pDriveParticles_CS);
        csBlob->Release();
        if (FAILED(hr)) {
            OutputDebugStringA("DriveParticlesWithLbmVelGPU: CreateComputeShader failed\n");
            return;
        }
    }

    // Create / update constant buffer
    struct DriveParamsCB {
        UINT NumParticles;
        UINT NX;
        UINT NY;
        UINT NZ;
        float blend;
        float pad0[3];
        DirectX::XMFLOAT3 boundsMin;
        float pad1;
        DirectX::XMFLOAT3 boundsSize;
        float pad2;
    };
    DriveParamsCB params = {};
    params.NumParticles = static_cast<UINT>(mParticles.count);
    params.NX = NX;
    params.NY = NY;
    params.NZ = NZ;
    params.blend = blend;
    params.boundsMin = DirectX::XMFLOAT3(mMinBounds.x, mMinBounds.y, mMinBounds.z);
    params.boundsSize = DirectX::XMFLOAT3(mBoundsSize.x, mBoundsSize.y, mBoundsSize.z);

    if (!m_pDriveParticles_CB) {
        D3D11_BUFFER_DESC cbd = {};
        cbd.Usage = D3D11_USAGE_DYNAMIC;
        cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        cbd.ByteWidth = sizeof(DriveParamsCB);
        device->CreateBuffer(&cbd, nullptr, &m_pDriveParticles_CB);
    }
    // update CB
    if (m_pDriveParticles_CB) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx->Map(m_pDriveParticles_CB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            memcpy(mapped.pData, &params, sizeof(params));
            ctx->Unmap(m_pDriveParticles_CB, 0);
        }
    }

    // Bind resources: t0 = m_pLBM_Vel_SRV, t1 = m_pParticlePos_SRV[adv_readIndex], u0 = m_pParticleVel_UAV[adv_writeIndex]
    ctx->CSSetShader(m_pDriveParticles_CS, nullptr, 0);
    ID3D11ShaderResourceView* srvs[2] = { m_pLBM_Vel_SRV, m_pParticlePos_SRV[adv_readIndex] };
    ctx->CSSetShaderResources(0, 2, srvs);
    ID3D11UnorderedAccessView* uavs[1] = { m_pParticleVel_UAV[adv_writeIndex] };
    ctx->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    ctx->CSSetConstantBuffers(0, 1, &m_pDriveParticles_CB);

    // Dispatch
    UINT threads = 256;
    UINT groupCount = static_cast<UINT>((mParticles.count + threads - 1) / threads);
    ctx->Dispatch(groupCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
    ctx->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
    ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
    ctx->CSSetShaderResources(0, 2, nullSRVs);
    ctx->CSSetShader(nullptr, nullptr, 0);

    // Optionally: copy result into both buffers / set ping-pong indices according to your pipeline.
    // Here we copy write buffer back to read buffer so subsequent GPU passes can see updated velocities.
    if (m_pParticleVel_Buffer[adv_writeIndex] && m_pParticleVel_Buffer[adv_readIndex]) {
        ctx->CopyResource(m_pParticleVel_Buffer[adv_readIndex], m_pParticleVel_Buffer[adv_writeIndex]);
    }
}

// Smagorinsky パラメータセット / ゲット
void FluidSimulation::SetSmagorinskyParams(float Cs, float delta, float tauMin, float tauMax)
{
    mSmagorinskyCs = Cs;
    mSmagorinskyDelta = delta;
    mTauMin = tauMin;
    mTauMax = tauMax;
}

void FluidSimulation::GetSmagorinskyParams(float& Cs, float& delta, float& tauMin, float& tauMax) const
{
    Cs = mSmagorinskyCs;
    delta = mSmagorinskyDelta;
    tauMin = mTauMin;
    tauMax = mTauMax;
}

// --- 追加: Gravity API 実装 ---
void FluidSimulation::SetGravity(const Vector3& gravity)
{
    mGravity = gravity;
}

Vector3 FluidSimulation::GetGravity() const
{
    return mGravity;
}

void FluidSimulation::SetEnableGravity(bool enable)
{
    mEnableGravity = enable;
}

bool FluidSimulation::IsGravityEnabled() const
{
    return mEnableGravity;
}

bool FluidSimulation::TestVisualizeSingleCell(UINT axis, UINT slice, UINT x, UINT y, UINT z, float testValue)
{
    // validate grid coords
    if (axis > 2u) axis = 2u;
    if (slice >= static_cast<UINT>((axis == 0) ? NX : (axis == 1) ? NY : NZ)) {
        char dbg[256];
        sprintf_s(dbg, sizeof(dbg), "TestVisualizeSingleCell: invalid slice %u for axis %u\n", slice, axis);
        OutputDebugStringA(dbg);
        return false;
    }
    if (x >= (UINT)NX || y >= (UINT)NY || z >= (UINT)NZ) {
        char dbg[256];
        sprintf_s(dbg, sizeof(dbg), "TestVisualizeSingleCell: invalid xyz (%u,%u,%u)\n", x, y, z);
        OutputDebugStringA(dbg);
        return false;
    }

    // Ensure CPU rho vector allocated to grid size
    const size_t cellCount = static_cast<size_t>(NX) * static_cast<size_t>(NY) * static_cast<size_t>(NZ);
    if (mLbmRho.size() != cellCount) mLbmRho.assign(cellCount, 0.0f);
    std::fill(mLbmRho.begin(), mLbmRho.end(), 0.0f);

    auto idx_from_xyz = [&](UINT xi, UINT yi, UINT zi)->size_t {
        return static_cast<size_t>(zi) * static_cast<size_t>(NX) * static_cast<size_t>(NY)
             + static_cast<size_t>(yi) * static_cast<size_t>(NX)
             + static_cast<size_t>(xi);
    };

    size_t testIdx = idx_from_xyz(x, y, z);
    if (testIdx >= mLbmRho.size()) {
        OutputDebugStringA("TestVisualizeSingleCell: computed test index out of range\n");
        return false;
    }
    mLbmRho[testIdx] = testValue;

    // Optionally set velocities zero
    if (mLbmVel.size() != cellCount) mLbmVel.assign(cellCount, Vector3{0.0f,0.0f,0.0f});
    mLbmVel[testIdx] = Vector3{0.0f, 0.0f, 0.0f};

    // Force Visualize to regenerate texture from current CPU data
    UINT prevAxis = mLbmSliceAxis;
    mLbmSliceAxis = axis;
    VisualizeLBMSlice(slice);
    mLbmSliceAxis = prevAxis;

    if (!m_pLbmVizTexture) {
        OutputDebugStringA("TestVisualizeSingleCell: m_pLbmVizTexture is null after VisualizeLBMSlice\n");
        return false;
    }

    ID3D11Device* device = Renderer::GetDevice();
    ID3D11DeviceContext* ctx = Renderer::GetDeviceContext();
    if (!device || !ctx) {
        OutputDebugStringA("TestVisualizeSingleCell: device/context null\n");
        return false;
    }

    // Determine output pixel coords that correspond to the test cell, per HLSL mapping:
    // AxisZ: pixel (x,y)
    // AxisY: pixel (x,z)
    // AxisX: pixel (y,z)
    UINT px = 0, py = 0;
    UINT outW = (axis == 0u) ? NY : NX;
    UINT outH = (axis == 2u) ? NY : NZ;
    if (axis == 2u) { px = x; py = y; }
    else if (axis == 1u) { px = x; py = z; }
    else { px = y; py = z; }

    if (px >= outW || py >= outH) {
        char dbg[256];
        sprintf_s(dbg, sizeof(dbg), "TestVisualizeSingleCell: computed pixel (%u,%u) out of bounds for outW/outH (%u,%u)\n", px, py, outW, outH);
        OutputDebugStringA(dbg);
        return false;
    }

    // Create staging texture for readback
    D3D11_TEXTURE2D_DESC td = {};
    m_pLbmVizTexture->GetDesc(&td);
    D3D11_TEXTURE2D_DESC stdesc = td;
    stdesc.Usage = D3D11_USAGE_STAGING;
    stdesc.BindFlags = 0;
    stdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stdesc.MiscFlags = 0;

    ID3D11Texture2D* staging = nullptr;
    HRESULT hr = device->CreateTexture2D(&stdesc, nullptr, &staging);
    if (FAILED(hr) || !staging) {
        OutputDebugStringA("TestVisualizeSingleCell: CreateTexture2D(staging) failed\n");
        return false;
    }

    // Copy GPU texture to staging
    ctx->CopyResource(staging, m_pLbmVizTexture);

    // Map and read pixel
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        staging->Release();
        OutputDebugStringA("TestVisualizeSingleCell: Map(staging) failed\n");
        return false;
    }

    // mapped.RowPitch is in bytes. Each pixel is 4 floats (16 bytes).
    size_t rowPitchFloats = mapped.RowPitch / sizeof(float);
    size_t pixelOffset = static_cast<size_t>(py) * rowPitchFloats + static_cast<size_t>(px) * 4;
    float* data = reinterpret_cast<float*>(mapped.pData);
    float r = data[pixelOffset + 0];
    float g = data[pixelOffset + 1];
    float b = data[pixelOffset + 2];
    float a = data[pixelOffset + 3];

    ctx->Unmap(staging, 0);
    staging->Release();

    // Evaluate result: expect non-zero color (test value produced color)
    float brightness = fabsf(r) + fabsf(g) + fabsf(b);
    bool pass = (brightness > 1e-6f);

    {
        char dbg[512];
        sprintf_s(dbg, sizeof(dbg),
            "TestVisualizeSingleCell: axis=%u slice=%u cell=(%u,%u,%u) pixel=(%u,%u) rgba=(%f,%f,%f,%f) pass=%d\n",
            axis, slice, x, y, z, px, py, r, g, b, a, pass ? 1 : 0);
        OutputDebugStringA(dbg);
    }

    return pass;
}

