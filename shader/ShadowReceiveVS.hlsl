#include "Common.hlsl"

// 影を受ける側(今は床Billboard)用の頂点シェーダー。
// 通常のカメラ用座標に加えて、ライト視点での座標もピクセルシェーダーへ渡す。
void main(in VS_IN In, out PS_IN Out)
{
    Out = (PS_IN)0;

    // 通常の画面表示用。これはプレイヤーカメラから見た座標。
    matrix wvp;
    wvp = mul(World, View);
    wvp = mul(wvp, Projection);

    // 影判定ではワールド座標が基準になる。
    float4 worldPosition = mul(In.Position, World);

    Out.Position = mul(In.Position, wvp);
    Out.WorldPosition = worldPosition;
    Out.Normal = In.Normal;
    Out.Diffuse = In.Diffuse;
    Out.TexCoord = In.TexCoord;

    // 同じ頂点をライト視点にも変換しておく。
    // ピクセルシェーダー側で、この値を使ってShadowMapを見る。
    Out.ShadowPosition = mul(worldPosition, LightViewProjection[0]);
}
