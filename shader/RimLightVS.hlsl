#include "Common.hlsl"

void main(in VS_IN In, out PS_IN Out)
{
    Out = (PS_IN)0;

    // モデル空間の頂点座標を、画面に描画できる座標へ変換する
    matrix wvp;
    wvp = mul(World, View);
    wvp = mul(wvp, Projection);

    Out.Position = mul(In.Position, wvp);
    Out.WorldPosition = mul(In.Position, World);

    // ピクセルシェーダーで使うため、法線をワールド空間へ変換する
    In.Normal.w = 0.0f;
    Out.Normal = mul(In.Normal, World);

    // マテリアルの色味だけ残し、暗いマテリアルで全体が沈まないように明るさを正規化する
    float3 materialTint = MaterialDiffuse.rgb;
    float tintMax = max(max(materialTint.r, materialTint.g), materialTint.b);
    if (tintMax > 0.001f)
    {
        materialTint /= tintMax;
    }
    else
    {
        materialTint = float3(1.0f, 1.0f, 1.0f);
    }

    // 色味を反映した頂点色とUV座標をピクセルシェーダーへ渡す
    Out.Diffuse.rgb = In.Diffuse.rgb * materialTint;
    Out.Diffuse.a = In.Diffuse.a * MaterialDiffuse.a;
    Out.TexCoord = In.TexCoord;
}
