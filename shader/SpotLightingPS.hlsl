#include "Common.hlsl" //必須インクルード

Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);

void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{
    // -----------------------------------------------
    // 基本ベクトルの準備
    // -----------------------------------------------
    float3 worldNormal = normalize(In.Normal.xyz);
    float3 V = normalize(CameraPosition.xyz - In.WorldPosition.xyz);

    // ライトからピクセルへ向かうベクトル（コーン判定用）
    float3 Vp = normalize(In.WorldPosition.xyz - Light.Position.xyz);

    // ピクセルからライトへ向かうベクトル（拡散・鏡面反射用）
    float3 lightVec = Light.Position.xyz - In.WorldPosition.xyz;
    float  dist     = length(lightVec);
    float3 L        = normalize(lightVec);

    // -----------------------------------------------
    // スポットライト変数の初期化
    // -----------------------------------------------
    float spot     = 0.0f;
    float light    = 0.0f;
    float specular = 0.0f;

    // -----------------------------------------------
    // スポットライト：コーン判定
    // -----------------------------------------------
    // ライトの向きと「ライト→ピクセル」のなす角
    float angle = abs(acos(dot(Vp, normalize(Light.Direction.xyz))));

    if (angle <= Light.Angle.x)
    {
        // コーン内：中心ほど 1.0 になるように補間
        // PointLightParam.y = エッジのシャープさ調整用 pow 値
        spot = saturate(1.0f - pow(saturate(angle / Light.Angle.x), Light.PointLightParam.y));

        // 距離減衰（PointLightParam.x = 到達距離）
        float ofs = saturate(1.0f - dist / Light.PointLightParam.x);
        spot *= ofs;

        // ランバート拡散反射
        light = saturate(dot(worldNormal, L));

        // スペキュラー反射（ブリン・フォン）
        float3 H = normalize(L + V);
        specular = pow(saturate(dot(worldNormal, H)), 30.0f);
    }

    // -----------------------------------------------
    // 最終カラー合成
    // -----------------------------------------------
    outDiffuse = g_Texture.Sample(g_SamplerState, In.TexCoord);
    outDiffuse.rgb *= Light.Diffuse.rgb * In.Diffuse.rgb * light * spot + Light.Ambient.rgb;
    outDiffuse.a   *= In.Diffuse.a;
    outDiffuse.rgb += specular * spot;
}
