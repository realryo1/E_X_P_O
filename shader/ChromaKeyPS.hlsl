#include "Common.hlsl" //必ずインクルード!

Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);

//inは入力される引数
//out はリターンされる戻り値
void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{
    float4 color = g_Texture.Sample(g_SamplerState, In.TexCoord) * In.Diffuse;

    // 黒背景 (ルミナンスキー) 透過
    // RGBの最大値を基準として、黒に近いピクセルを透過する
    float maxVal = max(color.r, max(color.g, color.b));

    // 暗いピクセル（黒に近い部分）を透過
    if (maxVal < 0.15f)
    {
        // maxVal が 0.02f から 0.15f の間で滑らかにアルファ値を変化させる
        float alpha = smoothstep(0.02f, 0.15f, maxVal);
        color.a *= alpha;
        
        // 完全に透明になったピクセルは破棄する
        if (color.a <= 0.0f)
        {
            discard;
        }
    }

    outDiffuse = color;
}
