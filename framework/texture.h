#pragma once
#include <d3d11.h>
#include <cstddef>
#include <string>
#include <vector>
#include "renderer.h"
using namespace DirectX;
#include "debug_ostream.h"
#include "scene.h"

struct HDRImageData
{
	size_t width = 0;
	size_t height = 0;
	std::vector<float> rgba;
};

ID3D11ShaderResourceView* LoadTexture(const wchar_t* texpass);
ID3D11ShaderResourceView* LoadTexture(const std::wstring& texpass);
ID3D11ShaderResourceView* LoadTextureLinear(const wchar_t* texpass);
ID3D11ShaderResourceView* LoadTextureLinear(const std::wstring& texpass);

bool LoadHDRTextureForSky(
	const wchar_t* texturePath,
	HDRImageData* outImage,
	ID3D11ShaderResourceView** outTexture);

void ReleaseTexturesForScene(SCENE scene);
void ReleaseAllTextures();
void UpdateTextureCache();
