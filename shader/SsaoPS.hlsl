#include "Common.hlsl"

Texture2D g_DepthTexture : register(t0);
SamplerState g_SsaoSampler : register(s0);

cbuffer SsaoBuffer : register(b10)
{
    matrix InvProjection;
    float4 SsaoParams;
    float4 SsaoSettings;
}

float3 ReconstructViewPosition(float2 uv, float depth)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 clipPosition = float4(ndc, depth, 1.0f);
    float4 viewPosition = mul(clipPosition, InvProjection);
    return viewPosition.xyz / max(viewPosition.w, 0.0001f);
}

float SampleDepth(float2 uv)
{
    return g_DepthTexture.SampleLevel(g_SsaoSampler, saturate(uv), 0).r;
}

void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{
    const float centerDepth = SampleDepth(In.TexCoord);
    if (centerDepth >= 0.9999f)
    {
        outDiffuse = float4(1.0f, 1.0f, 1.0f, 1.0f);
        return;
    }

    const float2 texelSize = SsaoParams.xy;
    const float radius = max(SsaoParams.z, 0.01f);
    const float bias = max(SsaoParams.w, 0.0001f);
    const float3 centerPosition =
        ReconstructViewPosition(In.TexCoord, centerDepth);

    const float2 offsets[12] = {
        float2( 1.0f,  0.0f), float2(-1.0f,  0.0f),
        float2( 0.0f,  1.0f), float2( 0.0f, -1.0f),
        float2( 0.7f,  0.7f), float2(-0.7f,  0.7f),
        float2( 0.7f, -0.7f), float2(-0.7f, -0.7f),
        float2( 1.7f,  0.0f), float2(-1.7f,  0.0f),
        float2( 0.0f,  1.7f), float2( 0.0f, -1.7f)
    };

    float occlusion = 0.0f;
    float sampleCount = 0.0f;
    [unroll]
    for (int i = 0; i < 12; ++i)
    {
        const float2 sampleUv = In.TexCoord + offsets[i] * texelSize * radius * 18.0f;
        const float sampleDepth = SampleDepth(sampleUv);
        if (sampleDepth >= 0.9999f)
        {
            continue;
        }

        const float3 samplePosition = ReconstructViewPosition(sampleUv, sampleDepth);
        const float3 delta = samplePosition - centerPosition;
        const float distanceToSample = length(delta);
        if (distanceToSample <= 0.0001f || distanceToSample > radius)
        {
            continue;
        }

        // 手前にある柱ほど空光を遮る。細い格子では法線推定を
        // 併用すると遮蔽候補を落としやすいため、深度差を主に使う。
        const float depthDelta = centerPosition.z - samplePosition.z;
        const float closer = saturate(
            (depthDelta - bias) / max(radius * 0.25f, bias));
        const float falloff = 1.0f - saturate(distanceToSample / radius);
        occlusion += closer * falloff;
        sampleCount += 1.0f;
    }

    const float normalizedOcclusion =
        occlusion / max(sampleCount, 1.0f);
    const float visibility =
        1.0f - saturate(normalizedOcclusion * 2.5f);
    outDiffuse = float4(visibility, visibility, visibility, 1.0f);
}
