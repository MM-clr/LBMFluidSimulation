#include "guo_force.hlsl"

cbuffer LBMConstants : register(b0)
{
    float4 weights[19];
    int4 dirs[19];
    float tau;
    float3 gravity;
    uint3 gridSize;
    float pad1;
    float3 wind;
    float pad2;
    float forceScale;
    float maxLbmSpeed;
    uint boundaryMode; // 0=periodic, 1=wall(bounce-back)
    float pad3;
    float pad4;

    // Smagorinsky 用パラメータ（追加）
    float Cs; // Smagorinsky 定数（例 0.1?0.18）
    float delta; // フィルタ幅（格子幅、通常 1.0）
    float tau_min; // tau の下限（例 0.501）
    float tau_max; // tau の上限（例 20.0）
};

// LocalWindCB : b1 にモードとガウスσを追加
cbuffer LocalWindCB : register(b1)
{
    uint gNumLocalWinds; // 登録済み局所風源数
    uint gLocalWindMode; // 0=linear, 1=smoothstep, 2=gaussian
    float gLocalWindSigma; // ガウス時の sigma（grid 単位）
    float pad_lwcb;
}

// 局所風源構造体（HLSL 側）
struct LocalWind
{
    float3 center; // 格子座標系で渡すこと（例: (x,y,z) in [0..NX-1]）
    float radius;
    float3 direction; // 単位ベクトル（CPU側で正規化しておく）
    float strength;
};

// 入出力バッファ
StructuredBuffer<float> f_in : register(t0);
StructuredBuffer<int> is_solid : register(t1);
// t2 を LocalWind SRV に使用
StructuredBuffer<LocalWind> gLocalWinds : register(t2);

RWStructuredBuffer<float> f_out : register(u0);
RWStructuredBuffer<float> lbm_rho : register(u1);
RWStructuredBuffer<float3> lbm_vel : register(u2);

#define Q 19

int get_opposite_direction(int i)
{
    switch (i)
    {
        case 0:
            return 0;
        case 1:
            return 2;
        case 2:
            return 1;
        case 3:
            return 4;
        case 4:
            return 3;
        case 5:
            return 6;
        case 6:
            return 5;
        case 7:
            return 10;
        case 8:
            return 9;
        case 9:
            return 8;
        case 10:
            return 7;
        case 11:
            return 14;
        case 12:
            return 13;
        case 13:
            return 12;
        case 14:
            return 11;
        case 15:
            return 18;
        case 16:
            return 17;
        case 17:
            return 16;
        case 18:
            return 15;
    }
    return 0;
}

[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gridSize.x || id.y >= gridSize.y || id.z >= gridSize.z)
        return;

    uint index = id.z * gridSize.x * gridSize.y + id.y * gridSize.x + id.x;

    // 境界セル（solid）の処理
    if (is_solid[index] > 0)
    {
        for (int i = 0; i < Q; i++)
        {
            int opposite_i = get_opposite_direction(i);
            f_out[index * Q + i] = f_in[index * Q + opposite_i];
        }
        lbm_rho[index] = 1.0f;
        lbm_vel[index] = float3(0, 0, 0);
        return;
    }

    float rho = 0;
    float3 vel = float3(0, 0, 0);
    float f_local[Q];

    for (int i = 0; i < Q; i++)
    {
        f_local[i] = f_in[index * Q + i];

        // NaN/Inf チェック
        if (isnan(f_local[i]) || isinf(f_local[i]))
            f_local[i] = weights[i].x;

        rho += f_local[i];
        vel += f_local[i] * (float3) dirs[i].xyz;
    }

    // 密度が小さすぎる場合はリセット
    if (rho < 0.01f)
    {
        rho = 1.0f;
        vel = float3(0, 0, 0);
        for (int i = 0; i < Q; i++)
        {
            f_local[i] = weights[i].x * rho;
        }
    }
    else
    {
        vel /= rho;
    }

    // 外力（重力 + global 風）を適用（ここでのスケーリングは任意）
    float3 totalForce = gravity + wind;

    // --- 局所風源を合成（mode に応じて減衰関数を切替） ---
    // サンプリング位置: セル中心（格子座標系）を使用
    float3 pos = float3(id.x + 0.5f, id.y + 0.5f, id.z + 0.5f);

    if (gNumLocalWinds > 0)
    {
        // 注意: gNumLocalWinds <= 実際に作成した SRV のサイズ（CPU側で MAX_LOCAL_WINDS）
        for (uint si = 0; si < gNumLocalWinds; ++si)
        {
            LocalWind lw = gLocalWinds[si];
            float3 diff = pos - lw.center;
            float dist = length(diff);

            if (dist < lw.radius && lw.radius > 1e-6f)
            {
                // 正規化距離 (0=center, 1=radius)
                float nd = dist / lw.radius;
                float w = 0.0f;

                if (gLocalWindMode == 1u)
                {
                    // smoothstep: まず線形 1-nd, それを smoothstep で滑らかに
                    float t = saturate(1.0f - nd);
                    w = smoothstep(0.0f, 1.0f, t);
                }
                else if (gLocalWindMode == 2u)
                {
                    // gaussian: 中心で 1.0。sigma はグリッド単位（gLocalWindSigma）
                    float sigma = max(gLocalWindSigma, 1e-6f);
                    w = exp(-(dist * dist) / (2.0f * sigma * sigma));
                    // optionally normalize by exp(0)=1 so center=1
                }
                else
                {
                    // default: linear
                    w = saturate(1.0f - nd);
                }

                totalForce += lw.direction * (lw.strength * w);
            }
        }
    }

    // スケールを適用
    float3 F = forceScale * totalForce;

    // 速度に外力を反映（簡易的）
    vel += F;

    // 速度制限（LBM格子単位で0.1以下が安全）
    float velMag = length(vel);
    if (velMag > maxLbmSpeed)
    {
        vel = vel / velMag * maxLbmSpeed;
    }

    // NaN/Inf チェック
    if (any(isnan(vel)) || any(isinf(vel)))
    {
        vel = float3(0, 0, 0);
    }

    lbm_rho[index] = rho;
    lbm_vel[index] = vel;

    // lattice sound speed^2 (典型値)
    const float cs2 = 1.0f / 3.0f;
    const float EPS = 1e-12f;

    // --- Smagorinsky 系列の実装（Π^neq から S_ij を復元し tau_eff を計算） ---
    // 1) まず各方向の平衡分布 feq を計算して一時保存
    float feq_local[Q];
    float u_sq = dot(vel, vel);
    for (int i = 0; i < Q; i++)
    {
        float u_dot_c = dot(vel, (float3) dirs[i].xyz);
        feq_local[i] = weights[i].x * rho * (1.0f + 3.0f * u_dot_c + 4.5f * u_dot_c * u_dot_c - 1.5f * u_sq);
    }

    // 2) Π^neq の二次モーメントを評価：P_ab = sum_i (f_i - feq_i) c_i_a c_i_b
    float Pxx = 0.0f, Pxy = 0.0f, Pxz = 0.0f;
    float Pyy = 0.0f, Pyz = 0.0f, Pzz = 0.0f;
    for (int i = 0; i < Q; ++i)
    {
        float df = f_local[i] - feq_local[i];
        float3 c = (float3) dirs[i].xyz;
        Pxx += df * c.x * c.x;
        Pxy += df * c.x * c.y;
        Pxz += df * c.x * c.z;
        Pyy += df * c.y * c.y;
        Pyz += df * c.y * c.z;
        Pzz += df * c.z * c.z;
    }

    // 3) 基礎粘性 nu0 と S_ij を復元
    float nu0 = cs2 * (tau - 0.5f);
    float denom = 2.0f * rho * nu0;
    if (abs(denom) < EPS)
        denom = EPS;

    float invTwoRhoNu = 1.0f / denom;

    float Sxx = -Pxx * invTwoRhoNu;
    float Sxy = -Pxy * invTwoRhoNu;
    float Sxz = -Pxz * invTwoRhoNu;
    float Syy = -Pyy * invTwoRhoNu;
    float Syz = -Pyz * invTwoRhoNu;
    float Szz = -Pzz * invTwoRhoNu;

    // 4) |S| を評価して Smagorinsky 粘性 ν_t を算出
    float Ssq = 2.0f * (Sxx * Sxx + Sxy * Sxy + Sxz * Sxz + Syy * Syy + Syz * Syz + Szz * Szz);
    Ssq = max(Ssq, 0.0f);
    float Smag = sqrt(Ssq);

    float Cs_eff = max(Cs, 0.0f);
    float delta_eff = max(delta, 1e-6f);
    float nu_t = (Cs_eff * delta_eff) * (Cs_eff * delta_eff) * Smag;

    // 5) 有効粘性・有効 tau を計算しクリップ
    float nu_eff = nu0 + nu_t;
    float tau_eff = 0.5f + nu_eff / cs2;
    tau_eff = clamp(tau_eff, tau_min, tau_max);

    // ローカルの緩和率（omega）
    float omega_loc = 1.0f / max(tau_eff, 1e-6f);

    // --- 衝突・伝播ステップ（tau -> tau_eff を使用） ---
    for (int i = 0; i < Q; i++)
    {
        float f_post_collision = f_local[i] - omega_loc * (f_local[i] - feq_local[i]);

        // Guo の強制項を追加：力項は tau に依存するため tau_eff を与える
        float fi_force = GuoForceScalar(weights[i].x, (float3) dirs[i].xyz, vel, F, cs2, tau_eff);
        f_post_collision += fi_force;

        // NaN/Inf チェック
        if (isnan(f_post_collision) || isinf(f_post_collision))
            f_post_collision = weights[i].x * rho;

        int3 push_pos = (int3) id + dirs[i].xyz;

        // Boundary handling modes:
        // 0 = periodic wrap-around (existing behavior)
        // 1 = wall (bounce-back): clamp to boundary and reflect by writing to opposite direction at boundary cell
        if (boundaryMode == 0u) {
            push_pos.x = (push_pos.x + (int) gridSize.x) % (int) gridSize.x;
            push_pos.y = (push_pos.y + (int) gridSize.y) % (int) gridSize.y;
            push_pos.z = (push_pos.z + (int) gridSize.z) % (int) gridSize.z;
        }
        else {
            // wall bounce-back: if neighbor is outside, instead write opposite direction back to current cell
            int3 neighbor = push_pos;
            bool outside = (neighbor.x < 0 || neighbor.x >= (int)gridSize.x || neighbor.y < 0 || neighbor.y >= (int)gridSize.y || neighbor.z < 0 || neighbor.z >= (int)gridSize.z);
            if (outside) {
                // write f_post_collision to current index but in opposite direction (bounce-back)
                int opposite_i = get_opposite_direction(i);
                f_out[index * Q + opposite_i] = f_post_collision;
                continue; // already written, skip default write
            }
            // otherwise inside - normal behavior
            neighbor.x = (neighbor.x + (int)gridSize.x) % (int)gridSize.x;
            neighbor.y = (neighbor.y + (int)gridSize.y) % (int)gridSize.y;
            neighbor.z = (neighbor.z + (int)gridSize.z) % (int)gridSize.z;
            push_pos = neighbor;
        }

        uint next_index = push_pos.z * gridSize.x * gridSize.y + push_pos.y * gridSize.x + push_pos.x;
        f_out[next_index * Q + i] = f_post_collision;
    }
}