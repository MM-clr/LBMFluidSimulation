// visualize_lbm_slice.hlsl
// LBM スライス可視化用 ComputeShader (LIC 実装)
// cbuffer のレイアウトは CPU 側の VizCB と一致させること

cbuffer VizCB : register(b0)
{
    uint3 gridDim; // gridDim.x = NX, gridDim.y = NY, gridDim.z = NZ
    uint axis; // 0 = X, 1 = Y, 2 = Z
    uint slice; // スライスインデックス
    uint mode; // 0 = VizRho, 1 = VizVelMag, 2 = VizVelVec

    float rhoMin;
    float rhoMax;
    float pad0;
    float velBlendLow;

    float velBlendHigh;
    float vizVelScale;
    float pad1;
    // LIC パラメータ（16 バイト境界内に収める）
    uint licSamples; // 両方向それぞれのサンプル数
    float licStep; // ステップ幅（格子単位）
    float licNoiseScale; // ノイズ UV のスケール
    float pad2;
};

// 入力バッファ / 出力
StructuredBuffer<float> lbm_rho : register(t0);
StructuredBuffer<float3> lbm_vel : register(t1); // 速度データ (XMFLOAT3 相当)
RWTexture2D<float4> outTex : register(u0);

// 共通カラーマップ関数（密度・速度大きさ両方で同じマップを使う）
float3 ColorMap(float v)
{
    // v: 0..1
    float3 c0 = float3(0.0f, 0.0f, 1.0f);
    float3 c1 = float3(0.0f, 1.0f, 1.0f);
    float3 c2 = float3(0.0f, 1.0f, 0.0f);
    float3 c3 = float3(1.0f, 1.0f, 0.0f);
    float3 c4 = float3(1.0f, 0.0f, 0.0f);

    const float s0 = 0.0f;
    const float s1 = 0.25f;
    const float s2 = 0.5f;
    const float s3 = 0.75f;
    const float s4 = 1.0f;

    float3 col = c0;
    if (v <= s1)
    {
        float t = smoothstep(0.0f, 1.0f, (v - s0) / (s1 - s0));
        col = lerp(c0, c1, t);
    }
    else if (v <= s2)
    {
        float t = smoothstep(0.0f, 1.0f, (v - s1) / (s2 - s1));
        col = lerp(c1, c2, t);
    }
    else if (v <= s3)
    {
        float t = smoothstep(0.0f, 1.0f, (v - s2) / (s3 - s2));
        col = lerp(c2, c3, t);
    }
    else
    {
        float t = smoothstep(0.0f, 1.0f, (v - s3) / (s4 - s3));
        col = lerp(c3, c4, t);
    }

    const float gamma = 1.0f;
    col = pow(col, float3(1.0f / gamma, 1.0f / gamma, 1.0f / gamma));
    return col;
}

// 簡易プロシージャルノイズ（2D） ? 基礎
float Hash21(float2 p)
{
    float h = sin(dot(p, float2(127.1f, 311.7f)));
    h = h * 43758.5453123f;
    return frac(h);
}
float ProceduralNoise(float2 uv)
{
    float2 p = uv;
    float n0 = Hash21(floor(p));
    float n1 = Hash21(floor(p + float2(1.0f, 0.0f)));
    float n2 = Hash21(floor(p + float2(0.0f, 1.0f)));
    float n3 = Hash21(floor(p + float2(1.0f, 1.0f)));
    float fx = frac(p.x);
    float fy = frac(p.y);
    float a = lerp(n0, n1, fx);
    float b = lerp(n2, n3, fx);
    return lerp(a, b, fy);
}

// fBm（複数オクターブ合成）でノイズを滑らかにする
float FBMNoise(float2 uv)
{
    // 3オクターブ程度で十分（コスト調整可能）
    float sum = 0.0f;
    float amp = 0.5f;
    float freq = 1.0f;
    float ampSum = 0.0f;
    [unroll]
    for (int o = 0; o < 3; ++o)
    {
        sum += amp * ProceduralNoise(uv * freq);
        ampSum += amp;
        freq *= 2.0f;
        amp *= 0.5f;
    }
    return (ampSum > 0.0f) ? sum / ampSum : 0.0f;
}

// ヘルパ: グリッド内の速度を nearest-sample で取得
float3 GetVelAt(int xi, int yi, int zi, uint NX, uint NY, uint NZ)
{
    if (xi < 0 || xi >= (int) NX || yi < 0 || yi >= (int) NY || zi < 0 || zi >= (int) NZ)
        return float3(0.0f, 0.0f, 0.0f);
    uint idx = (uint) zi * NX * NY + (uint) yi * NX + (uint) xi;
    return lbm_vel[idx];
}

// 連続座標 (浮動小数) で近傍速度を返す（簡易トリリニア相当）
float3 SampleVel(float fx, float fy, float fz, uint NX, uint NY, uint NZ)
{
    int x0 = (int) floor(fx);
    int y0 = (int) floor(fy);
    int z0 = (int) floor(fz);
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    int z1 = z0 + 1;

    float3 v000 = GetVelAt(x0, y0, z0, NX, NY, NZ);
    float3 v100 = GetVelAt(x1, y0, z0, NX, NY, NZ);
    float3 v010 = GetVelAt(x0, y1, z0, NX, NY, NZ);
    float3 v110 = GetVelAt(x1, y1, z0, NX, NY, NZ);
    float3 v001 = GetVelAt(x0, y0, z1, NX, NY, NZ);
    float3 v101 = GetVelAt(x1, y0, z1, NX, NY, NZ);
    float3 v011 = GetVelAt(x0, y1, z1, NX, NY, NZ);
    float3 v111 = GetVelAt(x1, y1, z1, NX, NY, NZ);

    float wx = fx - (float) x0;
    float wy = fy - (float) y0;
    float wz = fz - (float) z0;

    float3 c00 = lerp(v000, v100, wx);
    float3 c10 = lerp(v010, v110, wx);
    float3 c01 = lerp(v001, v101, wx);
    float3 c11 = lerp(v011, v111, wx);

    float3 c0 = lerp(c00, c10, wy);
    float3 c1 = lerp(c01, c11, wy);

    return lerp(c0, c1, wz);
}

// [numthreads] は C++ から dispatch されるグループサイズに合わせる (8x8)
[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint NX = gridDim.x;
    uint NY = gridDim.y;
    uint NZ = gridDim.z;

    // 出力幅 / 高さを軸に応じて決定（出力ピクセル = グリッドセル）
    uint outW = (axis == 0u) ? NY : NX;
    uint outH = (axis == 2u) ? NY : NZ;

    if (tid.x >= outW || tid.y >= outH)
        return;

    // スライスに対応するグリッド座標 (浮動小数, 中心)
    float gx = 0.0f, gy = 0.0f, gz = 0.0f;
    if (axis == 2u)
    { // Z slice: tid.x -> x, tid.y -> y, z = slice
        gx = (float) tid.x + 0.5f;
        gy = (float) tid.y + 0.5f;
        gz = (float) slice + 0.5f;
    }
    else if (axis == 1u)
    { // Y slice: tid.x -> x, tid.y -> z, y = slice
        gx = (float) tid.x + 0.5f;
        gy = (float) slice + 0.5f;
        gz = (float) tid.y + 0.5f;
    }
    else
    { // X slice: tid.x -> y, tid.y -> z, x = slice
        gx = (float) slice + 0.5f;
        gy = (float) tid.x + 0.5f;
        gz = (float) tid.y + 0.5f;
    }

    // 整数インデックス
    uint xi = 0u, yi = 0u, zi = 0u;
    if (axis == 2u)
    {
        xi = tid.x;
        yi = tid.y;
        zi = slice;
    }
    else if (axis == 1u)
    {
        xi = tid.x;
        yi = slice;
        zi = tid.y;
    }
    else
    {
        xi = slice;
        yi = tid.x;
        zi = tid.y;
    }
    uint idx = zi * NX * NY + yi * NX + xi;
    if (xi >= NX || yi >= NY || zi >= NZ)
    {
        outTex[uint2(tid.x, tid.y)] = float4(0, 0, 0, 1);
        return;
    }

    // LIC 実行（プロシージャル fBm ノイズ）
    uint samples = max(1u, licSamples);
    float halfCount = (float) (samples * 2u + 1u);
    float accum = 0.0f;

    // 中心ノイズ UV（出力平面ピクセル→0..1）
    float2 uvCenter;
    if (axis == 2u)
        uvCenter = float2(gx / max(1.0f, (float) (NX - 1)), gy / max(1.0f, (float) (NY - 1)));
    else if (axis == 1u)
        uvCenter = float2(gx / max(1.0f, (float) (NX - 1)), gz / max(1.0f, (float) (NZ - 1)));
    else
        uvCenter = float2(gy / max(1.0f, (float) (NY - 1)), gz / max(1.0f, (float) (NZ - 1)));

    // プロシージャルノイズは licNoiseScale によって調整
    float baseNoise = FBMNoise(uvCenter * licNoiseScale);
    accum += baseNoise;

    // forward: 停滞検出と座標クランプを導入
    float fx = gx;
    float fy = gy;
    float fz = gz;
    for (uint i = 1u; i <= samples; ++i)
    {
        float2 prevPos = float2(fx, fy);

        float3 vel = SampleVel(fx, fy, fz, NX, NY, NZ);
        float2 vel2;
        if (axis == 2u)
            vel2 = float2(vel.x, vel.y);
        else if (axis == 1u)
            vel2 = float2(vel.x, vel.z);
        else
            vel2 = float2(vel.y, vel.z);

        float len = length(vel2);
        if (len <= 1e-6f)
        {
            // ほとんど動かない -> 以降のサンプルは冗長になるため break
            break;
        }

        float2 dir = vel2 / len;
        fx += dir.x * licStep;
        fy += dir.y * licStep;

        // サンプリング位置をグリッド内部にクランプ（中心点基準）
        fx = clamp(fx, 0.5f, (float) NX - 0.5f);
        fy = clamp(fy, 0.5f, (float) NY - 0.5f);
        // fz はスライスに依存（固定または更新されない）

        float2 currPos = float2(fx, fy);
        if (length(currPos - prevPos) < 1e-4f)
        {
            // 実質移動なし -> break して重複加算を避ける
            break;
        }

        float2 uv;
        if (axis == 2u)
            uv = float2(fx / max(1.0f, (float) (NX - 1)), fy / max(1.0f, (float) (NY - 1)));
        else if (axis == 1u)
            uv = float2(fx / max(1.0f, (float) (NX - 1)), fz / max(1.0f, (float) (NZ - 1)));
        else
            uv = float2(fy / max(1.0f, (float) (NY - 1)), fz / max(1.0f, (float) (NZ - 1)));

        accum += FBMNoise(uv * licNoiseScale);
    }

    // backward
    fx = gx;
    fy = gy;
    fz = gz;
    for (uint i = 1u; i <= samples; ++i)
    {
        float2 prevPos = float2(fx, fy);

        float3 vel = SampleVel(fx, fy, fz, NX, NY, NZ);
        float2 vel2;
        if (axis == 2u)
            vel2 = float2(vel.x, vel.y);
        else if (axis == 1u)
            vel2 = float2(vel.x, vel.z);
        else
            vel2 = float2(vel.y, vel.z);

        float len = length(vel2);
        if (len <= 1e-6f)
            break;

        float2 dir = vel2 / len;
        fx -= dir.x * licStep;
        fy -= dir.y * licStep;

        fx = clamp(fx, 0.5f, (float) NX - 0.5f);
        fy = clamp(fy, 0.5f, (float) NY - 0.5f);

        float2 currPos = float2(fx, fy);
        if (length(currPos - prevPos) < 1e-4f)
            break;

        float2 uv;
        if (axis == 2u)
            uv = float2(fx / max(1.0f, (float) (NX - 1)), fy / max(1.0f, (float) (NY - 1)));
        else if (axis == 1u)
            uv = float2(fx / max(1.0f, (float) (NX - 1)), fz / max(1.0f, (float) (NZ - 1)));
        else
            uv = float2(fy / max(1.0f, (float) (NY - 1)), fz / max(1.0f, (float) (NZ - 1)));

        accum += FBMNoise(uv * licNoiseScale);
    }

    float licValue = accum / halfCount; // 0..1 のはず

    // 出力：LIC をカラーマップで表示（密度と同じカラーマップ）
    float3 color = ColorMap(saturate(licValue));

    // --- mode による上書き処理 ---
    if (mode == 0u)
    {
        float rhoDenom = max(1e-6f, rhoMax - rhoMin);
        float rv = saturate((lbm_rho[idx] - rhoMin) / rhoDenom);
        color = ColorMap(rv);
    }
    else if (mode == 1u)
    {
        float3 velCenter = lbm_vel[idx];
        float vMag = length(velCenter);
        float effScale = max(1e-6f, vizVelScale);
        float vnorm = saturate(vMag / effScale);
        color = ColorMap(vnorm);
    }
    else // mode == 2u
    {
        float3 velCenter = lbm_vel[idx];
        float vMag = length(velCenter);
        float vecThreshold = max(1e-6f, vizVelScale * 0.01f);
        if (vMag <= vecThreshold)
        {
            color = float3(0.5f, 0.5f, 0.5f);
        }
        else
        {
            float3 n = velCenter / vMag;
            color = n * 0.5f + 0.5f;
        }
    }

    outTex[uint2(tid.x, tid.y)] = float4(color, 1.0f);
}