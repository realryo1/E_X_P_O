#include "Common.hlsl" //必須インクルード

Texture2D g_Texture : register(t0);
// t1=ShadowMap, t2〜t5 は ModelDraw が Normal/PBR 用に上書きするため Ramp は t7
Texture2D g_TextureRamp : register(t7);
SamplerState g_SamplerState : register(s0);

void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{
    // 法線の正規化 (補間によって縮んでいるため)
    float3 worldNormal = normalize(In.Normal.xyz);

    // ポイントライト: 物体位置から光源位置へ向かうベクトルを求める
    float3 lightVec = Light.Position.xyz - In.WorldPosition.xyz;
    float  distance = length(lightVec);
    float3 L = normalize(lightVec);

    // ハーフランバート → Ramp の U 座標 (端のラップ混色を避けるため clamp)
    float light = 0.5f + 0.5f * dot(worldNormal, L);
    light = clamp(light, 0.01f, 0.99f);
    float texv = clamp(Parameter.x, 0.01f, 0.99f);

    // 減衰率
    float lightRange = Light.PointLightParam.x;
    float atten = saturate(1.0 - distance / lightRange);

    float4 toon = g_TextureRamp.Sample(g_SamplerState, float2(light, texv));
    toon *= atten;

    float4 texColor = g_Texture.Sample(g_SamplerState, In.TexCoord);
    outDiffuse.rgb = texColor.rgb * (toon.rgb * In.Diffuse.rgb + Light.Ambient.rgb);
    outDiffuse.a = texColor.a * In.Diffuse.a;

    // 簡易エッジ (視線と法線の内積で輪郭付近を暗く)
    float edgeThreshold = Parameter.z;
    float3 eyev = normalize(In.WorldPosition.xyz - CameraPosition.xyz);
    float d = dot(worldNormal, eyev);
    if (d > edgeThreshold)
        outDiffuse.rgb *= 0.3f;
}
