#include "Common.hlsl"

Texture2D g_SceneTexture : register(t0);
Texture2D g_SsaoTexture : register(t1);
SamplerState g_SsaoSampler : register(s0);

cbuffer SsaoBuffer : register(b10)
{
    matrix InvProjection;
    float4 SsaoParams;
    float4 SsaoSettings;
}

void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{
    const float4 scene = g_SceneTexture.Sample(g_SsaoSampler, In.TexCoord);
    const float ao = g_SsaoTexture.Sample(g_SsaoSampler, In.TexCoord).r;
    const float intensity = saturate(SsaoSettings.x);
    const float poweredAo = pow(saturate(ao), max(SsaoSettings.y, 0.1f));
    const float visibility = (SsaoSettings.z > 0.5f)
        ? lerp(1.0f, poweredAo, intensity)
        : 1.0f;
    outDiffuse = float4(scene.rgb * visibility, scene.a);
}
