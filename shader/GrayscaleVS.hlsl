#include "common.hlsl" //必ずインクルード

void main(in VS_IN In, out PS_IN Out)
{
    Out = (PS_IN)0;

    //頂点変換(必ず必要)
    matrix wvp;                     //ワールドビュープロジェクション行列
    wvp = mul(World, View);         //wvp = World * View
    wvp = mul(wvp, Projection);     //wvp = wvp * Projection
    Out.Position = mul(In.Position, wvp); //頂点座標を行列で変換して出力

    //座標以外の要素を出力
    Out.WorldPosition = mul(In.Position, World);
    Out.Normal = In.Normal;
    Out.TexCoord = In.TexCoord;
    Out.Diffuse = In.Diffuse;
}
