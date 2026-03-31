struct CircleInstanceData
{
    float4 color;
    row_major float4x4 world;
};

cbuffer Constants : register(b0)
{
    uint numParticles;
    float3 pad0;
    float4 color_water;
    float4 color_air;
    row_major matrix invView;
};

StructuredBuffer<float3> particle_pos_in : register(t0);
StructuredBuffer<uint> particle_type_in : register(t1);
StructuredBuffer<float3> particle_vel_in : register(t2); // ★追加: 速度バッファ

RWStructuredBuffer<CircleInstanceData> instance_buffer_out : register(u0);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= numParticles)
        return;

    // パーティクル位置と速度を読み取る
    float3 pos = particle_pos_in[id.x];
    float3 vel = particle_vel_in[id.x];
    float speed = length(vel);

    // 行列を構築
    row_major matrix scale =
    {
        0.1f, 0, 0, 0,
        0, 0.1f, 0, 0,
        0, 0, 0.1f, 0,
        0, 0, 0, 1
    };

    row_major matrix translation =
    {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        pos.x, pos.y, pos.z, 1
    };

    row_major matrix billboardRotation = invView;
    billboardRotation[3][0] = 0;
    billboardRotation[3][1] = 0;
    billboardRotation[3][2] = 0;

    // 行列を合成して格納
    row_major matrix worldMatrix = mul(mul(scale, billboardRotation), translation);
    instance_buffer_out[id.x].world = worldMatrix;

    // ★速度に基づいて色を決定
    float4 baseColor = (particle_type_in[id.x] == 1) ? color_water : color_air;
    float4 stoppedColor = float4(1.0f, 0.0f, 0.0f, 1.0f); // 赤色
    
    // 速度が0.1未満なら静止とみなす
    float speedThreshold = 0.1f;
    float t = saturate(speed / speedThreshold); // 0?1にクランプ
    
    instance_buffer_out[id.x].color = lerp(stoppedColor, baseColor, t);
}