#ifndef _COMMON_HLSL_
#define _COMMON_HLSL_

//--------------------------------------------------------------------------------------
// 定数バッファ (頂点/ピクセルシェーダー用)
//--------------------------------------------------------------------------------------
cbuffer WorldBuffer : register(b0)
{
    matrix World;
}
cbuffer ViewBuffer : register(b1)
{
    matrix View;
}
cbuffer ProjectionBuffer : register(b2)
{
    matrix Projection;
}

struct MATERIAL
{
    float4 Ambient;
    float4 Diffuse;
    float4 Specular;
    float4 Emission;
    float Shininess;
    bool TextureEnable;
    float2 Dummy;
};

cbuffer MaterialBuffer : register(b3)
{
    MATERIAL Material;
}

struct LIGHT
{
    bool Enable;
    bool3 Dummy;
    float4 Direction;
    float4 Diffuse;
    float4 Ambient;
};

cbuffer LightBuffer : register(b4)
{
    LIGHT Light;
}

//--------------------------------------------------------------------------------------
// 構造体定義
//--------------------------------------------------------------------------------------

// 頂点シェーダー入力 (標準)
struct VS_IN
{
    float4 Position : POSITION0;
    float4 Normal : NORMAL0;
    float4 Diffuse : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

// ピクセルシェーダー入力
struct PS_IN
{
    float4 Position : SV_POSITION;
    float4 Diffuse : COLOR0;
    float2 TexCoord : TEXCOORD0;
    float3 normal : NORMAL;
    float4 worldPosition : TEXCOORD1;
};

// インスタンシング用頂点シェーダー入力 (shaderInstancing.hlsl)
struct VS_IN_INSTANCING
{
    float4 Position : POSITION;
    float3 Normal : NORMAL;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;

    // インスタンスごとのデータ
    float4 InstanceColor : COLOR1;
    float4 WorldRow1 : TEXCOORD1;
    float4 WorldRow2 : TEXCOORD2;
    float4 WorldRow3 : TEXCOORD3;
    float4 WorldRow4 : TEXCOORD4;
};

// 三角形インスタンシング用頂点シェーダー入力
struct VS_IN_TRIANGLE_INSTANCING
{
    float3 v1 : POSITION0;
    float3 v2 : POSITION1;
    float3 v3 : POSITION2;
    float4 color : COLOR0;
};

// インスタンスデータ (build_instance_buffer.hlsl)
struct CircleInstanceData
{
    float4 color;
    float4x4 world;
};

#endif // _COMMON_HLSL_