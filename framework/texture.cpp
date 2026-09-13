#include "texture.h"
#include <Windows.h>
#include <cstdio>
#include <vector>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <limits>

// シーンごとのテクスチャキャッシュ
static std::unordered_map<std::wstring, ID3D11ShaderResourceView*> g_TextureCache[SCENE_MAX];
static std::unordered_map<std::wstring, ID3D11ShaderResourceView*> g_TextureCacheLinear[SCENE_MAX];

// 共通テクスチャキャッシュ（シーン切り替え時に破棄されない）
static std::unordered_map<std::wstring, ID3D11ShaderResourceView*> g_CommonTextureCache;
static std::unordered_map<std::wstring, ID3D11ShaderResourceView*> g_CommonTextureCacheLinear;

namespace
{
bool IsCommonTexture(const std::wstring& path)
{
	// "fade.png" などシーンをまたいで永続利用されるテクスチャを共通キャッシュにする
	return path.find(L"fade.png") != std::wstring::npos;
}

std::string ToString(const std::wstring& wstr)
{
	std::string str;
	for (wchar_t c : wstr) str += static_cast<char>(c);
	return str;
}

ID3D11ShaderResourceView* LoadTextureWithFlags(const wchar_t* texpass, WIC_FLAGS flags)
{
	TexMetadata metadata;
	ScratchImage image;
	ID3D11ShaderResourceView* g_Texture = nullptr;

	// flagsで「色テクスチャとして読むか」「データテクスチャとして読むか」を切り替える。
	HRESULT hr = LoadFromWICFile(texpass, flags, &metadata, image);
	if (FAILED(hr))
	{
		return nullptr;
	}

	// 標準的に SRV を作成（戻り値をチェック）
	hr = CreateShaderResourceView(
		GetDevice(),
		image.GetImages(),
		image.GetImageCount(),
		metadata,
		&g_Texture
	);

	if (FAILED(hr) || g_Texture == nullptr)
	{
		// 失敗時は NULL を返す（呼び出し側でフォールバック処理を行う）
		return nullptr;
	}

	return g_Texture;
}

static float ClampUnit(float value)
{
	if (!std::isfinite(value) || value <= 0.0f)
	{
		return 0.0f;
	}
	return value >= 1.0f ? 1.0f : value;
}

static unsigned char EncodeHdrChannel(float value, float exposure)
{
	// HDRの線形値を簡易Reinhardトーンマップしてから表示用sRGBへ変換する。
	const float positive = std::isfinite(value) ? (std::max)(value, 0.0f) : 0.0f;
	const float mapped = (positive * exposure) / (1.0f + positive * exposure);
	const float srgb = powf(ClampUnit(mapped), 1.0f / 2.2f);
	return static_cast<unsigned char>(srgb * 255.0f + 0.5f);
}
}

ID3D11ShaderResourceView* LoadTexture(const wchar_t* texpass)
{
	std::wstring path = texpass;
	std::string pathStr = ToString(path);

	if (IsCommonTexture(path))
	{
		auto it = g_CommonTextureCache.find(path);
		if (it != g_CommonTextureCache.end())
		{
			hal::dout << "[Texture Cache] COMMON HIT: " << pathStr << std::endl;
			return it->second;
		}
		hal::dout << "[Texture Cache] COMMON MISS (Load): " << pathStr << std::endl;
		ID3D11ShaderResourceView* srv = LoadTextureWithFlags(texpass, WIC_FLAGS_FORCE_SRGB);
		if (srv)
		{
			g_CommonTextureCache[path] = srv;
		}
		return srv;
	}

	SCENE currentScene = GetScene();
	if (currentScene < 0 || currentScene >= SCENE_MAX)
	{
		hal::dout << "[Texture Cache] BYPASS (Invalid Scene, Load): " << pathStr << std::endl;
		return LoadTextureWithFlags(texpass, WIC_FLAGS_FORCE_SRGB);
	}

	auto it = g_TextureCache[currentScene].find(path);
	if (it != g_TextureCache[currentScene].end())
	{
		hal::dout << "[Texture Cache] SCENE " << currentScene << " HIT: " << pathStr << std::endl;
		return it->second;
	}

	hal::dout << "[Texture Cache] SCENE " << currentScene << " MISS (Load): " << pathStr << std::endl;
	ID3D11ShaderResourceView* srv = LoadTextureWithFlags(texpass, WIC_FLAGS_FORCE_SRGB);
	if (srv)
	{
		g_TextureCache[currentScene][path] = srv;
	}
	return srv;
}

ID3D11ShaderResourceView* LoadTexture(const std::wstring& texpass)
{
	return LoadTexture(texpass.c_str());
}

ID3D11ShaderResourceView* LoadTextureLinear(const wchar_t* texpass)
{
	std::wstring path = texpass;
	std::string pathStr = ToString(path);

	if (IsCommonTexture(path))
	{
		auto it = g_CommonTextureCacheLinear.find(path);
		if (it != g_CommonTextureCacheLinear.end())
		{
			hal::dout << "[Texture Cache] COMMON LINEAR HIT: " << pathStr << std::endl;
			return it->second;
		}
		hal::dout << "[Texture Cache] COMMON LINEAR MISS (Load): " << pathStr << std::endl;
		ID3D11ShaderResourceView* srv = LoadTextureWithFlags(texpass, WIC_FLAGS_IGNORE_SRGB);
		if (srv)
		{
			g_CommonTextureCacheLinear[path] = srv;
		}
		return srv;
	}

	SCENE currentScene = GetScene();
	if (currentScene < 0 || currentScene >= SCENE_MAX)
	{
		hal::dout << "[Texture Cache] BYPASS LINEAR (Invalid Scene, Load): " << pathStr << std::endl;
		return LoadTextureWithFlags(texpass, WIC_FLAGS_IGNORE_SRGB);
	}

	auto it = g_TextureCacheLinear[currentScene].find(path);
	if (it != g_TextureCacheLinear[currentScene].end())
	{
		hal::dout << "[Texture Cache] SCENE " << currentScene << " LINEAR HIT: " << pathStr << std::endl;
		return it->second;
	}

	hal::dout << "[Texture Cache] SCENE " << currentScene << " LINEAR MISS (Load): " << pathStr << std::endl;
	ID3D11ShaderResourceView* srv = LoadTextureWithFlags(texpass, WIC_FLAGS_IGNORE_SRGB);
	if (srv)
	{
		g_TextureCacheLinear[currentScene][path] = srv;
	}
	return srv;
}

ID3D11ShaderResourceView* LoadTextureLinear(const std::wstring& texpass)
{
	return LoadTextureLinear(texpass.c_str());
}

bool LoadHDRTextureForSky(
	const wchar_t* texturePath,
	HDRImageData* outImage,
	ID3D11ShaderResourceView** outTexture)
{
	if (!texturePath || !outImage || !outTexture)
	{
		return false;
	}

	outImage->width = 0;
	outImage->height = 0;
	outImage->rgba.clear();
	*outTexture = nullptr;

	TexMetadata hdrMetadata = {};
	ScratchImage hdrImage;
	if (FAILED(LoadFromHDRFile(texturePath, &hdrMetadata, hdrImage)))
	{
		return false;
	}

	const Image* sourceImage = hdrImage.GetImage(0, 0, 0);
	if (!sourceImage || sourceImage->width == 0 || sourceImage->height == 0)
	{
		return false;
	}

	// DirectXTexのHDRローダは通常R32G32B32A32_FLOATを返すが、
	// フォーマットが異なる実装でも解析経路を壊さないよう統一する。
	ScratchImage convertedImage;
	if (sourceImage->format != DXGI_FORMAT_R32G32B32A32_FLOAT)
	{
		if (FAILED(Convert(
			*sourceImage,
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			TEX_FILTER_DEFAULT,
			0.5f,
			convertedImage)))
		{
			return false;
		}
		sourceImage = convertedImage.GetImage(0, 0, 0);
		if (!sourceImage)
		{
			return false;
		}
	}

	if (sourceImage->width > (std::numeric_limits<size_t>::max)() / sourceImage->height)
	{
		return false;
	}
	const size_t pixelCount = sourceImage->width * sourceImage->height;
	if (pixelCount > (std::numeric_limits<size_t>::max)() / 4)
	{
		return false;
	}

	outImage->width = sourceImage->width;
	outImage->height = sourceImage->height;
	outImage->rgba.resize(pixelCount * 4);

	double logLuminanceSum = 0.0;
	size_t skySampleCount = 0;
	float maxLuminance = 0.0f;
	for (size_t y = 0; y < sourceImage->height; ++y)
	{
		const unsigned char* sourceRow =
			sourceImage->pixels + y * sourceImage->rowPitch;
		const float* sourcePixels = reinterpret_cast<const float*>(sourceRow);
		float* destinationPixels = outImage->rgba.data() + y * sourceImage->width * 4;
		for (size_t x = 0; x < sourceImage->width; ++x)
		{
			const float red = sourcePixels[x * 4 + 0];
			const float green = sourcePixels[x * 4 + 1];
			const float blue = sourcePixels[x * 4 + 2];
			destinationPixels[x * 4 + 0] = red;
			destinationPixels[x * 4 + 1] = green;
			destinationPixels[x * 4 + 2] = blue;
			destinationPixels[x * 4 + 3] = sourcePixels[x * 4 + 3];
			const float luminance = 0.2126f * red + 0.7152f * green + 0.0722f * blue;
			if (std::isfinite(luminance) && luminance > maxLuminance)
			{
				maxLuminance = luminance;
			}
		}
	}

	const float skyThreshold = maxLuminance * 0.5f;
	for (size_t i = 0; i < pixelCount; ++i)
	{
		const float red = outImage->rgba[i * 4 + 0];
		const float green = outImage->rgba[i * 4 + 1];
		const float blue = outImage->rgba[i * 4 + 2];
		const float luminance = 0.2126f * red + 0.7152f * green + 0.0722f * blue;
		if (!std::isfinite(luminance) || luminance <= 0.0f || luminance >= skyThreshold)
		{
			continue;
		}
		logLuminanceSum += log(static_cast<double>(luminance) + 1.0e-4);
		++skySampleCount;
	}

	// 太陽を除いた空の対数平均がやや低めの中間調になるよう露出を決める。
	const float targetLdr = 0.40f;
	float exposure = 1.0f;
	if (skySampleCount > 0)
	{
		const float logAverage = static_cast<float>(
			exp(logLuminanceSum / static_cast<double>(skySampleCount)));
		if (logAverage > 1.0e-4f)
		{
			exposure = targetLdr / (logAverage * (1.0f - targetLdr));
		}
	}
	if (!std::isfinite(exposure) || exposure <= 0.0f)
	{
		exposure = 1.0f;
	}
	exposure = (std::max)(0.20f, (std::min)(exposure, 4.0f));

	ScratchImage displayImage;
	if (FAILED(displayImage.Initialize2D(
		DXGI_FORMAT_R8G8B8A8_UNORM,
		sourceImage->width,
		sourceImage->height,
		1,
		1)))
	{
		outImage->width = 0;
		outImage->height = 0;
		outImage->rgba.clear();
		return false;
	}

	const Image* displayImageData = displayImage.GetImage(0, 0, 0);
	if (!displayImageData)
	{
		outImage->width = 0;
		outImage->height = 0;
		outImage->rgba.clear();
		return false;
	}

	for (size_t y = 0; y < sourceImage->height; ++y)
	{
		const float* sourcePixels = outImage->rgba.data() + y * sourceImage->width * 4;
		unsigned char* destinationPixels =
			displayImageData->pixels + y * displayImageData->rowPitch;
		for (size_t x = 0; x < sourceImage->width; ++x)
		{
			destinationPixels[x * 4 + 0] = EncodeHdrChannel(sourcePixels[x * 4 + 0], exposure);
			destinationPixels[x * 4 + 1] = EncodeHdrChannel(sourcePixels[x * 4 + 1], exposure);
			destinationPixels[x * 4 + 2] = EncodeHdrChannel(sourcePixels[x * 4 + 2], exposure);
			destinationPixels[x * 4 + 3] = 255;
		}
	}

	ID3D11ShaderResourceView* texture = nullptr;
	if (FAILED(CreateShaderResourceView(
		GetDevice(),
		displayImage.GetImages(),
		displayImage.GetImageCount(),
		displayImage.GetMetadata(),
		&texture)) || !texture)
	{
		outImage->width = 0;
		outImage->height = 0;
		outImage->rgba.clear();
		return false;
	}

	// HDR表示用テクスチャはシーン切り替えでは破棄しない共通キャッシュで管理する。
	const std::wstring path(texturePath);
	auto cached = g_CommonTextureCache.find(path);
	if (cached != g_CommonTextureCache.end())
	{
		texture->Release();
		texture = cached->second;
	}
	else
	{
		g_CommonTextureCache[path] = texture;
	}

	*outTexture = texture;
	return true;
}

void ReleaseTexturesForScene(SCENE scene)
{
	if (scene < 0 || scene >= SCENE_MAX) return;

	hal::dout << "[Texture Cache] --- Releasing Scene " << scene << " Textures ---" << std::endl;

	for (auto& pair : g_TextureCache[scene])
	{
		if (pair.second)
		{
			hal::dout << "  Released: " << ToString(pair.first) << std::endl;
			pair.second->Release();
		}
	}
	g_TextureCache[scene].clear();

	for (auto& pair : g_TextureCacheLinear[scene])
	{
		if (pair.second)
		{
			hal::dout << "  Released (Linear): " << ToString(pair.first) << std::endl;
			pair.second->Release();
		}
	}
	g_TextureCacheLinear[scene].clear();
}

void ReleaseAllTextures()
{
	hal::dout << "[Texture Cache] --- Releasing All Textures (Shutdown) ---" << std::endl;

	for (int i = 0; i < SCENE_MAX; ++i)
	{
		ReleaseTexturesForScene(static_cast<SCENE>(i));
	}

	// 共通キャッシュの解放
	for (auto& pair : g_CommonTextureCache)
	{
		if (pair.second)
		{
			hal::dout << "  Released (Common): " << ToString(pair.first) << std::endl;
			pair.second->Release();
		}
	}
	g_CommonTextureCache.clear();

	for (auto& pair : g_CommonTextureCacheLinear)
	{
		if (pair.second)
		{
			hal::dout << "  Released (Common Linear): " << ToString(pair.first) << std::endl;
			pair.second->Release();
		}
	}
	g_CommonTextureCacheLinear.clear();
}

void UpdateTextureCache()
{
	SCENE currentScene = GetScene();
	static SCENE prevScene = SCENE_NONE;
	if (currentScene != prevScene)
	{
		if (prevScene >= 0 && prevScene < SCENE_MAX)
		{
			ReleaseTexturesForScene(prevScene);
		}
		prevScene = currentScene;
	}
}
