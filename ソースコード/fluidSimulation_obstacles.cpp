#include "fluidSimulation.h"
#include "renderer.h"
#include <d3d11.h>
#include <algorithm>
#include <vector>
#include <cstring>
#include <DirectXMath.h>
#include "fluidSimulation_obstacle_utils.h"

// 注: 点-三角形距離の実装は `fluidSimulation_obstacle_utils.cpp` に移動しました。
//       ヘッダ `fluidSimulation_obstacle_utils.h` を介して関数を使用します。

void FluidSimulation::AddObstacle(const OBB& /*obb*/)
{
    // Placeholder: keep no-op (see earlier comment). Use SetObstaclesFromScene() after scene update.
}

void FluidSimulation::ClearObstacles()
{
    mObstacles.clear();
    mIsSolid.clear();

    if (m_pObstacle_Buffer) { m_pObstacle_Buffer->Release(); m_pObstacle_Buffer = nullptr; }
    if (m_pObstacle_SRV) { m_pObstacle_SRV->Release(); m_pObstacle_SRV = nullptr; }

    if (m_pLBM_IsSolid_Buffer) { m_pLBM_IsSolid_Buffer->Release(); m_pLBM_IsSolid_Buffer = nullptr; }
    if (m_pLBM_IsSolid_SRV) { m_pLBM_IsSolid_SRV->Release(); m_pLBM_IsSolid_SRV = nullptr; }
}

// SetObstaclesFromScene: voxelize triangles with distance test (CPU)
// - Uses mTriangleInstanceData (world-space triangle verts).
// - Converts triangle verts -> grid space, computes per-triangle grid AABB,
//   then marks cell centers whose distance to triangle <= threshold as solid.
// - Uploads m_pObstacle_Buffer (GPUObstacle[]) for a coarse obstacle list (limited to MAX_OBSTACLES)
//   and m_pLBM_IsSolid_Buffer (int per cell) for per-cell solid mask used by LBM.
void FluidSimulation::SetObstaclesFromScene()
{
    ID3D11Device* device = Renderer::GetDevice();
    ID3D11DeviceContext* ctx = Renderer::GetDeviceContext();
    if (!device || !ctx) return;

    // Build coarse GPUObstacle list from triangle instance data (limit to MAX_OBSTACLES)
    std::vector<GPUObstacle> gpuObs;
    gpuObs.reserve(std::min<size_t>(mTriangleInstanceData.size(), MAX_OBSTACLES));
    for (size_t i = 0; i < mTriangleInstanceData.size() && gpuObs.size() < MAX_OBSTACLES; ++i)
    {
        const TriangleInstanceData& t = mTriangleInstanceData[i];
        float vx0 = t.v1.x, vy0 = t.v1.y, vz0 = t.v1.z;
        float vx1 = t.v2.x, vy1 = t.v2.y, vz1 = t.v2.z;
        float vx2 = t.v3.x, vy2 = t.v3.y, vz2 = t.v3.z;

        float minx = std::min(std::min(vx0, vx1), vx2);
        float miny = std::min(std::min(vy0, vy1), vy2);
        float minz = std::min(std::min(vz0, vz1), vz2);
        float maxx = std::max(std::max(vx0, vx1), vx2);
        float maxy = std::max(std::max(vy0, vy1), vy2);
        float maxz = std::max(std::max(vz0, vz1), vz2);

        float cxw = 0.5f * (minx + maxx);
        float cyw = 0.5f * (miny + maxy);
        float czw = 0.5f * (minz + maxz);
        float hsxw = 0.5f * (maxx - minx);
        float hsyw = 0.5f * (maxy - miny);
        float hszw = 0.5f * (maxz - minz);

        float sx = (mBoundsSize.x > 0.0f) ? (float)(NX - 1) / mBoundsSize.x : 0.0f;
        float sy = (mBoundsSize.y > 0.0f) ? (float)(NY - 1) / mBoundsSize.y : 0.0f;
        float sz = (mBoundsSize.z > 0.0f) ? (float)(NZ - 1) / mBoundsSize.z : 0.0f;

        float cxg = (cxw - mMinBounds.x) * sx;
        float cyg = (cyw - mMinBounds.y) * sy;
        float czg = (czw - mMinBounds.z) * sz;

        float hsxg = hsxw * sx;
        float hsyg = hsyw * sy;
        float hszg = hszw * sz;

        GPUObstacle go = {};
        go.center.x = cxg; go.center.y = cyg; go.center.z = czg; go.pad0 = 0.0f;
        go.halfSize.x = hsxg; go.halfSize.y = hsyg; go.halfSize.z = hszg; go.pad1 = 0.0f;
        go.axisX.x = 1.0f; go.axisX.y = 0.0f; go.axisX.z = 0.0f; go.pad2 = 0.0f;
        go.axisY.x = 0.0f; go.axisY.y = 1.0f; go.axisY.z = 0.0f; go.pad3 = 0.0f;
        go.axisZ.x = 0.0f; go.axisZ.y = 0.0f; go.axisZ.z = 1.0f; go.pad4 = 0.0f;

        gpuObs.push_back(go);
    }

    // Replace CPU-side obstacle container
    mObstacles.clear();
    for (const auto& g : gpuObs) mObstacles.push_back(g);

    // Upload obstacle structured buffer (GPUObstacle[]). Always provide an SRV (create dummy if none)
    if (m_pObstacle_Buffer) { m_pObstacle_Buffer->Release(); m_pObstacle_Buffer = nullptr; }
    if (m_pObstacle_SRV) { m_pObstacle_SRV->Release(); m_pObstacle_SRV = nullptr; }

    if (!gpuObs.empty())
    {
        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = (UINT)(sizeof(GPUObstacle) * gpuObs.size());
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bd.CPUAccessFlags = 0;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(GPUObstacle);

        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = gpuObs.data();

        HRESULT hr = device->CreateBuffer(&bd, &init, &m_pObstacle_Buffer);
        if (FAILED(hr) || !m_pObstacle_Buffer) {
            OutputDebugStringA("SetObstaclesFromScene: CreateBuffer(GPUObstacle) failed\n");
        } else {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
            srvd.Format = DXGI_FORMAT_UNKNOWN;
            srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            srvd.Buffer.FirstElement = 0;
            srvd.Buffer.NumElements = (UINT)gpuObs.size();
            hr = device->CreateShaderResourceView(m_pObstacle_Buffer, &srvd, &m_pObstacle_SRV);
            if (FAILED(hr) || !m_pObstacle_SRV) {
                OutputDebugStringA("SetObstaclesFromScene: CreateShaderResourceView(GPUObstacle) failed\n");
            }
        }
    }
    else
    {
        // create a 1-element dummy buffer so shaders can bind a valid SRV even when no obstacles
        GPUObstacle dummy = {};
        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = (UINT)sizeof(GPUObstacle);
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bd.CPUAccessFlags = 0;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(GPUObstacle);
        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = &dummy;
        HRESULT hr = device->CreateBuffer(&bd, &init, &m_pObstacle_Buffer);
        if (SUCCEEDED(hr) && m_pObstacle_Buffer) {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
            srvd.Format = DXGI_FORMAT_UNKNOWN;
            srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            srvd.Buffer.FirstElement = 0;
            srvd.Buffer.NumElements = 1;
            hr = device->CreateShaderResourceView(m_pObstacle_Buffer, &srvd, &m_pObstacle_SRV);
            if (FAILED(hr) || !m_pObstacle_SRV) {
                OutputDebugStringA("SetObstaclesFromScene: CreateShaderResourceView(dummy GPUObstacle) failed\n");
            }
        } else {
            OutputDebugStringA("SetObstaclesFromScene: CreateBuffer(dummy GPUObstacle) failed\n");
        }
    }

    // --- Voxelization using triangle distance test ---
    const size_t totalCells = (size_t)NX * (size_t)NY * (size_t)NZ;
    mIsSolid.assign(totalCells, 0);

    // Prepare grid scale factors (world -> grid)
    float sx = (mBoundsSize.x > 0.0f) ? (float)(NX - 1) / mBoundsSize.x : 0.0f;
    float sy = (mBoundsSize.y > 0.0f) ? (float)(NY - 1) / mBoundsSize.y : 0.0f;
    float sz = (mBoundsSize.z > 0.0f) ? (float)(NZ - 1) / mBoundsSize.z : 0.0f;

    // threshold in grid units (cell size == 1). Use ~0.8 to capture thin geometry.
    const float threshold = 0.8f;
    const float thrSq = threshold * threshold;

    // For each triangle in scene, mark nearby cells
    for (size_t ti = 0; ti < mTriangleInstanceData.size(); ++ti)
    {
        const TriangleInstanceData& t = mTriangleInstanceData[ti];

        // Convert triangle vertices to grid space
        DirectX::XMFLOAT3 aGrid, bGrid, cGrid;
        aGrid.x = (t.v1.x - mMinBounds.x) * sx;
        aGrid.y = (t.v1.y - mMinBounds.y) * sy;
        aGrid.z = (t.v1.z - mMinBounds.z) * sz;
        bGrid.x = (t.v2.x - mMinBounds.x) * sx;
        bGrid.y = (t.v2.y - mMinBounds.y) * sy;
        bGrid.z = (t.v2.z - mMinBounds.z) * sz;
        cGrid.x = (t.v3.x - mMinBounds.x) * sx;
        cGrid.y = (t.v3.y - mMinBounds.y) * sy;
        cGrid.z = (t.v3.z - mMinBounds.z) * sz;

        // Compute integer AABB in grid space with margin = ceil(threshold)
        float minx = std::min(std::min(aGrid.x, bGrid.x), cGrid.x) - threshold;
        float miny = std::min(std::min(aGrid.y, bGrid.y), cGrid.y) - threshold;
        float minz = std::min(std::min(aGrid.z, bGrid.z), cGrid.z) - threshold;
        float maxx = std::max(std::max(aGrid.x, bGrid.x), cGrid.x) + threshold;
        float maxy = std::max(std::max(aGrid.y, bGrid.y), cGrid.y) + threshold;
        float maxz = std::max(std::max(aGrid.z, bGrid.z), cGrid.z) + threshold;

        int ix0 = (int)floorf(minx);
        int iy0 = (int)floorf(miny);
        int iz0 = (int)floorf(minz);
        int ix1 = (int)ceilf(maxx);
        int iy1 = (int)ceilf(maxy);
        int iz1 = (int)ceilf(maxz);

        // clamp to grid
        ix0 = std::max(ix0, 0); iy0 = std::max(iy0, 0); iz0 = std::max(iz0, 0);
        ix1 = std::min(ix1, NX - 1); iy1 = std::min(iy1, NY - 1); iz1 = std::min(iz1, NZ - 1);

        for (int z = iz0; z <= iz1; ++z) {
            for (int y = iy0; y <= iy1; ++y) {
                size_t base = (size_t)z * (size_t)NX * (size_t)NY + (size_t)y * (size_t)NX;
                for (int x = ix0; x <= ix1; ++x) {
                    // compute cell center in grid coords (consistent with visualize shader)
                    DirectX::XMFLOAT3 cellCenter;
                    cellCenter.x = (float)x + 0.5f;
                    cellCenter.y = (float)y + 0.5f;
                    cellCenter.z = (float)z + 0.5f;

                    // compute squared distance to triangle (all in grid space)
                    float distSq = PointTriangleDistanceSquared(cellCenter, aGrid, bGrid, cGrid);
                    if (distSq <= thrSq) {
                        mIsSolid[base + (size_t)x] = 1;
                    }
                }
            }
        }
    }

    // Upload mIsSolid to GPU structured buffer (int per cell) ? keep as int to match LBM_Init
    if (m_pLBM_IsSolid_Buffer) { m_pLBM_IsSolid_Buffer->Release(); m_pLBM_IsSolid_Buffer = nullptr; }
    if (m_pLBM_IsSolid_SRV) { m_pLBM_IsSolid_SRV->Release(); m_pLBM_IsSolid_SRV = nullptr; }

    if (totalCells > 0)
    {
        // Convert to INT buffer for GPU (match LBM_Init which used sizeof(int))
        std::vector<int> solidData;
        solidData.resize(totalCells);
        for (size_t i = 0; i < totalCells; ++i) solidData[i] = (mIsSolid[i] != 0) ? 1 : 0;

        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = (UINT)(totalCells * sizeof(int));
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bd.CPUAccessFlags = 0;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(int);

        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = solidData.data();

        HRESULT hr = device->CreateBuffer(&bd, &init, &m_pLBM_IsSolid_Buffer);
        if (FAILED(hr) || !m_pLBM_IsSolid_Buffer) {
            OutputDebugStringA("SetObstaclesFromScene: CreateBuffer(IsSolid) failed\n");
        } else {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
            srvd.Format = DXGI_FORMAT_UNKNOWN;
            srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            srvd.Buffer.FirstElement = 0;
            srvd.Buffer.NumElements = (UINT)totalCells;
            hr = device->CreateShaderResourceView(m_pLBM_IsSolid_Buffer, &srvd, &m_pLBM_IsSolid_SRV);
            if (FAILED(hr) || !m_pLBM_IsSolid_SRV) {
                OutputDebugStringA("SetObstaclesFromScene: CreateShaderResourceView(IsSolid) failed\n");
            }
        }
    }
}