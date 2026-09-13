#include "sunlight.h"
#include "field.h"
#include "main.h"
#include "renderer.h"
#include "camera.h"
#include "texture.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

using namespace DirectX;

static const float SUN_FALLBACK_AZIMUTH = -111.5f;
static const float SUN_FALLBACK_ELEVATION = 63.4f;
static const float SUN_AZIMUTH_DEFAULT = -170.0f;
static const XMFLOAT3 SUN_FALLBACK_COLOR = {
	255.0f / 255.0f,
	232.0f / 255.0f,
	230.0f / 255.0f
};
static const float SUN_DEFAULT_INTENSITY = 2.0f;
static const float SUN_AMBIENT_SCALE_DEFAULT = 0.8f;
static const float SUN_AMBIENT_MIN = 0.20f;
static const float SUN_ROUGHNESS_DEFAULT = 0.81f;
static const float SUN_METALLIC_DEFAULT = 0.0f;
static const float SUN_SKY_YAW_OFFSET_DEFAULT = 0.0f;
static const float SUN_SPECULAR_STRENGTH = 1.0f;
static const float SUN_SHADOW_RADIUS_DEFAULT = 160.0f;
static const float SUN_SHADOW_CASCADE_1_DEFAULT = 10.0f;
static const float SUN_SHADOW_CASCADE_2_DEFAULT = 70.0f;
static const float SUN_SHADOW_BIAS_DEFAULT = 0.0005f;
static const float SUN_SHADOW_BRIGHTNESS_DEFAULT = 0.25f;
static const float SUN_SHADOW_MAP_SIZE = 4096.0f;
static const float SUN_SHADOW_NEAR_PROJECTION_PADDING = 8.0f;
static const float SUN_SHADOW_PROJECTION_PADDING = 24.0f;
static const float SUN_SHADOW_CASTER_PADDING = 24.0f;
static const float SUN_HDR_THRESHOLD_RATIO = 0.5f;
static const char* SUN_HDR_TEXTURE_PATH = "asset\\texture\\pizzo_pernice_puresky_4k.hdr";

struct SunlightExtractedData
{
	float azimuth = SUN_FALLBACK_AZIMUTH;
	float elevation = SUN_FALLBACK_ELEVATION;
	XMFLOAT3 color = SUN_FALLBACK_COLOR;
	float intensity = SUN_DEFAULT_INTENSITY;
	float u = 0.0f;
	float v = 0.0f;
	float threshold = 0.0f;
	size_t pixelCount = 0;
};

static SunlightExtractedData g_ExtractedSunlight;
static bool g_HasExtractedSunlight = false;
static float g_Azimuth = SUN_AZIMUTH_DEFAULT;
static float g_Elevation = SUN_FALLBACK_ELEVATION;
static XMFLOAT3 g_Color = SUN_FALLBACK_COLOR;
static float g_Intensity = SUN_DEFAULT_INTENSITY;
static float g_AmbientScale = SUN_AMBIENT_SCALE_DEFAULT;
static float g_SkyYawOffset = SUN_SKY_YAW_OFFSET_DEFAULT;
static float g_Roughness = SUN_ROUGHNESS_DEFAULT;
static float g_Metallic = SUN_METALLIC_DEFAULT;
static float g_ShadowRadius = SUN_SHADOW_RADIUS_DEFAULT;
static float g_ShadowCascadeDistances[NUM_SHADOW_CASCADES] = {
	SUN_SHADOW_CASCADE_1_DEFAULT,
	SUN_SHADOW_CASCADE_2_DEFAULT,
	SUN_SHADOW_RADIUS_DEFAULT
};
static float g_ShadowBias = SUN_SHADOW_BIAS_DEFAULT;
static float g_ShadowBrightness = SUN_SHADOW_BRIGHTNESS_DEFAULT;

static float Saturate(float value)
{
	if (value < 0.0f)
	{
		return 0.0f;
	}
	if (value > 1.0f)
	{
		return 1.0f;
	}
	return value;
}

struct SunlightComponent
{
	double luminanceSum = 0.0;
	double weightedUCos = 0.0;
	double weightedUSin = 0.0;
	double weightedV = 0.0;
	double weightedRed = 0.0;
	double weightedGreen = 0.0;
	double weightedBlue = 0.0;
	size_t pixelCount = 0;
};

static int FindComponentRoot(std::vector<int>& parent, int index)
{
	int root = index;
	while (parent[root] != root)
	{
		root = parent[root];
	}

	while (parent[index] != index)
	{
		const int next = parent[index];
		parent[index] = root;
		index = next;
	}
	return root;
}

static void UnionComponents(std::vector<int>& parent, int first, int second)
{
	first = FindComponentRoot(parent, first);
	second = FindComponentRoot(parent, second);
	if (first != second)
	{
		parent[second] = first;
	}
}

static bool ExtractSunlightFromHDR(
	const HDRImageData& image,
	SunlightExtractedData* outData)
{
	if (!outData || image.width == 0 || image.height == 0 ||
		image.width > (std::numeric_limits<size_t>::max)() / image.height)
	{
		return false;
	}

	const size_t pixelCount = image.width * image.height;
	if (pixelCount > (std::numeric_limits<size_t>::max)() / 4 ||
		pixelCount > static_cast<size_t>((std::numeric_limits<int>::max)()) ||
		image.rgba.size() < pixelCount * 4)
	{
		return false;
	}

	float maxLuminance = 0.0f;
	for (size_t i = 0; i < pixelCount; ++i)
	{
		const float red = image.rgba[i * 4 + 0];
		const float green = image.rgba[i * 4 + 1];
		const float blue = image.rgba[i * 4 + 2];
		const float luminance = 0.2126f * red + 0.7152f * green + 0.0722f * blue;
		if (std::isfinite(luminance) && luminance > maxLuminance)
		{
			maxLuminance = luminance;
		}
	}

	if (!(maxLuminance > 0.0f) || !std::isfinite(maxLuminance))
	{
		return false;
	}

	const float threshold = maxLuminance * SUN_HDR_THRESHOLD_RATIO;
	double skyLuminanceSum = 0.0;
	size_t skyPixelCount = 0;
	std::vector<unsigned char> bright(pixelCount, 0);
	std::vector<int> parent(pixelCount, -1);
	for (size_t i = 0; i < pixelCount; ++i)
	{
		const float red = image.rgba[i * 4 + 0];
		const float green = image.rgba[i * 4 + 1];
		const float blue = image.rgba[i * 4 + 2];
		const float luminance = 0.2126f * red + 0.7152f * green + 0.0722f * blue;
		if (std::isfinite(luminance) && luminance >= threshold)
		{
			bright[i] = 1;
			parent[i] = static_cast<int>(i);
		}
		else if (std::isfinite(luminance) && luminance > 0.0f)
		{
			skyLuminanceSum += static_cast<double>(luminance);
			++skyPixelCount;
		}
	}

	// 正距円筒図法の左右端を接続して、シーム上の太陽も1成分にする。
	for (size_t y = 0; y < image.height; ++y)
	{
		for (size_t x = 0; x < image.width; ++x)
		{
			const size_t index = y * image.width + x;
			if (!bright[index])
			{
				continue;
			}

			const size_t rightX = x + 1 < image.width ? x + 1 : 0;
			const size_t rightIndex = y * image.width + rightX;
			if (bright[rightIndex])
			{
				UnionComponents(
					parent,
					static_cast<int>(index),
					static_cast<int>(rightIndex));
			}

			if (y + 1 < image.height)
			{
				const size_t downIndex = (y + 1) * image.width + x;
				if (bright[downIndex])
				{
					UnionComponents(
						parent,
						static_cast<int>(index),
						static_cast<int>(downIndex));
				}
			}
		}
	}

	std::vector<SunlightComponent> components;
	std::unordered_map<int, size_t> rootToComponent;
	for (size_t y = 0; y < image.height; ++y)
	{
		for (size_t x = 0; x < image.width; ++x)
		{
			const size_t pixelIndex = y * image.width + x;
			if (!bright[pixelIndex])
			{
				continue;
			}

			const int root = FindComponentRoot(parent, static_cast<int>(pixelIndex));
			auto componentIt = rootToComponent.find(root);
			size_t componentIndex = 0;
			if (componentIt == rootToComponent.end())
			{
				componentIndex = components.size();
				rootToComponent.emplace(root, componentIndex);
				components.emplace_back();
			}
			else
			{
				componentIndex = componentIt->second;
			}

			const float red = image.rgba[pixelIndex * 4 + 0];
			const float green = image.rgba[pixelIndex * 4 + 1];
			const float blue = image.rgba[pixelIndex * 4 + 2];
			const float luminance =
				0.2126f * red + 0.7152f * green + 0.0722f * blue;
			const double weight = (std::max)(static_cast<double>(luminance), 0.0);
			const double longitude =
				(2.0 * XM_PI * (static_cast<double>(x) + 0.5)) /
				static_cast<double>(image.width);
			SunlightComponent& component = components[componentIndex];
			component.luminanceSum += weight;
			component.weightedUCos += weight * cos(longitude);
			component.weightedUSin += weight * sin(longitude);
			component.weightedV +=
				weight * ((static_cast<double>(y) + 0.5) /
					static_cast<double>(image.height));
			component.weightedRed += weight * (std::max)(static_cast<double>(red), 0.0);
			component.weightedGreen += weight * (std::max)(static_cast<double>(green), 0.0);
			component.weightedBlue += weight * (std::max)(static_cast<double>(blue), 0.0);
			++component.pixelCount;
		}
	}

	if (components.empty())
	{
		return false;
	}

	size_t bestIndex = 0;
	for (size_t i = 1; i < components.size(); ++i)
	{
		if (components[i].luminanceSum > components[bestIndex].luminanceSum)
		{
			bestIndex = i;
		}
	}

	const SunlightComponent& best = components[bestIndex];
	if (!(best.luminanceSum > 0.0) ||
		!(best.pixelCount > 0) ||
		!std::isfinite(best.luminanceSum))
	{
		return false;
	}

	double uAngle = atan2(best.weightedUSin, best.weightedUCos);
	if (uAngle < 0.0)
	{
		uAngle += 2.0 * XM_PI;
	}
	const float u = static_cast<float>(uAngle / (2.0 * XM_PI));
	const float v = Saturate(static_cast<float>(best.weightedV / best.luminanceSum));
	// スカイドームの正距円筒UVと同じY-up座標系へ戻す。
	// u=0.5 が +X、v=0 が天頂になる定義。
	const float longitude = (u - 0.5f) * 2.0f * XM_PI;
	const float latitude = v * XM_PI;
	const XMVECTOR localDirection = XMVectorSet(
		sinf(latitude) * cosf(longitude),
		cosf(latitude),
		sinf(latitude) * sinf(longitude),
		0.0f);
	const float azimuth = XMConvertToDegrees(atan2f(
		XMVectorGetX(localDirection),
		XMVectorGetZ(localDirection)));
	const float elevation = XMConvertToDegrees(asinf(
		Saturate(XMVectorGetY(localDirection) * 0.5f + 0.5f) * 2.0f - 1.0f));

	const XMFLOAT3 averageColor = {
		static_cast<float>(best.weightedRed / best.luminanceSum),
		static_cast<float>(best.weightedGreen / best.luminanceSum),
		static_cast<float>(best.weightedBlue / best.luminanceSum)
	};
	const float colorPeak = (std::max)(
		averageColor.x,
		(std::max)(averageColor.y, averageColor.z));
	if (!(colorPeak > 0.0f) || !std::isfinite(colorPeak))
	{
		return false;
	}

	outData->azimuth = azimuth;
	outData->elevation = elevation;
	outData->color = {
		averageColor.x / colorPeak,
		averageColor.y / colorPeak,
		averageColor.z / colorPeak
	};
	const float sunMean = static_cast<float>(best.luminanceSum / best.pixelCount);
	const float skyMean = skyPixelCount > 0
		? static_cast<float>(skyLuminanceSum / static_cast<double>(skyPixelCount))
		: sunMean;
	const float ratio = sunMean / (std::max)(skyMean, 1.0e-4f);
	// 太陽ディスクの生輝度は数千〜数万になり得るので、空との比を対数で会場向け強度へ落とす。
	outData->intensity = (std::min)(
		3.5f,
		(std::max)(0.80f, log2f(1.0f + ratio) * 0.35f));
	outData->u = u;
	outData->v = v;
	outData->threshold = threshold;
	outData->pixelCount = best.pixelCount;
	return std::isfinite(outData->intensity);
}

static void ApplySunlightState(void)
{
	const float azimuthRad = XMConvertToRadians(g_Azimuth);
	const float elevationRad = XMConvertToRadians(g_Elevation);
	const float cosEl = cosf(elevationRad);
	const float sinEl = sinf(elevationRad);
	const XMFLOAT3 sunVec = {
		cosEl * sinf(azimuthRad),
		sinEl,
		cosEl * cosf(azimuthRad)
	};

	const float elevFactor =
		SUN_AMBIENT_MIN + (1.0f - SUN_AMBIENT_MIN) * Saturate(sinEl);
	const XMFLOAT3 skyTint = { 0.55f, 0.70f, 1.0f };
	const float ambientMix = 0.35f;
	const XMFLOAT3 ambientColor = {
		(g_Color.x * (1.0f - ambientMix) + skyTint.x * ambientMix) *
			g_AmbientScale * elevFactor,
		(g_Color.y * (1.0f - ambientMix) + skyTint.y * ambientMix) *
			g_AmbientScale * elevFactor,
		(g_Color.z * (1.0f - ambientMix) + skyTint.z * ambientMix) *
			g_AmbientScale * elevFactor
	};

	LIGHT light = {};
	light.Enable = TRUE;
	light.Direction = XMFLOAT4(-sunVec.x, -sunVec.y, -sunVec.z, 0.0f);
	light.Diffuse = XMFLOAT4(g_Color.x, g_Color.y, g_Color.z, 1.0f);
	light.Ambient = XMFLOAT4(ambientColor.x, ambientColor.y, ambientColor.z, 1.0f);
	light.Position = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
	light.PointLightParam = XMFLOAT4(0.0f, g_Intensity, SUN_SPECULAR_STRENGTH, 0.0f);
	SetLight(light);
	SetParameter(XMFLOAT4(g_Roughness, g_Metallic, 0.0f, 0.0f));

	const float skyYaw =
		(g_Azimuth - g_ExtractedSunlight.azimuth) +
		g_SkyYawOffset;
	Field_SetSkyboxYaw(skyYaw);
}

void Sunlight_Initialize(void)
{
	g_ExtractedSunlight = SunlightExtractedData();
	g_HasExtractedSunlight = false;
	g_Azimuth = SUN_AZIMUTH_DEFAULT;
	g_Elevation = SUN_FALLBACK_ELEVATION;
	g_Color = SUN_FALLBACK_COLOR;
	g_Intensity = SUN_DEFAULT_INTENSITY;
	g_AmbientScale = SUN_AMBIENT_SCALE_DEFAULT;
	g_SkyYawOffset = SUN_SKY_YAW_OFFSET_DEFAULT;
	g_Roughness = SUN_ROUGHNESS_DEFAULT;
	g_Metallic = SUN_METALLIC_DEFAULT;
	g_ShadowRadius = SUN_SHADOW_RADIUS_DEFAULT;
	g_ShadowCascadeDistances[0] = SUN_SHADOW_CASCADE_1_DEFAULT;
	g_ShadowCascadeDistances[1] = SUN_SHADOW_CASCADE_2_DEFAULT;
	g_ShadowCascadeDistances[2] = SUN_SHADOW_RADIUS_DEFAULT;
	g_ShadowBias = SUN_SHADOW_BIAS_DEFAULT;
	g_ShadowBrightness = SUN_SHADOW_BRIGHTNESS_DEFAULT;

	HDRImageData hdrImage;
	ID3D11ShaderResourceView* skyTexture = nullptr;
	int pathLength = MultiByteToWideChar(
		CP_ACP,
		0,
		SUN_HDR_TEXTURE_PATH,
		-1,
		nullptr,
		0);
	std::vector<wchar_t> widePath(
		pathLength > 0 ? static_cast<size_t>(pathLength) : 1,
		L'\0');
	if (pathLength > 0 &&
		MultiByteToWideChar(
			CP_ACP,
			0,
			SUN_HDR_TEXTURE_PATH,
			-1,
			widePath.data(),
			pathLength) > 0 &&
		LoadHDRTextureForSky(widePath.data(), &hdrImage, &skyTexture))
	{
		Field_SetSkyboxTexture(skyTexture);
		if (ExtractSunlightFromHDR(hdrImage, &g_ExtractedSunlight))
		{
			g_HasExtractedSunlight = true;
			g_Azimuth = SUN_AZIMUTH_DEFAULT;
			g_Elevation = g_ExtractedSunlight.elevation;
			g_Color = g_ExtractedSunlight.color;
		}
	}
	ApplySunlightState();
}

void Sunlight_Finalize(void)
{
}

void Sunlight_Update(void)
{
	ApplySunlightState();
}

void Sunlight_Apply(void)
{
	ApplySunlightState();
}

static bool BuildShadowCascade(
	Camera* camera,
	const XMVECTOR& lightDir,
	float startDepth,
	float endDepth,
	float projectionPadding,
	XMMATRIX* outView,
	XMMATRIX* outProjection,
	XMFLOAT3* outFocus,
	float* outRadius,
	float* outTexelSize)
{
	if (!camera || !outView || !outProjection || !outFocus ||
		!outRadius || !outTexelSize || projectionPadding < 0.0f ||
		endDepth <= startDepth)
	{
		return false;
	}

	XMVECTOR determinant = {};
	const XMMATRIX inverseProjection =
		XMMatrixInverse(&determinant, camera->GetProjection());
	if (fabsf(XMVectorGetX(determinant)) < 0.000001f)
	{
		return false;
	}
	const XMMATRIX inverseView =
		XMMatrixInverse(&determinant, camera->GetView());
	if (fabsf(XMVectorGetX(determinant)) < 0.000001f)
	{
		return false;
	}

	XMFLOAT3 corners[8] = {};
	int cornerIndex = 0;
	for (int depthIndex = 0; depthIndex < 2; ++depthIndex)
	{
		const float depth = depthIndex == 0 ? startDepth : endDepth;
		for (int yIndex = 0; yIndex < 2; ++yIndex)
		{
			for (int xIndex = 0; xIndex < 2; ++xIndex)
			{
				const float ndcX = xIndex == 0 ? -1.0f : 1.0f;
				const float ndcY = yIndex == 0 ? -1.0f : 1.0f;
				XMVECTOR viewPoint = XMVector3TransformCoord(
					XMVectorSet(ndcX, ndcY, 1.0f, 1.0f),
					inverseProjection);
				const float viewZ = XMVectorGetZ(viewPoint);
				if (viewZ <= 0.000001f)
				{
					return false;
				}
				viewPoint = XMVectorScale(viewPoint, depth / viewZ);
				XMStoreFloat3(
					&corners[cornerIndex++],
					XMVector3TransformCoord(viewPoint, inverseView));
			}
		}
	}

	XMVECTOR cascadeCenter = XMVectorZero();
	for (const XMFLOAT3& corner : corners)
	{
		cascadeCenter = XMVectorAdd(cascadeCenter, XMLoadFloat3(&corner));
	}
	cascadeCenter = XMVectorScale(cascadeCenter, 1.0f / 8.0f);

	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	if (fabsf(XMVectorGetX(XMVector3Dot(lightDir, up))) > 0.99f)
	{
		up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
	}

	const float lightDistance = 200.0f;
	XMMATRIX lightView = XMMatrixLookAtLH(
		XMVectorSubtract(cascadeCenter, XMVectorScale(lightDir, lightDistance)),
		cascadeCenter,
		up);

	const float maxFloat = (std::numeric_limits<float>::max)();
	float minX = maxFloat;
	float minY = maxFloat;
	float minZ = maxFloat;
	float maxX = -maxFloat;
	float maxY = -maxFloat;
	float maxZ = -maxFloat;
	for (const XMFLOAT3& corner : corners)
	{
		const XMVECTOR lightSpace = XMVector3TransformCoord(
			XMLoadFloat3(&corner),
			lightView);
		minX = (std::min)(minX, XMVectorGetX(lightSpace));
		minY = (std::min)(minY, XMVectorGetY(lightSpace));
		minZ = (std::min)(minZ, XMVectorGetZ(lightSpace));
		maxX = (std::max)(maxX, XMVectorGetX(lightSpace));
		maxY = (std::max)(maxY, XMVectorGetY(lightSpace));
		maxZ = (std::max)(maxZ, XMVectorGetZ(lightSpace));
	}

	// 物体の高さによる影のずれを含める余白。投影範囲を広げすぎない。
	const float casterPadding = SUN_SHADOW_CASTER_PADDING;
	const float halfExtent = 0.5f *
		(std::max)(maxX - minX, maxY - minY) +
		projectionPadding;
	if (!(halfExtent > 0.0f) || !std::isfinite(halfExtent))
	{
		return false;
	}

	const XMVECTOR cascadeCenterLightSpace =
		XMVector3TransformCoord(cascadeCenter, lightView);
	const XMVECTOR centerLightSpace = XMVectorSet(
		(minX + maxX) * 0.5f,
		(minY + maxY) * 0.5f,
		XMVectorGetZ(cascadeCenterLightSpace),
		1.0f);
	XMVECTOR inverseLightDeterminant = {};
	const XMMATRIX inverseLightView =
		XMMatrixInverse(&inverseLightDeterminant, lightView);
	const float texelSize = (halfExtent * 2.0f) / SUN_SHADOW_MAP_SIZE;
	const XMVECTOR snappedLightSpace = XMVectorSet(
		floorf(XMVectorGetX(centerLightSpace) / texelSize) * texelSize,
		floorf(XMVectorGetY(centerLightSpace) / texelSize) * texelSize,
		XMVectorGetZ(centerLightSpace),
		1.0f);
	const XMVECTOR focus = XMVector3TransformCoord(
		snappedLightSpace,
		inverseLightView);
	lightView = XMMatrixLookAtLH(
		XMVectorSubtract(focus, XMVectorScale(lightDir, lightDistance)),
		focus,
		up);

	minZ = maxFloat;
	maxZ = -maxFloat;
	for (const XMFLOAT3& corner : corners)
	{
		const XMVECTOR lightSpace = XMVector3TransformCoord(
			XMLoadFloat3(&corner),
			lightView);
		minZ = (std::min)(minZ, XMVectorGetZ(lightSpace));
		maxZ = (std::max)(maxZ, XMVectorGetZ(lightSpace));
	}
	const float nearPlane = (std::max)(0.1f, minZ - casterPadding);
	const float farPlane = (std::max)(nearPlane + 1.0f, maxZ + casterPadding);
	const XMMATRIX lightProjection = XMMatrixOrthographicLH(
		halfExtent * 2.0f,
		halfExtent * 2.0f,
		nearPlane,
		farPlane);

	XMStoreFloat3(outFocus, focus);
	*outView = lightView;
	*outProjection = lightProjection;
	*outRadius = casterPadding;
	for (const XMFLOAT3& corner : corners)
	{
		const XMFLOAT3 focusFloat = *outFocus;
		*outRadius = (std::max)(
			*outRadius,
			(std::max)(
				(std::max)(
					fabsf(corner.x - focusFloat.x),
					fabsf(corner.y - focusFloat.y)),
				fabsf(corner.z - focusFloat.z)));
	}
	*outRadius += casterPadding;
	*outTexelSize = 1.0f / SUN_SHADOW_MAP_SIZE;
	return true;
}

bool Sunlight_BeginLocalShadow(
	XMMATRIX outView[NUM_SHADOW_CASCADES],
	XMMATRIX outProjection[NUM_SHADOW_CASCADES],
	XMFLOAT3 outFocus[NUM_SHADOW_CASCADES],
	float outRadius[NUM_SHADOW_CASCADES])
{
	Camera* camera = GetCamera();
	if (!camera || !outView || !outProjection || !outFocus || !outRadius ||
		g_ShadowCascadeDistances[NUM_SHADOW_CASCADES - 1] <= camera->GetNear())
	{
		return false;
	}

	const float azimuthRad = XMConvertToRadians(g_Azimuth);
	const float elevationRad = XMConvertToRadians(g_Elevation);
	const float cosEl = cosf(elevationRad);
	const float sinEl = sinf(elevationRad);
	XMFLOAT3 sunVec = {
		cosEl * sinf(azimuthRad),
		sinEl,
		cosEl * cosf(azimuthRad)
	};
	XMVECTOR lightDir = XMVector3Normalize(XMVectorSet(
		-sunVec.x, -sunVec.y, -sunVec.z, 0.0f));
	if (XMVectorGetX(XMVector3LengthSq(lightDir)) < 0.0001f)
	{
		return false;
	}

	XMMATRIX lightViewProjection[NUM_SHADOW_CASCADES] = {};
	XMFLOAT4 cascadeSplits = {};
	XMFLOAT4 cascadeTexelSize = {};
	float cascadeTexelSizeValues[NUM_SHADOW_CASCADES] = {};
	float startDepth = (std::max)(camera->GetNear(), 0.1f);
	for (int i = 0; i < NUM_SHADOW_CASCADES; ++i)
	{
		const float endDepth = g_ShadowCascadeDistances[i];
		if (!BuildShadowCascade(
			camera,
			lightDir,
			startDepth,
			endDepth,
			i == 0
				? SUN_SHADOW_NEAR_PROJECTION_PADDING
				: SUN_SHADOW_PROJECTION_PADDING,
			&outView[i],
			&outProjection[i],
			&outFocus[i],
			&outRadius[i],
			&cascadeTexelSizeValues[i]))
		{
			return false;
		}
		lightViewProjection[i] = outView[i] * outProjection[i];
		if (i == 0) cascadeSplits.x = endDepth;
		if (i == 1) cascadeSplits.y = endDepth;
		if (i == 2) cascadeSplits.z = endDepth;
		startDepth = endDepth;
	}
	cascadeTexelSize = XMFLOAT4(
		cascadeTexelSizeValues[0],
		cascadeTexelSizeValues[1],
		cascadeTexelSizeValues[2],
		0.0f);

	SetShadowCascadeMatrices(
		lightViewProjection,
		XMFLOAT4(g_ShadowBias, g_ShadowBrightness, 0.0f, 0.0f),
		cascadeSplits,
		cascadeTexelSize);
	SetCullState(CULLSTATE_FRONT);
	BeginShadowMapSlice(0);
	return true;
}

void Sunlight_EndLocalShadow(void)
{
	SetCullState(CULLSTATE_NONE);
	EndShadowMap();
}

void Sunlight_DrawDebug(void)
{
#if defined(_DEBUG)
	if (Direct3D_IsTakingScreenshot())
	{
		return;
	}

	ImGui::Begin("Expo Sunlight");
	bool changed = false;
	changed |= ImGui::SliderFloat("Azimuth", &g_Azimuth, -180.0f, 180.0f, "%.1f");
	changed |= ImGui::SliderFloat("Elevation", &g_Elevation, -10.0f, 90.0f, "%.1f");
	changed |= ImGui::ColorEdit3("Color", &g_Color.x);
	changed |= ImGui::SliderFloat("Intensity", &g_Intensity, 0.0f, 20.0f, "%.2f");
	changed |= ImGui::SliderFloat("Ambient Scale", &g_AmbientScale, 0.0f, 4.0f, "%.3f");
	changed |= ImGui::SliderFloat("Sky Yaw Offset", &g_SkyYawOffset, -180.0f, 180.0f, "%.1f");
	changed |= ImGui::SliderFloat("Roughness", &g_Roughness, 0.04f, 1.0f, "%.2f");
	changed |= ImGui::SliderFloat("Metallic", &g_Metallic, 0.0f, 1.0f, "%.2f");
	changed |= ImGui::SliderFloat(
		"Shadow Cascade 1 End",
		&g_ShadowCascadeDistances[0],
		2.0f,
		40.0f,
		"%.1f m");
	changed |= ImGui::SliderFloat(
		"Shadow Cascade 2 End",
		&g_ShadowCascadeDistances[1],
		10.0f,
		240.0f,
		"%.1f m");
	changed |= ImGui::SliderFloat(
		"Shadow Cascade 3 End (Draw Distance)",
		&g_ShadowRadius,
		16.0f,
		320.0f,
		"%.1f m");
	if (g_ShadowCascadeDistances[0] >= g_ShadowCascadeDistances[1])
	{
		g_ShadowCascadeDistances[0] =
			(std::max)(2.0f, g_ShadowCascadeDistances[1] - 0.1f);
		changed = true;
	}
	if (g_ShadowCascadeDistances[1] >= g_ShadowRadius)
	{
		g_ShadowCascadeDistances[1] =
			(std::max)(10.0f, g_ShadowRadius - 0.1f);
		changed = true;
	}
	g_ShadowCascadeDistances[2] = g_ShadowRadius;
	changed |= ImGui::SliderFloat("Shadow Bias", &g_ShadowBias, 0.0005f, 0.02f, "%.4f");
	changed |= ImGui::SliderFloat("Shadow Brightness", &g_ShadowBrightness, 0.0f, 1.0f, "%.2f");
	ImGui::Text(
		"HDR Sun: %s",
		g_HasExtractedSunlight ? "extracted" : "fallback");
	if (g_HasExtractedSunlight)
	{
		ImGui::Text(
			"HDR UV: %.4f, %.4f  Threshold: %.4f",
			g_ExtractedSunlight.u,
			g_ExtractedSunlight.v,
			g_ExtractedSunlight.threshold);
		ImGui::Text(
			"HDR pixels: %zu",
			g_ExtractedSunlight.pixelCount);
	}
	if (ImGui::Button("Reset"))
	{
		g_Azimuth = SUN_AZIMUTH_DEFAULT;
		g_Elevation = g_ExtractedSunlight.elevation;
		g_Color = g_ExtractedSunlight.color;
		g_Intensity = SUN_DEFAULT_INTENSITY;
		g_AmbientScale = SUN_AMBIENT_SCALE_DEFAULT;
		g_SkyYawOffset = SUN_SKY_YAW_OFFSET_DEFAULT;
		g_Roughness = SUN_ROUGHNESS_DEFAULT;
		g_Metallic = SUN_METALLIC_DEFAULT;
		g_ShadowRadius = SUN_SHADOW_RADIUS_DEFAULT;
		g_ShadowCascadeDistances[0] = SUN_SHADOW_CASCADE_1_DEFAULT;
		g_ShadowCascadeDistances[1] = SUN_SHADOW_CASCADE_2_DEFAULT;
		g_ShadowCascadeDistances[2] = SUN_SHADOW_RADIUS_DEFAULT;
		g_ShadowBias = SUN_SHADOW_BIAS_DEFAULT;
		g_ShadowBrightness = SUN_SHADOW_BRIGHTNESS_DEFAULT;
		changed = true;
	}
	ImGui::End();

	if (changed)
	{
		RequestRedraw();
	}
#endif
}
