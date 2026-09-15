#include "Common.hlsl"

Texture2D g_Texture : register(t0);
Texture2D g_NormalMap : register(t2);
Texture2D g_MetallicMap : register(t3);
Texture2D g_RoughnessMap : register(t4);
Texture2D g_EmissiveMap : register(t5);
TextureCube g_EnvCube : register(t6);
SamplerState g_SamplerState : register(s0);

static const float PI = 3.14159265f;

// 1灯分の直接光(拡散+鏡面)を計算して返す。
// Position.w == 0 のときは平行光（Direction が進む向き、強度は PointLightParam.y）。
float3 CalcDirectLight(LIGHT lgt, float3 N, float3 V, float3 worldPos,
                       float3 albedo, float metallic, float roughness)
{
	float3 result = float3(0.0f, 0.0f, 0.0f);
	if (lgt.Enable)
	{
		float lightIntensity = max(lgt.PointLightParam.y, 0.0f);
		float3 L;
		float attenuation;
		if (lgt.Position.w == 0.0f)
		{
			L = normalize(-lgt.Direction.xyz);
			attenuation = lightIntensity;
		}
		else
		{
			L = normalize(lgt.Position.xyz - worldPos);
			float distanceToLight = length(lgt.Position.xyz - worldPos);
			float lightRange = max(lgt.PointLightParam.x, 0.001f);
			attenuation = saturate(1.0f - distanceToLight / lightRange);
			attenuation = attenuation * attenuation * lightIntensity;
		}
		float3 H = normalize(V + L);

		float NdotL = saturate(dot(N, L)) * attenuation;
		float NdotV = max(dot(N, V), 0.001f);
		float NdotH = max(dot(N, H), 0.001f);
		float VdotH = max(dot(V, H), 0.001f);

		// GGX ベースの簡易 PBR 反射計算。
		float a = roughness * roughness;
		float a2 = a * a;
		float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
		float D = a2 / max(PI * denom * denom, 0.001f);

		float k = (roughness + 1.0f);
		k = (k * k) / 8.0f;
		float Gv = NdotV / (NdotV * (1.0f - k) + k);
		float Gl = NdotL / (NdotL * (1.0f - k) + k);
		float G = Gv * Gl;

		float3 F0 = lerp(0.04f.xxx, albedo, metallic);
		float3 F = F0 + (1.0f - F0) * pow(1.0f - VdotH, 5.0f);
		float3 specular = (D * G * F) / max(4.0f * NdotV * max(NdotL, 0.001f), 0.001f);
		float3 diffuse = albedo * (1.0f - metallic) * (1.0f / PI);

		// PointLightParam.z にスペキュラーの強さ倍率を入れている。
		float specularStrength = lgt.PointLightParam.z;

		result = (diffuse + specular * specularStrength) * lgt.Diffuse.rgb * NdotL;
	}
	return result;
}

float3 CalcNormalMapWorldNormal(PS_IN In)
{
	float3 baseNormal = normalize(In.Normal.xyz);
	float3 normalSample = g_NormalMap.Sample(g_SamplerState, In.TexCoord).xyz * 2.0f - 1.0f;

	float3 dp1 = ddx(In.WorldPosition.xyz);
	float3 dp2 = ddy(In.WorldPosition.xyz);
	float2 duv1 = ddx(In.TexCoord);
	float2 duv2 = ddy(In.TexCoord);

	float det = duv1.x * duv2.y - duv1.y * duv2.x;
	float3 tangent;
	float3 bitangent;
	if (abs(det) > 0.00000001f)
	{
		float invDet = 1.0f / det;
		tangent = (dp1 * duv2.y - dp2 * duv1.y) * invDet;
		bitangent = (dp2 * duv1.x - dp1 * duv2.x) * invDet;
	}
	else
	{
		float3 axis = (abs(baseNormal.y) < 0.99f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
		tangent = cross(axis, baseNormal);
		bitangent = cross(baseNormal, tangent);
	}

	tangent = normalize(tangent);
	bitangent = normalize(bitangent);
	return normalize(normalSample.x * tangent + normalSample.y * bitangent + normalSample.z * baseNormal);
}

void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{
	float4 texColor = g_Texture.Sample(g_SamplerState, In.TexCoord);
	float3 albedo = texColor.rgb * In.Diffuse.rgb;
	float3 emissive = float3(0.0f, 0.0f, 0.0f);
	float metallic;
	float roughness;
	float3 N;
	// Parameter.z は TexMode。
	// < 0.5: マップ無し。x=粗さ y=金属度。頂点法線。
	// < 1.5: 独立マップ（metallic/roughness は .r）。
	// >= 1.5: glTF packed ORM（G=roughness, B=metallic）。xy は factor。
	[branch]
	if (Parameter.z >= 2.5f)
	{
		metallic = saturate(Parameter.y);
		roughness = max(saturate(Parameter.x), 0.04f);
		N = normalize(In.Normal.xyz);
	}
	else if (Parameter.z < 0.5f)
	{
		metallic = saturate(Parameter.y);
		roughness = max(saturate(Parameter.x), 0.04f);
		N = normalize(In.Normal.xyz);
	}
	else if (Parameter.z < 1.0f)
	{
		// 法線またはエミッシブだけを持つマテリアルは係数を使用する。
		emissive = g_EmissiveMap.Sample(g_SamplerState, In.TexCoord).rgb;
		metallic = saturate(Parameter.y);
		roughness = max(saturate(Parameter.x), 0.04f);
		N = CalcNormalMapWorldNormal(In);
	}
	else if (Parameter.z < 1.5f)
	{
		emissive = g_EmissiveMap.Sample(g_SamplerState, In.TexCoord).rgb;
		metallic = saturate(g_MetallicMap.Sample(g_SamplerState, In.TexCoord).r);
		roughness = saturate(g_RoughnessMap.Sample(g_SamplerState, In.TexCoord).r);
		roughness = max(roughness, 0.04f);
		N = CalcNormalMapWorldNormal(In);
	}
	else
	{
		emissive = g_EmissiveMap.Sample(g_SamplerState, In.TexCoord).rgb;
		float4 packedMr = g_MetallicMap.Sample(g_SamplerState, In.TexCoord);
		metallic = saturate(packedMr.b * Parameter.y);
		roughness = saturate(packedMr.g * Parameter.x);
		roughness = max(roughness, 0.04f);
		N = CalcNormalMapWorldNormal(In);
	}
	float3 V = normalize(CameraPosition.xyz - In.WorldPosition.xyz);
	float3 worldPos = In.WorldPosition.xyz;

	// 3点照明(キー/フィル/リム)を積算する。
	// いずれも無効なら、従来の単一ライトへフォールバックする。
	float3 direct = float3(0.0f, 0.0f, 0.0f);
	bool anyPlayerLight = false;
	[unroll]
	for (int i = 0; i < NUM_PLAYER_LIGHTS; i++)
	{
		if (PlayerLights[i].Enable)
		{
			anyPlayerLight = true;
			direct += CalcDirectLight(PlayerLights[i], N, V, worldPos, albedo, metallic, roughness);
		}
	}
	if (!anyPlayerLight)
	{
		direct += CalcDirectLight(Light, N, V, worldPos, albedo, metallic, roughness);
	}

	// アンビエントは、3点照明使用時はその専用値(PlayerLights[0].Ambient)を、
	// 単一ライトへフォールバック時は Light.Ambient を使う。
	// これにより Player と Field(Phong) の環境光を独立して設定できる。
	float3 ambientColor = anyPlayerLight ? PlayerLights[0].Ambient.rgb : Light.Ambient.rgb;
	float3 ambient = albedo * max(ambientColor, 0.0f);
	float shadow = 1.0f;
	if (Parameter.w > 0.5f)
	{
		shadow = CalcCascadedShadow(In.WorldPosition.xyz);
	}
	float3 envSpecular = float3(0.0f, 0.0f, 0.0f);
	if (Parameter.z >= 2.5f)
	{
		float3 R = reflect(
			-V,
			Null2MembraneNormal(N, In.WorldPosition.xyz, CameraPosition.w));
		float3 env = g_EnvCube.SampleLevel(g_SamplerState, R, 0.0f).rgb;
		float3 F0 = lerp(0.04f.xxx, albedo, metallic);
		float NdotV = max(dot(N, V), 0.001f);
		float3 F = F0 + (1.0f - F0) * pow(1.0f - NdotV, 5.0f);
		envSpecular = env * F;
		ambient = float3(0.0f, 0.0f, 0.0f);
	}
	float3 hdr = ambient + envSpecular + direct * shadow + emissive;
	// LDRへ出す前に、1を超えた画素だけピークを畳む。
	float peak = max(max(hdr.r, hdr.g), hdr.b);
	if (peak > 1.0f)
	{
		hdr *= 1.0f / peak;
	}
	outDiffuse.rgb = saturate(ApplyFog(saturate(hdr), In.WorldPosition.xyz));
	outDiffuse.a = texColor.a * In.Diffuse.a;
}
