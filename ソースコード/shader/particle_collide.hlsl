// GPU 衝突解決: BuildGrid + ResolveCollisions
// ビルド時にエントリポイント名を常に "main" にできるよう、
// 実処理をそれぞれの実装関数に分離し、main() で切り替えます。
// コンパイル時に /D BUILDGRID を付ければ BuildGrid 用、付けなければ ResolveCollisions 用としてビルドされます。

cbuffer CollideConstants : register(b0)
{
    uint numParticles;
    float3 boundsMin;
    float pad0;
    float3 boundsSize;
    float pad1;
    uint3 gridDim; // gx, gy, gz
    uint maxPerCell; // MAX_PER_CELL
    float cellSize; // セル幅
    float pad2;
};

StructuredBuffer<float3> particlePosIn : register(t0);
StructuredBuffer<float3> particleVelIn : register(t1);

// Build phase: cellCounts[cell] += 1; cellLists[cell*MAX + idx] = particleIndex;
RWStructuredBuffer<uint> cellCounts : register(u0);
RWStructuredBuffer<uint> cellLists : register(u1); // flattened (cell * MAX_PER_CELL + slot) -> particleIndex

[numthreads(256, 1, 1)]
void BuildGrid_impl(uint3 tid)
{
    uint id = tid.x;
    if (id >= numParticles)
        return;

    float3 p = particlePosIn[id];
    float3 rel = p - boundsMin;
    int ix = (int) floor(rel.x / cellSize);
    int iy = (int) floor(rel.y / cellSize);
    int iz = (int) floor(rel.z / cellSize);

    // clamp indices
    ix = clamp(ix, 0, (int) gridDim.x - 1);
    iy = clamp(iy, 0, (int) gridDim.y - 1);
    iz = clamp(iz, 0, (int) gridDim.z - 1);

    uint cellIndex = (uint) ix + (uint) iy * gridDim.x + (uint) iz * gridDim.x * gridDim.y;
    uint slot;
    InterlockedAdd(cellCounts[cellIndex], 1, slot);
    if (slot < maxPerCell)
    {
        uint writeIndex = cellIndex * maxPerCell + slot;
        cellLists[writeIndex] = id;
    }
}

// Resolve phase: read cellCounts & cellLists (as SRV), examine neighbors and produce new positions/velocities
StructuredBuffer<uint> cellCountsSRV : register(t2);
StructuredBuffer<uint> cellListsSRV : register(t3);
RWStructuredBuffer<float3> particlePosOut : register(u2);
RWStructuredBuffer<float3> particleVelOut : register(u3);

[numthreads(256, 1, 1)]
void ResolveCollisions_impl(uint3 tid)
{
    uint id = tid.x;
    if (id >= numParticles)
        return;

    float3 pi = particlePosIn[id];
    float3 vi = particleVelIn[id];

    // grid indices
    float3 rel = pi - boundsMin;
    int ix = (int) floor(rel.x / cellSize);
    int iy = (int) floor(rel.y / cellSize);
    int iz = (int) floor(rel.z / cellSize);
    ix = clamp(ix, 0, (int) gridDim.x - 1);
    iy = clamp(iy, 0, (int) gridDim.y - 1);
    iz = clamp(iz, 0, (int) gridDim.z - 1);

    float diameter = cellSize; // assume cellSize == particle diameter (設計上調整可能)
    float diameterSq = diameter * diameter;

    for (int dx = -1; dx <= 1; ++dx)
    {
        int nx = ix + dx;
        if (nx < 0 || nx >= (int) gridDim.x)
            continue;
        for (int dy = -1; dy <= 1; ++dy)
        {
            int ny = iy + dy;
            if (ny < 0 || ny >= (int) gridDim.y)
                continue;
            for (int dz = -1; dz <= 1; ++dz)
            {
                int nz = iz + dz;
                if (nz < 0 || nz >= (int) gridDim.z)
                    continue;

                uint ncell = (uint) nx + (uint) ny * gridDim.x + (uint) nz * gridDim.x * gridDim.y;
                uint count = cellCountsSRV[ncell];
                uint base = ncell * maxPerCell;
                for (uint s = 0; s < count && s < maxPerCell; ++s)
                {
                    uint j = cellListsSRV[base + s];
                    if (j == id)
                        continue;
                    // guard: if j is invalid sentinel (optional), skip
                    // if (j == 0xffffffffu) continue;

                    float3 pj = particlePosIn[j];
                    float3 vj = particleVelIn[j];
                    float3 diff = pi - pj;
                    float distSq = dot(diff, diff);
                    if (distSq <= 0.0f)
                        continue;
                    if (distSq < diameterSq)
                    {
                        float dist = sqrt(distSq);
                        float3 nrm = diff / dist;
                        float overlap = diameter - dist;
                        // simple position correction split
                        float k = 0.5f;
                        pi += nrm * (overlap * k);
                        // for j we can't write here (would race). Instead apply impulse to velocities symmetrically.
                        float rel = dot(vi - vj, nrm);
                        if (rel < 0.0f)
                        {
                            float restitution = 0.2f; // 固定（必要なら Constants に入れる）
                            float impulse = -(1.0f + restitution) * rel * 0.5f;
                            vi += nrm * impulse;
                        }
                    }
                }
            }
        }
    }

    particlePosOut[id] = pi;
    particleVelOut[id] = vi;
}

// 常に entry point は 'main' にするためのラッパー
[numthreads(256, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
#ifdef BUILDGRID
    BuildGrid_impl(tid);
#else
    ResolveCollisions_impl(tid);
#endif
}