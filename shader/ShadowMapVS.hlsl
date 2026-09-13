#include "Common.hlsl"

// ShadowMap作成用の頂点シェーダー。
// カメラではなく「ライトから見た位置」に変換して、深度だけをShadowMapに書き込ませる。
void main(in VS_IN In, out float4 OutPosition : SV_POSITION)
{
    // この描画パスでは、ViewとProjectionにライト視点の行列が入っている。
    matrix wvp;
    wvp = mul(World, View);
    wvp = mul(wvp, Projection);

    // SV_POSITIONに入れたZ値が、ShadowMapの深度として保存される。
    OutPosition = mul(In.Position, wvp);
}
