#include "Common.hlsl"

Texture2D g_SsaoTexture : register(t0);
Texture2D g_DepthTexture : register(t1);
SamplerState g_SsaoSampler : register(s0);

cbuffer SsaoBuffer : register(b10)
{
    matrix InvProjection;
    float4 SsaoParams;
    float4 SsaoSettings;
}

void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{
    const float2 texelSize = SsaoParams.xy * 2.0f;
    const float centerDepth =
        g_DepthTexture.SampleLevel(g_SsaoSampler, In.TexCoord, 0).r;
    const float centerAo =
        g_SsaoTexture.SampleLevel(g_SsaoSampler, In.TexCoord, 0).r;

    float sum = centerAo;
    float weightSum = 1.0f;
    const float2 offsets[4] = {
        float2(1.0f, 0.0f), float2(-1.0f, 0.0f),
        float2(0.0f, 1.0f), float2(0.0f, -1.0f)
    };

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        const float2 uv = In.TexCoord + offsets[i] * texelSize;
        const float2 clampedUv = saturate(uv);
        const float depth = g_DepthTexture.SampleLevel(
            g_SsaoSampler, clampedUv, 0).r;
        const float depthWeight =
            1.0f - saturate(abs(depth - centerDepth) * 180.0f);
        const float weight = max(depthWeight, 0.0f);
        sum += g_SsaoTexture.SampleLevel(
            g_SsaoSampler, clampedUv, 0).r * weight;
        weightSum += weight;
    }

    const float ao = sum / max(weightSum, 0.001f);
    outDiffuse = float4(ao, ao, ao, 1.0f);
}
