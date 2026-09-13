#include "Common.hlsl"

Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);

static const float PI = 3.14159265f;
static const float TWO_PI = 6.28318530f;

void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{
	// スカイドームの球面上の位置から、カメラへ向かう視線の方向を求める。
	// HDRの正距円筒図法と同じY-up座標系でサンプルする。
	float3 direction = normalize(In.WorldPosition.xyz - CameraPosition.xyz);
	float yaw = radians(Parameter.w);
	float sinYaw;
	float cosYaw;
	sincos(yaw, sinYaw, cosYaw);
	direction = float3(
		direction.x * cosYaw - direction.z * sinYaw,
		direction.y,
		direction.x * sinYaw + direction.z * cosYaw);
	float u = atan2(direction.z, direction.x) / TWO_PI + 0.5f;
	float v = acos(clamp(direction.y, -1.0f, 1.0f)) / PI;

	outDiffuse = g_Texture.Sample(g_SamplerState, float2(frac(u), v))
		* In.Diffuse;
}
