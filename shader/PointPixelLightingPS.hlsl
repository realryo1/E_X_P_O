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

    // ランバート反射
    float lambert = saturate(dot(worldNormal, L));

    // スペキュラー反射（ブリン・フォン）
    float3 V = normalize(CameraPosition.xyz - In.WorldPosition.xyz);
    float3 H = normalize(L + V);
    float specular = pow(saturate(dot(worldNormal, H)), 50.0);

    // 減衰率: 距離が lightRange を超えると 0 になる
    float lightRange = Light.PointLightParam.x;
    float atten = saturate(1.0 - distance / lightRange);

    // テクスチャサンプリング
    float4 texColor = g_Texture.Sample(g_SamplerState, In.TexCoord);

    // 最終出力 (ランバート + スペキュラー + 環境光) に減衰を乗算
    outDiffuse.rgb = (texColor.rgb * (In.Diffuse.rgb * Light.Diffuse.rgb * lambert) + specular * Light.Diffuse.rgb) * atten
                   + texColor.rgb * Light.Ambient.rgb;
    outDiffuse.a = texColor.a * In.Diffuse.a;
}
