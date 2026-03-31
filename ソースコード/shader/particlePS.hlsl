struct PS_IN
{
    float4 Position : SV_POSITION;
    float4 Diffuse : COLOR0;
    float2 TexCoord : TEXCOORD0;
    float3 normal : NORMAL;
    float4 worldPosition : TEXCOORD1;
};

float4 main(PS_IN In) : SV_Target
{
    // テクスチャなし、ライティングなしで色をそのまま出力
    return In.Diffuse;
}