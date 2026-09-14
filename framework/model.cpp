#define NOMINMAX

#include "model.h"
#include "glb_model.h"
#include "texture.h"
#include "anim_sprite3d.h"

#include "camera.h"
#include "debug_ostream.h"
#include "shadermanager.h"
#include <DirectXMath.h>
#include <assert.h>
#include <iostream>
#include <float.h>
#include <algorithm>
#include <map>
#include <io.h>
#include <cstddef>
#include <cstring> // memcpy
#include <cctype>
#include <windows.h> // GetFileAttributesA
using namespace DirectX;

static std::unordered_map<std::string, MODEL*> g_ModelCache;
static ID3D11VertexShader* g_SkinningVertexShader = nullptr;
static ID3D11InputLayout* g_SkinningVertexLayout = nullptr;
static ID3D11Buffer* g_SkinningBoneBuffer = nullptr;

static bool IsEmbeddedTextureRef(const std::string& path)
{
	return !path.empty() && path[0] == '*';
}

static std::string GetDirectoryName(const char* path)
{
	std::string value = path ? path : "";
	size_t slashPos = value.find_last_of("/\\");
	return (slashPos == std::string::npos) ? "" : value.substr(0, slashPos + 1);
}

static std::wstring ToWideString(const std::string& value)
{
	return std::wstring(value.begin(), value.end());
}

static std::string ResolveTexturePath(const char* modelPath, const std::string& texturePath)
{
	if (texturePath.empty() || IsEmbeddedTextureRef(texturePath))
	{
		return texturePath;
	}
	if (texturePath.size() > 1 && texturePath[1] == ':')
	{
		return texturePath;
	}
	if (GetFileAttributesA(texturePath.c_str()) != INVALID_FILE_ATTRIBUTES)
	{
		return texturePath;
	}

	std::string relativePath = GetDirectoryName(modelPath) + texturePath;
	if (GetFileAttributesA(relativePath.c_str()) != INVALID_FILE_ATTRIBUTES)
	{
		return relativePath;
	}

	return texturePath;
}

static ID3D11ShaderResourceView* CreateSolidTexture(unsigned int pixel)
{
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = 1;
	desc.Height = 1;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA data = {};
	data.pSysMem = &pixel;
	data.SysMemPitch = sizeof(pixel);

	ID3D11Texture2D* texture = nullptr;
	if (FAILED(GetDevice()->CreateTexture2D(&desc, &data, &texture)))
	{
		return nullptr;
	}

	ID3D11ShaderResourceView* srv = nullptr;
	if (FAILED(GetDevice()->CreateShaderResourceView(texture, nullptr, &srv)))
	{
		texture->Release();
		return nullptr;
	}

	texture->Release();
	return srv;
}

static ID3D11ShaderResourceView* CreateFlatNormalTexture()
{
	return CreateSolidTexture(0xFFFF8080); // RGBA = (0.5, 0.5, 1.0, 1.0)
}

static ID3D11ShaderResourceView* FindCachedTexture(MODEL* model, const std::string& texturePath)
{
	auto it = model->Texture.find(texturePath);
	if (it != model->Texture.end())
	{
		return it->second;
	}

	if (IsEmbeddedTextureRef(texturePath))
	{
		int texIdx = atoi(texturePath.c_str() + 1);
		if (texIdx >= 0 && (unsigned int)texIdx < model->AiScene->mNumTextures)
		{
			const aiTexture* pTex = model->AiScene->mTextures[texIdx];
			if (pTex && pTex->mFilename.length > 0)
			{
				it = model->Texture.find(pTex->mFilename.data);
				if (it != model->Texture.end())
				{
					return it->second;
				}
			}
		}
	}

	return nullptr;
}

static ID3D11ShaderResourceView* LoadNormalTexture(MODEL* model, const char* modelPath, const std::string& texturePath)
{
	ID3D11ShaderResourceView* cached = FindCachedTexture(model, texturePath);
	if (cached)
	{
		return cached;
	}
	if (IsEmbeddedTextureRef(texturePath))
	{
		return nullptr;
	}

	std::string resolvedPath = ResolveTexturePath(modelPath, texturePath);
	ID3D11ShaderResourceView* texture = LoadTextureLinear(ToWideString(resolvedPath).c_str());
	if (texture)
	{
		model->Texture[texturePath] = texture;
	}
	return texture;
}

static bool UsesKnightPbrTextures(const char* modelPath)
{
	std::string path = modelPath ? modelPath : "";
	std::replace(path.begin(), path.end(), '\\', '/');
	std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return path.find("knight_02.fbx") != std::string::npos
		|| path.find("knight_run.fbx") != std::string::npos;
}

static bool IsFieldAllNormalModel(const char* modelPath)
{
	std::string path = modelPath ? modelPath : "";
	std::replace(path.begin(), path.end(), '\\', '/');
	std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return path.find("field_allnormal.fbx") != std::string::npos;
}

static ID3D11ShaderResourceView* LoadCachedLinearTexture(MODEL* model, const std::string& cacheKey, const wchar_t* filePath)
{
	auto it = model->Texture.find(cacheKey);
	if (it != model->Texture.end())
	{
		return it->second;
	}

	ID3D11ShaderResourceView* texture = LoadTextureLinear(filePath);
	if (texture)
	{
		model->Texture[cacheKey] = texture;
	}
	return texture;
}

static bool HasMaterialNamePart(const MODEL* model, unsigned int meshIndex, const char* namePart)
{
	if (!model || !model->AiScene || meshIndex >= model->AiScene->mNumMeshes) return false;

	const aiMesh* mesh = model->AiScene->mMeshes[meshIndex];
	if (!mesh || mesh->mMaterialIndex >= model->AiScene->mNumMaterials) return false;

	aiString materialName;
	if (AI_SUCCESS != model->AiScene->mMaterials[mesh->mMaterialIndex]->Get(AI_MATKEY_NAME, materialName))
	{
		return false;
	}

	return std::string(materialName.C_Str()).find(namePart) != std::string::npos;
}

static XMMATRIX QuatToMatrixForNodeAnimation(const XMFLOAT4& q)
{
	return XMMatrixRotationQuaternion(XMLoadFloat4(&q));
}

static std::string GetAssimpFbxBaseNameForNodeAnimation(const std::string& nodeName)
{
	static const char* suffixes[] = {
		"_$AssimpFbx$_Translation",
		"_$AssimpFbx$_Rotation",
		"_$AssimpFbx$_Scaling",
		"_$AssimpFbx$_PreRotation",
		"_$AssimpFbx$_PostRotation",
		"_$AssimpFbx$_RotationPivot",
		"_$AssimpFbx$_RotationPivotInverse",
		"_$AssimpFbx$_ScalingPivot",
		"_$AssimpFbx$_ScalingPivotInverse",
		"_$AssimpFbx$_RotationOffset",
		"_$AssimpFbx$_ScalingOffset",
		"_$AssimpFbx$_GeometricTranslation",
		"_$AssimpFbx$_GeometricRotation",
		"_$AssimpFbx$_GeometricScaling",
	};

	for (const char* suffix : suffixes)
	{
		size_t suffixLen = strlen(suffix);
		if (nodeName.size() > suffixLen &&
			nodeName.compare(nodeName.size() - suffixLen, suffixLen, suffix) == 0)
		{
			return nodeName.substr(0, nodeName.size() - suffixLen);
		}
	}

	return "";
}

static void RenderAnimatedNodeRecursive(
	MODEL* model,
	aiNode* node,
	XMMATRIX parentTransform,
	const AnimationClip* clip,
	double time,
	const XMFLOAT4& color,
	bool useColorReplace,
	XMMATRIX worldTransform)
{
	std::string nodeName = node->mName.data;
	XMMATRIX nodeTransform = AiMatrixToXMMatrix(node->mTransformation);

	std::string baseName = GetAssimpFbxBaseNameForNodeAnimation(nodeName);
	if (!baseName.empty() && model->NodeToAnimIndex.find(baseName) != model->NodeToAnimIndex.end())
	{
		nodeTransform = XMMatrixIdentity();
	}

	if (clip)
	{
		auto it = model->NodeToAnimIndex.find(nodeName);
		if (it != model->NodeToAnimIndex.end())
		{
			int trackIdx = it->second;
			if (trackIdx >= 0 && trackIdx < (int)clip->tracks.size())
			{
				const BoneKeyframes& kf = clip->tracks[trackIdx];
				XMFLOAT3 trans = AnimSprite3D::InterpolateVec3(kf.trans, time);
				XMFLOAT4 rot = AnimSprite3D::InterpolateQuat(kf.rot, time);
				XMFLOAT3 scale = AnimSprite3D::InterpolateVec3(kf.scale, time);
				if (kf.scale.empty())
				{
					scale = XMFLOAT3(1.0f, 1.0f, 1.0f);
				}

				nodeTransform = XMMatrixScaling(scale.x, scale.y, scale.z)
					* QuatToMatrixForNodeAnimation(rot)
					* XMMatrixTranslation(trans.x, trans.y, trans.z);
			}
		}
	}

	XMMATRIX currentTransform = nodeTransform * parentTransform;

	for (unsigned int i = 0; i < node->mNumMeshes; i++)
	{
		unsigned int meshIndex = node->mMeshes[i];

		XMFLOAT4 finalColor;
		if (useColorReplace)
		{
			finalColor = color;
		}
		else if (meshIndex < model->AiScene->mNumMeshes && model->MeshMaterials)
		{
			XMFLOAT4 meshColor = model->MeshMaterials[meshIndex].diffuseColor;
			finalColor = XMFLOAT4(
				meshColor.x * color.x,
				meshColor.y * color.y,
				meshColor.z * color.z,
				meshColor.w * color.w
			);
		}
		else
		{
			finalColor = color;
		}

		MATERIAL material = {};
		material.Diffuse = finalColor;
		material.Ambient = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		material.Specular = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		material.Emission = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		material.Shininess = 50.0f;
		SetMaterial(material);

		ID3D11ShaderResourceView* textureToSet = model->MeshMaterials[meshIndex].textureView;
		GetDeviceContext()->PSSetShaderResources(0, 1, &textureToSet);
		ID3D11ShaderResourceView* normalTextureToSet = model->MeshMaterials[meshIndex].normalTextureView;
		GetDeviceContext()->PSSetShaderResources(2, 1, &normalTextureToSet);
		// PBR/Emissive 用の追加テクスチャスロット。
		ID3D11ShaderResourceView* metallicTextureToSet = model->MeshMaterials[meshIndex].metallicTextureView;
		GetDeviceContext()->PSSetShaderResources(3, 1, &metallicTextureToSet);
		ID3D11ShaderResourceView* roughnessTextureToSet = model->MeshMaterials[meshIndex].roughnessTextureView;
		GetDeviceContext()->PSSetShaderResources(4, 1, &roughnessTextureToSet);
		ID3D11ShaderResourceView* emissiveTextureToSet = model->MeshMaterials[meshIndex].emissiveTextureView;
		GetDeviceContext()->PSSetShaderResources(5, 1, &emissiveTextureToSet);

		UINT stride = sizeof(Vertex3D);
		UINT offset = 0;
		GetDeviceContext()->IASetVertexBuffers(0, 1, &model->VertexBuffer[meshIndex], &stride, &offset);
		GetDeviceContext()->IASetIndexBuffer(model->IndexBuffer[meshIndex], DXGI_FORMAT_R32_UINT, 0);

		SetWorldMatrix(currentTransform * worldTransform);

		unsigned int indexCount = model->MeshIndexCounts[meshIndex];
		if (indexCount > 0)
		{
			GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
		}
	}

	for (unsigned int i = 0; i < node->mNumChildren; i++)
	{
		RenderAnimatedNodeRecursive(model, node->mChildren[i], currentTransform, clip, time, color, useColorReplace, worldTransform);
	}
}

static bool EnsureSkinningResources()
{
	if (g_SkinningVertexShader && g_SkinningVertexLayout && g_SkinningBoneBuffer)
	{
		return true;
	}

	FILE* file = nullptr;
	long int fsize = 0;

	fopen_s(&file, "shader/shader_vertex_skinning.cso", "rb");
	if (file == nullptr)
	{
		MessageBoxA(NULL, "shader/shader_vertex_skinning.cso", "Skinning Shader File Not Found", MB_OK);
		return false;
	}

	fsize = _filelength(_fileno(file));
	if (fsize <= 0)
	{
		fclose(file);
		return false;
	}

	unsigned char* buffer = new unsigned char[fsize];
	fread(buffer, fsize, 1, file);
	fclose(file);

	ID3D11Device* device = GetDevice();
	if (!device)
	{
		delete[] buffer;
		return false;
	}

	if (!g_SkinningVertexShader)
	{
		HRESULT hr = device->CreateVertexShader(buffer, fsize, nullptr, &g_SkinningVertexShader);
		if (FAILED(hr))
		{
			delete[] buffer;
			return false;
		}
	}

	if (!g_SkinningVertexLayout)
	{
		D3D11_INPUT_ELEMENT_DESC layout[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, (UINT)offsetof(VertexSkinned, position),   D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, (UINT)offsetof(VertexSkinned, normal),     D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, (UINT)offsetof(VertexSkinned, color),      D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, (UINT)offsetof(VertexSkinned, texCoord),   D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT,  0, (UINT)offsetof(VertexSkinned, boneIndex),  D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, (UINT)offsetof(VertexSkinned, boneWeight), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		HRESULT hr = device->CreateInputLayout(layout, ARRAYSIZE(layout), buffer, fsize, &g_SkinningVertexLayout);
		if (FAILED(hr))
		{
			delete[] buffer;
			return false;
		}
	}

	delete[] buffer;

	if (!g_SkinningBoneBuffer)
	{
		D3D11_BUFFER_DESC bd;
		ZeroMemory(&bd, sizeof(bd));
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.ByteWidth = sizeof(XMMATRIX) * BoneMatrices::MAX_BONES;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		HRESULT hr = device->CreateBuffer(&bd, nullptr, &g_SkinningBoneBuffer);
		if (FAILED(hr))
		{
			return false;
		}
	}

	return true;
}

// Assimpの行列をDirectXMath形式に変換
// 両方とも行優先(row-major)だが、Assimpは列ベクトル方式(v' = M*v)で平行移動が4列目、
// DirectXMathは行ベクトル方式(v' = v*M)で平行移動が4行目なので転置が必要
XMMATRIX AiMatrixToXMMatrix(const aiMatrix4x4& mat)
{
	// aiMatrix4x4 は row-major で以下のレイアウト:
	//   a1 a2 a3 a4     (a4=tx)
	//   b1 b2 b3 b4     (b4=ty)
	//   c1 c2 c3 c4     (c4=tz)
	//   d1 d2 d3 d4     (d4=1)
	// DirectXMath の XMMATRIX は row-major で行ベクトル方式:
	//   _11 _12 _13 _14
	//   _21 _22 _23 _24
	//   _31 _32 _33 _34
	//   tx  ty  tz  1.0
	// そのまま転置してコピーする
	return XMMATRIX(
		mat.a1, mat.b1, mat.c1, mat.d1,
		mat.a2, mat.b2, mat.c2, mat.d2,
		mat.a3, mat.b3, mat.c3, mat.d3,
		mat.a4, mat.b4, mat.c4, mat.d4
	);
}

// ノードを再帰的に描画する内部関数
void RenderNode(MODEL* model, aiNode* node, XMMATRIX parentTransform, const XMFLOAT4& color, bool useColorReplace = false, ID3D11ShaderResourceView* customTexture = nullptr)
{
	// このノードのローカル変換行列と親の変換を組み合わせ
	XMMATRIX currentTransform = AiMatrixToXMMatrix(node->mTransformation) * parentTransform;

	// このノード以下のすべてのメッシュを描画
	for (unsigned int i = 0; i < node->mNumMeshes; i++)
	{
		unsigned int meshIndex = node->mMeshes[i];
		aiMesh* mesh = model->AiScene->mMeshes[meshIndex];

		// マテリアルの色を計算
		XMFLOAT4 finalColor;
		if (useColorReplace)
		{
			// 色を置き換え（マテリアル色を無視）
			finalColor = color;
		}
		else
		{
			// マテリアル色を反映
			if (meshIndex < model->AiScene->mNumMeshes && model->MeshMaterials)
			{
				XMFLOAT4 meshColor = model->MeshMaterials[meshIndex].diffuseColor;
				
				finalColor = XMFLOAT4(
					meshColor.x * color.x,
					meshColor.y * color.y,
					meshColor.z * color.z,
					meshColor.w * color.w
				);
			}
			else
			{
				finalColor = color;
			}
		}
		
		MATERIAL material = {};
		material.Diffuse = finalColor;
		material.Ambient = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		material.Specular = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		material.Emission = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		material.Shininess = 50.0f;
		SetMaterial(material);

		// ワールド行列の設定（ノード変換 + モデルワールド変換を組み合わせ）
		// RenderNode では親のModelDrawでWVPが設定されているが、ノードごとの変形を考慮
		// ただし既存の ModelDraw が WVP を設定しているため、ここでは単純化
		// 本来は RenderNodeAnimation と同様に WVP を再計算すべきだが、互換性のために最小限の変更に留める

		// テクスチャをシェーダーに設定(プリキャッシュされた値を使用。カスタムテクスチャがあればそれを使用)
		ID3D11ShaderResourceView* textureToSet = customTexture ? customTexture : model->MeshMaterials[meshIndex].textureView;
		GetDeviceContext()->PSSetShaderResources(0, 1, &textureToSet);
		ID3D11ShaderResourceView* normalTextureToSet = model->MeshMaterials[meshIndex].normalTextureView;
		GetDeviceContext()->PSSetShaderResources(2, 1, &normalTextureToSet);
		// PBR/Emissive 用の追加テクスチャスロット。
		ID3D11ShaderResourceView* metallicTextureToSet = model->MeshMaterials[meshIndex].metallicTextureView;
		GetDeviceContext()->PSSetShaderResources(3, 1, &metallicTextureToSet);
		ID3D11ShaderResourceView* roughnessTextureToSet = model->MeshMaterials[meshIndex].roughnessTextureView;
		GetDeviceContext()->PSSetShaderResources(4, 1, &roughnessTextureToSet);
		ID3D11ShaderResourceView* emissiveTextureToSet = model->MeshMaterials[meshIndex].emissiveTextureView;
		GetDeviceContext()->PSSetShaderResources(5, 1, &emissiveTextureToSet);


		// 頂点バッファ設定
		UINT stride = sizeof(Vertex3D);
		UINT offset = 0;
		GetDeviceContext()->IASetVertexBuffers(0, 1, &model->VertexBuffer[meshIndex], &stride, &offset);

		// インデックスバッファ設定
		GetDeviceContext()->IASetIndexBuffer(model->IndexBuffer[meshIndex], DXGI_FORMAT_R32_UINT, 0);

		// インデックス数チェック：0の場合はスキップ
		unsigned int indexCount = model->MeshIndexCounts[meshIndex];
		if (indexCount > 0)
		{
			// 描画(保持されているインデックス数を使用)
			GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
		}
	}

	// 子ノードを再帰実行
	for (unsigned int i = 0; i < node->mNumChildren; i++)
	{
		RenderNode(model, node->mChildren[i], currentTransform, color, useColorReplace, customTexture);
	}
}

// アニメーション対応のノード描画関数（ノード変換適用版）
void RenderNodeAnimation(MODEL* model, aiNode* node, XMMATRIX parentTransform, const BoneMatrices& boneMatrices, const XMFLOAT4& color, bool useColorReplace, XMMATRIX worldTransform, XMMATRIX viewProjection)
{
	//このノードのローカル変換行列と親の変換を組み合わせ
	XMMATRIX currentTransform = AiMatrixToXMMatrix(node->mTransformation) * parentTransform;


	// ノード名がアニメーション対象の場合、ボーン行列を適用
	if (!model->NodeToAnimIndex.empty())
	{
		auto it = model->NodeToAnimIndex.find(node->mName.data);
		if (it != model->NodeToAnimIndex.end())
		{
			int nodeAnimIndex = it->second;
			if (nodeAnimIndex >= 0 && nodeAnimIndex < (int)BoneMatrices::MAX_BONES)
			{
				// ボーン行列を適用（ノード階層変換の代わりにアニメーション行列を使用）
				currentTransform = boneMatrices.matrices[nodeAnimIndex] * parentTransform;
			}
		}
	}

	// このノード以下のすべてのメッシュを描画
	for (unsigned int i = 0; i < node->mNumMeshes; i++)
	{
		unsigned int meshIndex = node->mMeshes[i];
		aiMesh* mesh = model->AiScene->mMeshes[meshIndex];

		// マテリアルの色を計算
		XMFLOAT4 finalColor;
		if (useColorReplace)
		{
			finalColor = color;
		}
		else
		{
			if (meshIndex < model->AiScene->mNumMeshes && model->MeshMaterials)
			{
				XMFLOAT4 meshColor = model->MeshMaterials[meshIndex].diffuseColor;
				
				finalColor = XMFLOAT4(
					meshColor.x * color.x,
					meshColor.y * color.y,
					meshColor.z * color.z,
					meshColor.w * color.w
				);
			}
			else
			{
				finalColor = color;
			}
		}
		
		MATERIAL material = {};
		material.Diffuse = finalColor;
		material.Ambient = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		material.Specular = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		material.Emission = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		material.Shininess = 50.0f;
		SetMaterial(material);

		// テクスチャをシェーダーに設定
		ID3D11ShaderResourceView* textureToSet = model->MeshMaterials[meshIndex].textureView;
		GetDeviceContext()->PSSetShaderResources(0, 1, &textureToSet);
		ID3D11ShaderResourceView* normalTextureToSet = model->MeshMaterials[meshIndex].normalTextureView;
		GetDeviceContext()->PSSetShaderResources(2, 1, &normalTextureToSet);
		// PBR/Emissive 用の追加テクスチャスロット。
		ID3D11ShaderResourceView* metallicTextureToSet = model->MeshMaterials[meshIndex].metallicTextureView;
		GetDeviceContext()->PSSetShaderResources(3, 1, &metallicTextureToSet);
		ID3D11ShaderResourceView* roughnessTextureToSet = model->MeshMaterials[meshIndex].roughnessTextureView;
		GetDeviceContext()->PSSetShaderResources(4, 1, &roughnessTextureToSet);
		ID3D11ShaderResourceView* emissiveTextureToSet = model->MeshMaterials[meshIndex].emissiveTextureView;
		GetDeviceContext()->PSSetShaderResources(5, 1, &emissiveTextureToSet);

		// 頂点バッファ設定
		UINT stride = sizeof(Vertex3D);
		UINT offset = 0;
		GetDeviceContext()->IASetVertexBuffers(0, 1, &model->VertexBuffer[meshIndex], &stride, &offset);

		// インデックスバッファ設定
		GetDeviceContext()->IASetIndexBuffer(model->IndexBuffer[meshIndex], DXGI_FORMAT_R32_UINT, 0);

		// ワールド行列の設定（アニメーション変換 + モデルワールド変換を組み合わせ）
		XMMATRIX meshWorldMatrix = currentTransform * worldTransform;
		SetWorldMatrix(meshWorldMatrix);

		// インデックス数チェック：0の場合はスキップ
		unsigned int indexCount = model->MeshIndexCounts[meshIndex];
		if (indexCount > 0)
		{
			// 描画
			GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
		}
	}

	// 子ノードを再帰実行
	for (unsigned int i = 0; i < node->mNumChildren; i++)
	{
		RenderNodeAnimation(model, node->mChildren[i], currentTransform, boneMatrices, color, useColorReplace, worldTransform, viewProjection);
	}
}


MODEL* ModelLoad(const char* FileName)
{
	if (g_ModelCache.count(FileName) > 0)
	{
		g_ModelCache[FileName]->RefCount++;
		return g_ModelCache[FileName];
	}

	MODEL* model = new MODEL;
	model->FilePath = FileName;
	model->RefCount = 1;

	const std::string modelPath(FileName);

	// ファイルの存在チェックを追加
	DWORD dwAttrib = GetFileAttributesA(FileName);
	if (dwAttrib == INVALID_FILE_ATTRIBUTES || (dwAttrib & FILE_ATTRIBUTE_DIRECTORY))
	{
		std::string msg = "「" + std::string(FileName) + "」は存在しません";
		MessageBoxA(NULL, msg.c_str(), "Model Load Error", MB_OK | MB_ICONERROR);
		delete model;
		return nullptr;
	}

	// ===== モデルファイルの読み込み開始 =====

	// 拡張子を取得して小文字に変換
	std::string ext = "";
	size_t dotPos = modelPath.find_last_of('.');
	if (dotPos != std::string::npos) {
		ext = modelPath.substr(dotPos);
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
	}
	bool isGlb = (ext == ".glb" || ext == ".gltf");

	// プロパティストアを作成
	aiPropertyStore* props = aiCreatePropertyStore();

	// Assimpのフラグ
	unsigned int pFlags = aiProcessPreset_TargetRealtime_MaxQuality | 
		aiProcess_ConvertToLeftHanded |
		aiProcess_Triangulate |              // 四角形以上を三角形化
		aiProcess_GenSmoothNormals |         // スムーズ法線生成
		aiProcess_JoinIdenticalVertices;     // 重複頂点削除
		// aiProcess_OptimizeGraph は除外（アニメーション対象ノードが消える可能性がある）

	if (isGlb) {
		// GLB/GLTFの場合はスケールを100倍にする
		aiSetImportPropertyFloat(props, AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY, 100.0f);
		pFlags |= aiProcess_GlobalScale;
	}

	std::lock_guard<std::mutex> assimpLock(Glb_GetAssimpMutex());
	model->AiScene = aiImportFileExWithProperties(FileName, pFlags, nullptr, props);

	aiReleasePropertyStore(props);

	if (!model->AiScene)
	{
		// エラー内容を取得
		const char* errorString = aiGetErrorString();


		std::string msg = "モデルの読み込みに失敗しました。\n";
		msg += "ファイルパス: " + std::string(FileName) + "\n";
		msg += "エラー内容: " + std::string(errorString);

		// メッセージボックスを表示
		MessageBoxA(NULL, msg.c_str(), "Model Load Error", MB_OK | MB_ICONERROR);

		// 強制終了せずに安全に終わる（またはここで止める）
//		delete model;
		return nullptr;
	}

	// 視錐台カリング用にモデル空間の境界ボックスを保存する。
	// 描画時にノード変換・ワールド変換を適用するため、ここでは頂点のローカル範囲を保持する。
	XMFLOAT3 minBounds(FLT_MAX, FLT_MAX, FLT_MAX);
	XMFLOAT3 maxBounds(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (unsigned int m = 0; m < model->AiScene->mNumMeshes; ++m)
	{
		const aiMesh* mesh = model->AiScene->mMeshes[m];
		if (!mesh) continue;
		for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
		{
			const aiVector3D& position = mesh->mVertices[v];
			minBounds.x = (std::min)(minBounds.x, position.x);
			minBounds.y = (std::min)(minBounds.y, position.y);
			minBounds.z = (std::min)(minBounds.z, position.z);
			maxBounds.x = (std::max)(maxBounds.x, position.x);
			maxBounds.y = (std::max)(maxBounds.y, position.y);
			maxBounds.z = (std::max)(maxBounds.z, position.z);
		}
	}
	if (minBounds.x != FLT_MAX)
	{
		model->BoundsMin = minBounds;
		model->BoundsMax = maxBounds;
		model->BoundsCenter = XMFLOAT3(
			(minBounds.x + maxBounds.x) * 0.5f,
			(minBounds.y + maxBounds.y) * 0.5f,
			(minBounds.z + maxBounds.z) * 0.5f);
	}

	model->VertexBuffer = new ID3D11Buffer * [model->AiScene->mNumMeshes];
	model->IndexBuffer = new ID3D11Buffer * [model->AiScene->mNumMeshes];
	model->SkinnedVertexBuffer = new ID3D11Buffer * [model->AiScene->mNumMeshes];
	model->MeshIndexCounts = new unsigned int[model->AiScene->mNumMeshes];
	model->MeshMaterials = new MeshMaterial[model->AiScene->mNumMeshes];

	// スキニング用ボーン情報の収集（全メッシュを横断して一意なボーンリストを作成）
	model->HasSkinning = false;
	model->TotalBoneCount = 0;

	// ルートノードのグローバル逆変換行列を計算（スキニングの座標系補正に必要）
	{
		XMMATRIX rootTransform = AiMatrixToXMMatrix(model->AiScene->mRootNode->mTransformation);
		XMVECTOR det;
		model->GlobalInverseTransform = XMMatrixInverse(&det, rootTransform);
	}

	for (unsigned int m = 0; m < model->AiScene->mNumMeshes; m++)
	{
		aiMesh* mesh = model->AiScene->mMeshes[m];
		if (mesh->mNumBones > 0) model->HasSkinning = true;
		for (unsigned int b = 0; b < mesh->mNumBones; b++)
		{
			std::string boneName = mesh->mBones[b]->mName.data;
			if (model->BoneNameToIndex.find(boneName) == model->BoneNameToIndex.end())
			{
				unsigned int idx = model->TotalBoneCount++;
				model->BoneNameToIndex[boneName] = idx;
				model->BoneOffsetMatrices.push_back(AiMatrixToXMMatrix(mesh->mBones[b]->mOffsetMatrix));
			}
		}
	}
	if (model->HasSkinning)
	{
	}

	for (unsigned int m = 0; m < model->AiScene->mNumMeshes; m++)
	{
		aiMesh* mesh = model->AiScene->mMeshes[m];

		// ===== マテリアル情報の取得 =====
		{
			aiMaterial* material = model->AiScene->mMaterials[mesh->mMaterialIndex];

			// ディフューズ色（基本色）を取得
			aiColor4D diffuseColor(1.0f, 1.0f, 1.0f, 1.0f);
			aiReturn colorResult = material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColor);
			
			// Diffuse色が取得できなかった、または黒(0,0,0)の場合はフォールバックを試みる
			// FBXのPhongマテリアルではDiffuseが黒で、実際の色がBase ColorやAmbientに格納される場合がある
			bool diffuseIsBlack = (colorResult == AI_SUCCESS &&
				diffuseColor.r == 0.0f && diffuseColor.g == 0.0f && diffuseColor.b == 0.0f);

			if (colorResult != AI_SUCCESS || diffuseIsBlack)
			{
				bool foundFallback = false;

				// フォールバック1: PBR Base Color を試す
				aiColor4D baseColor;
				if (AI_SUCCESS == material->Get(AI_MATKEY_BASE_COLOR, baseColor))
				{
					if (baseColor.r != 0.0f || baseColor.g != 0.0f || baseColor.b != 0.0f)
					{
						diffuseColor = baseColor;
						foundFallback = true;
					}
				}

				// フォールバック2: Ambient Color を試す（Phongマテリアルではここに色が入ることがある）
				if (!foundFallback)
				{
					aiColor4D ambientColor;
					if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_AMBIENT, ambientColor))
					{
						if (ambientColor.r != 0.0f || ambientColor.g != 0.0f || ambientColor.b != 0.0f)
						{
							diffuseColor = ambientColor;
							foundFallback = true;
						}
					}
				}

				// テクスチャがある場合は白にしてテクスチャ色をそのまま使う
				// テクスチャがない場合はDiffuse色をそのまま尊重する（黒でも正しい色の場合がある）
				if (!foundFallback)
				{
					aiString texPath;
					bool hasTexture = (AI_SUCCESS == material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath));
					if (hasTexture)
					{
						// テクスチャがあるのでマテリアル色は白（テクスチャ色 × 白 = テクスチャ色）
						diffuseColor = aiColor4D(1.0f, 1.0f, 1.0f, 1.0f);
					}
					else if (colorResult != AI_SUCCESS)
					{
						// Diffuse色自体が取得できなかった場合のみ白をデフォルトにする
						diffuseColor = aiColor4D(1.0f, 1.0f, 1.0f, 1.0f);
					}
					else
					{
						// Diffuse色が黒(0,0,0)でテクスチャもないが、Assimpが返した色をそのまま使用
					}
				}
			}

			// アルファ値の補正（0の場合は不透明に設定）
			if (diffuseColor.a == 0.0f)
			{
				diffuseColor.a = 1.0f;
			}

			model->MeshMaterials[m].diffuseColor = XMFLOAT4(diffuseColor.r, diffuseColor.g, diffuseColor.b, diffuseColor.a);

			// テクスチャ情報の取得
			aiString texturePath;
			if (AI_SUCCESS == material->GetTexture(aiTextureType_DIFFUSE, 0, &texturePath))
			{
				model->MeshMaterials[m].hasTexture = true;
				model->MeshMaterials[m].texturePath = texturePath.data;
			}
			else
			{
				model->MeshMaterials[m].hasTexture = false;
				model->MeshMaterials[m].texturePath.clear();
			}

			aiString normalTexturePath;
			if (AI_SUCCESS == material->GetTexture(aiTextureType_NORMALS, 0, &normalTexturePath) ||
				AI_SUCCESS == material->GetTexture(aiTextureType_HEIGHT, 0, &normalTexturePath) ||
				AI_SUCCESS == material->GetTexture(aiTextureType_NORMAL_CAMERA, 0, &normalTexturePath))
			{
				model->MeshMaterials[m].hasNormalTexture = true;
				model->MeshMaterials[m].normalTexturePath = normalTexturePath.data;
			}
			else
			{
				model->MeshMaterials[m].hasNormalTexture = false;
				model->MeshMaterials[m].normalTexturePath.clear();
			}

			// Blinn/光沢度パラメータも取得可能（参考）
//			float shininess = 32.0f;  // デフォルト光沢度
//			material->Get(AI_MATKEY_SHININESS, shininess);
			// ※ 将来的にシェーダーコンスタントバッファに追加可能
		}

		// ===== 頂点バッファ生成 =====
		{
			Vertex3D* vertex = new Vertex3D[mesh->mNumVertices];

			for (unsigned int v = 0; v < mesh->mNumVertices; v++)
			{
				// 座標変換注意: aiProcess_ConvertToLeftHandedを使う場合は素直に代入
				vertex[v].position = XMFLOAT3(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);
				
				// テクスチャ座標が存在するかチェック
				if (mesh->HasTextureCoords(0))
				{
					vertex[v].texCoord = XMFLOAT2(mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y);
				}
				else
				{
					// テクスチャ座標がない場合はデフォルト値
					vertex[v].texCoord = XMFLOAT2(0.5f, 0.5f);
				}
				
				if (mesh->HasVertexColors(0))
				{
					vertex[v].color = XMFLOAT4(
						mesh->mColors[0][v].r,
						mesh->mColors[0][v].g,
						mesh->mColors[0][v].b,
						mesh->mColors[0][v].a
					);
				}
				else
				{
					vertex[v].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
				}

				// 法線が存在するかチェック
				if (mesh->HasNormals())
				{
					vertex[v].normal = XMFLOAT3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z);
				}
				else
				{
					// 法線がない場合はデフォルト（上向き）
//					vertex[v].normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
					vertex[v].normal = XMFLOAT3(0.57735f, 0.57735f, 0.57735f);  // 法線のデフォルト値（単位ベクトル）
				}
			}

			// 頂点数の検証
			if (mesh->mNumVertices == 0)
			{
				delete[] vertex;
				return nullptr;
			}

			D3D11_BUFFER_DESC bd;
			ZeroMemory(&bd, sizeof(bd));
			bd.Usage = D3D11_USAGE_DEFAULT;
			bd.ByteWidth = sizeof(Vertex3D) * mesh->mNumVertices;
			bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			bd.CPUAccessFlags = 0;

			D3D11_SUBRESOURCE_DATA sd = {};
			sd.pSysMem = vertex;

			HRESULT hr = GetDevice()->CreateBuffer(&bd, &sd, &model->VertexBuffer[m]);
			if (FAILED(hr))
			{
				delete[] vertex;
				return nullptr;
			}

			delete[] vertex;
		}

		// ===== スキニング用頂点バッファ生成 =====
		if (model->HasSkinning)
		{
			VertexSkinned* skinVertex = new VertexSkinned[mesh->mNumVertices];

			for (unsigned int v = 0; v < mesh->mNumVertices; v++)
			{
				skinVertex[v].position = XMFLOAT3(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);
				
				if (mesh->HasTextureCoords(0))
					skinVertex[v].texCoord = XMFLOAT2(mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y);
				else
					skinVertex[v].texCoord = XMFLOAT2(0.5f, 0.5f);

				if (mesh->HasVertexColors(0))
				{
					skinVertex[v].color = XMFLOAT4(
						mesh->mColors[0][v].r,
						mesh->mColors[0][v].g,
						mesh->mColors[0][v].b,
						mesh->mColors[0][v].a
					);
				}
				else
				{
					skinVertex[v].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
				}

				if (mesh->HasNormals())
					skinVertex[v].normal = XMFLOAT3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z);
				else
					skinVertex[v].normal = XMFLOAT3(0.57735f, 0.57735f, 0.57735f);

				for (int bi = 0; bi < 4; bi++)
				{
					skinVertex[v].boneIndex[bi] = 0;
					skinVertex[v].boneWeight[bi] = 0.0f;
				}
			}

			// ボーンウェイト情報を頂点に書き込み
			for (unsigned int b = 0; b < mesh->mNumBones; b++)
			{
				aiBone* bone = mesh->mBones[b];
				unsigned int globalBoneIdx = model->BoneNameToIndex[bone->mName.data];

				for (unsigned int w = 0; w < bone->mNumWeights; w++)
				{
					unsigned int vertexId = bone->mWeights[w].mVertexId;
					float weight = bone->mWeights[w].mWeight;

					// 空いているスロットにウェイトを追加（最大4つ）
					for (int slot = 0; slot < 4; slot++)
					{
						if (skinVertex[vertexId].boneWeight[slot] == 0.0f)
						{
							skinVertex[vertexId].boneIndex[slot] = globalBoneIdx;
							skinVertex[vertexId].boneWeight[slot] = weight;
							break;
						}
					}
				}
			}

			D3D11_BUFFER_DESC bd;
			ZeroMemory(&bd, sizeof(bd));
			bd.Usage = D3D11_USAGE_DEFAULT;
			bd.ByteWidth = sizeof(VertexSkinned) * mesh->mNumVertices;
			bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			bd.CPUAccessFlags = 0;

			D3D11_SUBRESOURCE_DATA sd;
			ZeroMemory(&sd, sizeof(sd));
			sd.pSysMem = skinVertex;

			HRESULT hr = GetDevice()->CreateBuffer(&bd, &sd, &model->SkinnedVertexBuffer[m]);
			if (FAILED(hr))
			{
			}

			delete[] skinVertex;
		}
		else
		{
			model->SkinnedVertexBuffer[m] = nullptr;
		}

		// ===== インデックスバッファ生成 =====
		{
			// 三角形化されているため、すべてのフェイスは3つのインデックスを持つ
			unsigned int indexCount = 0;
			
			// インデックス数を計算
			for (unsigned int f = 0; f < mesh->mNumFaces; f++)
			{
				const aiFace* face = &mesh->mFaces[f];
				
				if (face->mNumIndices >= 3)
				{
					// 三角形化後は通常3、稀に4以上の場合は最初の三角形のみを使用
					indexCount += 3;
				}
			}

			// インデックス数を保存
			model->MeshIndexCounts[m] = indexCount;

			// インデックス数が0の場合、ダミーバッファを作成
			if (indexCount == 0)
			{
				// ダミーインデックス（1つの無効なインデックス）を作成
				unsigned int dummyIndex = 0;
				D3D11_BUFFER_DESC bd;
				ZeroMemory(&bd, sizeof(bd));
				bd.Usage = D3D11_USAGE_DEFAULT;
				bd.ByteWidth = sizeof(unsigned int);  // 最小サイズ
				bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
				bd.CPUAccessFlags = 0;

				D3D11_SUBRESOURCE_DATA sd;
				ZeroMemory(&sd, sizeof(sd));
				sd.pSysMem = &dummyIndex;

				HRESULT hr = GetDevice()->CreateBuffer(&bd, &sd, &model->IndexBuffer[m]);
				if (FAILED(hr))
				{
					return nullptr;
				}
				continue;  // 次のメッシュへ
			}

			unsigned int* index = new unsigned int[indexCount];
			unsigned int indexOffset = 0;

			for (unsigned int f = 0; f < mesh->mNumFaces; f++)
			{
				const aiFace* face = &mesh->mFaces[f];

				// 三角形チェック（より柔軟に対応）
//				if (face->mNumIndices >= 3 && indexOffset + 3 <= indexCount)
				if (face->mNumIndices > 0 && indexOffset < indexCount)
				{
					index[indexOffset + 0] = face->mIndices[0];
					index[indexOffset + 1] = face->mIndices[1];
					index[indexOffset + 2] = face->mIndices[2];
					indexOffset += 3;
				}
			}

			D3D11_BUFFER_DESC bd;
			ZeroMemory(&bd, sizeof(bd));
			bd.Usage = D3D11_USAGE_DEFAULT;
			bd.ByteWidth = sizeof(unsigned int) * indexCount;
			bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
			bd.CPUAccessFlags = 0;

			D3D11_SUBRESOURCE_DATA sd;
			ZeroMemory(&sd, sizeof(sd));
			sd.pSysMem = index;

			HRESULT hr = GetDevice()->CreateBuffer(&bd, &sd, &model->IndexBuffer[m]);
			if (FAILED(hr))
			{
				delete[] index;
				return nullptr;
			}

			delete[] index;
		}
	}

	// ===== テクスチャ読み込み =====
	for (unsigned int i = 0; i < model->AiScene->mNumTextures; i++)
	{
		aiTexture* aitexture = model->AiScene->mTextures[i];
		if (!aitexture) continue;

		// テクスチャ名を取得してログ出力
		std::string texFileName;
		if (aitexture->mFilename.length > 0)
		{
			texFileName = aitexture->mFilename.data;
		}
		else
		{
			texFileName = std::string("*") + std::to_string(i);
		}
		hal::dout << "[Model] Embedded Texture[" << i << "]: " << texFileName.c_str() << "\n";

		ID3D11ShaderResourceView* texture = nullptr;
		TexMetadata metadata;
		ScratchImage image;

		// aiTexture: mHeight == 0 -> compressed image data (e.g. PNG/JPG) stored in pcData with size mWidth
		// mHeight  != 0 -> uncompressed raw image data (RGBA) with dimensions mWidth x mHeight
		if (aitexture->mHeight == 0)
		{
			// 圧縮データ (WIC対応) をメモリから読み込む
			if (aitexture->pcData && aitexture->mWidth > 0)
			{
				HRESULT hr = LoadFromWICMemory(reinterpret_cast<const uint8_t*>(aitexture->pcData), (size_t)aitexture->mWidth, WIC_FLAGS_NONE, &metadata, image);
				if (SUCCEEDED(hr))
				{
					hr = CreateShaderResourceView(GetDevice(), image.GetImages(), image.GetImageCount(), metadata, &texture);
					if (FAILED(hr))
					{
					}
				}
				else
				{
				}
			}
			else
			{
			}
		}
		else
		{
			// 生データ (RGBA) を ScratchImage にコピーして SRV を作成する
			if (aitexture->pcData && aitexture->mWidth > 0 && aitexture->mHeight > 0)
			{
				HRESULT hr = image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, (size_t)aitexture->mWidth, (size_t)aitexture->mHeight, 1, 1);
				if (SUCCEEDED(hr))
				{
					const DirectX::Image* img = image.GetImage(0, 0, 0);
					if (img && img->pixels)
					{
						size_t bytes = (size_t)aitexture->mWidth * (size_t)aitexture->mHeight * 4; // RGBA
						memcpy(img->pixels, reinterpret_cast<const uint8_t*>(aitexture->pcData), bytes);
						metadata = image.GetMetadata();
						hr = CreateShaderResourceView(GetDevice(), image.GetImages(), image.GetImageCount(), metadata, &texture);
						if (FAILED(hr))
						{
						}
					}
					else
					{
					}
				}
				else
				{
				}
			}
			else
			{
			}
		}

		if (texture)
		{
			model->Texture[texFileName] = texture;
		}
		else
		{
		}
	}

	// ダミー白テクスチャ（テクスチャなしメッシュ用）をモデルごとに読み込み
	model->WhiteTexture = LoadTexture(L"asset\\texture\\fade.png");
	if (!model->WhiteTexture)
	{
	}
	model->FlatNormalTexture = CreateFlatNormalTexture();
	model->BlackTexture = CreateSolidTexture(0xFF000000);

	// メッシュごとのテクスチャをプリキャッシュ
	for (unsigned int m = 0; m < model->AiScene->mNumMeshes; m++)
	{
		model->MeshMaterials[m].isFaceMesh = false;

		if (model->MeshMaterials[m].hasTexture)
		{
			std::string texPath = model->MeshMaterials[m].texturePath;

			// テクスチャパスからファイル名部分を抽出
			std::string fileName = texPath;
			size_t slashPos = texPath.find_last_of("/\\");
			if (slashPos != std::string::npos)
			{
				fileName = texPath.substr(slashPos + 1);
			}

			// 埋め込みテクスチャ参照（"*0"等）の場合、シーンから元ファイル名を取得
			if (texPath.size() > 0 && texPath[0] == '*')
			{
				int texIdx = atoi(texPath.c_str() + 1);
				if (texIdx >= 0 && (unsigned int)texIdx < model->AiScene->mNumTextures)
				{
					const aiTexture* pTex = model->AiScene->mTextures[texIdx];
					if (pTex->mFilename.length > 0)
					{
						std::string embName(pTex->mFilename.data);
						size_t pos = embName.find_last_of("/\\");
						if (pos != std::string::npos)
							fileName = embName.substr(pos + 1);
						else
							fileName = embName;
					}
				}
			}

			hal::dout << "[Model] Mesh[" << m << "] Texture: " << fileName.c_str() << " (path: " << texPath.c_str() << ")\n";

			if (model->Texture.count(texPath))
			{
				model->MeshMaterials[m].textureView = model->Texture[texPath];
			}
			else
			{
				model->MeshMaterials[m].textureView = model->WhiteTexture;
			}
		}
		else
		{
			model->MeshMaterials[m].textureView = model->WhiteTexture;
		}

		if (model->MeshMaterials[m].hasNormalTexture)
		{
			std::string normalPath = model->MeshMaterials[m].normalTexturePath;
			ID3D11ShaderResourceView* normalTexture = LoadNormalTexture(model, FileName, normalPath);
			model->MeshMaterials[m].normalTextureView = normalTexture ? normalTexture : model->FlatNormalTexture;
			hal::dout << "[Model] Mesh[" << m << "] NormalMap: " << normalPath.c_str() << "\n";
		}
		else
		{
			model->MeshMaterials[m].normalTextureView = model->FlatNormalTexture;
		}

		// Knight 系モデルだけ専用の Metallic/Roughness テクスチャを使う。
		if (UsesKnightPbrTextures(FileName))
		{
			ID3D11ShaderResourceView* metallicTexture = LoadCachedLinearTexture(
				model, "KnightModel_Metallic", L"asset\\texture\\KnightModel_Metallic.png");
			ID3D11ShaderResourceView* roughnessTexture = LoadCachedLinearTexture(
				model, "KnightModel_Roughness", L"asset\\texture\\KnightModel_Roughness.png");
			model->MeshMaterials[m].metallicTextureView = metallicTexture ? metallicTexture : model->BlackTexture;
			model->MeshMaterials[m].roughnessTextureView = roughnessTexture ? roughnessTexture : model->WhiteTexture;
		}
		else
		{
			model->MeshMaterials[m].metallicTextureView = model->BlackTexture;
			model->MeshMaterials[m].roughnessTextureView = model->WhiteTexture;
		}

		// field_allnormal は壁マテリアル(kabee)だけ Emissive を有効にする。
		if (IsFieldAllNormalModel(FileName) && HasMaterialNamePart(model, m, "kabee"))
		{
			ID3D11ShaderResourceView* emissiveTexture = LoadCachedLinearTexture(
				model, "Wall_EmissiveColor", L"asset\\texture\\Wall_EmissiveColor.png");
			model->MeshMaterials[m].emissiveTextureView = emissiveTexture ? emissiveTexture : model->BlackTexture;
		}
		else
		{
			model->MeshMaterials[m].emissiveTextureView = model->BlackTexture;
		}
	}

	// アニメーションチャンネルのマッピングを作成
	if (model->AiScene->mNumAnimations > 0)
	{
		aiAnimation* anim = model->AiScene->mAnimations[0];
		for (unsigned int c = 0; c < anim->mNumChannels; c++)
		{
			model->NodeToAnimIndex[anim->mChannels[c]->mNodeName.data] = c;
		}
	}

	g_ModelCache[FileName] = model;

	return model;
}

void ModelRelease(MODEL* model)
{
	if (!model) return;

	model->RefCount--;
	if (model->RefCount > 0) return;

	if (!model->FilePath.empty())
	{
		g_ModelCache.erase(model->FilePath);
	}

	for (unsigned int m = 0; m < model->AiScene->mNumMeshes; m++)
	{
		if (model->VertexBuffer[m])
			model->VertexBuffer[m]->Release();
		if (model->IndexBuffer[m])
		model->IndexBuffer[m]->Release();
		if (model->SkinnedVertexBuffer && model->SkinnedVertexBuffer[m])
			model->SkinnedVertexBuffer[m]->Release();
	}

	delete[] model->VertexBuffer;
	delete[] model->IndexBuffer;
	delete[] model->SkinnedVertexBuffer;
	delete[] model->MeshIndexCounts;
	delete[] model->MeshMaterials;

	for (std::pair<const std::string, ID3D11ShaderResourceView*> pair : model->Texture)
	{
		if (pair.second)
			pair.second->Release();
	}

	if (model->WhiteTexture)
		model->WhiteTexture = nullptr;
	if (model->FlatNormalTexture)
		model->FlatNormalTexture->Release();
	if (model->BlackTexture)
		model->BlackTexture->Release();

	// アニメーションキャッシュの破棄
	for (std::pair<const std::string, AnimationClip*> pair : model->AnimationClips)
	{
		delete pair.second;
	}
	model->AnimationClips.clear();

	if (model->AiScene)
		aiReleaseImport(model->AiScene);

	delete model;

	if (g_ModelCache.empty())
	{
		SAFE_RELEASE(g_SkinningBoneBuffer);
		SAFE_RELEASE(g_SkinningVertexLayout);
		SAFE_RELEASE(g_SkinningVertexShader);
	}
}

void ModelDraw(MODEL* model, XMFLOAT3 pos, XMFLOAT3 rot, XMFLOAT3 scale, const XMFLOAT4& color, bool useColorReplace, SHADERTYPE shadertype, ID3D11ShaderResourceView* customTexture)
{
	if (!model) return;

	Camera* pCamera = GetCamera();
	if (!pCamera) return;

	// 頂点レイアウトとシェーダーのセット
	GetDeviceContext()->IASetInputLayout(GetShader(shadertype)->GetVertexLayout());
	GetDeviceContext()->VSSetShader(GetShader(shadertype)->GetVertexShader(), NULL, 0);
	GetDeviceContext()->PSSetShader(GetShader(shadertype)->GetPixelShader(), NULL, 0);

	XMMATRIX View = pCamera->GetView();
	XMMATRIX Projection = pCamera->GetProjection();

	XMMATRIX TranslationMatrix = XMMatrixTranslation(pos.x, pos.y, pos.z);
	XMMATRIX RotationMatrix = XMMatrixRotationRollPitchYaw(
		XMConvertToRadians(rot.x),
		XMConvertToRadians(rot.y),
		XMConvertToRadians(rot.z));
	XMMATRIX ScalingMatrix = XMMatrixScaling(scale.x, scale.y, scale.z);

	XMMATRIX World = ScalingMatrix * RotationMatrix * TranslationMatrix;

	SetWorldMatrix(World);
	SetViewMatrix(View);
	SetProjectionMatrix(Projection);

	GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	XMFLOAT4 finalColor = color;
	if (!useColorReplace)
	{
		finalColor = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	XMMATRIX identity = XMMatrixIdentity();
	RenderNode(model, model->AiScene->mRootNode, identity, finalColor, useColorReplace, customTexture);
}

void ModelDrawShadowMap(MODEL* model, XMFLOAT3 pos, XMFLOAT3 rot, XMFLOAT3 scale, XMMATRIX lightView, XMMATRIX lightProjection)
{
	if (!model) return;

	// 影を作るだけなので、ShadowMap専用シェーダーを使う。
	// このシェーダーは色を出さず、深度バッファに距離だけを書き込む。
	GetDeviceContext()->IASetInputLayout(GetShader(S_SHADOW_MAP)->GetVertexLayout());
	GetDeviceContext()->VSSetShader(GetShader(S_SHADOW_MAP)->GetVertexShader(), NULL, 0);
	GetDeviceContext()->PSSetShader(GetShader(S_SHADOW_MAP)->GetPixelShader(), NULL, 0);

	XMMATRIX TranslationMatrix = XMMatrixTranslation(pos.x, pos.y, pos.z);
	XMMATRIX RotationMatrix = XMMatrixRotationRollPitchYaw(
		XMConvertToRadians(rot.x),
		XMConvertToRadians(rot.y),
		XMConvertToRadians(rot.z));
	XMMATRIX ScalingMatrix = XMMatrixScaling(scale.x, scale.y, scale.z);

	XMMATRIX World = ScalingMatrix * RotationMatrix * TranslationMatrix;

	SetWorldMatrix(World);

	// 通常描画ではカメラのView/Projectionを使うが、ここではライト視点の行列を使う。
	SetViewMatrix(lightView);
	SetProjectionMatrix(lightProjection);

	GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	XMMATRIX identity = XMMatrixIdentity();
	// RenderNodeは通常描画と同じメッシュを描く。ShadowMapでは色やテクスチャは最終結果に使われない。
	RenderNode(model, model->AiScene->mRootNode, identity, XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), false);
}

// スキニングモデルをShadowMapへ描く。
// 既存のスキニング用VS(ボーンで頂点を動かす)を流用し、
// View/Projectionにライト視点の行列を入れて深度だけを書き込む。
// ピクセルシェーダーは色を出さない ShadowMap 用の空PSを使う。
void ModelDrawShadowMapSkinned(MODEL* model, XMFLOAT3 pos, XMFLOAT3 rot, XMFLOAT3 scale, const BoneMatrices& boneMatrices, XMMATRIX lightView, XMMATRIX lightProjection)
{
	if (!model) return;

	// スキニングリソース(専用VS・入力レイアウト・ボーン定数バッファ)を用意
	if (!(model->HasSkinning && EnsureSkinningResources()))
	{
		// スキニングが無いモデルは静的版で影を落とす
		ModelDrawShadowMap(model, pos, rot, scale, lightView, lightProjection);
		return;
	}

	ID3D11DeviceContext* context = GetDeviceContext();

	context->IASetInputLayout(g_SkinningVertexLayout);
	context->VSSetShader(g_SkinningVertexShader, nullptr, 0);
	// ボーン行列をb7(VS)へ転送
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (SUCCEEDED(context->Map(g_SkinningBoneBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
	{
		memcpy(mapped.pData, boneMatrices.matrices, sizeof(XMMATRIX) * BoneMatrices::MAX_BONES);
		context->Unmap(g_SkinningBoneBuffer, 0);
		context->VSSetConstantBuffers(7, 1, &g_SkinningBoneBuffer);
	}

	// 深度だけを書く空のピクセルシェーダー(ShadowMap用)
	context->PSSetShader(GetShader(S_SHADOW_MAP)->GetPixelShader(), nullptr, 0);

	XMMATRIX TranslationMatrix = XMMatrixTranslation(pos.x, pos.y, pos.z);
	XMMATRIX RotationMatrix = XMMatrixRotationRollPitchYaw(
		XMConvertToRadians(rot.x),
		XMConvertToRadians(rot.y),
		XMConvertToRadians(rot.z));
	XMMATRIX ScalingMatrix = XMMatrixScaling(scale.x, scale.y, scale.z);
	XMMATRIX World = ScalingMatrix * RotationMatrix * TranslationMatrix;

	SetWorldMatrix(World);
	// View/Projectionにライト視点の行列を入れる
	SetViewMatrix(lightView);
	SetProjectionMatrix(lightProjection);

	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// スキニング済み頂点バッファでメッシュを描画
	for (unsigned int m = 0; m < model->AiScene->mNumMeshes; m++)
	{
		ID3D11Buffer* vertexBuffer = nullptr;
		UINT stride = sizeof(VertexSkinned);
		UINT offset = 0;
		if (model->SkinnedVertexBuffer && model->SkinnedVertexBuffer[m])
		{
			vertexBuffer = model->SkinnedVertexBuffer[m];
		}
		else if (model->VertexBuffer)
		{
			// スキニングバッファが無いメッシュは通常頂点で
			vertexBuffer = model->VertexBuffer[m];
			stride = sizeof(Vertex3D);
		}
		if (!vertexBuffer) continue;

		context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
		context->IASetIndexBuffer(model->IndexBuffer[m], DXGI_FORMAT_R32_UINT, 0);

		unsigned int indexCount = model->MeshIndexCounts[m];
		if (indexCount > 0)
		{
			context->DrawIndexed(indexCount, 0, 0);
		}
	}
}

void ModelAnimationDraw(MODEL* model, XMFLOAT3 pos, XMFLOAT3 rot, XMFLOAT3 scale, const BoneMatrices& boneMatrices, const XMFLOAT4& color, bool useColorReplace, SHADERTYPE shadertype, const AnimationClip* clip, double animTime)
{
	if (!model) return;

	Camera* pCamera = GetCamera();
	if (!pCamera) return;

	XMMATRIX View = pCamera->GetView();
	XMMATRIX Projection = pCamera->GetProjection();

	XMMATRIX TranslationMatrix = XMMatrixTranslation(pos.x, pos.y, pos.z);
	XMMATRIX RotationMatrix = XMMatrixRotationRollPitchYaw(
		XMConvertToRadians(rot.x),
		XMConvertToRadians(rot.y),
		XMConvertToRadians(rot.z));
	XMMATRIX ScalingMatrix = XMMatrixScaling(scale.x, scale.y, scale.z);

	XMMATRIX worldMatrix = ScalingMatrix * RotationMatrix * TranslationMatrix;
	ID3D11DeviceContext* context = GetDeviceContext();

	bool useSkinningShader = model->HasSkinning && EnsureSkinningResources();
	if (useSkinningShader)
	{
		context->IASetInputLayout(g_SkinningVertexLayout);
		context->VSSetShader(g_SkinningVertexShader, nullptr, 0);

		D3D11_MAPPED_SUBRESOURCE mapped;
		if (SUCCEEDED(context->Map(g_SkinningBoneBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			memcpy(mapped.pData, boneMatrices.matrices, sizeof(XMMATRIX) * BoneMatrices::MAX_BONES);
			context->Unmap(g_SkinningBoneBuffer, 0);
			context->VSSetConstantBuffers(7, 1, &g_SkinningBoneBuffer);
		}
	}
	else
	{
		context->IASetInputLayout(GetShader(shadertype)->GetVertexLayout());
		context->VSSetShader(GetShader(shadertype)->GetVertexShader(), nullptr, 0);
	}

	context->PSSetShader(GetShader(shadertype)->GetPixelShader(), nullptr, 0);

	SetWorldMatrix(worldMatrix);
	SetViewMatrix(View);
	SetProjectionMatrix(Projection);

	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	XMFLOAT4 finalColor = color;
	if (!useColorReplace)
	{
		finalColor = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	if (!useSkinningShader && clip && !clip->tracks.empty() && !model->NodeToAnimIndex.empty())
	{
		RenderAnimatedNodeRecursive(
			model,
			model->AiScene->mRootNode,
			XMMatrixIdentity(),
			clip,
			animTime,
			finalColor,
			useColorReplace,
			worldMatrix
		);
		return;
	}

	for (unsigned int m = 0; m < model->AiScene->mNumMeshes; m++)
	{
		XMFLOAT4 meshFinalColor;
		if (useColorReplace)
		{
			meshFinalColor = finalColor;
		}
		else
		{
			if (m < model->AiScene->mNumMeshes && model->MeshMaterials)
			{
				XMFLOAT4 meshColor = model->MeshMaterials[m].diffuseColor;
				meshFinalColor = XMFLOAT4(
					meshColor.x * finalColor.x, meshColor.y * finalColor.y,
					meshColor.z * finalColor.z, meshColor.w * finalColor.w);
			}
			else
			{
				meshFinalColor = finalColor;
			}
		}
		MATERIAL material = {};
		material.Diffuse = meshFinalColor;
		material.Ambient = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		material.Specular = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		material.Emission = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		material.Shininess = 50.0f;
		SetMaterial(material);

		ID3D11ShaderResourceView* pSRV = model->MeshMaterials[m].textureView;
		context->PSSetShaderResources(0, 1, &pSRV);
		ID3D11ShaderResourceView* pNormalSRV = model->MeshMaterials[m].normalTextureView;
		context->PSSetShaderResources(2, 1, &pNormalSRV);
		// PBR/Emissive 用の追加テクスチャスロット。
		ID3D11ShaderResourceView* pMetallicSRV = model->MeshMaterials[m].metallicTextureView;
		context->PSSetShaderResources(3, 1, &pMetallicSRV);
		ID3D11ShaderResourceView* pRoughnessSRV = model->MeshMaterials[m].roughnessTextureView;
		context->PSSetShaderResources(4, 1, &pRoughnessSRV);
		ID3D11ShaderResourceView* pEmissiveSRV = model->MeshMaterials[m].emissiveTextureView;
		context->PSSetShaderResources(5, 1, &pEmissiveSRV);

		UINT stride = sizeof(Vertex3D);
		UINT offset = 0;
		ID3D11Buffer* vertexBuffer = model->VertexBuffer[m];
		if (useSkinningShader && model->SkinnedVertexBuffer && model->SkinnedVertexBuffer[m])
		{
			vertexBuffer = model->SkinnedVertexBuffer[m];
			stride = sizeof(VertexSkinned);
		}

		context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
		context->IASetIndexBuffer(model->IndexBuffer[m], DXGI_FORMAT_R32_UINT, 0);

		unsigned int indexCount = model->MeshIndexCounts[m];
		if (indexCount > 0)
		{
			context->DrawIndexed(indexCount, 0, 0);
		}
	}
}

XMFLOAT3 ModelGetSize(MODEL* model)
{
	if (!model || !model->AiScene)
	{
		return XMFLOAT3(0.0f, 0.0f, 0.0f);
	}

	XMFLOAT3 minBounds(FLT_MAX, FLT_MAX, FLT_MAX);
	XMFLOAT3 maxBounds(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	for (unsigned int m = 0; m < model->AiScene->mNumMeshes; m++)
	{
		aiMesh* mesh = model->AiScene->mMeshes[m];

		for (unsigned int v = 0; v < mesh->mNumVertices; v++)
		{
			aiVector3D pos = mesh->mVertices[v];

			minBounds.x = std::min(minBounds.x, pos.x);
			minBounds.y = std::min(minBounds.y, pos.y);
			minBounds.z = std::min(minBounds.z, pos.z);

			maxBounds.x = std::max(maxBounds.x, pos.x);
			maxBounds.y = std::max(maxBounds.y, pos.y);
			maxBounds.z = std::max(maxBounds.z, pos.z);
		}
	}

	XMFLOAT3 size(
		maxBounds.x - minBounds.x,
		maxBounds.y - minBounds.y,
		maxBounds.z - minBounds.z
	);

	return size;
}

void ModelGetBounds(MODEL* model, XMFLOAT3* minBounds, XMFLOAT3* maxBounds, XMFLOAT3* center)
{
	if (!model)
	{
		if (minBounds) *minBounds = XMFLOAT3(0.0f, 0.0f, 0.0f);
		if (maxBounds) *maxBounds = XMFLOAT3(0.0f, 0.0f, 0.0f);
		if (center) *center = XMFLOAT3(0.0f, 0.0f, 0.0f);
		return;
	}

	if (minBounds) *minBounds = model->BoundsMin;
	if (maxBounds) *maxBounds = model->BoundsMax;
	if (center) *center = model->BoundsCenter;
}

XMFLOAT4 ModelGetAverageMaterialColor(MODEL* model)
{
	if (!model || !model->AiScene || model->AiScene->mNumMaterials == 0)
	{
		return XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
	unsigned int count = 0;

	for (unsigned int m = 0; m < model->AiScene->mNumMaterials; m++)
	{
		if (!model->MeshMaterials || m >= model->AiScene->mNumMeshes) continue;

		XMFLOAT4 matColor = model->MeshMaterials[m].diffuseColor;

		r += matColor.x;
		g += matColor.y;
		b += matColor.z;
		a += matColor.w;
		count++;
	}

	if (count > 0)
	{
		return XMFLOAT4(r / count, g / count, b / count, a / count);
	}

	return XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
}
