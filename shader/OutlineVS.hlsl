#include "Common.hlsl"

void main(in VS_IN In, out PS_IN Out)
{
    Out = (PS_IN)0;

    // 法線方向に少し膨らませ、元モデルより一回り大きい輪郭用モデルにする
    float outlineWidth = max(Parameter.x, 0.0f);
    float4 worldPosition = mul(In.Position, World);

    In.Normal.w = 0.0f;
    float4 worldNormal = mul(In.Normal, World);
    worldPosition.xyz += normalize(worldNormal.xyz) * outlineWidth;

    // 膨らませた座標を画面に描画できる座標へ変換する
    matrix vp;
    vp = mul(View, Projection);

    Out.Position = mul(worldPosition, vp);
    Out.WorldPosition = worldPosition;
    Out.Normal = worldNormal;

    // アウトライン色はC++側で指定したMaterialDiffuseを使う
    Out.Diffuse = MaterialDiffuse;
    Out.TexCoord = In.TexCoord;
}
