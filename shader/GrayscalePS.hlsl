#include "Common.hlsl" //必ずインクルード!

Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);

//inは入力される引数
//out はリターンされる戻り値
void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{
    // 入力されてきたピクセル色をそのまま出力
    outDiffuse = In.Diffuse;
    
    outDiffuse = g_Texture.Sample(g_SamplerState, In.TexCoord);
    float Y = 0.299f * outDiffuse.r + 0.587f * outDiffuse.g + 0.114f * outDiffuse.b;
    outDiffuse.rgb = Y;
    
    //セピア対応を書くなどしてもいい
    
}