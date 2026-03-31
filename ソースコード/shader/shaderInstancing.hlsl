#include "common.hlsl"

void main(in VS_IN_INSTANCING In, out PS_IN Out)
{
    // インスタンスデータからワールド行列を再構築
    // ★ row_major に戻す
    row_major matrix instanceWorld =
    {
        In.WorldRow1,
        In.WorldRow2,
        In.WorldRow3,
        In.WorldRow4
    };

    // 頂点位置にワールド行列を適用
    float4 worldPos = mul(In.Position, instanceWorld);
    Out.worldPosition = worldPos;
    
    // ビュー・プロジェクション変換
    float4 viewPos = mul(worldPos, View);
    Out.Position = mul(viewPos, Projection);

    // 法線を変換
    Out.normal = normalize(mul(In.Normal, (float3x3) instanceWorld));

    // 色とテクスチャ座標を設定
    Out.Diffuse = In.InstanceColor;
    Out.TexCoord = In.TexCoord;
}