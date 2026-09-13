#include "Common.hlsl"

Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);

// 影を受ける側のピクセルシェーダー。
// 通常のテクスチャ色に、ShadowMapから求めた影の暗さを掛ける。
void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{
    float4 texColor = g_Texture.Sample(g_SamplerState, In.TexCoord) * In.Diffuse;

    // 1.0なら影なし、ShadowParam.yなら影あり。
    float shadow = CalcShadow(In.ShadowPosition);

    texColor.rgb *= shadow;
    outDiffuse = texColor;
}
