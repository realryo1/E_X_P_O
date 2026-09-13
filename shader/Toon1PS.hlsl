#include "Common.hlsl" //必須インクルード

Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);

void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{
    // 法線の正規化 (補間によって縮んでいるため)
    float3 worldNormal = normalize(In.Normal.xyz);

    // ポイントライト: 物体位置から光源位置へ向かうベクトルを求める
    float3 lightVec = Light.Position.xyz - In.WorldPosition.xyz;
    float  distance = length(lightVec);
    float3 L = normalize(lightVec);

    // ハーフランバート
    float light = 0.5f + 0.5f * dot(worldNormal, L);

    // Parameter.x = Level-1, Parameter.y = Level-2 で3段階に量子化
    if (light < Parameter.x)
        light = 0.4f;
    else if (light < Parameter.y)
        light = 0.7f;
    else
        light = 1.0f;

    // 減衰率: 距離が lightRange を超えると 0 になる
    float lightRange = Light.PointLightParam.x;
    float atten = saturate(1.0 - distance / lightRange);
    light *= atten;

    // テクスチャサンプリング
    float4 texColor = g_Texture.Sample(g_SamplerState, In.TexCoord);

    outDiffuse.rgb = texColor.rgb * (In.Diffuse.rgb * Light.Diffuse.rgb * light) + texColor.rgb * Light.Ambient.rgb;
    outDiffuse.a = texColor.a * In.Diffuse.a;

    // 簡易エッジ (視線と法線の内積で輪郭付近を暗く)
    float edgeThreshold = Parameter.z;
    float3 eyev = normalize(In.WorldPosition.xyz - CameraPosition.xyz);
    float d = dot(worldNormal, eyev);
    if (d > edgeThreshold)
        outDiffuse.rgb *= 0.3f;
}
