#include "Common.hlsl"

Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);

float GetDitherThreshold(float2 screenPosition)
{
    static const float dither4x4[16] =
    {
         0.0f,  8.0f,  2.0f, 10.0f,
        12.0f,  4.0f, 14.0f,  6.0f,
         3.0f, 11.0f,  1.0f,  9.0f,
        15.0f,  7.0f, 13.0f,  5.0f
    };
    uint2 pixel = uint2(screenPosition) & 3;
    return (dither4x4[pixel.y * 4 + pixel.x] + 0.5f) / 16.0f;
}

void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{
    // スキニングあり/なしの両方で同じライト計算になるよう、PixelShader側でLambertを計算する。
    float3 normal = normalize(In.Normal.xyz);
    float3 toLight = Light.Position.xyz - In.WorldPosition.xyz;
    float distanceToLight = length(toLight);
    float3 lightDir = (distanceToLight > 0.001f) ? toLight / distanceToLight : float3(0.0f, 1.0f, 0.0f);

    // PointLightの距離減衰。範囲外へ行くほど暗くなる。
    float lightRange = max(Light.PointLightParam.x, 0.001f);
    float lightIntensity = max(Light.PointLightParam.y, 0.0f);
    float attenuation = saturate(1.0f - distanceToLight / lightRange);
    attenuation = attenuation * attenuation * lightIntensity;

    float light = 0.0f;
    if (Light.Enable)
    {
        light = saturate(dot(normal, lightDir)) * attenuation;
    }

    float4 texColor = g_Texture.Sample(g_SamplerState, In.TexCoord);
    float opacity = saturate(texColor.a * In.Diffuse.a);
    if (In.Diffuse.a < 1.0f)
    {
        clip(opacity - GetDitherThreshold(In.Position.xy));
        opacity = 1.0f;
    }
    float3 baseColor = texColor.rgb * In.Diffuse.rgb;
    float3 ambient = saturate(Light.Ambient.rgb);
    float3 diffuse = baseColor * light * Light.Diffuse.rgb;

    outDiffuse.rgb = saturate(baseColor * ambient + diffuse);
    outDiffuse.a = opacity;
}
