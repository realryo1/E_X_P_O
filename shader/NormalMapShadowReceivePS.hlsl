#include "Common.hlsl"

Texture2D g_Texture : register(t0);
// t1はCommon.hlslのShadowMapで使用済みなので、NormalMapはt2へ入れる。
Texture2D g_NormalMap : register(t2);
SamplerState g_SamplerState : register(s0);

// NormalMapのRGBを、ライト計算に使えるワールド空間の法線へ変換する。
// 頂点にTangentを持っていないので、ワールド座標とUVの変化量からピクセル単位でTBNを作る。
float3 CalcNormalMapWorldNormal(PS_IN In)
{
	float3 baseNormal = normalize(In.Normal.xyz);

	// NormalMapはRGB(0～1)で保存されているので、法線ベクトル用の -1～1 に戻す。
	// RGBの中心値(0.5, 0.5, 1.0)は、ほぼ「元の面の法線方向」を意味する。
	float normalScale = 0.5f;
	float3 normalSample = g_NormalMap.Sample(g_SamplerState, In.TexCoord * normalScale).xyz * 2.0f - 1.0f;

	// 隣のピクセルとの差分から、床の上で「UVの横方向・縦方向」がワールド空間のどちらを向くか求める。
	// これにより、頂点データにTangentが無くても床テスト用のTBNを作れる。
	float3 dp1 = ddx(In.WorldPosition.xyz);
	float3 dp2 = ddy(In.WorldPosition.xyz);
	float2 duv1 = ddx(In.TexCoord);
	float2 duv2 = ddy(In.TexCoord);

	// UVの変化が小さすぎるとTangentを作れないので、その場合は通常の法線で描く。
	float det = duv1.x * duv2.y - duv1.y * duv2.x;
	float3 outNormal;
	if (abs(det) < 0.00001f)
	{
		outNormal = baseNormal;
	}
	else
	{
		float invDet = 1.0f / det;
		float3 tangent = normalize((dp1 * duv2.y - dp2 * duv1.y) * invDet);
		float3 bitangent = normalize((dp2 * duv1.x - dp1 * duv2.x) * invDet);

		// NormalMap内の法線を、Tangent/Bitangent/元の法線を使ってワールド空間へ変換する。
		outNormal = normalize(
			normalSample.x * tangent +
			normalSample.y * bitangent +
			normalSample.z * baseNormal
		);
	}

	return outNormal;
}

void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{
	// 床の通常テクスチャ色。
	float4 texColor = g_Texture.Sample(g_SamplerState, In.TexCoord) * In.Diffuse;

	// NormalMapで少し向きが変わった法線。この法線でライトの当たり方を変える。
	float3 normal = CalcNormalMapWorldNormal(In);

	// PointLightなので、ピクセル位置からライト位置までの方向と距離を毎ピクセル計算する。
	float3 toLight = Light.Position.xyz - In.WorldPosition.xyz;
	float distanceToLight = length(toLight);
	float3 lightDirection = (distanceToLight > 0.001f) ? toLight / distanceToLight : float3(0.0f, 1.0f, 0.0f);

	// ライトから遠いほど暗くなる距離減衰。
	float lightRange = max(Light.PointLightParam.x, 0.001f);
	float lightIntensity = max(Light.PointLightParam.y, 0.0f);
	float attenuation = saturate(1.0f - distanceToLight / lightRange);
	attenuation = attenuation * attenuation * lightIntensity;

	float diffuseLight = 0.0f;
	if (Light.Enable)
	{
		// NormalMapで変化した法線とライト方向の角度で、床の凹凸っぽい明暗を作る。
		diffuseLight = saturate(dot(normal, lightDirection)) * attenuation;
	}

	// 既存のShadowMapの影も残す。NormalMapは明暗、ShadowMapは落ち影を担当する。
	float shadow = CalcShadow(In.ShadowPosition);
	float3 ambient = saturate(Light.Ambient.rgb);
	float3 lighting = saturate(ambient + diffuseLight * Light.Diffuse.rgb);

	// 最終色 = テクスチャ色 × ライト明暗 × ShadowMapの影。
	outDiffuse.rgb = texColor.rgb * lighting * shadow;
	outDiffuse.a = texColor.a;
}
