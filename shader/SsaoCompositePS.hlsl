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

cbuffer PhotoBuffer : register(b13)
{
    float4 PhotoParams;
    float4 PhotoTime;
}

float PhotoHash(float2 value)
{
    value = frac(value * float2(123.34f, 456.21f));
    value += dot(value, value + 45.32f);
    return frac(value.x * value.y);
}

float3 ApplyPhotoEffects(float2 uv, float3 color)
{
    const float posterizeLevels = PhotoParams.x;
    const float noiseAmount = PhotoParams.y;
    const float grainAmount = PhotoParams.z;
    const float rgbShift = PhotoParams.w;

    if (rgbShift > 0.0001f)
    {
        const float2 shift = float2(rgbShift, 0.0f);
        color.r = g_SceneTexture.Sample(g_SsaoSampler, uv + shift).r;
        color.b = g_SceneTexture.Sample(g_SsaoSampler, uv - shift).b;
    }

    if (posterizeLevels > 1.0f)
    {
        color = floor(color * posterizeLevels + 0.5f) / posterizeLevels;
    }

    const float noise = PhotoHash(uv * 173.0f + PhotoTime.x);
    color += (noise - 0.5f) * noiseAmount;

    const float grain = PhotoHash(uv * 911.0f + PhotoTime.x * 3.7f);
    color *= 1.0f + (grain - 0.5f) * grainAmount;
    return saturate(color);
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
    const float3 photoColor = ApplyPhotoEffects(
        In.TexCoord,
        scene.rgb * visibility);
    outDiffuse = float4(photoColor, scene.a);
}
