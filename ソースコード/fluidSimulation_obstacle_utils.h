#pragma once

#include <DirectXMath.h>

// Helper: 点と三角形の距離（二乗）を計算するユーティリティ
// p,a,b,c は同じ座標系（ここではグリッド座標）で与えること
float PointTriangleDistanceSquared(const DirectX::XMFLOAT3& p,
                                   const DirectX::XMFLOAT3& a,
                                   const DirectX::XMFLOAT3& b,
                                   const DirectX::XMFLOAT3& c);
