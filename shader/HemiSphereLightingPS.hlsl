#include "Common.hlsl"
Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);

void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{
    float4 normal = normalize(In.Normal);
    float light = dot(normal.xyz, -Light.Direction.xyz);
    light = saturate(light);
    float3 L = normalize(-Light.Direction.xyz);
    float3 V = normalize(CameraPosition.xyz - In.WorldPosition.xyz);
    float3 H = normalize(L + V);
    float specular = pow(saturate(dot(normal.xyz, H)), 50.0);
    float4 texColor = g_Texture.Sample(g_SamplerState, In.TexCoord);
    outDiffuse.rgb = texColor.rgb * (In.Diffuse.rgb * Light.Diffuse.rgb * light + Light.Ambient.rgb) + specular * Light.Diffuse.rgb;
    outDiffuse.a = texColor.a * In.Diffuse.a;
    float norm = dot(normal.xyz, Light.GroundNormal.xyz);
    norm = (norm + 1.0f) / 2.0f;
    float3 hemiColor = lerp(Light.GroundColor.rgb, Light.SkyColor.rgb, norm);
    outDiffuse.rgb += hemiColor;
}
