#include "Common.hlsl"

Texture2D g_SceneTexture : register(t0);
SamplerState g_BloomSampler : register(s0);

cbuffer BloomBuffer : register(b10)
{
    float4 BloomParams;  // x:輝度しきい値 (Luminance)
    float4 BloomWeights; // 未使用（合成パスと共通の定数バッファ）
}

void main(in PS_IN In, out float4 outColor : SV_Target)
{
    float3 color = g_SceneTexture.SampleLevel(g_BloomSampler, In.TexCoord, 0).rgb;

    // 輝度を求め、しきい値以上の画素だけを残す（未満は黒）。色味は保つ。
    const float luminance = dot(color, float3(0.299f, 0.587f, 0.114f));
    const float keep = step(BloomParams.x, luminance);

    outColor = float4(color * keep, 1.0f);
}
