/*定数バッファ C言語から受け取るデータ楊の変数*/
//ワールド行列
cbuffer WorldBuffer : register(b0)
{
	matrix World;
}

//カメラ行列
cbuffer ViewBuffer : register(b1)
{
	matrix View;
}

//プロジェクション行列
cbuffer ProjectionBuffer : register(b2)
{
	matrix Projection;
}

cbuffer MaterialBuffer : register(b3)
{
	float4 MaterialAmbient;
	float4 MaterialDiffuse;
	float4 MaterialSpecular;
	float4 MaterialEmission;
	float MaterialShininess;
	float3 MaterialPadding;
}

//頂点構造体 頂点シェーダーが頂点バッファの情報を受け取るための構造体
struct VS_IN
{
	float4 Position : POSITION0;
	float4 Normal : NORMAL0;
	float4 Diffuse : COLOR0;
	float2 TexCoord : TEXCOORD0;
};

//頂点(ピクセル)構造体 頂点シェーダーの出力とピクセルシェーダーの入力を兼ねている
struct PS_IN
{
	float4 Position : SV_POSITION;
	float4 WorldPosition : POSITION0;
	float4 Normal : NORMAL0;
	float4 Diffuse : COLOR0;
	float2 TexCoord : TEXCOORD0;
    // ライトから見たときの座標。床などが「自分は影の中か」を調べるために使う。
	float4 ShadowPosition : TEXCOORD1;
};

//ライト構造体 今後使用するライトのデータを受け取る構造体
struct LIGHT
{
	bool Enable;
	bool3 Dummy;
	float4 Direction;
	float4 Diffuse;
	float4 Ambient;
    
	float4 Position;
	float4 PointLightParam;
	float4 Angle; // スポットライト: x=コーン半角(rad)
	float4 SkyColor; // 天球色
	float4 GroundColor; // 地面色
	float4 GroundNormal; // 地面法線
};

/*その他の定数バッファ*/
//ライトオブジェクト
cbuffer LightBuffer : register(b4)
{
	LIGHT Light;
};

//カメラ座標。w はシェーダー用経過秒（null2 膜の揺れなど）。
cbuffer CameraBuffer : register(b5)
{
	float4 CameraPosition;
};

cbuffer Null2Buffer : register(b12)
{
	float4 Null2Param;
};

float Null2MembraneOffset(float3 worldPos, float timeSeconds)
{
	float phase = dot(worldPos, float3(19.7f, 8.3f, 14.1f));
	float t = timeSeconds * Null2Param.y;
	float shake = sin(t * 44.0f + phase);
	shake += 0.40f * sin(t * 63.0f + phase * 1.63f);
	shake += 0.22f * sin(t * 29.5f + worldPos.y * 31.0f);
	return shake * Null2Param.x;
}

float3 Null2MembraneNormal(float3 baseNormal, float3 worldPos, float timeSeconds)
{
	float3 N = normalize(baseNormal);
	float3 axis = (abs(N.y) < 0.94f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
	float3 T = normalize(cross(axis, N));
	float3 B = cross(N, T);
	float n0 = Null2MembraneOffset(worldPos, timeSeconds);
	float nx = Null2MembraneOffset(worldPos + T * 0.045f, timeSeconds);
	float nz = Null2MembraneOffset(worldPos + B * 0.045f, timeSeconds);
	return normalize(N + T * ((nx - n0) * Null2Param.w) + B * ((nz - n0) * Null2Param.w));
}

//3点照明(PBR専用)。キー/フィル/リムの3灯。
#define NUM_PLAYER_LIGHTS 3
cbuffer PlayerLightBuffer : register(b7)
{
	LIGHT PlayerLights[NUM_PLAYER_LIGHTS];
};

//汎用パラメーター
cbuffer ParameterBuffer : register(b6)
{
	float4 Parameter;
};

// ShadowMap用の共通データ。
// LightViewProjection は「ライト視点のView行列 * Projection行列」。
// ShadowParam.x は深度ずれ防止の補正値、ShadowParam.y は影の暗さ。
// CascadeSplits.xyz はカメラ深度による各カスケードの終端距離。
// CascadeTexelSize.xyz は各カスケードの1テクセル分のUVサイズ。
cbuffer ShadowBuffer : register(b8)
{
	matrix LightViewProjection[3];
	float4 ShadowParam;
	float4 CascadeSplits;
	float4 CascadeTexelSize;
};

// 距離・高度フォグ。
// FogColor.a は密度、FogParam は start/end/heightMin/heightRange。
cbuffer FogBuffer : register(b11)
{
	float4 FogColor;
	float4 FogParam;
};

float3 ApplyFog(float3 sceneColor, float3 worldPosition)
{
	const float HEIGHT_WEIGHT = 0.65f;
	float distanceToCamera = distance(worldPosition, CameraPosition.xyz);
	float distanceRange = max(FogParam.y - FogParam.x, 0.001f);
	float distanceFactor = saturate(
		(distanceToCamera - FogParam.x) / distanceRange);
	float heightRange = max(FogParam.w, 0.001f);
	float heightFactor = saturate(
		1.0f - (worldPosition.y - FogParam.z) / heightRange);
	float fogAmount = saturate(
		distanceFactor *
		lerp(1.0f, heightFactor, HEIGHT_WEIGHT) *
		max(FogColor.a, 0.0f));
	return lerp(sceneColor, FogColor.rgb, fogAmount);
}

// t1には、先にライト視点で描いた深度テクスチャ(ShadowMap)を入れる。
Texture2DArray g_ShadowMap : register(t1);
SamplerState g_ShadowSampler : register(s1);

// ShadowPositionをShadowMap上のUVに変換して、影の中かどうかを返す。
// 戻り値は 1.0 が影なし、ShadowParam.y に近いほど影が濃い。
float CalcShadowProjected(float4 shadowPosition, float2 texelSize, int cascadeIndex)
{
	float shadowVal = 1.0f;

    // クリップ空間から、-1～1の投影座標に戻す。
	float3 proj = shadowPosition.xyz / shadowPosition.w;

    // 投影座標(-1～1)をテクスチャ座標(0～1)に変換する。
	float2 uv = float2(proj.x * 0.5f + 0.5f, -proj.y * 0.5f + 0.5f);

    // ShadowMapの範囲外なら、この場所は影判定できないので影なしにする。
	if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f || proj.z < 0.0f || proj.z > 1.0f)
	{
		shadowVal = 1.0f;
	}
	else
	{
		float currentDepth = proj.z - ShadowParam.x;

		// PCF(Percentage Closer Filtering)。
		// 中心の1点だけで影判定すると境界がギザギザになりやすいので、
		// 周囲3x3の深度も見て、影あり/なしを平均する。

		float litCount = 0.0f;

		[unroll]
		for (int y = -1; y <= 1; y++)
		{
			[unroll]
			for (int x = -1; x <= 1; x++)
			{
				float2 sampleUV = uv + float2(x, y) * texelSize;
				float shadowDepth = g_ShadowMap.Sample(
					g_ShadowSampler,
					float3(sampleUV, (float)cascadeIndex)).r;

				// 現在の場所のほうが手前なら明るい。奥なら手前に何かがあるので影。
				litCount += (currentDepth > shadowDepth) ? 0.0f : 1.0f;
			}
		}

		// 9点のうち明るい割合を使って、影の境界をなめらかにする。
		float visibility = litCount / 9.0f;
		shadowVal = lerp(ShadowParam.y, 1.0f, visibility);
	}

	return shadowVal;
}

// 既存の単一シャドウマップ利用箇所は、互換用に先頭カスケードを読む。
float CalcShadow(float4 shadowPosition)
{
	return CalcShadowProjected(
		shadowPosition,
		CascadeTexelSize.xx,
		0);
}

// カメラ深度に応じてカスケードを選択する。
// 近距離のカスケードほど狭い範囲を同じ解像度で描くため、高精度になる。
float CalcCascadedShadow(float3 worldPosition)
{
	float viewDepth = mul(float4(worldPosition, 1.0f), View).z;
	if (viewDepth < 0.0f || viewDepth > CascadeSplits.z)
	{
		return 1.0f;
	}

	int cascadeIndex = 0;
	if (viewDepth > CascadeSplits.x)
	{
		cascadeIndex = 1;
	}
	if (viewDepth > CascadeSplits.y)
	{
		cascadeIndex = 2;
	}

	float4 shadowPosition =
		mul(float4(worldPosition, 1.0f), LightViewProjection[cascadeIndex]);
	float texelSize = CascadeTexelSize[cascadeIndex];
	return CalcShadowProjected(
		shadowPosition,
		float2(texelSize, texelSize),
		cascadeIndex);
}
