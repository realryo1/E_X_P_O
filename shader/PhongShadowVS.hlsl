#include "Common.hlsl" //必須インクルード

// Phong(点光源ランバート) + ShadowMap受け取り用の頂点シェーダー。
// 通常のPhong VSに、ライト視点座標(ShadowPosition)の出力を追加したもの。
void main(in VS_IN In, out PS_IN Out)
{
    Out = (PS_IN) 0;

    matrix wvp;
    wvp = mul(World, View);
    wvp = mul(wvp, Projection);
    Out.Position = mul(In.Position, wvp);

    In.Normal.w = 0.0f;
    float4 worldNormal = mul(In.Normal, World);
    Out.Normal = worldNormal;

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

    Out.Diffuse.rgb = In.Diffuse.rgb * materialTint;
    Out.Diffuse.a = In.Diffuse.a;
    Out.TexCoord = In.TexCoord;

    float4 worldPosition = mul(In.Position, World);
    Out.WorldPosition = worldPosition;

    // 影判定用に、ライト視点でのクリップ座標も渡す。
    Out.ShadowPosition = mul(worldPosition, LightViewProjection[0]);
}
