
#include "common.hlsl"


void main(in VS_IN In, out PS_IN Out)
{

	matrix wvp;
	wvp = mul(World, View);
	wvp = mul(wvp, Projection);
	
    float4 worldNormal, normal;
    normal = float4(In.Normal.xyz, 0.0);
    worldNormal = mul(normal, World);
	worldNormal= normalize(worldNormal);
    float3 LightDir = normalize(Light.Direction.xyz); // light direction in world space
    float light = -dot(LightDir, worldNormal.xyz);
    light = saturate(light);
	
    Out.Diffuse = In.Diffuse * Material.Diffuse * light * Light.Diffuse;
    Out.Diffuse += In.Diffuse * Material.Ambient * Light.Ambient; // add ambient light
    Out.Diffuse += Material.Emission; // add emissive light
    Out.Diffuse.a = In.Diffuse.a * Material.Diffuse.a; // set alpha

	Out.Position = mul(In.Position, wvp);
	Out.TexCoord = In.TexCoord;

}

