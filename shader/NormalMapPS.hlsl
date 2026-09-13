#include "Common.hlsl" //必須インクルード

Texture2D    diffuseTex : register(t0);
// t1 は Common.hlsl の ShadowMap 用のため、NormalMap は t2 を使う
Texture2D    normalTex  : register(t2);
SamplerState samp       : register(s0);

struct PS_IN_NM
{
    float4 Position      : SV_POSITION;
    float4 WorldPosition : POSITION0;
    float4 Normal        : NORMAL0;
    float4 Tangent       : TANGENT0;
    float2 TexCoord      : TEXCOORD0;
    float4 Diffuse       : COLOR0;
};

void main(in PS_IN_NM In, out float4 outDiffuse : SV_Target)
{
    // TBN 行列構築
    float3 N = normalize(In.Normal.xyz);
    float3 T = normalize(In.Tangent.xyz);
    T = normalize(T - dot(T, N) * N);  // 再直交化
    float3 B = cross(N, T);            // 従法線をシェーダー内で計算

    // 法線マップサンプリング [0,1] -> [-1,1]
    float3 normalMap = normalTex.Sample(samp, In.TexCoord).xyz;
    normalMap = normalMap * 2.0f - 1.0f;

    // TBN 行列によるワールド空間への変換
    float3x3 TBN = float3x3(T, B, N);
    float3 worldNormal = normalize(mul(normalMap, TBN));

    // ランバート反射
    float light = -dot(Light.Direction.xyz, worldNormal);
    light = saturate(light);

    // スペキュラー反射（ブリン・フォン）
    float3 L = normalize(-Light.Direction.xyz);
    float3 V = normalize(CameraPosition.xyz - In.WorldPosition.xyz);
    float3 H = normalize(L + V);
    float specular = pow(saturate(dot(worldNormal, H)), 50.0);

    // テクスチャサンプリング
    float4 texColor = diffuseTex.Sample(samp, In.TexCoord);

    // 最終出力 (ランバート + スペキュラー + 環境光)
    outDiffuse.rgb = texColor.rgb * In.Diffuse.rgb * (Light.Diffuse.rgb * light + Light.Ambient.rgb) + specular * Light.Diffuse.rgb;
    outDiffuse.a = 1.0f;
}
