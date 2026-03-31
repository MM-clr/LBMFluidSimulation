cbuffer Constants : register(b0)
{
    uint numParticles;
    float timeStep;
    float boundaryDamping;
    uint numObstacles; // ★追加
    float3 boundsMin;
    float pad1;
    float3 boundsMax;
    float pad2;
    float3 boundsSize;
    float pad3;
    uint3 gridSize;
    float pad4;
    float3 initialVelocity;
    float pad5;
    float3 wind; // 追加: 動的風ベクトル
    float pad6;
};

// 障害物データ
struct Obstacle
{
    float3 center;
    float pad0;
    float3 halfSize;
    float pad1;
    float3 axisX;
    float pad2;
    float3 axisY;
    float pad3;
    float3 axisZ;
    float pad4;
};

StructuredBuffer<float3> lbm_vel_in : register(t0);
StructuredBuffer<float3> particle_pos_in : register(t1);
StructuredBuffer<float3> particle_vel_in : register(t2);
StructuredBuffer<Obstacle> obstacles : register(t3); // ★追加

RWStructuredBuffer<float3> particle_pos_out : register(u0);
RWStructuredBuffer<float3> particle_vel_out : register(u1);

// OBBとの衝突判定と押し出し
bool CheckOBBCollision(float3 pos, Obstacle obs, out float3 pushDir, out float penetration)
{
    float3 d = pos - obs.center;
    
    // 各軸への投影
    float3 localPos;
    localPos.x = dot(d, obs.axisX);
    localPos.y = dot(d, obs.axisY);
    localPos.z = dot(d, obs.axisZ);
    
    // 最近接点を計算
    float3 closest;
    closest.x = clamp(localPos.x, -obs.halfSize.x, obs.halfSize.x);
    closest.y = clamp(localPos.y, -obs.halfSize.y, obs.halfSize.y);
    closest.z = clamp(localPos.z, -obs.halfSize.z, obs.halfSize.z);
    
    // 内部にいるかチェック
    float3 diff = localPos - closest;
    float distSq = dot(diff, diff);
    
    if (distSq < 0.0001f)  // 内部にいる
    {
        // 最も近い面を見つける
        float3 distToFace;
        distToFace.x = obs.halfSize.x - abs(localPos.x);
        distToFace.y = obs.halfSize.y - abs(localPos.y);
        distToFace.z = obs.halfSize.z - abs(localPos.z);
        
        if (distToFace.x < distToFace.y && distToFace.x < distToFace.z)
        {
            pushDir = obs.axisX * sign(localPos.x);
            penetration = distToFace.x;
        }
        else if (distToFace.y < distToFace.z)
        {
            pushDir = obs.axisY * sign(localPos.y);
            penetration = distToFace.y;
        }
        else
        {
            pushDir = obs.axisZ * sign(localPos.z);
            penetration = distToFace.z;
        }
        return true;
    }
    
    return false;
}

float rand(float2 seed)
{
    return frac(sin(dot(seed, float2(12.9898f, 78.233f))) * 43758.5453f);
}

float3 rand3(uint id, float time)
{
    return float3(
        rand(float2(float(id) * 0.123f, time * 0.456f)),
        rand(float2(float(id) * 0.789f, time * 0.012f)),
        rand(float2(float(id) * 0.345f, time * 0.678f))
    );
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= numParticles)
        return;

    float3 pos = particle_pos_in[id.x];
    float3 vel = particle_vel_in[id.x];

    // min/max は実際の境界
    float3 minB = boundsMin;
    float3 maxB = boundsMax;

    // LBM速度場から速度を取得
    float3 normalizedPos = (pos - boundsMin) / boundsSize;
    float3 gridPos = normalizedPos * float3(gridSize);
    bool isInterior = all(gridPos >= 2.0f) && all(gridPos < float3(gridSize) - 2.0f);
    
    if (isInterior)
    {
        int3 idx = int3(floor(gridPos));
        uint gridIndex = idx.z * gridSize.y * gridSize.x + idx.y * gridSize.x + idx.x;
        float3 lbmVel = lbm_vel_in[gridIndex];
        
        if (!any(isnan(lbmVel)) && !any(isinf(lbmVel)))
        {
            float3 cellSize = boundsSize / float3(gridSize);
            float3 worldLbmVel = lbmVel * cellSize * 50.0f;
            vel = lerp(vel, worldLbmVel, 0.3f);
        }
    }

    // 風の影響を追加（timeStep を乗じて加速度として扱う）
    float windStrength = 1.0f; // 調整可能
    vel += wind * windStrength * timeStep;

    // 停止防止
    float speed = length(vel);
    if (speed < 0.01f)
    {
        float3 r = rand3(id.x, pos.x + pos.y + pos.z);
        vel = (r - 0.5f) * 0.5f;
    }

    vel *= 0.995f;

    float maxSpeed = 3.0f;
    if (speed > maxSpeed)
    {
        vel = vel / speed * maxSpeed;
    }

    // 位置更新
    pos += vel * timeStep;

    // ★障害物との衝突判定
    for (uint i = 0; i < numObstacles; ++i)
    {
        float3 pushDir;
        float penetration;
        
        if (CheckOBBCollision(pos, obstacles[i], pushDir, penetration))
        {
            // 押し出し
            pos += pushDir * (penetration + 0.05f);
            
            // 速度を反射
            float velDotNormal = dot(vel, pushDir);
            if (velDotNormal < 0)
            {
                vel -= 2.0f * velDotNormal * pushDir;
                vel *= 0.5f; // 反発係数
            }
        }
    }

    // 境界処理：位置をクランプし、境界に当たった軸の速度を減衰させて自然に内側に保つ
    float3 clampedPos = clamp(pos, minB, maxB);

    if (clampedPos.x != pos.x)
        vel.x *= boundaryDamping;
    if (clampedPos.y != pos.y)
        vel.y *= boundaryDamping;
    if (clampedPos.z != pos.z)
        vel.z *= boundaryDamping;

    pos = clampedPos;

    particle_pos_out[id.x] = pos;
    particle_vel_out[id.x] = vel;
}