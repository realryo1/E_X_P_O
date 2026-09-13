#include "Common.hlsl" //必須インクルード
#include "CookTorranceSub.hlsl"

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

    // カメラへの方向ベクトル
    float3 V = normalize(CameraPosition.xyz - In.WorldPosition.xyz);
    
    // ハーフベクトル
    float3 H = normalize(L + V);

    // 各角度の dot 積
    float nv = saturate(dot(worldNormal, V));
    float nh = saturate(dot(worldNormal, H));
    float vh = saturate(dot(V, H));
    float nl = saturate(dot(worldNormal, L));

    // cbufferから PBR パラメータを取得
    float smooth   = saturate(Parameter.x);
    float metallic = saturate(Parameter.y);
    float useTex   = Parameter.z; // 1.0f: 使用, 0.0f: 未使用

    // テクスチャサンプリング（フラグが立っている場合のみサンプリング、そうでなければ白色）
    float4 texColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    if (useTex > 0.5f)
    {
        texColor = g_Texture.Sample(g_SamplerState, In.TexCoord);
    }

    // Cook-Torrance 物理ベーススペキュラー計算
    float D = CalculateBeckmann(smooth, nh);
    float G = CalculateGeometricDamping(nh, nv, nl, vh);
    float F = CalculateFresnel(metallic, dot(L, H));

    // 鏡面反射強度合成 (分母に安全係数を加えてゼロ除算を防止)
    float3 specular = max(0.0f, F * D * G / max(nv, 0.001f)) * texColor.rgb;

    // 減衰率: 距離が lightRange を超えると 0 になる
    float lightRange = Light.PointLightParam.x;
    float atten = saturate(1.0 - distance / lightRange);

    // ディフューズとスペキュラーを合成
    float3 diffuseColor = texColor.rgb * In.Diffuse.rgb * Light.Diffuse.rgb * lambert;
    float3 specularColor = specular * Light.Diffuse.rgb;

    // 最終出力に減衰を乗算
    outDiffuse.rgb = (diffuseColor + specularColor) * atten + texColor.rgb * Light.Ambient.rgb;
    outDiffuse.a = texColor.a * In.Diffuse.a;
}
