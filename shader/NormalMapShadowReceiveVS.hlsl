#include "Common.hlsl"

// NormalMapつきの影受け床用頂点シェーダー。
// ShadowReceiveVSと同じ座標を出しつつ、NormalMapのライト計算に使う法線をワールド空間へ変換する。
void main(in VS_IN In, out PS_IN Out)
{
	Out = (PS_IN)0;

	matrix wvp;
	wvp = mul(World, View);
	wvp = mul(wvp, Projection);

	float4 worldPosition = mul(In.Position, World);

	// NormalMapの凹凸はライト計算で使うので、床の法線もワールド空間へ変換してPSへ渡す。
	// w=0にすることで、位置の移動成分ではなく回転・拡大縮小だけを法線に反映する。
	float4 localNormal = In.Normal;
	localNormal.w = 0.0f;
	float4 worldNormal = mul(localNormal, World);

	// 通常描画用の座標・色・UV。
	Out.Position = mul(In.Position, wvp);
	Out.WorldPosition = worldPosition;
	Out.Normal = normalize(worldNormal);
	Out.Diffuse = In.Diffuse;
	Out.TexCoord = In.TexCoord;

	// ShadowMap用の座標。PS側のCalcShadow()がこの値から影の中かを判定する。
	Out.ShadowPosition = mul(worldPosition, LightViewProjection[0]);
}
