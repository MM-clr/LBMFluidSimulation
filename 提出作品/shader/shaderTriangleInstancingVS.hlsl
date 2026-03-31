#include "common.hlsl"

// ピクセルシェーダーへの入力はcommon.hlslで定義


void main(VS_IN_TRIANGLE_INSTANCING input, uint vertexID : SV_VertexID, out PS_IN output)
{
    output = (PS_IN) 0;

    float3 pos[6];
    pos[0] = input.v1;
    pos[1] = input.v2;
    pos[2] = input.v3;
    // 裏面
    pos[3] = input.v1;
    pos[4] = input.v3;
    pos[5] = input.v2;

    float3 worldPos = pos[vertexID];

    output.worldPosition = mul(float4(worldPos, 1.0f), World);
    output.Position = mul(output.worldPosition, View);
    output.Position = mul(output.Position, Projection);

    // 法線を計算
    float3 normal = cross(pos[1] - pos[0], pos[2] - pos[0]);
    normal = normalize(normal);
    if (vertexID >= 3)
    {
        normal = -normal; // 裏面は法線を反転
    }
    output.normal = mul(normal, (float3x3) World);

    output.Diffuse = input.color;
    output.TexCoord = float2(0.0f, 0.0f); // テクスチャは使わない
}