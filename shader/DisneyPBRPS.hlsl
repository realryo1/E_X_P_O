#include "Common.hlsl" //必須インクルード
#include "CookTorranceSub.hlsl"

// テクスチャスロット定義
// t1 は Common.hlsl の ShadowMap、b7 は PlayerLightBuffer と衝突するためずらす
Texture2D g_Texture : register(t0);          // ベースカラー
Texture2D g_MetallicTexture : register(t2);  // メタリックネステクスチャ
Texture2D g_RoughnessTexture : register(t3); // ラフネステクスチャ
SamplerState g_SamplerState : register(s0);

// 専用ライト定数バッファ (C++ 側のスロット10に対応)
cbuffer DisneyLightBuffer : register(b10)
{
    LIGHT DisneyLights[10];
    int LightCount;
    float3 Dummy;
};

void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{
    // 法線の正規化 (補間によって縮んでいるため)
    float3 worldNormal = normalize(In.Normal.xyz);

    // カメラへの方向ベクトル
    float3 V = normalize(CameraPosition.xyz - In.WorldPosition.xyz);

    // cbufferからテクスチャ使用フラグを取得
    float useTex = Parameter.z; // 1.0f: 使用, 0.0f: 未使用

    // テクスチャサンプリング
    float4 baseColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    float metallic = 0.0f;
    float roughness = 0.5f;

    if (useTex > 0.5f)
    {
        baseColor = g_Texture.Sample(g_SamplerState, In.TexCoord);
        // 各テクスチャからR値を取得（グレースケール想定）
        metallic = g_MetallicTexture.Sample(g_SamplerState, In.TexCoord).r;
        roughness = g_RoughnessTexture.Sample(g_SamplerState, In.TexCoord).r;
    }
    else
    {
        // テクスチャ未使用時は cbuffer からの調整値を使用
        metallic = saturate(Parameter.y);
        // cbufferのsmoothパラメータから粗さ(roughness)に変換: roughness = 1 - smooth
        roughness = saturate(1.0f - Parameter.x);
    }

    float3 totalDiffuse = float3(0.0f, 0.0f, 0.0f);
    float3 totalSpecular = float3(0.0f, 0.0f, 0.0f);
    float3 totalAmbient = float3(0.0f, 0.0f, 0.0f);

    // 複数光源ループ処理
    int activeLightCount = clamp(LightCount, 1, 10);
    for (int i = 0; i < activeLightCount; i++)
    {
        if (!DisneyLights[i].Enable)
            continue;

        // ライト方向ベクトルと距離
        float3 lightVec = DisneyLights[i].Position.xyz - In.WorldPosition.xyz;
        float distance = length(lightVec);
        float3 L = normalize(lightVec);

        // ランバート反射 (N・L)
        float lambert = saturate(dot(worldNormal, L));

        // ハーフベクトル
        float3 H = normalize(L + V);

        // 各角度の dot 積
        float nv = saturate(dot(worldNormal, V));
        float nh = saturate(dot(worldNormal, H));
        float vh = saturate(dot(V, H));
        float nl = saturate(dot(worldNormal, L));

        // ディフューズ項 (正規化ランバート拡散反射: / π)
        // 光源数 (activeLightCount) で補正を掛け、スケール感を維持する
        float3 diffuse = (baseColor.rgb * lambert / 3.14159265f) * (float)activeLightCount;

        // スペキュラー項 (Cook-Torrance / Beckmann分布)
        // m に roughness を直接使用（smooth = 1 - roughness）
        float D = CalculateBeckmann(1.0f - roughness, nh);
        float G = CalculateGeometricDamping(nh, nv, nl, vh);
        float F = CalculateFresnel(metallic, dot(L, H));

        float3 specular = max(0.0f, F * D * G / max(nv, 0.001f)) * baseColor.rgb;

        // 減衰率
        float lightRange = DisneyLights[i].PointLightParam.x;
        float atten = saturate(1.0f - distance / lightRange);

        // 各ライトの光量・減衰・マテリアル色を合成して累積
        totalDiffuse += diffuse * In.Diffuse.rgb * DisneyLights[i].Diffuse.rgb * atten;
        totalSpecular += specular * DisneyLights[i].Diffuse.rgb * atten;
        totalAmbient += baseColor.rgb * DisneyLights[i].Ambient.rgb;
    }

    // PBR 合成式: diffuse * (1 - metallic) + specular
    // ※金属質はディフューズ反射（拡散反射）を一切しないため、(1 - metallic) を掛ける
    outDiffuse.rgb = totalDiffuse * (1.0f - metallic) + totalSpecular + (totalAmbient / (float)activeLightCount);
    outDiffuse.a = baseColor.a * In.Diffuse.a;
}
