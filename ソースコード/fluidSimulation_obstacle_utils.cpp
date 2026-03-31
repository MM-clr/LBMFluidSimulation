#include "fluidSimulation_obstacle_utils.h"
#include <DirectXMath.h>

// ヘルパ: 二乗長さ
static inline float LenSq(float x, float y, float z) {
    return x*x + y*y + z*z;
}

// 点と三角形の距離（二乗）を計算する実装（Real-Time Collision Detection / Eberly）
float PointTriangleDistanceSquared(const DirectX::XMFLOAT3& p,
                                   const DirectX::XMFLOAT3& a,
                                   const DirectX::XMFLOAT3& b,
                                   const DirectX::XMFLOAT3& c)
    {
        // ベクトル計算
    float abx = b.x - a.x; float aby = b.y - a.y; float abz = b.z - a.z;
    float acx = c.x - a.x; float acy = c.y - a.y; float acz = c.z - a.z;
    float apx = p.x - a.x; float apy = p.y - a.y; float apz = p.z - a.z;

    float d1 = abx*apx + aby*apy + abz*apz;
    float d2 = acx*apx + acy*apy + acz*apz;
    if (d1 <= 0.0f && d2 <= 0.0f) {
        // 頂点Aに最も近い
        return LenSq(apx, apy, apz);
    }

    // 頂点Bをチェック
    float bpx = p.x - b.x; float bpy = p.y - b.y; float bpz = p.z - b.z;
    float d3 = abx*bpx + aby*bpy + abz*bpz;
    float d4 = acx*bpx + acy*bpy + acz*bpz;
    if (d3 >= 0.0f && d4 <= d3) {
        return LenSq(bpx, bpy, bpz);
    }

    // 辺 AB
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        float projx = a.x + v * abx;
        float projy = a.y + v * aby;
        float projz = a.z + v * abz;
        float dx = p.x - projx, dy = p.y - projy, dz = p.z - projz;
        return LenSq(dx, dy, dz);
    }

    // 頂点Cをチェック
    float cpx = p.x - c.x; float cpy = p.y - c.y; float cpz = p.z - c.z;
    float d5 = abx*cpx + aby*cpy + abz*cpz;
    float d6 = acx*cpx + acy*cpy + acz*cpz;
    if (d6 >= 0.0f && d5 <= d6) {
        return LenSq(cpx, cpy, cpz);
    }

    // 辺 AC
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        float projx = a.x + w * acx;
        float projy = a.y + w * acy;
        float projz = a.z + w * acz;
        float dx = p.x - projx, dy = p.y - projy, dz = p.z - projz;
        return LenSq(dx, dy, dz);
    }

    // 辺 BC
    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        float bcx = c.x - b.x, bcy = c.y - b.y, bcz = c.z - b.z;
        float projx = b.x + w * bcx;
        float projy = b.y + w * bcy;
        float projz = b.z + w * bcz;
        float dx = p.x - projx, dy = p.y - projy, dz = p.z - projz;
        return LenSq(dx, dy, dz);
    }

    // 面内部領域: 平面への射影を計算
    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    float projx = a.x + abx * v + acx * w;
    float projy = a.y + aby * v + acy * w;
    float projz = a.z + abz * v + acz * w;
    float dx = p.x - projx, dy = p.y - projy, dz = p.z - projz;
    return LenSq(dx, dy, dz);
}
