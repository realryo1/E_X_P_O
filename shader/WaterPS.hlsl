#include "Common.hlsl"

// ウォータープラザなどの水面。
// 水面エディタで置いた多角形を三角形分割して描き、加算波の法線で空（HDRスカイ）と太陽を映す。
// 床のオルソ写真を透かして見せるため、αブレンドで重ねる。
Texture2D g_SkyTexture : register(t0);
// 水域の輪郭の辺。xy:始点XZ zw:終点XZ（ワールド座標）
StructuredBuffer<float4> g_WaterEdges : register(t2);
SamplerState g_SamplerState : register(s0);

// b0〜b13 は埋まっているため、4面ShadowMap用の b9（SCENE_GAME では未使用）を借りる。
// 描画中だけ差し替え、次のシーン開始時にレンダラーが b9 を張り直す。
cbuffer WaterBuffer : register(b9)
{
	float4 WaterShape;		// x:輪郭の先頭の辺 y:辺の数 w:縁のぼかし幅（ワールド単位）
	float4 WaterWave;		// x:波の強さ y:波長倍率 z:速さ w:1ワールド単位あたりのメートル
	float4 WaterShallow;	// rgb:浅瀬色 a:浅瀬の不透明度
	float4 WaterDeep;		// rgb:深場色 a:深場の不透明度
	float4 WaterMisc;		// x:太陽の反射 y:コースティクス z:泡 w:空のYaw(度)
	float4 WaterMisc2;		// x:浅瀬から深場までの距離 y:空の映り込み
};

static const float PI = 3.14159265f;
static const float TWO_PI = 6.28318530f;
static const float WATER_F0 = 0.02f;
static const int WAVE_COUNT = 6;

// 輪郭までの最短距離。三角形の内側だけ描くので符号は要らない。
float DistanceToShore(float2 p)
{
	int start = (int)WaterShape.x;
	int count = (int)WaterShape.y;
	float distanceSquared = 1.0e12f;
	[loop]
	for (int i = 0; i < count; ++i)
	{
		float4 edge = g_WaterEdges[start + i];
		float2 ab = edge.zw - edge.xy;
		float2 ap = p - edge.xy;
		float t = saturate(dot(ap, ab) / max(dot(ab, ab), 0.000001f));
		float2 d = ap - ab * t;
		distanceSquared = min(distanceSquared, dot(d, d));
	}
	return sqrt(distanceSquared);
}

// 方向の異なる正弦波6本の勾配（メートル座標）。
// 波長の短い成分ほど遠方で消して、ちらつきを抑える。
float2 WaveGradient(float2 p, float time, float distanceMeters)
{
	static const float2 DIRS[WAVE_COUNT] = {
		float2(0.97f, 0.24f), float2(-0.62f, 0.78f), float2(0.31f, -0.95f),
		float2(-0.88f, -0.47f), float2(0.71f, 0.71f), float2(-0.24f, 0.97f)
	};
	static const float LENGTHS[WAVE_COUNT] = { 5.2f, 3.1f, 1.9f, 1.2f, 0.8f, 0.55f };
	static const float PHASES[WAVE_COUNT] = { 0.0f, 1.7f, 3.1f, 4.4f, 5.6f, 2.3f };

	float2 gradient = float2(0.0f, 0.0f);
	[unroll]
	for (int i = 0; i < WAVE_COUNT; ++i)
	{
		float wavelength = LENGTHS[i] * WaterWave.y;
		float k = TWO_PI / wavelength;
		// 深水波の分散関係 w = sqrt(g k)
		float w = sqrt(9.8f * k) * WaterWave.z;
		float fade = 1.0f - smoothstep(60.0f * wavelength, 220.0f * wavelength, distanceMeters);
		// 波長に比例した振幅にして、傾き（勾配）がどの成分でもそろうようにする
		float slope = WaterWave.x * 0.12f * fade;
		gradient += DIRS[i] * slope * cos(dot(DIRS[i], p) * k + w * time + PHASES[i]);
	}
	return gradient;
}

float Caustic(float2 p, float time)
{
	float2 q = p * 0.9f;
	float c = 0.0f;
	[unroll]
	for (int i = 0; i < 3; ++i)
	{
		q += float2(sin(q.y * 1.3f + time * 0.8f + i), cos(q.x * 1.1f - time * 0.7f + i * 1.7f)) * 0.55f;
		float v = (sin(q.x * 2.1f) + sin(q.y * 2.1f) + sin((q.x + q.y) * 1.5f)) * (1.0f / 3.0f);
		c = max(c, pow(saturate(1.0f - abs(v)), 8.0f));
	}
	return c;
}

float3 SampleSky(float3 direction)
{
	float yaw = radians(WaterMisc.w);
	float sinYaw;
	float cosYaw;
	sincos(yaw, sinYaw, cosYaw);
	float3 d = float3(
		direction.x * cosYaw - direction.z * sinYaw,
		direction.y,
		direction.x * sinYaw + direction.z * cosYaw);
	float u = atan2(d.z, d.x) / TWO_PI + 0.5f;
	float v = acos(clamp(d.y, -1.0f, 1.0f)) / PI;
	return g_SkyTexture.SampleLevel(g_SamplerState, float2(frac(u), v), 0.0f).rgb;
}

// GGX法線分布。太陽のきらめき用。
float GgxD(float NdotH, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float d = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
	return a2 / max(PI * d * d, 0.0001f);
}

void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{
	float3 worldPos = In.WorldPosition.xyz;

	// 縁はぼかして床写真になじませる。内側を負にして以降の式で使う。
	float edgeDistance = -DistanceToShore(worldPos.xz);
	float edgeMask = saturate(-edgeDistance / max(WaterShape.w, 0.0001f));
	float3 toCamera = CameraPosition.xyz - worldPos;
	float distanceUnits = length(toCamera);
	float3 V = toCamera / max(distanceUnits, 0.0001f);
	float metersPerUnit = WaterWave.w;
	float distanceMeters = distanceUnits * metersPerUnit;
	float2 p = worldPos.xz * metersPerUnit;
	float time = CameraPosition.w;

	float2 gradient = WaveGradient(p, time, distanceMeters);
	float3 N = normalize(float3(-gradient.x, 1.0f, -gradient.y));
	float NdotV = max(dot(N, V), 0.02f);

	float3 L = normalize(-Light.Direction.xyz);
	float3 sunColor = Light.Diffuse.rgb * max(Light.PointLightParam.y, 0.0f);
	float sunUp = saturate(L.y);
	float shadow = CalcCascadedShadow(worldPos);

	// 水の色。縁が浅瀬、奥へ進むほど深場。
	float depthT = saturate(-edgeDistance / max(WaterMisc2.x, 0.0001f));
	float3 waterColor = lerp(WaterShallow.rgb, WaterDeep.rgb, depthT);
	float bodyAlpha = lerp(WaterShallow.a, WaterDeep.a, depthT);
	float3 lighting = Light.Ambient.rgb + sunColor * (0.15f + 0.25f * sunUp) * shadow;
	float3 body = waterColor * lighting;

	// 水底に揺れる光の網目。遠くでは平均化されるので消す。
	float causticFade = 1.0f - smoothstep(30.0f, 120.0f, distanceMeters);
	body += sunColor * sunUp * shadow * WaterMisc.y * causticFade * (1.0f - depthT * 0.6f) *
		Caustic(p, time) * waterColor * 1.5f;

	// 縁の泡。波にあわせて出入りさせる。
	float foamBand = 1.0f - smoothstep(0.0f, 0.6f, -edgeDistance * metersPerUnit);
	float foamWave = 0.5f + 0.5f * sin(edgeDistance * metersPerUnit * 5.0f - time * 1.3f +
		sin(p.x * 0.9f + time * 0.5f) * 2.0f + cos(p.y * 1.1f - time * 0.4f) * 2.0f);
	float foam = saturate(foamBand * foamWave * WaterMisc.z);
	body = lerp(body, lighting * 0.9f + 0.1f, foam);
	bodyAlpha = lerp(bodyAlpha, 1.0f, foam);

	// 空の映り込み。真下へ向く反射は水平側へ折り返す。
	float3 R = reflect(-V, N);
	R.y = abs(R.y);
	float3 sky = SampleSky(R) * WaterMisc2.y;
	float fresnel = WATER_F0 + (1.0f - WATER_F0) * pow(1.0f - NdotV, 5.0f);

	// 太陽のきらめき（GGX）。見かけの大きさが揺れる波で変わるので細かく光る。
	float3 H = normalize(V + L);
	float NdotL = saturate(dot(N, L));
	float NdotH = saturate(dot(N, H));
	float VdotH = saturate(dot(V, H));
	float fresnelH = WATER_F0 + (1.0f - WATER_F0) * pow(1.0f - VdotH, 5.0f);
	float glint = GgxD(NdotH, 0.06f) * fresnelH * NdotL / max(4.0f * NdotV * max(NdotL, 0.05f), 0.05f);
	float3 sunSpec = sunColor * min(glint, 40.0f) * WaterMisc.x * shadow * (1.0f - foam);

	float3 emitted = body * bodyAlpha * (1.0f - fresnel) + sky * fresnel * (1.0f - foam) + sunSpec;
	float coverage = 1.0f - (1.0f - fresnel) * (1.0f - bodyAlpha);
	coverage = max(coverage, 0.0001f);

	float3 color = emitted / coverage;
	outDiffuse.rgb = ApplyFog(color, worldPos);
	outDiffuse.a = coverage * edgeMask;
}
