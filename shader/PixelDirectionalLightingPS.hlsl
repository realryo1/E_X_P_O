#include "Common.hlsl" //必須インクルード

Texture2D g_Texture : register(t0);
Texture2D g_NormalMap : register(t2);
Texture2D g_EmissiveMap : register(t5);
SamplerState g_SamplerState : register(s0);

float3 CalcNormalMapWorldNormal(PS_IN In)
{
    float3 baseNormal = normalize(In.Normal.xyz);
    float3 normalSample = g_NormalMap.Sample(g_SamplerState, In.TexCoord).xyz * 2.0f - 1.0f;
    float3 tangent;
    float3 bitangent;

    float3 dp1 = ddx(In.WorldPosition.xyz);
    float3 dp2 = ddy(In.WorldPosition.xyz);
    float2 duv1 = ddx(In.TexCoord);
    float2 duv2 = ddy(In.TexCoord);

    float det = duv1.x * duv2.y - duv1.y * duv2.x;
    if (abs(det) > 0.00000001f)
    {
        float invDet = 1.0f / det;
        tangent = (dp1 * duv2.y - dp2 * duv1.y) * invDet;
        bitangent = (dp2 * duv1.x - dp1 * duv2.x) * invDet;
    }
    else
    {
        float3 axis = (abs(baseNormal.y) < 0.99f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
        tangent = cross(axis, baseNormal);
        bitangent = cross(baseNormal, tangent);
    }

    tangent = normalize(tangent);
    bitangent = normalize(bitangent);

    return normalize(
        normalSample.x * tangent +
        normalSample.y * bitangent +
        normalSample.z * baseNormal
    );
}

void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{
    // 法線の正規化 (補間によって縮んでいるため)
    float3 worldNormal = CalcNormalMapWorldNormal(In);
    float3 toLight = Light.Position.xyz - In.WorldPosition.xyz;
    float distanceToLight = length(toLight);
    float3 lightDirection = (distanceToLight > 0.001f) ? toLight / distanceToLight : float3(0.0f, 1.0f, 0.0f);
    float lightRange = max(Light.PointLightParam.x, 0.001f);
    float lightIntensity = max(Light.PointLightParam.y, 0.0f);
    float attenuation = saturate(1.0f - distanceToLight / lightRange);
    attenuation = attenuation * attenuation * lightIntensity;

    // ランバート反射
    float light = 0.0f;
    if (Light.Enable)
    {
        light = saturate(dot(lightDirection, worldNormal)) * attenuation;
    }

    // スペキュラー反射（ブリン・フォン）
    float specular = 0.0f;

    // テクスチャサンプリング
    float4 texColor = g_Texture.Sample(g_SamplerState, In.TexCoord);
	// 壁などの自己発光色をライティング結果に足す。
	float3 emissive = g_EmissiveMap.Sample(g_SamplerState, In.TexCoord).rgb;
	float3 baseColor = texColor.rgb * In.Diffuse.rgb;
	float3 ambient = saturate(Light.Ambient.rgb);
	float3 diffuse = baseColor * light * Light.Diffuse.rgb;

    // 最終出力 (ランバート + スペキュラー)
	outDiffuse.rgb = saturate(baseColor * ambient + diffuse + specular * Light.Diffuse.rgb + emissive);
    outDiffuse.a = texColor.a * In.Diffuse.a;
}
