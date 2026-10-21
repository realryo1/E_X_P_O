#include "Common.hlsl"

Texture2D g_SceneTexture : register(t0); // 通常シーン (RT0)
Texture2D g_GlowTexture : register(t1);  // 輝度マップ (RT1、ミップマップ付き)
SamplerState g_BloomSampler : register(s0);

cbuffer BloomBuffer : register(b10)
{
    float4 BloomParams;  // 未使用（抽出パスと共通の定数バッファ）
    float4 BloomWeights; // x,y,z:Blend 1〜3
}

void main(in PS_IN In, out float4 outColor : SV_Target)
{
    float3 scene = g_SceneTexture.SampleLevel(g_BloomSampler, In.TexCoord, 0).rgb;

    // 低解像度のミップレベルを複数読んで加算する。
    // 小数のLODは隣り合う2段の線形補間になり、なめらかなぼけになる。
    float3 glow = float3(0.0f, 0.0f, 0.0f);
    glow += g_GlowTexture.SampleLevel(g_BloomSampler, In.TexCoord, 2.5f).rgb * BloomWeights.x;
    glow += g_GlowTexture.SampleLevel(g_BloomSampler, In.TexCoord, 4.5f).rgb * BloomWeights.y;
    glow += g_GlowTexture.SampleLevel(g_BloomSampler, In.TexCoord, 6.5f).rgb * BloomWeights.z;

    // 画面出力はLDR。明るい部分は白に張り付き、その周囲に滲みが広がる。
    outColor = float4(saturate(scene + saturate(glow)), 1.0f);
}
