#include "Common.hlsl"

void main(in VS_IN In, out PS_IN Out)
{
    Out = (PS_IN)0;

    // 頂点を画面に出すための座標へ変換する。
    matrix wvp = mul(World, View);
    wvp = mul(wvp, Projection);
    Out.Position = mul(In.Position, wvp);

    // ライト計算はPixelShader側で行うので、ワールド座標とワールド法線を渡す。
    float4 worldPosition = mul(In.Position, World);
    float4 localNormal = float4(In.Normal.xyz, 0.0f);
    float4 worldNormal = mul(localNormal, World);

    // マテリアル色は明るさを落としすぎないよう、色味だけを使う。
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

    Out.WorldPosition = worldPosition;
    Out.Normal = normalize(worldNormal);
    Out.Diffuse.rgb = In.Diffuse.rgb * materialTint;
    Out.Diffuse.a = In.Diffuse.a * MaterialDiffuse.a;
    Out.TexCoord = In.TexCoord;
}
