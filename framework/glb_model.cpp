//==============================================================================
// GLBモデル読み込み・描画クラス [glb_model.cpp]
// Assimp + DirectX 11 による .glb (glTF Binary) 専用ローダー
//==============================================================================

#include "glb_model.h"
#include "renderer.h"
#include "texture.h"
#include "camera.h"
#include "debug_ostream.h"
#include "DirectXTex.h"
#include <windows.h>
#include <cstring>
#include <algorithm>
#include <float.h>
#include <memory>
#include <cstdint>
#include <fstream>
#include <functional>
#include <limits>
#include <cmath>
#include <unordered_map>
#include <thread>
#include "nlohmann/json.hpp"

using namespace DirectX;
using json = nlohmann::json;

static const size_t GLB_MAX_TEXTURE_DIMENSION = 2048;
static const unsigned int GLB_MAX_CELL_DRAW_INDEXED = 1;
static std::mutex g_AssimpMutex;

static XMMATRIX AiMatrixToGlbMatrix(const aiMatrix4x4& matrix)
{
	return XMMATRIX(
		matrix.a1, matrix.b1, matrix.c1, matrix.d1,
		matrix.a2, matrix.b2, matrix.c2, matrix.d2,
		matrix.a3, matrix.b3, matrix.c3, matrix.d3,
		matrix.a4, matrix.b4, matrix.c4, matrix.d4);
}

static const aiNodeAnim* FindNodeAnimation(
	const aiAnimation* animation,
	const aiString& nodeName)
{
	if (!animation)
	{
		return nullptr;
	}

	for (unsigned int i = 0; i < animation->mNumChannels; ++i)
	{
		const aiNodeAnim* channel = animation->mChannels[i];
		if (channel && std::strcmp(channel->mNodeName.C_Str(), nodeName.C_Str()) == 0)
		{
			return channel;
		}
	}
	return nullptr;
}

static XMMATRIX GetNodeTransformAtAnimationStart(
	const aiNode* node,
	const aiAnimation* animation)
{
	if (!node)
	{
		return XMMatrixIdentity();
	}

	const aiNodeAnim* channel = FindNodeAnimation(animation, node->mName);
	if (!channel)
	{
		return AiMatrixToGlbMatrix(node->mTransformation);
	}

	const aiVector3D translation = channel->mNumPositionKeys > 0
		? channel->mPositionKeys[0].mValue
		: aiVector3D(0.0f, 0.0f, 0.0f);
	const aiVector3D scale = channel->mNumScalingKeys > 0
		? channel->mScalingKeys[0].mValue
		: aiVector3D(1.0f, 1.0f, 1.0f);
	const aiQuaternion rotation = channel->mNumRotationKeys > 0
		? channel->mRotationKeys[0].mValue
		: aiQuaternion(1.0f, 0.0f, 0.0f, 0.0f);

	return XMMatrixScaling(scale.x, scale.y, scale.z) *
		XMMatrixRotationQuaternion(XMVectorSet(
			rotation.x, rotation.y, rotation.z, rotation.w)) *
		XMMatrixTranslation(translation.x, translation.y, translation.z);
}

static void BuildMeshNodeTransforms(
	const aiNode* node,
	const XMMATRIX& parentTransform,
	const aiAnimation* animation,
	std::vector<XMMATRIX>* outTransforms)
{
	if (!node || !outTransforms)
	{
		return;
	}

	const XMMATRIX nodeTransform =
		GetNodeTransformAtAnimationStart(node, animation);
	const XMMATRIX worldTransform = nodeTransform * parentTransform;

	for (unsigned int i = 0; i < node->mNumMeshes; ++i)
	{
		const unsigned int meshIndex = node->mMeshes[i];
		if (meshIndex < outTransforms->size())
		{
			(*outTransforms)[meshIndex] = worldTransform;
		}
	}

	for (unsigned int i = 0; i < node->mNumChildren; ++i)
	{
		BuildMeshNodeTransforms(
			node->mChildren[i],
			worldTransform,
			animation,
			outTransforms);
	}
}

std::mutex& Glb_GetAssimpMutex(void)
{
	return g_AssimpMutex;
}

struct GlbDecodedTexture
{
	ScratchImage image;
	TexMetadata metadata{};
	bool ok = false;
	ID3D11Texture2D* pGpuTexture = nullptr;
	std::size_t uploadedMip = 0;
	std::size_t uploadedRows = 0;
};

struct GlbShadowCellPrep
{
	XMFLOAT3 aabbMin = XMFLOAT3(0.0f, 0.0f, 0.0f);
	XMFLOAT3 aabbMax = XMFLOAT3(0.0f, 0.0f, 0.0f);
	std::uint32_t indexOffset = 0;
	std::uint32_t indexCount = 0;
};

struct GlbPreparedMeshData
{
	std::vector<Vertex3D> vertices;
	std::vector<std::uint32_t> indices;
	std::vector<std::uint32_t> shadowIndices;
	std::vector<GlbShadowCellPrep> shadowCells;
	XMFLOAT3 boundsMin = XMFLOAT3(0.0f, 0.0f, 0.0f);
	XMFLOAT3 boundsMax = XMFLOAT3(0.0f, 0.0f, 0.0f);
	bool hasBounds = false;
	XMFLOAT4 diffuseColor = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	int textureIndex = -1;
	int metallicRoughnessIndex = -1;
	int normalIndex = -1;
	int emissiveIndex = -1;
	float metallicFactor = 1.0f;
	float roughnessFactor = 1.0f;
	int batchId = -1;
	std::vector<GlbBatchRange> batchRanges;
};

struct GlbPreparedTextureData
{
	std::vector<std::uint8_t> bytes;
	std::string name;
};

struct GlbPreparedData
{
	std::vector<GlbPreparedMeshData> meshes;
	std::vector<GlbPreparedTextureData> textures;
	XMFLOAT3 boundsMin = XMFLOAT3(0.0f, 0.0f, 0.0f);
	XMFLOAT3 boundsMax = XMFLOAT3(0.0f, 0.0f, 0.0f);
	XMFLOAT3 boundsCenter = XMFLOAT3(0.0f, 0.0f, 0.0f);
	bool hasBounds = false;
	bool enablePreparedPbr = false;
	std::vector<Vertex3D> combinedShadowVertices;
	std::vector<std::uint32_t> combinedShadowIndices;
};

struct GlbAccessorView
{
	const std::uint8_t* data = nullptr;
	std::size_t stride = 0;
	std::size_t elementSize = 0;
	std::uint32_t count = 0;
	int componentType = 0;
	bool normalized = false;
};

static bool ReadBinaryFile(const char* filePath, std::vector<std::uint8_t>* outBytes)
{
	if (!filePath || !outBytes)
	{
		return false;
	}

	std::ifstream file(filePath, std::ios::binary | std::ios::ate);
	if (!file)
	{
		return false;
	}
	const std::ifstream::pos_type end = file.tellg();
	if (end <= 0)
	{
		return false;
	}
	const std::size_t size = static_cast<std::size_t>(end);
	outBytes->resize(size);
	file.seekg(0, std::ios::beg);
	return static_cast<bool>(file.read(
		reinterpret_cast<char*>(outBytes->data()),
		static_cast<std::streamsize>(size)));
}

static std::size_t ComponentSize(int componentType)
{
	switch (componentType)
	{
	case 5121: return sizeof(std::uint8_t);
	case 5123: return sizeof(std::uint16_t);
	case 5125: return sizeof(std::uint32_t);
	case 5126: return sizeof(float);
	default: return 0;
	}
}

static std::size_t TypeComponentCount(const std::string& type)
{
	if (type == "SCALAR") return 1;
	if (type == "VEC2") return 2;
	if (type == "VEC3") return 3;
	if (type == "VEC4") return 4;
	if (type == "MAT2") return 4;
	if (type == "MAT3") return 9;
	if (type == "MAT4") return 16;
	return 0;
}

static bool GetAccessorView(
	const json& root,
	const std::vector<std::uint8_t>& bin,
	int accessorIndex,
	const char* expectedType,
	std::size_t expectedComponents,
	GlbAccessorView* outView)
{
	if (!outView || accessorIndex < 0 ||
		!root.contains("accessors") || !root["accessors"].is_array() ||
		static_cast<std::size_t>(accessorIndex) >= root["accessors"].size())
	{
		return false;
	}

	const json& accessor = root["accessors"][accessorIndex];
	if (!accessor.is_object() ||
		accessor.value("type", std::string()) != expectedType ||
		accessor.value("count", 0u) == 0u ||
		accessor.contains("sparse"))
	{
		return false;
	}
	const int componentType = accessor.value("componentType", 0);
	const std::size_t componentSize = ComponentSize(componentType);
	if (componentSize == 0 ||
		!accessor.contains("bufferView") ||
		!accessor["bufferView"].is_number_integer() ||
		!root.contains("bufferViews") || !root["bufferViews"].is_array())
	{
		return false;
	}

	const int bufferViewIndex = accessor["bufferView"].get<int>();
	if (bufferViewIndex < 0 ||
		static_cast<std::size_t>(bufferViewIndex) >= root["bufferViews"].size())
	{
		return false;
	}
	const json& bufferView = root["bufferViews"][bufferViewIndex];
	const std::size_t components = TypeComponentCount(accessor.value("type", std::string()));
	if (components != expectedComponents || !bufferView.is_object())
	{
		return false;
	}

	const std::size_t elementSize = componentSize * components;
	const std::size_t stride = bufferView.value("byteStride", elementSize);
	const std::size_t viewOffset = bufferView.value("byteOffset", 0u);
	const std::size_t accessorOffset = accessor.value("byteOffset", 0u);
	const std::size_t count = accessor.value("count", 0u);
	const std::size_t byteLength = bufferView.value("byteLength", 0u);
	if (count > (std::numeric_limits<std::uint32_t>::max)() ||
		stride < elementSize ||
		viewOffset > bin.size() ||
		byteLength > bin.size() - viewOffset ||
		accessorOffset > byteLength ||
		accessorOffset > (std::numeric_limits<std::size_t>::max)() - elementSize ||
		(count > 0 &&
			(count - 1) > ((std::numeric_limits<std::size_t>::max)() -
				accessorOffset - elementSize) / stride))
	{
		return false;
	}
	const std::size_t lastByte =
		viewOffset + accessorOffset + (count - 1) * stride + elementSize;
	if (lastByte > viewOffset + byteLength || lastByte > bin.size())
	{
		return false;
	}

	outView->data = bin.data() + viewOffset + accessorOffset;
	outView->stride = stride;
	outView->elementSize = elementSize;
	outView->count = static_cast<std::uint32_t>(count);
	outView->componentType = componentType;
	outView->normalized = accessor.value("normalized", false);
	return true;
}

static bool ReadFloatAccessorView(
	const GlbAccessorView& view,
	std::uint32_t index,
	float* outValues)
{
	if (!outValues || index >= view.count || view.componentType != 5126)
	{
		return false;
	}
	const std::uint8_t* source = view.data + static_cast<std::size_t>(index) * view.stride;
	memcpy(outValues, source, view.elementSize);
	const std::size_t componentCount = view.elementSize / sizeof(float);
	for (std::size_t i = 0; i < componentCount; ++i)
	{
		if (!std::isfinite(outValues[i]))
		{
			return false;
		}
	}
	return true;
}

static bool ReadIndexAccessorView(
	const GlbAccessorView& view,
	std::uint32_t index,
	std::uint32_t* outValue)
{
	if (!outValue || index >= view.count)
	{
		return false;
	}
	const std::uint8_t* source = view.data + static_cast<std::size_t>(index) * view.stride;
	switch (view.componentType)
	{
	case 5121:
		*outValue = *source;
		return true;
	case 5123:
	{
		std::uint16_t value = 0;
		memcpy(&value, source, sizeof(value));
		*outValue = value;
		return true;
	}
	case 5125:
	{
		std::uint32_t value = 0;
		memcpy(&value, source, sizeof(value));
		*outValue = value;
		return true;
	}
	default:
		return false;
	}
}

static bool ReadColorAccessorView(
	const GlbAccessorView& view,
	std::uint32_t index,
	float* outRgba)
{
	if (!outRgba || index >= view.count || !view.data)
	{
		return false;
	}
	const std::size_t componentSize = ComponentSize(view.componentType);
	if (componentSize == 0)
	{
		return false;
	}
	const std::size_t componentCount = view.elementSize / componentSize;
	if (componentCount < 3 || componentCount > 4)
	{
		return false;
	}
	const std::uint8_t* source = view.data + static_cast<std::size_t>(index) * view.stride;
	float values[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	for (std::size_t i = 0; i < componentCount; ++i)
	{
		const std::uint8_t* component = source + i * componentSize;
		switch (view.componentType)
		{
		case 5126:
		{
			float value = 0.0f;
			memcpy(&value, component, sizeof(value));
			if (!std::isfinite(value))
			{
				return false;
			}
			values[i] = value;
			break;
		}
		case 5121:
			values[i] = view.normalized
				? static_cast<float>(*component) / 255.0f
				: static_cast<float>(*component);
			break;
		case 5123:
		{
			std::uint16_t value = 0;
			memcpy(&value, component, sizeof(value));
			values[i] = view.normalized
				? static_cast<float>(value) / 65535.0f
				: static_cast<float>(value);
			break;
		}
		default:
			return false;
		}
	}
	outRgba[0] = values[0];
	outRgba[1] = values[1];
	outRgba[2] = values[2];
	outRgba[3] = values[3];
	return true;
}

static XMMATRIX ReadGltfNodeTransform(const json& node)
{
	if (node.contains("matrix") && node["matrix"].is_array() &&
		node["matrix"].size() == 16)
	{
		float m[16] = {};
		for (int i = 0; i < 16; ++i)
		{
			m[i] = node["matrix"][i].get<float>();
		}
		// glTFは列ベクトル・列優先、DirectXMathは行ベクトルで扱うため転置する。
		return XMMatrixSet(
			m[0], m[1], m[2], m[3],
			m[4], m[5], m[6], m[7],
			m[8], m[9], m[10], m[11],
			m[12], m[13], m[14], m[15]);
	}

	const json translation = node.value("translation", json::array({ 0.0f, 0.0f, 0.0f }));
	const json rotation = node.value("rotation", json::array({ 0.0f, 0.0f, 0.0f, 1.0f }));
	const json scale = node.value("scale", json::array({ 1.0f, 1.0f, 1.0f }));
	if (!translation.is_array() || translation.size() != 3 ||
		!rotation.is_array() || rotation.size() != 4 ||
		!scale.is_array() || scale.size() != 3)
	{
		return XMMatrixIdentity();
	}
	const XMVECTOR q = XMVectorSet(
		rotation[0].get<float>(),
		rotation[1].get<float>(),
		rotation[2].get<float>(),
		rotation[3].get<float>());
	return XMMatrixScaling(
		scale[0].get<float>(),
		scale[1].get<float>(),
		scale[2].get<float>())
		* XMMatrixRotationQuaternion(q)
		* XMMatrixTranslation(
			translation[0].get<float>(),
			translation[1].get<float>(),
			translation[2].get<float>());
}

static int ResolveGltfImageIndex(const json& root, int textureIndex)
{
	if (textureIndex < 0 || !root.contains("textures") ||
		!root["textures"].is_array() ||
		static_cast<std::size_t>(textureIndex) >= root["textures"].size())
	{
		return -1;
	}
	const json& texture = root["textures"][textureIndex];
	int source = texture.value("source", -1);
	if (source >= 0)
	{
		return source;
	}
	// Blender の標準 GLB は WebP を EXT_texture_webp.source にだけ書き、
	// コアの textures[].source を省略する。
	if (!texture.contains("extensions") || !texture["extensions"].is_object())
	{
		return -1;
	}
	const json& extensions = texture["extensions"];
	static const char* kTextureSourceExtensions[] = {
		"EXT_texture_webp",
		"KHR_texture_webp",
		"KHR_texture_basisu"
	};
	for (const char* name : kTextureSourceExtensions)
	{
		if (!extensions.contains(name) || !extensions[name].is_object())
		{
			continue;
		}
		source = extensions[name].value("source", -1);
		if (source >= 0)
		{
			return source;
		}
	}
	return -1;
}

static int GltfTextureInfoIndex(const json& textureInfo)
{
	if (!textureInfo.is_object())
	{
		return -1;
	}
	return textureInfo.value("index", -1);
}

static void FillPreparedMeshPbr(
	const json& root,
	const json& primitive,
	GlbPreparedMeshData* mesh)
{
	if (!mesh)
	{
		return;
	}
	mesh->diffuseColor = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	mesh->textureIndex = -1;
	mesh->metallicRoughnessIndex = -1;
	mesh->normalIndex = -1;
	mesh->emissiveIndex = -1;
	mesh->metallicFactor = 1.0f;
	mesh->roughnessFactor = 1.0f;
	const int materialIndex = primitive.value("material", -1);
	if (materialIndex < 0 || !root.contains("materials") ||
		!root["materials"].is_array() ||
		static_cast<std::size_t>(materialIndex) >= root["materials"].size())
	{
		return;
	}
	const json& material = root["materials"][materialIndex];
	if (material.contains("pbrMetallicRoughness") &&
		material["pbrMetallicRoughness"].is_object())
	{
		const json& pbr = material["pbrMetallicRoughness"];
		if (pbr.contains("baseColorFactor") &&
			pbr["baseColorFactor"].is_array() &&
			pbr["baseColorFactor"].size() >= 4)
		{
			mesh->diffuseColor = XMFLOAT4(
				pbr["baseColorFactor"][0].get<float>(),
				pbr["baseColorFactor"][1].get<float>(),
				pbr["baseColorFactor"][2].get<float>(),
				pbr["baseColorFactor"][3].get<float>());
		}
		if (pbr.contains("metallicFactor") && pbr["metallicFactor"].is_number())
		{
			mesh->metallicFactor = pbr["metallicFactor"].get<float>();
		}
		if (pbr.contains("roughnessFactor") && pbr["roughnessFactor"].is_number())
		{
			mesh->roughnessFactor = pbr["roughnessFactor"].get<float>();
		}
		mesh->textureIndex = ResolveGltfImageIndex(
			root, GltfTextureInfoIndex(pbr.value("baseColorTexture", json::object())));
		mesh->metallicRoughnessIndex = ResolveGltfImageIndex(
			root,
			GltfTextureInfoIndex(pbr.value("metallicRoughnessTexture", json::object())));
	}
	if (material.contains("normalTexture"))
	{
		mesh->normalIndex = ResolveGltfImageIndex(
			root, GltfTextureInfoIndex(material["normalTexture"]));
	}
	if (material.contains("emissiveTexture"))
	{
		mesh->emissiveIndex = ResolveGltfImageIndex(
			root, GltfTextureInfoIndex(material["emissiveTexture"]));
	}
}

static bool AppendPreparedPrimitive(
	const json& root,
	const std::vector<std::uint8_t>& bin,
	const json& primitive,
	const XMMATRIX& nodeTransform,
	GlbPreparedData* outData)
{
	if (!outData || !primitive.is_object() ||
		primitive.value("mode", 4) != 4 &&
		primitive.value("mode", 4) != 5 &&
		primitive.value("mode", 4) != 6)
	{
		return false;
	}
	const json& attributes = primitive.value("attributes", json::object());
	if (!attributes.is_object() || !attributes.contains("POSITION"))
	{
		return false;
	}
	const int positionAccessor = attributes["POSITION"].get<int>();
	GlbAccessorView positionView;
	if (!GetAccessorView(root, bin, positionAccessor, "VEC3", 3, &positionView))
	{
		return false;
	}
	const int normalAccessor = attributes.value("NORMAL", -1);
	const int uvAccessor = attributes.value("TEXCOORD_0", -1);
	const int colorAccessor = attributes.value("COLOR_0", -1);
	GlbAccessorView normalView;
	GlbAccessorView uvView;
	GlbAccessorView colorView;
	bool hasVertexColor = false;
	if (normalAccessor >= 0)
	{
		if (!GetAccessorView(root, bin, normalAccessor, "VEC3", 3, &normalView) ||
			normalView.count != positionView.count)
		{
			return false;
		}
	}
	if (uvAccessor >= 0)
	{
		if (!GetAccessorView(root, bin, uvAccessor, "VEC2", 2, &uvView) ||
			uvView.count != positionView.count)
		{
			return false;
		}
	}
	if (colorAccessor >= 0 &&
		root.contains("accessors") && root["accessors"].is_array() &&
		static_cast<std::size_t>(colorAccessor) < root["accessors"].size() &&
		root["accessors"][colorAccessor].is_object())
	{
		const std::string colorType =
			root["accessors"][colorAccessor].value("type", std::string());
		const std::size_t colorComponents = (colorType == "VEC3") ? 3u : 4u;
		if ((colorType == "VEC3" || colorType == "VEC4") &&
			GetAccessorView(
				root, bin, colorAccessor, colorType.c_str(), colorComponents, &colorView) &&
			colorView.count == positionView.count)
		{
			hasVertexColor = true;
		}
	}

	std::vector<std::uint32_t> sourceIndices;
	const int indexAccessor = primitive.value("indices", -1);
	if (indexAccessor >= 0)
	{
		GlbAccessorView indexView;
		if (!GetAccessorView(root, bin, indexAccessor, "SCALAR", 1, &indexView))
		{
			return false;
		}
		sourceIndices.resize(indexView.count);
		for (std::uint32_t i = 0; i < indexView.count; ++i)
		{
			if (!ReadIndexAccessorView(indexView, i, &sourceIndices[i]) ||
				sourceIndices[i] >= positionView.count)
			{
				return false;
			}
		}
	}
	else
	{
		sourceIndices.resize(positionView.count);
		for (std::uint32_t i = 0; i < positionView.count; ++i)
		{
			sourceIndices[i] = i;
		}
	}

	std::vector<std::uint32_t> triangles;
	if (primitive.value("mode", 4) == 4)
	{
		if (sourceIndices.size() % 3 != 0)
		{
			return false;
		}
		triangles = sourceIndices;
	}
	else if (primitive.value("mode", 4) == 5)
	{
		for (std::size_t i = 2; i < sourceIndices.size(); ++i)
		{
			if ((i & 1u) == 0)
			{
				triangles.insert(triangles.end(), {
					sourceIndices[i - 2], sourceIndices[i - 1], sourceIndices[i] });
			}
			else
			{
				triangles.insert(triangles.end(), {
					sourceIndices[i - 1], sourceIndices[i - 2], sourceIndices[i] });
			}
		}
	}
	else
	{
		for (std::size_t i = 2; i < sourceIndices.size(); ++i)
		{
			triangles.insert(triangles.end(), {
				sourceIndices[0], sourceIndices[i - 1], sourceIndices[i] });
		}
	}
	if (triangles.empty())
	{
		return true;
	}

	GlbPreparedMeshData mesh;
	mesh.vertices.resize(positionView.count);
	mesh.indices.reserve(triangles.size());
	FillPreparedMeshPbr(root, primitive, &mesh);
	const json extras = primitive.value("extras", json::object());
	if (extras.is_object() &&
		extras.contains("expo_batch_id") &&
		extras["expo_batch_id"].is_number_integer())
	{
		mesh.batchId = extras["expo_batch_id"].get<int>();
	}
	XMVECTOR determinant = {};
	const XMMATRIX inverseTranspose =
		XMMatrixTranspose(XMMatrixInverse(&determinant, nodeTransform));
	for (std::uint32_t v = 0; v < positionView.count; ++v)
	{
		float position[3] = {};
		if (!ReadFloatAccessorView(positionView, v, position))
		{
			return false;
		}
		const XMVECTOR transformed =
			XMVector3TransformCoord(XMVectorSet(position[0], position[1], position[2], 1.0f), nodeTransform);
		const XMFLOAT3 transformedPosition = {
			XMVectorGetX(transformed) * 100.0f,
			XMVectorGetY(transformed) * 100.0f,
			-XMVectorGetZ(transformed) * 100.0f
		};
		mesh.vertices[v].position = transformedPosition;
		if (!mesh.hasBounds)
		{
			mesh.boundsMin = transformedPosition;
			mesh.boundsMax = transformedPosition;
			mesh.hasBounds = true;
		}
		else
		{
			mesh.boundsMin.x = (std::min)(mesh.boundsMin.x, transformedPosition.x);
			mesh.boundsMin.y = (std::min)(mesh.boundsMin.y, transformedPosition.y);
			mesh.boundsMin.z = (std::min)(mesh.boundsMin.z, transformedPosition.z);
			mesh.boundsMax.x = (std::max)(mesh.boundsMax.x, transformedPosition.x);
			mesh.boundsMax.y = (std::max)(mesh.boundsMax.y, transformedPosition.y);
			mesh.boundsMax.z = (std::max)(mesh.boundsMax.z, transformedPosition.z);
		}

		float normal[3] = { 0.0f, 1.0f, 0.0f };
		if (normalAccessor >= 0 &&
			!ReadFloatAccessorView(normalView, v, normal))
		{
			return false;
		}
		const XMVECTOR transformedNormal = XMVector3Normalize(
			XMVector3TransformNormal(
				XMVectorSet(normal[0], normal[1], normal[2], 0.0f),
				inverseTranspose));
		mesh.vertices[v].normal = {
			XMVectorGetX(transformedNormal),
			XMVectorGetY(transformedNormal),
			-XMVectorGetZ(transformedNormal)
		};
		if (!std::isfinite(mesh.vertices[v].normal.x) ||
			!std::isfinite(mesh.vertices[v].normal.y) ||
			!std::isfinite(mesh.vertices[v].normal.z))
		{
			return false;
		}

		mesh.vertices[v].texCoord = XMFLOAT2(0.0f, 0.0f);
		if (uvAccessor >= 0)
		{
			float uv[2] = {};
			if (!ReadFloatAccessorView(uvView, v, uv))
			{
				return false;
			}
			// glTF の UV は DirectX のテクスチャ座標と同じ上端原点。
			// 直接デコード経路では Assimp の FlipUVs を通らないため、
			// V を反転せず、そのまま描画用頂点へ渡す。
			mesh.vertices[v].texCoord = XMFLOAT2(uv[0], uv[1]);
		}
		mesh.vertices[v].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		if (hasVertexColor)
		{
			float rgba[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			if (!ReadColorAccessorView(colorView, v, rgba))
			{
				return false;
			}
			mesh.vertices[v].color = XMFLOAT4(rgba[0], rgba[1], rgba[2], rgba[3]);
		}
	}

	for (std::size_t i = 0; i < triangles.size(); i += 3)
	{
		// Z反転で巻き方向が反転するため、Assimpの左手変換と同じ順序にする。
		mesh.indices.push_back(triangles[i + 0]);
		mesh.indices.push_back(triangles[i + 2]);
		mesh.indices.push_back(triangles[i + 1]);
	}
	for (std::uint32_t index : mesh.indices)
	{
		const XMFLOAT3& p = mesh.vertices[index].position;
		if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
		{
			return false;
		}
		if (!outData->hasBounds)
		{
			outData->boundsMin = p;
			outData->boundsMax = p;
			outData->hasBounds = true;
		}
		else
		{
			outData->boundsMin.x = (std::min)(outData->boundsMin.x, p.x);
			outData->boundsMin.y = (std::min)(outData->boundsMin.y, p.y);
			outData->boundsMin.z = (std::min)(outData->boundsMin.z, p.z);
			outData->boundsMax.x = (std::max)(outData->boundsMax.x, p.x);
			outData->boundsMax.y = (std::max)(outData->boundsMax.y, p.y);
			outData->boundsMax.z = (std::max)(outData->boundsMax.z, p.z);
		}
	}
	outData->meshes.push_back(std::move(mesh));
	return true;
}

static bool AppendPreparedMesh(
	const json& root,
	const std::vector<std::uint8_t>& bin,
	int meshIndex,
	const XMMATRIX& nodeTransform,
	GlbPreparedData* outData)
{
	if (!root.contains("meshes") || !root["meshes"].is_array() ||
		meshIndex < 0 || static_cast<std::size_t>(meshIndex) >= root["meshes"].size())
	{
		return false;
	}
	const json& mesh = root["meshes"][meshIndex];
	if (!mesh.contains("primitives") || !mesh["primitives"].is_array())
	{
		return false;
	}
	for (const json& primitive : mesh["primitives"])
	{
		if (!AppendPreparedPrimitive(root, bin, primitive, nodeTransform, outData))
		{
			return false;
		}
	}
	return true;
}

static bool WalkPreparedNodes(
	const json& root,
	const std::vector<std::uint8_t>& bin,
	int nodeIndex,
	const XMMATRIX& parentTransform,
	GlbPreparedData* outData)
{
	if (!root.contains("nodes") || !root["nodes"].is_array() ||
		nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= root["nodes"].size())
	{
		return false;
	}
	const json& node = root["nodes"][nodeIndex];
	const XMMATRIX world = ReadGltfNodeTransform(node) * parentTransform;
	if (node.contains("mesh"))
	{
		if (!AppendPreparedMesh(root, bin, node["mesh"].get<int>(), world, outData))
		{
			return false;
		}
	}
	if (node.contains("children") && node["children"].is_array())
	{
		for (const json& child : node["children"])
		{
			if (!WalkPreparedNodes(root, bin, child.get<int>(), world, outData))
			{
				return false;
			}
		}
	}
	return true;
}

static bool ExtractPreparedGlb(
	const std::vector<std::uint8_t>& bytes,
	GlbPreparedData* outData,
	bool skipTextures)
{
	const std::uint32_t GLB_MAGIC = 0x46546c67;
	const std::uint32_t JSON_CHUNK = 0x4e4f534a;
	const std::uint32_t BIN_CHUNK = 0x004e4942;
	if (!outData || bytes.size() < 20)
	{
		return false;
	}
	std::uint32_t magic = 0;
	std::uint32_t version = 0;
	std::uint32_t length = 0;
	memcpy(&magic, bytes.data(), sizeof(magic));
	memcpy(&version, bytes.data() + 4, sizeof(version));
	memcpy(&length, bytes.data() + 8, sizeof(length));
	if (magic != GLB_MAGIC || version != 2 || length > bytes.size())
	{
		return false;
	}

	std::size_t offset = 12;
	std::string jsonText;
	std::vector<std::uint8_t> bin;
	while (offset + 8 <= length)
	{
		std::uint32_t chunkLength = 0;
		std::uint32_t chunkType = 0;
		memcpy(&chunkLength, bytes.data() + offset, sizeof(chunkLength));
		memcpy(&chunkType, bytes.data() + offset + 4, sizeof(chunkType));
		offset += 8;
		if (chunkLength > length - offset)
		{
			return false;
		}
		if (chunkType == JSON_CHUNK)
		{
			jsonText.assign(
				reinterpret_cast<const char*>(bytes.data() + offset),
				chunkLength);
		}
		else if (chunkType == BIN_CHUNK && bin.empty())
		{
			bin.assign(bytes.begin() + offset, bytes.begin() + offset + chunkLength);
		}
		offset += chunkLength;
	}
	if (jsonText.empty())
	{
		return false;
	}

	const json root = json::parse(jsonText);
	if (!root.contains("asset") || root["asset"].value("version", std::string()) != "2.0" ||
		!root.contains("buffers") || !root["buffers"].is_array() ||
		root["buffers"].empty() ||
		root["buffers"][0].value("byteLength", 0u) > bin.size())
	{
		return false;
	}

	if (!skipTextures &&
		root.contains("images") && root["images"].is_array())
	{
		outData->textures.reserve(root["images"].size());
		for (const json& image : root["images"])
		{
			GlbPreparedTextureData texture;
			if (!image.contains("bufferView") ||
				!image["bufferView"].is_number_integer() ||
				!root.contains("bufferViews") || !root["bufferViews"].is_array())
			{
				return false;
			}
			const int viewIndex = image["bufferView"].get<int>();
			if (viewIndex < 0 || static_cast<std::size_t>(viewIndex) >= root["bufferViews"].size())
			{
				return false;
			}
			const json& view = root["bufferViews"][viewIndex];
			const std::size_t viewOffset = view.value("byteOffset", 0u);
			const std::size_t viewLength = view.value("byteLength", 0u);
			if (viewOffset > bin.size() || viewLength > bin.size() - viewOffset)
			{
				return false;
			}
			texture.bytes.assign(
				bin.begin() + viewOffset,
				bin.begin() + viewOffset + viewLength);
			texture.name = image.value("name", std::string());
			outData->textures.push_back(std::move(texture));
		}
	}

	bool walkedNodes = false;
	if (root.contains("scenes") && root["scenes"].is_array() &&
		!root["scenes"].empty())
	{
		const int sceneIndex = root.value("scene", 0);
		if (sceneIndex < 0 || static_cast<std::size_t>(sceneIndex) >= root["scenes"].size())
		{
			return false;
		}
		const json& scene = root["scenes"][sceneIndex];
		if (scene.contains("nodes") && scene["nodes"].is_array())
		{
			for (const json& node : scene["nodes"])
			{
				if (!WalkPreparedNodes(root, bin, node.get<int>(), XMMatrixIdentity(), outData))
				{
					return false;
				}
				walkedNodes = true;
			}
		}
	}
	if (!walkedNodes && root.contains("meshes") && root["meshes"].is_array())
	{
		for (std::size_t i = 0; i < root["meshes"].size(); ++i)
		{
			if (!AppendPreparedMesh(
				root, bin, static_cast<int>(i), XMMatrixIdentity(), outData))
			{
				return false;
			}
		}
	}
	if (outData->hasBounds)
	{
		outData->boundsCenter = XMFLOAT3(
			(outData->boundsMin.x + outData->boundsMax.x) * 0.5f,
			(outData->boundsMin.y + outData->boundsMax.y) * 0.5f,
			(outData->boundsMin.z + outData->boundsMax.z) * 0.5f);
	}
	return !outData->meshes.empty() && outData->hasBounds;
}

static void ResizeDecodedTextureIfNeeded(GlbDecodedTexture* decoded)
{
	if (!decoded || !decoded->ok)
	{
		return;
	}

	const size_t maxDimension = (std::max)(decoded->metadata.width, decoded->metadata.height);
	if (maxDimension <= GLB_MAX_TEXTURE_DIMENSION)
	{
		return;
	}

	const size_t width = (std::max)(
		static_cast<size_t>(1),
		decoded->metadata.width * GLB_MAX_TEXTURE_DIMENSION / maxDimension);
	const size_t height = (std::max)(
		static_cast<size_t>(1),
		decoded->metadata.height * GLB_MAX_TEXTURE_DIMENSION / maxDimension);

	ScratchImage resized;
	const HRESULT hr = Resize(
		decoded->image.GetImages(),
		decoded->image.GetImageCount(),
		decoded->metadata,
		width,
		height,
		TEX_FILTER_LINEAR,
		resized);
	if (SUCCEEDED(hr))
	{
		decoded->image = std::move(resized);
		decoded->metadata = decoded->image.GetMetadata();
	}
}

static void NormalizeDecodedTextureForUpload(GlbDecodedTexture* decoded)
{
	if (!decoded || !decoded->ok)
	{
		return;
	}
	const Image* image = decoded->image.GetImage(0, 0, 0);
	if (!image || image->format == DXGI_FORMAT_R8G8B8A8_UNORM)
	{
		return;
	}
	ScratchImage converted;
	if (SUCCEEDED(Convert(
		*image,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		TEX_FILTER_DEFAULT,
		TEX_THRESHOLD_DEFAULT,
		converted)))
	{
		decoded->image = std::move(converted);
		decoded->metadata = decoded->image.GetMetadata();
	}
}

static void GenerateMipMapsIfNeeded(
	GlbDecodedTexture* decoded,
	bool skipMipMaps)
{
	if (!decoded || !decoded->ok || skipMipMaps)
	{
		return;
	}
	const Image* image = decoded->image.GetImage(0, 0, 0);
	if (!image ||
		(image->width <= 1 && image->height <= 1) ||
		decoded->metadata.mipLevels > 1)
	{
		return;
	}

	ScratchImage mipChain;
	const HRESULT hr = GenerateMipMaps(
		decoded->image.GetImages(),
		decoded->image.GetImageCount(),
		decoded->metadata,
		TEX_FILTER_LINEAR,
		0,
		mipChain);
	if (SUCCEEDED(hr) && mipChain.GetImageCount() > 1)
	{
		decoded->image = std::move(mipChain);
		decoded->metadata = decoded->image.GetMetadata();
	}
}

static ID3D11ShaderResourceView* CreateRgbaTexture(
	ID3D11Device* pDevice, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	if (!pDevice)
	{
		return nullptr;
	}

	const UINT pixel =
		static_cast<UINT>(r) |
		(static_cast<UINT>(g) << 8) |
		(static_cast<UINT>(b) << 16) |
		(static_cast<UINT>(a) << 24);
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
	HRESULT hr = pDevice->CreateTexture2D(&desc, &data, &texture);
	if (FAILED(hr))
	{
		return nullptr;
	}

	ID3D11ShaderResourceView* srv = nullptr;
	hr = pDevice->CreateShaderResourceView(texture, nullptr, &srv);
	texture->Release();
	return SUCCEEDED(hr) ? srv : nullptr;
}

static ID3D11ShaderResourceView* CreateWhiteTexture(ID3D11Device* pDevice)
{
	return CreateRgbaTexture(pDevice, 255, 255, 255, 255);
}

//==============================================================================
// 拡張子判定ユーティリティ
//==============================================================================
static const char* GlbFileNameFromPath(const char* filePath)
{
	if (!filePath)
	{
		return nullptr;
	}
	const char* fileName = strrchr(filePath, '\\');
	if (!fileName)
	{
		fileName = strrchr(filePath, '/');
	}
	return fileName ? fileName + 1 : filePath;
}

static bool IsExpoFloorGlbPath(const char* filePath)
{
	const char* fileName = GlbFileNameFromPath(filePath);
	return fileName && _stricmp(fileName, "expo_floor.glb") == 0;
}

bool IsGlbFile(const char* filePath)
{
	if (!filePath) return false;
	size_t len = strlen(filePath);
	if (len < 4) return false;
	const char* ext = filePath + len - 4;
	return (_stricmp(ext, ".glb") == 0);
}

//==============================================================================
// コンストラクタ・デストラクタ
//==============================================================================
GlbModel::GlbModel()
{
}

GlbModel::~GlbModel()
{
	Release();
}

//==============================================================================
// Assimp インポート（ワーカー可。D3Dは触らない）
//==============================================================================
const aiScene* GlbModel::ImportSceneFile(const char* filePath, const GlbImportOptions* options)
{
	if (!filePath)
	{
		return nullptr;
	}

	DWORD dwAttrib = GetFileAttributesA(filePath);
	if (dwAttrib == INVALID_FILE_ATTRIBUTES || (dwAttrib & FILE_ATTRIBUTE_DIRECTORY))
	{
		return nullptr;
	}

	GlbImportOptions opts;
	if (options)
	{
		opts = *options;
	}

	std::lock_guard<std::mutex> assimpLock(Glb_GetAssimpMutex());
	aiPropertyStore* props = aiCreatePropertyStore();
	aiSetImportPropertyFloat(props, AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY, 100.0f);

	unsigned int pFlags =
		aiProcess_Triangulate |
		aiProcess_ConvertToLeftHanded |
		aiProcess_GlobalScale;
	if (opts.genNormals)
	{
		pFlags |= aiProcess_GenSmoothNormals;
	}
	if (opts.joinVertices)
	{
		pFlags |= aiProcess_JoinIdenticalVertices;
	}

	const aiScene* scene = aiImportFileExWithProperties(filePath, pFlags, nullptr, props);
	aiReleasePropertyStore(props);

	if (!scene || !scene->mRootNode)
	{
		return nullptr;
	}
	return scene;
}

GlbPreparedData* GlbModel::ImportPreparedFile(const char* filePath, bool skipTextures)
{
	if (!filePath)
	{
		return nullptr;
	}
	std::vector<std::uint8_t> bytes;
	if (!ReadBinaryFile(filePath, &bytes))
	{
		return nullptr;
	}

	std::unique_ptr<GlbPreparedData> data(new GlbPreparedData());
	// 直接デコードするGLBはすべてglTF PBRマテリアル経路で描画する。
	data->enablePreparedPbr = true;
	try
	{
		if (!ExtractPreparedGlb(bytes, data.get(), skipTextures))
		{
			return nullptr;
		}
	}
	catch (const std::exception&)
	{
		return nullptr;
	}
	return data.release();
}

bool GlbModel::ImportCollisionTriangles(
	const char* filePath,
	std::vector<XMFLOAT3>* outTriangleVertices)
{
	if (!outTriangleVertices)
	{
		return false;
	}
	outTriangleVertices->clear();
	GlbPreparedData* data = ImportPreparedFile(filePath, true);
	if (!data)
	{
		return false;
	}

	for (const GlbPreparedMeshData& mesh : data->meshes)
	{
		if (mesh.vertices.empty() || mesh.indices.size() < 3)
		{
			continue;
		}
		for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
		{
			const std::uint32_t ia = mesh.indices[i + 0];
			const std::uint32_t ib = mesh.indices[i + 1];
			const std::uint32_t ic = mesh.indices[i + 2];
			if (ia >= mesh.vertices.size() ||
				ib >= mesh.vertices.size() ||
				ic >= mesh.vertices.size())
			{
				continue;
			}
			outTriangleVertices->push_back(mesh.vertices[ia].position);
			outTriangleVertices->push_back(mesh.vertices[ib].position);
			outTriangleVertices->push_back(mesh.vertices[ic].position);
		}
	}
	delete data;
	return !outTriangleVertices->empty();
}

bool GlbModel::AttachImportedScene(const aiScene* scene)
{
	if (m_IsLoaded || m_pScene || m_pPreparedData)
	{
		return false;
	}
	if (!scene || !scene->mRootNode)
	{
		return false;
	}
	m_pScene = scene;
	// Assimp経路のGLB（プレイヤー機体など）も会場GLBと同じPBR経路に揃える。
	m_EnablePreparedPbr = true;
	m_GpuPhase = 0;
	m_GpuIndex = 0;
	m_IsLoaded = false;
	m_TexturesDecoded = false;
	m_DecodedTextures.clear();

	m_MeshNodeTransforms.assign(
		m_pScene->mNumMeshes,
		XMMatrixIdentity());
	const aiAnimation* animation =
		m_pScene->mNumAnimations > 0 ? m_pScene->mAnimations[0] : nullptr;
	BuildMeshNodeTransforms(
		m_pScene->mRootNode,
		XMMatrixIdentity(),
		animation,
		&m_MeshNodeTransforms);

	// ノード階層とアニメーションの0フレーム目を反映した境界を保存する。
	XMFLOAT3 minBounds(FLT_MAX, FLT_MAX, FLT_MAX);
	XMFLOAT3 maxBounds(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (unsigned int m = 0; m < m_pScene->mNumMeshes; ++m)
	{
		const aiMesh* mesh = m_pScene->mMeshes[m];
		if (!mesh) continue;
		const XMMATRIX& nodeTransform = m_MeshNodeTransforms[m];
		for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
		{
			const aiVector3D& position = mesh->mVertices[v];
			XMFLOAT3 transformedPosition;
			XMStoreFloat3(
				&transformedPosition,
				XMVector3TransformCoord(
					XMVectorSet(position.x, position.y, position.z, 1.0f),
					nodeTransform));
			minBounds.x = (std::min)(minBounds.x, transformedPosition.x);
			minBounds.y = (std::min)(minBounds.y, transformedPosition.y);
			minBounds.z = (std::min)(minBounds.z, transformedPosition.z);
			maxBounds.x = (std::max)(maxBounds.x, transformedPosition.x);
			maxBounds.y = (std::max)(maxBounds.y, transformedPosition.y);
			maxBounds.z = (std::max)(maxBounds.z, transformedPosition.z);
		}
	}
	if (minBounds.x != FLT_MAX)
	{
		m_BoundsMin = minBounds;
		m_BoundsMax = maxBounds;
		m_BoundsCenter = XMFLOAT3(
			(minBounds.x + maxBounds.x) * 0.5f,
			(minBounds.y + maxBounds.y) * 0.5f,
			(minBounds.z + maxBounds.z) * 0.5f);
	}
	return true;
}

bool GlbModel::AttachPreparedData(GlbPreparedData* data)
{
	std::unique_ptr<GlbPreparedData> ownedData(data);
	if (m_IsLoaded || m_pScene || m_pPreparedData || !ownedData ||
		!ownedData->hasBounds || ownedData->meshes.empty())
	{
		return false;
	}
	m_pPreparedData = std::move(ownedData);
	m_EnablePreparedPbr = m_pPreparedData->enablePreparedPbr;
	m_GpuPhase = 0;
	m_GpuIndex = 0;
	m_IsLoaded = false;
	m_TexturesDecoded = false;
	m_DecodedTextures.clear();
	m_Meshes.clear();
	m_BoundsMin = m_pPreparedData->boundsMin;
	m_BoundsMax = m_pPreparedData->boundsMax;
	m_BoundsCenter = m_pPreparedData->boundsCenter;
	return true;
}

static bool AppendPreparedMesh(
	GlbPreparedMeshData* destination,
	const GlbPreparedMeshData& source)
{
	if (!destination || source.vertices.empty() || source.indices.empty())
	{
		return true;
	}

	const std::size_t maxIndex =
		static_cast<std::size_t>((std::numeric_limits<unsigned int>::max)());
	const std::size_t vertexBase = destination->vertices.size();
	const std::size_t indexBase = destination->indices.size();
	if (vertexBase > maxIndex ||
		source.vertices.size() > maxIndex - vertexBase ||
		indexBase > maxIndex ||
		source.indices.size() > maxIndex - indexBase)
	{
		return false;
	}

	for (std::uint32_t index : source.indices)
	{
		if (index >= source.vertices.size() ||
			static_cast<std::size_t>(index) > maxIndex - vertexBase)
		{
			return false;
		}
		destination->indices.push_back(
			static_cast<std::uint32_t>(vertexBase) + index);
	}
	destination->vertices.insert(
		destination->vertices.end(),
		source.vertices.begin(),
		source.vertices.end());

	if (!destination->hasBounds && source.hasBounds)
	{
		destination->boundsMin = source.boundsMin;
		destination->boundsMax = source.boundsMax;
		destination->hasBounds = true;
	}
	else if (source.hasBounds)
	{
		destination->boundsMin.x =
			(std::min)(destination->boundsMin.x, source.boundsMin.x);
		destination->boundsMin.y =
			(std::min)(destination->boundsMin.y, source.boundsMin.y);
		destination->boundsMin.z =
			(std::min)(destination->boundsMin.z, source.boundsMin.z);
		destination->boundsMax.x =
			(std::max)(destination->boundsMax.x, source.boundsMax.x);
		destination->boundsMax.y =
			(std::max)(destination->boundsMax.y, source.boundsMax.y);
		destination->boundsMax.z =
			(std::max)(destination->boundsMax.z, source.boundsMax.z);
	}

	if (!source.batchRanges.empty())
	{
		for (const GlbBatchRange& sourceRange : source.batchRanges)
		{
			if (sourceRange.indexOffset > source.indices.size() ||
				sourceRange.indexCount >
					source.indices.size() - sourceRange.indexOffset)
			{
				continue;
			}
			GlbBatchRange destinationRange = sourceRange;
			destinationRange.indexOffset =
				static_cast<unsigned int>(indexBase) +
				sourceRange.indexOffset;
			destination->batchRanges.push_back(destinationRange);
		}
	}
	else
	{
		GlbBatchRange destinationRange;
		destinationRange.batchId = source.batchId;
		destinationRange.indexOffset =
			static_cast<unsigned int>(indexBase);
		destinationRange.indexCount =
			static_cast<unsigned int>(source.indices.size());
		destinationRange.boundsMin = source.boundsMin;
		destinationRange.boundsMax = source.boundsMax;
		destinationRange.hasBounds = source.hasBounds;
		destination->batchRanges.push_back(destinationRange);
	}
	return true;
}

static void CopyPreparedMaterial(
	GlbPreparedMeshData* destination,
	const GlbPreparedMeshData& source)
{
	if (!destination)
	{
		return;
	}
	destination->diffuseColor = source.diffuseColor;
	destination->textureIndex = source.textureIndex;
	destination->metallicRoughnessIndex = source.metallicRoughnessIndex;
	destination->normalIndex = source.normalIndex;
	destination->emissiveIndex = source.emissiveIndex;
	destination->metallicFactor = source.metallicFactor;
	destination->roughnessFactor = source.roughnessFactor;
}

void GlbModel::MergePreparedMeshesByMaterial(void)
{
	if (!m_pPreparedData || m_pPreparedData->meshes.size() < 2)
	{
		return;
	}

	auto sameMaterial = [](const GlbPreparedMeshData& left,
		const GlbPreparedMeshData& right) -> bool
	{
		if (left.textureIndex != right.textureIndex)
		{
			return false;
		}
		if (left.textureIndex >= 0)
		{
			return true;
		}
		return left.diffuseColor.x == right.diffuseColor.x &&
			left.diffuseColor.y == right.diffuseColor.y &&
			left.diffuseColor.z == right.diffuseColor.z &&
			left.diffuseColor.w == right.diffuseColor.w;
	};

	std::vector<GlbPreparedMeshData> merged;
	merged.reserve(m_pPreparedData->meshes.size());

	for (const GlbPreparedMeshData& source : m_pPreparedData->meshes)
	{
		if (source.vertices.empty() || source.indices.empty())
		{
			continue;
		}

		std::size_t targetIndex = merged.size();
		for (std::size_t i = 0; i < merged.size(); ++i)
		{
			if (sameMaterial(merged[i], source))
			{
				targetIndex = i;
				break;
			}
		}

		if (targetIndex == merged.size())
		{
			GlbPreparedMeshData destination;
			CopyPreparedMaterial(&destination, source);
			merged.push_back(std::move(destination));
		}

		if (!AppendPreparedMesh(&merged[targetIndex], source))
		{
			if (merged[targetIndex].vertices.empty() &&
				targetIndex + 1 == merged.size())
			{
				merged.pop_back();
			}
		}
	}

	if (!merged.empty())
	{
		m_pPreparedData->meshes = std::move(merged);
	}
}

void GlbModel::CollapsePreparedMeshes(unsigned int maxMeshes)
{
	if (!m_pPreparedData || maxMeshes == 0)
	{
		return;
	}

	std::vector<GlbPreparedMeshData>& meshes = m_pPreparedData->meshes;
	if (meshes.size() <= maxMeshes)
	{
		return;
	}

	std::vector<std::size_t> order(meshes.size());
	for (std::size_t i = 0; i < order.size(); ++i)
	{
		order[i] = i;
	}
	std::sort(
		order.begin(),
		order.end(),
		[&meshes](std::size_t left, std::size_t right)
		{
			return meshes[left].vertices.size() > meshes[right].vertices.size();
		});

	std::vector<char> taken(meshes.size(), 0);
	std::vector<GlbPreparedMeshData> kept;
	kept.reserve(maxMeshes);
	for (unsigned int i = 0; i < maxMeshes && i < order.size(); ++i)
	{
		taken[order[i]] = 1;
		kept.push_back(std::move(meshes[order[i]]));
	}
	if (kept.empty())
	{
		return;
	}
	for (std::size_t i = 0; i < meshes.size(); ++i)
	{
		if (taken[i])
		{
			continue;
		}
		AppendPreparedMesh(&kept[0], meshes[i]);
	}
	meshes = std::move(kept);
}

void GlbModel::PrepareShadowCells(float modelSpaceCellSize)
{
	if (!m_pPreparedData || modelSpaceCellSize <= 0.0f)
	{
		return;
	}

	const auto cellOf = [modelSpaceCellSize](float value) -> int
	{
		return static_cast<int>(floorf(value / modelSpaceCellSize));
	};
	const auto packKey = [](int x, int z) -> std::uint64_t
	{
		return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
			static_cast<std::uint32_t>(z);
	};

	struct CellAcc
	{
		XMFLOAT3 aabbMin = {};
		XMFLOAT3 aabbMax = {};
		std::vector<std::uint32_t> indices;
		int cellX = 0;
		int cellZ = 0;
		bool has = false;
	};

	for (GlbPreparedMeshData& mesh : m_pPreparedData->meshes)
	{
		mesh.shadowIndices.clear();
		mesh.shadowCells.clear();
		if (mesh.indices.size() < 3 || mesh.vertices.empty())
		{
			continue;
		}

		std::unordered_map<std::uint64_t, CellAcc> cells;
		for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
		{
			const std::uint32_t i0 = mesh.indices[i];
			const std::uint32_t i1 = mesh.indices[i + 1];
			const std::uint32_t i2 = mesh.indices[i + 2];
			if (i0 >= mesh.vertices.size() ||
				i1 >= mesh.vertices.size() ||
				i2 >= mesh.vertices.size())
			{
				continue;
			}
			const XMFLOAT3& p0 = mesh.vertices[i0].position;
			const XMFLOAT3& p1 = mesh.vertices[i1].position;
			const XMFLOAT3& p2 = mesh.vertices[i2].position;
			const float cx = (p0.x + p1.x + p2.x) / 3.0f;
			const float cz = (p0.z + p1.z + p2.z) / 3.0f;
			const int cellX = cellOf(cx);
			const int cellZ = cellOf(cz);
			CellAcc& cell = cells[packKey(cellX, cellZ)];
			if (!cell.has)
			{
				cell.cellX = cellX;
				cell.cellZ = cellZ;
			}
			const XMFLOAT3 triMin = {
				(std::min)(p0.x, (std::min)(p1.x, p2.x)),
				(std::min)(p0.y, (std::min)(p1.y, p2.y)),
				(std::min)(p0.z, (std::min)(p1.z, p2.z))
			};
			const XMFLOAT3 triMax = {
				(std::max)(p0.x, (std::max)(p1.x, p2.x)),
				(std::max)(p0.y, (std::max)(p1.y, p2.y)),
				(std::max)(p0.z, (std::max)(p1.z, p2.z))
			};
			if (!cell.has)
			{
				cell.aabbMin = triMin;
				cell.aabbMax = triMax;
				cell.has = true;
			}
			else
			{
				cell.aabbMin.x = (std::min)(cell.aabbMin.x, triMin.x);
				cell.aabbMin.y = (std::min)(cell.aabbMin.y, triMin.y);
				cell.aabbMin.z = (std::min)(cell.aabbMin.z, triMin.z);
				cell.aabbMax.x = (std::max)(cell.aabbMax.x, triMax.x);
				cell.aabbMax.y = (std::max)(cell.aabbMax.y, triMax.y);
				cell.aabbMax.z = (std::max)(cell.aabbMax.z, triMax.z);
			}
			cell.indices.push_back(i0);
			cell.indices.push_back(i1);
			cell.indices.push_back(i2);
		}

		mesh.shadowIndices.reserve(mesh.indices.size());
		std::vector<CellAcc*> sortedCells;
		sortedCells.reserve(cells.size());
		for (auto& entry : cells)
		{
			sortedCells.push_back(&entry.second);
		}
		std::sort(
			sortedCells.begin(),
			sortedCells.end(),
			[](const CellAcc* left, const CellAcc* right)
			{
				if (left->cellZ != right->cellZ)
				{
					return left->cellZ < right->cellZ;
				}
				return left->cellX < right->cellX;
			});
		for (CellAcc* cell : sortedCells)
		{
			if (!cell || !cell->has || cell->indices.empty())
			{
				continue;
			}
			GlbShadowCellPrep prep;
			prep.aabbMin = cell->aabbMin;
			prep.aabbMax = cell->aabbMax;
			prep.indexOffset = static_cast<std::uint32_t>(mesh.shadowIndices.size());
			prep.indexCount = static_cast<std::uint32_t>(cell->indices.size());
			mesh.shadowIndices.insert(
				mesh.shadowIndices.end(), cell->indices.begin(), cell->indices.end());
			mesh.shadowCells.push_back(prep);
		}
	}
	BuildCombinedShadowGeometry();
}

void GlbModel::BuildCombinedShadowGeometry(void)
{
	if (!m_pPreparedData || m_pPreparedData->meshes.empty())
	{
		return;
	}

	m_pPreparedData->combinedShadowVertices.clear();
	m_pPreparedData->combinedShadowIndices.clear();
	const std::size_t maxIndex =
		static_cast<std::size_t>((std::numeric_limits<unsigned int>::max)());
	for (const GlbPreparedMeshData& mesh : m_pPreparedData->meshes)
	{
		if (mesh.vertices.empty() || mesh.indices.size() < 3)
		{
			continue;
		}
		const std::size_t vertexBase = m_pPreparedData->combinedShadowVertices.size();
		if (vertexBase > maxIndex ||
			mesh.vertices.size() > maxIndex - vertexBase)
		{
			break;
		}
		for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
		{
			const std::uint32_t i0 = mesh.indices[i];
			const std::uint32_t i1 = mesh.indices[i + 1];
			const std::uint32_t i2 = mesh.indices[i + 2];
			if (i0 >= mesh.vertices.size() ||
				i1 >= mesh.vertices.size() ||
				i2 >= mesh.vertices.size() ||
				static_cast<std::size_t>(i0) > maxIndex - vertexBase ||
				static_cast<std::size_t>(i1) > maxIndex - vertexBase ||
				static_cast<std::size_t>(i2) > maxIndex - vertexBase)
			{
				continue;
			}
			m_pPreparedData->combinedShadowIndices.push_back(
				static_cast<std::uint32_t>(vertexBase) + i0);
			m_pPreparedData->combinedShadowIndices.push_back(
				static_cast<std::uint32_t>(vertexBase) + i1);
			m_pPreparedData->combinedShadowIndices.push_back(
				static_cast<std::uint32_t>(vertexBase) + i2);
		}
		m_pPreparedData->combinedShadowVertices.insert(
			m_pPreparedData->combinedShadowVertices.end(),
			mesh.vertices.begin(),
			mesh.vertices.end());
	}
	if (m_pPreparedData->combinedShadowIndices.size() < 3)
	{
		m_pPreparedData->combinedShadowVertices.clear();
		m_pPreparedData->combinedShadowIndices.clear();
	}
}

int GlbModel::PumpCombinedShadow(ID3D11Device* pDevice)
{
	if (!m_pPreparedData ||
		m_pPreparedData->combinedShadowVertices.empty() ||
		m_pPreparedData->combinedShadowIndices.empty() ||
		!pDevice)
	{
		return 1;
	}

	const std::size_t vertexBytes =
		m_pPreparedData->combinedShadowVertices.size() * sizeof(Vertex3D);
	const std::size_t indexBytes =
		m_pPreparedData->combinedShadowIndices.size() * sizeof(std::uint32_t);
	if (vertexBytes == 0 ||
		indexBytes == 0 ||
		vertexBytes > static_cast<std::size_t>((std::numeric_limits<UINT>::max)()) ||
		indexBytes > static_cast<std::size_t>((std::numeric_limits<UINT>::max)()) ||
		m_pPreparedData->combinedShadowIndices.size() >
			static_cast<std::size_t>((std::numeric_limits<unsigned int>::max)()))
	{
		return 1;
	}

	auto skipCombined = [this]() -> int
	{
		SAFE_RELEASE(m_pCombinedShadowVertexBuffer);
		SAFE_RELEASE(m_pCombinedShadowIndexBuffer);
		m_CombinedShadowIndexCount = 0;
		m_CombinedShadowVertexUploadOffset = 0;
		m_CombinedShadowIndexUploadOffset = 0;
		return 1;
	};

	if (!m_pCombinedShadowVertexBuffer)
	{
		D3D11_BUFFER_DESC desc = {};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.ByteWidth = static_cast<UINT>(vertexBytes);
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		D3D11_SUBRESOURCE_DATA data = {};
		data.pSysMem = m_pPreparedData->combinedShadowVertices.data();
		if (FAILED(pDevice->CreateBuffer(
			&desc, &data, &m_pCombinedShadowVertexBuffer)))
		{
			return skipCombined();
		}
		m_CombinedShadowVertexUploadOffset = vertexBytes;
	}

	if (!m_pCombinedShadowIndexBuffer)
	{
		D3D11_BUFFER_DESC desc = {};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.ByteWidth = static_cast<UINT>(indexBytes);
		desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		D3D11_SUBRESOURCE_DATA data = {};
		data.pSysMem = m_pPreparedData->combinedShadowIndices.data();
		if (FAILED(pDevice->CreateBuffer(
			&desc, &data, &m_pCombinedShadowIndexBuffer)))
		{
			return skipCombined();
		}
		m_CombinedShadowIndexUploadOffset = indexBytes;
	}

	m_CombinedShadowIndexCount = static_cast<unsigned int>(
		m_pPreparedData->combinedShadowIndices.size());
	return 1;
}

void GlbModel::GetGpuProgress(unsigned int* done, unsigned int* total) const
{
	unsigned int tex = 0;
	unsigned int mesh = 0;
	if (m_pScene)
	{
		tex = m_pScene->mNumTextures;
		mesh = m_pScene->mNumMeshes;
	}
	else if (m_pPreparedData)
	{
		tex = static_cast<unsigned int>(m_pPreparedData->textures.size());
		mesh = static_cast<unsigned int>(m_pPreparedData->meshes.size());
	}
	const unsigned int tot = tex + 1u + mesh + 1u;
	if (total)
	{
		*total = tot;
	}
	unsigned int d = 0;
	if (m_GpuPhase <= 0)
	{
		d = m_GpuIndex;
	}
	else if (m_GpuPhase == 1)
	{
		d = tex;
	}
	else if (m_GpuPhase == 2)
	{
		d = tex + 1u;
	}
	else if (m_GpuPhase == 3)
	{
		d = tex + 1u + m_GpuIndex;
	}
	else
	{
		d = tot;
	}
	if (done)
	{
		*done = d;
	}
}

int GlbModel::PumpGpu(ID3D11Device* pDevice, int itemBudget)
{
	if ((!m_pScene && !m_pPreparedData) || !pDevice || m_IsLoaded)
	{
		return m_IsLoaded ? 1 : -1;
	}
	if (itemBudget < 1)
	{
		itemBudget = 1;
	}

	while (itemBudget > 0)
	{
		if (m_GpuPhase == 0)
		{
			const unsigned int textureCount = m_pScene
				? m_pScene->mNumTextures
				: static_cast<unsigned int>(m_pPreparedData->textures.size());
			if (m_GpuIndex >= textureCount)
			{
				m_GpuPhase = 1;
				continue;
			}
			if (m_TexturesDecoded)
			{
				const int textureResult = CreateSrvFromDecoded(m_GpuIndex, pDevice);
				if (textureResult < 0)
				{
					Release();
					return -1;
				}
				if (textureResult == 0)
				{
					return 0;
				}
			}
			else
			{
				if (!LoadOneEmbeddedTexture(m_GpuIndex, pDevice))
				{
					Release();
					return -1;
				}
			}
			m_GpuIndex += 1;
			itemBudget -= 1;
			continue;
		}
		if (m_GpuPhase == 1)
		{
			if (!m_pWhiteTexture)
			{
				m_pWhiteTexture = CreateWhiteTexture(pDevice);
			}
			if (!m_pBlackTexture)
			{
				m_pBlackTexture = CreateRgbaTexture(pDevice, 0, 0, 0, 255);
			}
			if (!m_pFlatNormalTexture)
			{
				m_pFlatNormalTexture = CreateRgbaTexture(pDevice, 128, 128, 255, 255);
			}
			if (!m_pWhiteTexture || !m_pBlackTexture || !m_pFlatNormalTexture)
			{
				Release();
				return -1;
			}
			m_GpuPhase = 2;
			m_GpuIndex = 0;
			itemBudget -= 1;
			continue;
		}
		if (m_GpuPhase == 2)
		{
			const unsigned int meshCount = m_pScene
				? m_pScene->mNumMeshes
				: static_cast<unsigned int>(m_pPreparedData->meshes.size());
			m_Meshes.assign(meshCount, GlbMesh{});
			m_GpuPhase = 3;
			m_GpuIndex = 0;
			continue;
		}
		if (m_GpuPhase == 3)
		{
			const unsigned int meshCount = m_pScene
				? m_pScene->mNumMeshes
				: static_cast<unsigned int>(m_pPreparedData->meshes.size());
			if (m_GpuIndex >= meshCount)
			{
				m_GpuPhase = 4;
				continue;
			}
			const int meshResult = ProcessOneMesh(m_GpuIndex, pDevice);
			if (meshResult < 0)
			{
				Release();
				return -1;
			}
			if (meshResult > 0)
			{
				m_GpuIndex += 1;
				itemBudget -= 1;
			}
			else
			{
				// 大きなメッシュは1回のPumpで全量をCreateBufferへ渡さず、
				// 次のフレームへ転送カーソルを持ち越す。
				return 0;
			}
			continue;
		}
		if (m_GpuPhase == 4)
		{
			PumpCombinedShadow(pDevice);
			m_GpuPhase = 5;
			continue;
		}
		if (m_GpuPhase == 5)
		{
			if (m_pScene)
			{
				SetupMeshMaterials(m_pScene);
				// 静的GLBの描画はGPUバッファとSRVだけで成立する。
				// Assimpシーンを常駐させると、GPU側と頂点・埋め込み画像が二重保持される。
				aiReleaseImport(m_pScene);
				m_pScene = nullptr;
			}
			else
			{
				SetupPreparedMeshMaterials();
				m_pPreparedData.reset();
			}
			m_DecodedTextures.clear();
			m_TexturesDecoded = false;
			m_IsLoaded = true;
			return 1;
		}
		return -1;
	}
	return 0;
}

//==============================================================================
// モデル読み込み
//==============================================================================
bool GlbModel::Load(const char* filePath, ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
	(void)pContext;
	if (m_IsLoaded)
	{
		return false;
	}

	const aiScene* scene = ImportSceneFile(filePath);
	if (!AttachImportedScene(scene))
	{
		if (scene)
		{
			aiReleaseImport(scene);
		}
		return false;
	}
	if (!DecodeEmbeddedTextures(IsExpoFloorGlbPath(filePath)))
	{
		Release();
		return false;
	}

	int result = 0;
	do
	{
		result = PumpGpu(pDevice, 64);
	} while (result == 0);

	return result == 1;
}

//==============================================================================
// リソース解放
//==============================================================================
void GlbModel::Release()
{
	for (auto& mesh : m_Meshes)
	{
		SAFE_RELEASE(mesh.pVertexBuffer);
		SAFE_RELEASE(mesh.pIndexBuffer);
		SAFE_RELEASE(mesh.pShadowIndexBuffer);
		SAFE_RELEASE(mesh.pVisibleIndexBuffer);
		mesh.sourceIndices.clear();
		mesh.shadowCells.clear();
		mesh.batchRanges.clear();
		mesh.visibleIndexCount = 0;
	}
	m_Meshes.clear();
	m_MeshNodeTransforms.clear();

	for (auto& pair : m_EmbeddedTextures)
	{
		SAFE_RELEASE(pair.second);
	}
	m_EmbeddedTextures.clear();
	m_HiddenBatchIds.clear();

	SAFE_RELEASE(m_pWhiteTexture);
	SAFE_RELEASE(m_pBlackTexture);
	SAFE_RELEASE(m_pFlatNormalTexture);
	SAFE_RELEASE(m_pCombinedShadowVertexBuffer);
	SAFE_RELEASE(m_pCombinedShadowIndexBuffer);
	m_CombinedShadowIndexCount = 0;
	m_CombinedShadowVertexUploadOffset = 0;
	m_CombinedShadowIndexUploadOffset = 0;
	m_EnablePreparedPbr = false;
	for (const std::unique_ptr<GlbDecodedTexture>& decoded : m_DecodedTextures)
	{
		if (decoded)
		{
			SAFE_RELEASE(decoded->pGpuTexture);
		}
	}
	m_DecodedTextures.clear();
	m_TexturesDecoded = false;

	if (m_pScene)
	{
		aiReleaseImport(m_pScene);
		m_pScene = nullptr;
	}
	m_pPreparedData.reset();

	m_IsLoaded = false;
	m_ReceiveShadow = false;
	m_PhotoAlbedo = false;
	m_MirrorEnv = false;
	m_MainPassCellCulling = false;
	m_ShadowUseCells = true;
	m_GpuPhase = 0;
	m_GpuIndex = 0;
	m_BoundsMin = XMFLOAT3(0.0f, 0.0f, 0.0f);
	m_BoundsMax = XMFLOAT3(0.0f, 0.0f, 0.0f);
	m_BoundsCenter = XMFLOAT3(0.0f, 0.0f, 0.0f);
}

void GlbModel::ClearHiddenBatchIds(void)
{
	if (m_HiddenBatchIds.empty())
	{
		return;
	}
	m_HiddenBatchIds.clear();
	for (GlbMesh& mesh : m_Meshes)
	{
		RebuildVisibleIndexBuffer(mesh);
	}
}

void GlbModel::HideBatchId(int batchId)
{
	if (batchId < 0)
	{
		return;
	}
	if (!m_HiddenBatchIds.insert(batchId).second)
	{
		return;
	}
	for (GlbMesh& mesh : m_Meshes)
	{
		RebuildVisibleIndexBuffer(mesh);
	}
}

void GlbModel::SetHiddenBatchIds(const std::unordered_set<int>& batchIds)
{
	if (batchIds == m_HiddenBatchIds)
	{
		return;
	}
	m_HiddenBatchIds = batchIds;
	for (GlbMesh& mesh : m_Meshes)
	{
		RebuildVisibleIndexBuffer(mesh);
	}
}

void GlbModel::RebuildVisibleIndexBuffer(GlbMesh& mesh)
{
	SAFE_RELEASE(mesh.pVisibleIndexBuffer);
	mesh.visibleIndexCount = 0;
	if (m_HiddenBatchIds.empty() ||
		mesh.sourceIndices.empty() ||
		mesh.batchRanges.empty())
	{
		return;
	}

	std::vector<std::uint32_t> compact;
	compact.reserve(mesh.sourceIndices.size());
	for (const GlbBatchRange& range : mesh.batchRanges)
	{
		if (range.indexCount == 0)
		{
			continue;
		}
		if (range.batchId >= 0 &&
			m_HiddenBatchIds.find(range.batchId) != m_HiddenBatchIds.end())
		{
			continue;
		}
		if (range.indexOffset > mesh.sourceIndices.size() ||
			range.indexCount > mesh.sourceIndices.size() - range.indexOffset)
		{
			continue;
		}
		compact.insert(
			compact.end(),
			mesh.sourceIndices.begin() + range.indexOffset,
			mesh.sourceIndices.begin() + range.indexOffset + range.indexCount);
	}
	if (compact.empty())
	{
		return;
	}
	if (compact.size() >
		static_cast<std::size_t>((std::numeric_limits<unsigned int>::max)()))
	{
		return;
	}

	ID3D11Device* pDevice = GetDevice();
	if (!pDevice)
	{
		return;
	}
	D3D11_BUFFER_DESC desc = {};
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.ByteWidth = static_cast<UINT>(compact.size() * sizeof(std::uint32_t));
	desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	D3D11_SUBRESOURCE_DATA data = {};
	data.pSysMem = compact.data();
	if (FAILED(pDevice->CreateBuffer(&desc, &data, &mesh.pVisibleIndexBuffer)))
	{
		mesh.pVisibleIndexBuffer = nullptr;
		return;
	}
	mesh.visibleIndexCount = static_cast<unsigned int>(compact.size());
}

//==============================================================================
// メッシュデータの抽出 + DX11バッファ生成
//==============================================================================
int GlbModel::ProcessOneMesh(unsigned int meshIndex, ID3D11Device* pDevice)
{
	if (m_pPreparedData)
	{
		if (meshIndex >= m_pPreparedData->meshes.size() ||
			meshIndex >= m_Meshes.size() || !pDevice || !GetDeviceContext())
		{
			return -1;
		}
		const GlbPreparedMeshData& source = m_pPreparedData->meshes[meshIndex];
		GlbMesh& glbMesh = m_Meshes[meshIndex];
		glbMesh.diffuseColor = source.diffuseColor;
		glbMesh.batchId = source.batchId;
		glbMesh.batchRanges = source.batchRanges;
		if (!source.batchRanges.empty() && glbMesh.sourceIndices.empty())
		{
			glbMesh.sourceIndices = source.indices;
		}
		glbMesh.boundsMin = source.boundsMin;
		glbMesh.boundsMax = source.boundsMax;
		glbMesh.hasBounds = source.hasBounds;
		if (source.indices.size() >
			static_cast<std::size_t>((std::numeric_limits<unsigned int>::max)()))
		{
			return -1;
		}
		glbMesh.indexCount = static_cast<unsigned int>(source.indices.size());
		const std::size_t vertexBytes = source.vertices.size() * sizeof(Vertex3D);
		const std::size_t indexBytes = source.indices.size() * sizeof(std::uint32_t);
		if (vertexBytes > static_cast<std::size_t>((std::numeric_limits<UINT>::max)()) ||
			indexBytes > static_cast<std::size_t>((std::numeric_limits<UINT>::max)()))
		{
			return -1;
		}

		const std::size_t chunkBytes = 256u * 1024u;
		if (vertexBytes > 0 && !glbMesh.pVertexBuffer)
		{
			D3D11_BUFFER_DESC desc = {};
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.ByteWidth = static_cast<UINT>(vertexBytes);
			desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			if (FAILED(pDevice->CreateBuffer(&desc, nullptr, &glbMesh.pVertexBuffer)))
			{
				return -1;
			}
		}
		if (glbMesh.vertexUploadOffset < vertexBytes)
		{
			const std::size_t remaining = vertexBytes - glbMesh.vertexUploadOffset;
			const std::size_t bytes = (std::min)(chunkBytes, remaining);
			D3D11_BOX box = {};
			box.left = static_cast<UINT>(glbMesh.vertexUploadOffset);
			box.right = static_cast<UINT>(glbMesh.vertexUploadOffset + bytes);
			box.top = 0;
			box.bottom = 1;
			box.front = 0;
			box.back = 1;
			GetDeviceContext()->UpdateSubresource(
				glbMesh.pVertexBuffer,
				0,
				&box,
				reinterpret_cast<const std::uint8_t*>(source.vertices.data()) +
					glbMesh.vertexUploadOffset,
				0,
				0);
			glbMesh.vertexUploadOffset += bytes;
			return 0;
		}

		if (indexBytes > 0 && !glbMesh.pIndexBuffer)
		{
			D3D11_BUFFER_DESC desc = {};
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.ByteWidth = static_cast<UINT>(indexBytes);
			desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
			if (FAILED(pDevice->CreateBuffer(&desc, nullptr, &glbMesh.pIndexBuffer)))
			{
				return -1;
			}
		}
		if (glbMesh.indexUploadOffset < indexBytes)
		{
			const std::size_t remaining = indexBytes - glbMesh.indexUploadOffset;
			const std::size_t bytes = (std::min)(chunkBytes, remaining);
			D3D11_BOX box = {};
			box.left = static_cast<UINT>(glbMesh.indexUploadOffset);
			box.right = static_cast<UINT>(glbMesh.indexUploadOffset + bytes);
			box.top = 0;
			box.bottom = 1;
			box.front = 0;
			box.back = 1;
			GetDeviceContext()->UpdateSubresource(
				glbMesh.pIndexBuffer,
				0,
				&box,
				reinterpret_cast<const std::uint8_t*>(source.indices.data()) +
					glbMesh.indexUploadOffset,
				0,
				0);
			glbMesh.indexUploadOffset += bytes;
			return 0;
		}

		if (!source.shadowIndices.empty())
		{
			if (source.shadowIndices.size() >
				static_cast<std::size_t>((std::numeric_limits<unsigned int>::max)()))
			{
				return -1;
			}
			glbMesh.shadowIndexCount =
				static_cast<unsigned int>(source.shadowIndices.size());
			const std::size_t shadowBytes =
				source.shadowIndices.size() * sizeof(std::uint32_t);
			if (shadowBytes > static_cast<std::size_t>((std::numeric_limits<UINT>::max)()))
			{
				return -1;
			}
			if (!glbMesh.pShadowIndexBuffer)
			{
				D3D11_BUFFER_DESC desc = {};
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.ByteWidth = static_cast<UINT>(shadowBytes);
				desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
				if (FAILED(pDevice->CreateBuffer(&desc, nullptr, &glbMesh.pShadowIndexBuffer)))
				{
					return -1;
				}
				glbMesh.shadowCells.clear();
				glbMesh.shadowCells.reserve(source.shadowCells.size());
				for (const GlbShadowCellPrep& prep : source.shadowCells)
				{
					GlbShadowCell cell;
					cell.aabbMin = prep.aabbMin;
					cell.aabbMax = prep.aabbMax;
					cell.indexOffset = prep.indexOffset;
					cell.indexCount = prep.indexCount;
					glbMesh.shadowCells.push_back(cell);
				}
			}
			if (glbMesh.shadowIndexUploadOffset < shadowBytes)
			{
				const std::size_t remaining = shadowBytes - glbMesh.shadowIndexUploadOffset;
				const std::size_t bytes = (std::min)(chunkBytes, remaining);
				D3D11_BOX box = {};
				box.left = static_cast<UINT>(glbMesh.shadowIndexUploadOffset);
				box.right = static_cast<UINT>(glbMesh.shadowIndexUploadOffset + bytes);
				box.top = 0;
				box.bottom = 1;
				box.front = 0;
				box.back = 1;
				GetDeviceContext()->UpdateSubresource(
					glbMesh.pShadowIndexBuffer,
					0,
					&box,
					reinterpret_cast<const std::uint8_t*>(source.shadowIndices.data()) +
						glbMesh.shadowIndexUploadOffset,
					0,
					0);
				glbMesh.shadowIndexUploadOffset += bytes;
				return 0;
			}
		}
		return 1;
	}

	if (!m_pScene || meshIndex >= m_pScene->mNumMeshes || meshIndex >= m_Meshes.size())
	{
		return -1;
	}

	aiMesh* pMesh = m_pScene->mMeshes[meshIndex];
	if (!pMesh)
	{
		return 1;
	}
	GlbMesh& glbMesh = m_Meshes[meshIndex];
	const XMMATRIX& nodeTransform = m_MeshNodeTransforms[meshIndex];
	XMVECTOR determinant;
	const XMMATRIX normalTransform =
		XMMatrixTranspose(XMMatrixInverse(&determinant, nodeTransform));

	std::vector<Vertex3D> vertices(pMesh->mNumVertices);

		for (unsigned int v = 0; v < pMesh->mNumVertices; v++)
		{
			XMStoreFloat3(
				&vertices[v].position,
				XMVector3TransformCoord(
					XMVectorSet(
						pMesh->mVertices[v].x,
						pMesh->mVertices[v].y,
						pMesh->mVertices[v].z,
						1.0f),
					nodeTransform));

			if (pMesh->HasNormals())
			{
				XMStoreFloat3(
					&vertices[v].normal,
					XMVector3Normalize(
						XMVector3TransformNormal(
							XMVectorSet(
								pMesh->mNormals[v].x,
								pMesh->mNormals[v].y,
								pMesh->mNormals[v].z,
								0.0f),
							normalTransform)));
			}
			else
			{
				vertices[v].normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
			}

			if (pMesh->HasTextureCoords(0))
			{
				vertices[v].texCoord = XMFLOAT2(
					pMesh->mTextureCoords[0][v].x,
					pMesh->mTextureCoords[0][v].y
				);
			}
			else
			{
				vertices[v].texCoord = XMFLOAT2(0.0f, 0.0f);
			}

			if (pMesh->HasVertexColors(0))
			{
				vertices[v].color = XMFLOAT4(
					pMesh->mColors[0][v].r,
					pMesh->mColors[0][v].g,
					pMesh->mColors[0][v].b,
					pMesh->mColors[0][v].a
				);
			}
			else
			{
				vertices[v].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
			}
		}

		if (!vertices.empty())
		{
			glbMesh.boundsMin = vertices[0].position;
			glbMesh.boundsMax = vertices[0].position;
			glbMesh.hasBounds = true;
			for (std::size_t v = 1; v < vertices.size(); ++v)
			{
				const XMFLOAT3& position = vertices[v].position;
				glbMesh.boundsMin.x = (std::min)(glbMesh.boundsMin.x, position.x);
				glbMesh.boundsMin.y = (std::min)(glbMesh.boundsMin.y, position.y);
				glbMesh.boundsMin.z = (std::min)(glbMesh.boundsMin.z, position.z);
				glbMesh.boundsMax.x = (std::max)(glbMesh.boundsMax.x, position.x);
				glbMesh.boundsMax.y = (std::max)(glbMesh.boundsMax.y, position.y);
				glbMesh.boundsMax.z = (std::max)(glbMesh.boundsMax.z, position.z);
			}
		}

		std::vector<unsigned int> indices;
		indices.reserve(pMesh->mNumFaces * 3);

		for (unsigned int f = 0; f < pMesh->mNumFaces; f++)
		{
			const aiFace& face = pMesh->mFaces[f];
			if (face.mNumIndices == 3)
			{
				indices.push_back(face.mIndices[0]);
				indices.push_back(face.mIndices[1]);
				indices.push_back(face.mIndices[2]);
			}
		}

		glbMesh.indexCount = (unsigned int)indices.size();

		if (pMesh->mNumVertices == 0 || glbMesh.indexCount == 0)
		{
			return true;
		}

		{
			D3D11_BUFFER_DESC bd = {};
			bd.Usage = D3D11_USAGE_DEFAULT;
			bd.ByteWidth = sizeof(Vertex3D) * pMesh->mNumVertices;
			bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

			D3D11_SUBRESOURCE_DATA sd = {};
			sd.pSysMem = vertices.data();

			HRESULT hr = pDevice->CreateBuffer(&bd, &sd, &glbMesh.pVertexBuffer);
			if (FAILED(hr))
			{
				return -1;
			}
		}

		{
			D3D11_BUFFER_DESC bd = {};
			bd.Usage = D3D11_USAGE_DEFAULT;
			bd.ByteWidth = sizeof(unsigned int) * glbMesh.indexCount;
			bd.BindFlags = D3D11_BIND_INDEX_BUFFER;

			D3D11_SUBRESOURCE_DATA sd = {};
			sd.pSysMem = indices.data();

			HRESULT hr = pDevice->CreateBuffer(&bd, &sd, &glbMesh.pIndexBuffer);
			if (FAILED(hr))
			{
				return -1;
			}
		}

	return 1;
}

static void FinishDecodedTexture(
	GlbDecodedTexture* decoded,
	bool skipTextureResize)
{
	if (!skipTextureResize)
	{
		ResizeDecodedTextureIfNeeded(decoded);
	}
	NormalizeDecodedTextureForUpload(decoded);
	// expo_floor.glb は分割前の巨大テクスチャをそのまま使うため、
	// skipTextureResize=true の経路ではミップマップも生成しない。
	GenerateMipMapsIfNeeded(decoded, skipTextureResize);
}

static void DecodePreparedTexture(
	const GlbPreparedTextureData& source,
	GlbDecodedTexture* decoded,
	bool skipTextureResize)
{
	if (!decoded)
	{
		return;
	}
	if (!source.bytes.empty())
	{
		HRESULT hr = LoadFromWICMemory(
			source.bytes.data(),
			source.bytes.size(),
			WIC_FLAGS_NONE,
			&decoded->metadata,
			decoded->image);
		if (FAILED(hr))
		{
			hr = LoadFromWICMemory(
				source.bytes.data(),
				source.bytes.size(),
				WIC_FLAGS_FORCE_SRGB,
				&decoded->metadata,
				decoded->image);
		}
		decoded->ok = SUCCEEDED(hr);
	}
	FinishDecodedTexture(decoded, skipTextureResize);
}

static void DecodeAssimpTexture(
	const aiTexture* pAiTex,
	GlbDecodedTexture* decoded,
	bool skipTextureResize)
{
	if (!decoded)
	{
		return;
	}
	if (!pAiTex || !pAiTex->pcData)
	{
		return;
	}

	if (pAiTex->mHeight == 0)
	{
		if (pAiTex->mWidth > 0)
		{
			HRESULT hr = LoadFromWICMemory(
				reinterpret_cast<const uint8_t*>(pAiTex->pcData),
				static_cast<size_t>(pAiTex->mWidth),
				WIC_FLAGS_NONE,
				&decoded->metadata,
				decoded->image
			);
			if (FAILED(hr))
			{
				hr = LoadFromWICMemory(
					reinterpret_cast<const uint8_t*>(pAiTex->pcData),
					static_cast<size_t>(pAiTex->mWidth),
					WIC_FLAGS_FORCE_SRGB,
					&decoded->metadata,
					decoded->image
				);
			}
			decoded->ok = SUCCEEDED(hr);
		}
	}
	else if (pAiTex->mWidth > 0 && pAiTex->mHeight > 0)
	{
		HRESULT hr = decoded->image.Initialize2D(
			DXGI_FORMAT_R8G8B8A8_UNORM,
			static_cast<size_t>(pAiTex->mWidth),
			static_cast<size_t>(pAiTex->mHeight),
			1, 1
		);
		if (SUCCEEDED(hr))
		{
			const Image* pImg = decoded->image.GetImage(0, 0, 0);
			if (pImg && pImg->pixels)
			{
				const size_t byteSize = static_cast<size_t>(pAiTex->mWidth)
					* static_cast<size_t>(pAiTex->mHeight) * 4;
				memcpy(
					pImg->pixels,
					reinterpret_cast<const uint8_t*>(pAiTex->pcData),
					byteSize
				);
				decoded->metadata = decoded->image.GetMetadata();
				decoded->ok = true;
			}
		}
	}
	if (!skipTextureResize)
	{
		ResizeDecodedTextureIfNeeded(decoded);
	}
	GenerateMipMapsIfNeeded(decoded, skipTextureResize);
}

static unsigned int TextureDecodeWorkerCount(std::size_t textureCount)
{
	if (textureCount <= 1)
	{
		return 1;
	}
	unsigned int n = std::thread::hardware_concurrency();
	if (n == 0)
	{
		n = 2;
	}
	if (n > 4)
	{
		n = 4;
	}
	if (n > textureCount)
	{
		n = static_cast<unsigned int>(textureCount);
	}
	return n;
}

static void RunTextureDecodeJobs(
	std::size_t textureCount,
	const std::function<void(std::size_t)>& decodeOne)
{
	const unsigned int workerCount = TextureDecodeWorkerCount(textureCount);
	if (workerCount <= 1)
	{
		HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const bool shouldUninit = SUCCEEDED(coHr);
		for (std::size_t i = 0; i < textureCount; ++i)
		{
			decodeOne(i);
		}
		if (shouldUninit)
		{
			CoUninitialize();
		}
		return;
	}

	std::vector<std::thread> workers;
	workers.reserve(workerCount);
	const std::size_t chunk = (textureCount + workerCount - 1) / workerCount;
	for (unsigned int w = 0; w < workerCount; ++w)
	{
		const std::size_t begin = static_cast<std::size_t>(w) * chunk;
		if (begin >= textureCount)
		{
			break;
		}
		std::size_t end = begin + chunk;
		if (end > textureCount)
		{
			end = textureCount;
		}
		workers.emplace_back([begin, end, &decodeOne]()
		{
			HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			const bool shouldUninit = SUCCEEDED(coHr);
			for (std::size_t i = begin; i < end; ++i)
			{
				decodeOne(i);
			}
			if (shouldUninit)
			{
				CoUninitialize();
			}
		});
	}
	for (std::thread& worker : workers)
	{
		worker.join();
	}
}

//==============================================================================
// 埋め込みテクスチャの CPU デコード（ワーカー可。D3Dは触らない）
//==============================================================================
bool GlbModel::DecodeEmbeddedTextures(bool skipTextureResize)
{
	m_DecodedTextures.clear();
	m_TexturesDecoded = false;
	if (m_pPreparedData)
	{
		m_DecodedTextures.resize(m_pPreparedData->textures.size());
		for (std::size_t i = 0; i < m_pPreparedData->textures.size(); ++i)
		{
			m_DecodedTextures[i].reset(new GlbDecodedTexture());
		}
		RunTextureDecodeJobs(
			m_pPreparedData->textures.size(),
			[this, skipTextureResize](std::size_t i)
			{
				DecodePreparedTexture(
					m_pPreparedData->textures[i],
					m_DecodedTextures[i].get(),
					skipTextureResize);
			});
		m_TexturesDecoded = true;
		return true;
	}
	if (!m_pScene)
	{
		return false;
	}

	m_DecodedTextures.resize(m_pScene->mNumTextures);
	for (unsigned int i = 0; i < m_pScene->mNumTextures; ++i)
	{
		m_DecodedTextures[i].reset(new GlbDecodedTexture());
	}
	RunTextureDecodeJobs(
		static_cast<std::size_t>(m_pScene->mNumTextures),
		[this, skipTextureResize](std::size_t i)
		{
			DecodeAssimpTexture(
				m_pScene->mTextures[i],
				m_DecodedTextures[i].get(),
				skipTextureResize);
		});
	m_TexturesDecoded = true;
	return true;
}

int GlbModel::CreateSrvFromDecoded(unsigned int textureIndex, ID3D11Device* pDevice)
{
	const unsigned int textureCount = m_pScene
		? m_pScene->mNumTextures
		: (m_pPreparedData
			? static_cast<unsigned int>(m_pPreparedData->textures.size())
			: 0u);
	if (textureIndex >= textureCount || !pDevice)
	{
		return -1;
	}
	if (textureIndex >= m_DecodedTextures.size() || !m_DecodedTextures[textureIndex] || !m_DecodedTextures[textureIndex]->ok)
	{
		return true;
	}

	GlbDecodedTexture& decoded = *m_DecodedTextures[textureIndex];
	if (m_pPreparedData)
	{
		if (!GetDeviceContext())
		{
			return -1;
		}
		const std::size_t mipLevels =
			(std::max)(std::size_t(1), decoded.metadata.mipLevels);
		if (decoded.uploadedMip >= mipLevels)
		{
			decoded.uploadedMip = 0;
			decoded.uploadedRows = 0;
		}
		const Image* image = decoded.image.GetImage(decoded.uploadedMip, 0, 0);
		if (!image || image->format != DXGI_FORMAT_R8G8B8A8_UNORM ||
			image->width == 0 || image->height == 0 || !image->pixels)
		{
			return 1;
		}
		if (!decoded.pGpuTexture)
		{
			D3D11_TEXTURE2D_DESC desc = {};
			desc.Width = static_cast<UINT>(image->width);
			desc.Height = static_cast<UINT>(image->height);
			desc.MipLevels = static_cast<UINT>(mipLevels);
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			if (FAILED(pDevice->CreateTexture2D(&desc, nullptr, &decoded.pGpuTexture)))
			{
				return -1;
			}
		}
		const std::size_t rowPitch = image->rowPitch;
		if (rowPitch == 0)
		{
			return -1;
		}
		if (decoded.uploadedRows < image->height)
		{
			const std::size_t maxRows = (256u * 1024u) / rowPitch;
			const std::size_t rows = (std::max)(std::size_t(1),
				(std::min)(maxRows, image->height - decoded.uploadedRows));
			D3D11_BOX box = {};
			box.left = 0;
			box.right = static_cast<UINT>(image->width);
			box.top = static_cast<UINT>(decoded.uploadedRows);
			box.bottom = static_cast<UINT>(decoded.uploadedRows + rows);
			box.front = 0;
			box.back = 1;
			GetDeviceContext()->UpdateSubresource(
				decoded.pGpuTexture,
				static_cast<UINT>(decoded.uploadedMip),
				&box,
				image->pixels + decoded.uploadedRows * rowPitch,
				static_cast<UINT>(rowPitch),
				0);
			decoded.uploadedRows += rows;
			return 0;
		}
		decoded.uploadedMip += 1;
		decoded.uploadedRows = 0;
		if (decoded.uploadedMip < mipLevels)
		{
			return 0;
		}

		ID3D11ShaderResourceView* pSRV = nullptr;
		const HRESULT hr = pDevice->CreateShaderResourceView(
			decoded.pGpuTexture,
			nullptr,
			&pSRV);
		SAFE_RELEASE(decoded.pGpuTexture);
		decoded.image.Release();
		decoded.ok = false;
		if (FAILED(hr) || !pSRV)
		{
			return 1;
		}
		const std::string indexName = std::string("*") + std::to_string(textureIndex);
		m_EmbeddedTextures[indexName] = pSRV;
		const std::string& fileName = m_pPreparedData->textures[textureIndex].name;
		if (!fileName.empty() && fileName != indexName)
		{
			pSRV->AddRef();
			m_EmbeddedTextures[fileName] = pSRV;
		}
		return 1;
	}

	ID3D11ShaderResourceView* pSRV = nullptr;
	const HRESULT hr = CreateShaderResourceView(
		pDevice,
		decoded.image.GetImages(),
		decoded.image.GetImageCount(),
		decoded.metadata,
		&pSRV
	);
	decoded.image.Release();
	decoded.ok = false;
	if (FAILED(hr) || !pSRV)
	{
		return true;
	}

	const std::string indexName = std::string("*") + std::to_string(textureIndex);
	m_EmbeddedTextures[indexName] = pSRV;
	std::string fileName;
	if (m_pScene)
	{
		const aiTexture* pAiTex = m_pScene->mTextures[textureIndex];
		if (pAiTex && pAiTex->mFilename.length > 0)
		{
			fileName = pAiTex->mFilename.data;
		}
	}
	else if (m_pPreparedData)
	{
		fileName = m_pPreparedData->textures[textureIndex].name;
	}
	if (!fileName.empty() && fileName != indexName)
	{
		pSRV->AddRef();
		m_EmbeddedTextures[fileName] = pSRV;
	}
	return 1;
}

//==============================================================================
// GLB埋め込みテクスチャの処理（1枚）
//==============================================================================
bool GlbModel::LoadOneEmbeddedTexture(unsigned int textureIndex, ID3D11Device* pDevice)
{
	if (!m_pScene || textureIndex >= m_pScene->mNumTextures)
	{
		return false;
	}

	const aiTexture* pAiTex = m_pScene->mTextures[textureIndex];
	if (!pAiTex)
	{
		return true;
	}

	ID3D11ShaderResourceView* pSRV = nullptr;
	TexMetadata metadata;
	ScratchImage image;
	bool allSucceeded = true;
	const unsigned int i = textureIndex;

		if (pAiTex->mHeight == 0)
		{
			if (pAiTex->pcData && pAiTex->mWidth > 0)
			{
				// LOD2/LOD3の埋め込みWebPも含め、WICが解釈できる圧縮画像を読む。
				// 色テクスチャとして扱うため、失敗時はsRGB強制でも再試行する。
				HRESULT hr = LoadFromWICMemory(
					reinterpret_cast<const uint8_t*>(pAiTex->pcData),
					static_cast<size_t>(pAiTex->mWidth),
					WIC_FLAGS_NONE,
					&metadata,
					image
				);
				if (FAILED(hr))
				{
					hr = LoadFromWICMemory(
						reinterpret_cast<const uint8_t*>(pAiTex->pcData),
						static_cast<size_t>(pAiTex->mWidth),
						WIC_FLAGS_FORCE_SRGB,
						&metadata,
						image
					);
				}

				if (SUCCEEDED(hr))
				{
					hr = CreateShaderResourceView(
						pDevice,
						image.GetImages(),
						image.GetImageCount(),
						metadata,
						&pSRV
					);
				}

				if (FAILED(hr))
				{
					allSucceeded = false;
				}
			}
		}
		else
		{
			if (pAiTex->pcData && pAiTex->mWidth > 0 && pAiTex->mHeight > 0)
			{
				HRESULT hr = image.Initialize2D(
					DXGI_FORMAT_R8G8B8A8_UNORM,
					static_cast<size_t>(pAiTex->mWidth),
					static_cast<size_t>(pAiTex->mHeight),
					1, 1
				);

				if (SUCCEEDED(hr))
				{
					const Image* pImg = image.GetImage(0, 0, 0);
					if (pImg && pImg->pixels)
					{
						size_t byteSize = static_cast<size_t>(pAiTex->mWidth)
							* static_cast<size_t>(pAiTex->mHeight) * 4;
						memcpy(pImg->pixels,
							reinterpret_cast<const uint8_t*>(pAiTex->pcData),
							byteSize);

						metadata = image.GetMetadata();
						hr = CreateShaderResourceView(
							pDevice,
							image.GetImages(),
							image.GetImageCount(),
							metadata,
							&pSRV
						);
					}
				}

				if (FAILED(hr))
				{
					allSucceeded = false;
				}
			}
		}

		if (pSRV)
		{
			const std::string indexName = std::string("*") + std::to_string(i);
			m_EmbeddedTextures[indexName] = pSRV;
			if (pAiTex->mFilename.length > 0)
			{
				std::string fileName(pAiTex->mFilename.data);
				if (fileName != indexName)
				{
					pSRV->AddRef();
					m_EmbeddedTextures[fileName] = pSRV;
				}
			}
		}

	return true;
}

//==============================================================================
// マテリアルとテクスチャの紐づけ
//==============================================================================
void GlbModel::SetupMeshMaterials(const aiScene* pScene)
{
	for (unsigned int m = 0; m < pScene->mNumMeshes; m++)
	{
		const aiMesh* pMesh = pScene->mMeshes[m];
		GlbMesh& glbMesh = m_Meshes[m];

		if (pMesh->mMaterialIndex < pScene->mNumMaterials)
		{
			const aiMaterial* pMat = pScene->mMaterials[pMesh->mMaterialIndex];
			auto lookupTexture =
				[this, pScene, pMat](aiTextureType textureType)
				-> ID3D11ShaderResourceView*
			{
				aiString texturePath;
				if (AI_SUCCESS != pMat->GetTexture(textureType, 0, &texturePath))
				{
					return nullptr;
				}
				const std::string key(texturePath.data);
				auto it = m_EmbeddedTextures.find(key);
				if (it != m_EmbeddedTextures.end())
				{
					return it->second;
				}
				const aiTexture* embedded = pScene->GetEmbeddedTexture(texturePath.data);
				if (!embedded)
				{
					return nullptr;
				}
				const std::string embeddedName =
					embedded->mFilename.length > 0
					? std::string(embedded->mFilename.data)
					: key;
				it = m_EmbeddedTextures.find(embeddedName);
				return it != m_EmbeddedTextures.end() ? it->second : nullptr;
			};

			aiColor4D color(1.0f, 1.0f, 1.0f, 1.0f);
			if (AI_SUCCESS != pMat->Get(AI_MATKEY_BASE_COLOR, color))
			{
				pMat->Get(AI_MATKEY_COLOR_DIFFUSE, color);
			}
			glbMesh.diffuseColor = XMFLOAT4(color.r, color.g, color.b, color.a);

			// アルファ値の補正
			if (glbMesh.diffuseColor.w == 0.0f)
			{
				glbMesh.diffuseColor.w = 1.0f;
			}

			glbMesh.pTextureSRV = lookupTexture(aiTextureType_DIFFUSE);
			if (!glbMesh.pTextureSRV)
			{
				glbMesh.pTextureSRV = lookupTexture(aiTextureType_BASE_COLOR);
			}
			if (!glbMesh.pTextureSRV)
			{
				glbMesh.pTextureSRV = m_pWhiteTexture;
			}

			pMat->Get(AI_MATKEY_METALLIC_FACTOR, glbMesh.metallicFactor);
			pMat->Get(AI_MATKEY_ROUGHNESS_FACTOR, glbMesh.roughnessFactor);
			glbMesh.pNormalSRV = lookupTexture(aiTextureType_NORMALS);
			glbMesh.pMetallicRoughnessSRV =
				lookupTexture(aiTextureType_GLTF_METALLIC_ROUGHNESS);
			glbMesh.pEmissiveSRV = lookupTexture(aiTextureType_EMISSIVE);
		}
	}
}

void GlbModel::SetupPreparedMeshMaterials(void)
{
	if (!m_pPreparedData)
	{
		return;
	}
	auto lookupSrv = [this](int imageIndex) -> ID3D11ShaderResourceView*
	{
		if (imageIndex < 0 ||
			static_cast<std::size_t>(imageIndex) >= m_pPreparedData->textures.size())
		{
			return nullptr;
		}
		const std::string key = std::string("*") + std::to_string(imageIndex);
		const auto it = m_EmbeddedTextures.find(key);
		if (it == m_EmbeddedTextures.end())
		{
			return nullptr;
		}
		return it->second;
	};
	for (std::size_t i = 0; i < m_pPreparedData->meshes.size() && i < m_Meshes.size(); ++i)
	{
		const GlbPreparedMeshData& source = m_pPreparedData->meshes[i];
		GlbMesh& mesh = m_Meshes[i];
		mesh.diffuseColor = source.diffuseColor;
		mesh.metallicFactor = source.metallicFactor;
		mesh.roughnessFactor = source.roughnessFactor;
		if (mesh.diffuseColor.w == 0.0f)
		{
			mesh.diffuseColor.w = 1.0f;
		}
		mesh.pTextureSRV = lookupSrv(source.textureIndex);
		if (!mesh.pTextureSRV)
		{
			mesh.pTextureSRV = m_pWhiteTexture;
		}
		if (m_EnablePreparedPbr)
		{
			mesh.pNormalSRV = lookupSrv(source.normalIndex);
			mesh.pMetallicRoughnessSRV = lookupSrv(source.metallicRoughnessIndex);
			mesh.pEmissiveSRV = lookupSrv(source.emissiveIndex);
		}
	}
}

//==============================================================================
// モデルのバウンディングボックスサイズを取得
//==============================================================================
XMFLOAT3 GlbModel::GetSize() const
{
	return XMFLOAT3(
		m_BoundsMax.x - m_BoundsMin.x,
		m_BoundsMax.y - m_BoundsMin.y,
		m_BoundsMax.z - m_BoundsMin.z);
}

void GlbModel::GetBounds(XMFLOAT3* minBounds, XMFLOAT3* maxBounds, XMFLOAT3* center) const
{
	if (minBounds) *minBounds = m_BoundsMin;
	if (maxBounds) *maxBounds = m_BoundsMax;
	if (center) *center = m_BoundsCenter;
}

//==============================================================================
// 全マテリアルの平均色を取得
//==============================================================================
XMFLOAT4 GlbModel::GetAverageMaterialColor() const
{
	if (m_Meshes.empty()) return XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);

	float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
	unsigned int count = (unsigned int)m_Meshes.size();

	for (unsigned int i = 0; i < count; i++)
	{
		r += m_Meshes[i].diffuseColor.x;
		g += m_Meshes[i].diffuseColor.y;
		b += m_Meshes[i].diffuseColor.z;
		a += m_Meshes[i].diffuseColor.w;
	}

	return XMFLOAT4(r / count, g / count, b / count, a / count);
}

static bool WorldAabbOverlaps(
	const XMFLOAT3& minA, const XMFLOAT3& maxA,
	const XMFLOAT3& minB, const XMFLOAT3& maxB)
{
	return minA.x <= maxB.x && maxA.x >= minB.x &&
		minA.y <= maxB.y && maxA.y >= minB.y &&
		minA.z <= maxB.z && maxA.z >= minB.z;
}

static void TransformAabb(
	const XMFLOAT3& localMin, const XMFLOAT3& localMax,
	const XMMATRIX& world, XMFLOAT3* outMin, XMFLOAT3* outMax)
{
	const XMVECTOR corners[8] = {
		XMVector3TransformCoord(XMVectorSet(localMin.x, localMin.y, localMin.z, 1.0f), world),
		XMVector3TransformCoord(XMVectorSet(localMax.x, localMin.y, localMin.z, 1.0f), world),
		XMVector3TransformCoord(XMVectorSet(localMin.x, localMax.y, localMin.z, 1.0f), world),
		XMVector3TransformCoord(XMVectorSet(localMax.x, localMax.y, localMin.z, 1.0f), world),
		XMVector3TransformCoord(XMVectorSet(localMin.x, localMin.y, localMax.z, 1.0f), world),
		XMVector3TransformCoord(XMVectorSet(localMax.x, localMin.y, localMax.z, 1.0f), world),
		XMVector3TransformCoord(XMVectorSet(localMin.x, localMax.y, localMax.z, 1.0f), world),
		XMVector3TransformCoord(XMVectorSet(localMax.x, localMax.y, localMax.z, 1.0f), world)
	};
	XMFLOAT3 mn, mx;
	XMStoreFloat3(&mn, corners[0]);
	mx = mn;
	for (int i = 1; i < 8; ++i)
	{
		XMFLOAT3 p;
		XMStoreFloat3(&p, corners[i]);
		mn.x = (std::min)(mn.x, p.x);
		mn.y = (std::min)(mn.y, p.y);
		mn.z = (std::min)(mn.z, p.z);
		mx.x = (std::max)(mx.x, p.x);
		mx.y = (std::max)(mx.y, p.y);
		mx.z = (std::max)(mx.z, p.z);
	}
	*outMin = mn;
	*outMax = mx;
}

static void SetAabbSphere(
	const XMFLOAT3& minBounds,
	const XMFLOAT3& maxBounds,
	XMFLOAT3* outCenter,
	float* outRadius)
{
	if (!outCenter || !outRadius)
	{
		return;
	}
	*outCenter = XMFLOAT3(
		(minBounds.x + maxBounds.x) * 0.5f,
		(minBounds.y + maxBounds.y) * 0.5f,
		(minBounds.z + maxBounds.z) * 0.5f);
	const float halfX = (maxBounds.x - minBounds.x) * 0.5f;
	const float halfY = (maxBounds.y - minBounds.y) * 0.5f;
	const float halfZ = (maxBounds.z - minBounds.z) * 0.5f;
	*outRadius = sqrtf(halfX * halfX + halfY * halfY + halfZ * halfZ);
}

static bool IsSameMatrix(const XMMATRIX& left, const XMMATRIX& right)
{
	return std::memcmp(&left, &right, sizeof(XMMATRIX)) == 0;
}

static void UpdateShadowWorldBounds(GlbMesh& mesh, const XMMATRIX& world)
{
	if (mesh.shadowBoundsValid && IsSameMatrix(mesh.shadowWorld, world))
	{
		return;
	}

	mesh.shadowWorld = world;
	mesh.shadowBoundsValid = true;
	if (mesh.hasBounds)
	{
		TransformAabb(
			mesh.boundsMin,
			mesh.boundsMax,
			world,
			&mesh.worldBoundsMin,
			&mesh.worldBoundsMax);
		SetAabbSphere(
			mesh.worldBoundsMin,
			mesh.worldBoundsMax,
			&mesh.worldCenter,
			&mesh.worldRadius);
	}
	for (GlbShadowCell& cell : mesh.shadowCells)
	{
		TransformAabb(
			cell.aabbMin,
			cell.aabbMax,
			world,
			&cell.worldAabbMin,
			&cell.worldAabbMax);
		SetAabbSphere(
			cell.worldAabbMin,
			cell.worldAabbMax,
			&cell.worldCenter,
			&cell.worldRadius);
	}
}

static bool IsSphereVisibleFromCamera(
	const XMFLOAT3& center,
	float radius,
	Camera* camera)
{
	if (!camera || radius <= 0.001f)
	{
		return true;
	}

	const XMVECTOR viewPosition = XMVector3TransformCoord(
		XMLoadFloat3(&center),
		camera->GetView());
	const float viewX = XMVectorGetX(viewPosition);
	const float viewY = XMVectorGetY(viewPosition);
	const float viewZ = XMVectorGetZ(viewPosition);
	const XMMATRIX projection = camera->GetProjection();
	const float xScale = XMVectorGetX(projection.r[0]);
	const float yScale = XMVectorGetY(projection.r[1]);
	if (xScale <= 0.0f || yScale <= 0.0f)
	{
		return true;
	}

	const float nearPlane = (std::max)(0.0f, camera->GetNear());
	const float farPlane = (std::max)(nearPlane, camera->GetFar());
	if (viewZ + radius < nearPlane || viewZ - radius > farPlane)
	{
		return false;
	}

	const float sideDepth = (std::max)(viewZ + radius, nearPlane);
	if (fabsf(viewX) - radius > sideDepth / xScale)
	{
		return false;
	}
	if (fabsf(viewY) - radius > sideDepth / yScale)
	{
		return false;
	}
	return true;
}

//==============================================================================
// 描画処理 (pos/rot/scale指定版)
//==============================================================================
void GlbModel::Draw(XMFLOAT3 pos, XMFLOAT3 rot, XMFLOAT3 scale,
	const XMFLOAT4& color, bool useColorReplace, SHADERTYPE shadertype)
{
	const XMMATRIX RotationMatrix = XMMatrixRotationRollPitchYaw(
		XMConvertToRadians(rot.x),
		XMConvertToRadians(rot.y),
		XMConvertToRadians(rot.z));
	Draw(pos, RotationMatrix, scale, color, useColorReplace, shadertype);
}

void GlbModel::Draw(XMFLOAT3 pos, const XMMATRIX& rotation, XMFLOAT3 scale,
	const XMFLOAT4& color, bool useColorReplace, SHADERTYPE shadertype)
{
	if (!m_IsLoaded) return;

	const XMFLOAT4 savedParameter = GetParameter();
	SetParameterW(m_ReceiveShadow ? 1.0f : 0.0f);
	const float receiveShadow = GetParameter().w;

	Camera* pCamera = GetCamera();
	if (!pCamera) return;

	GetDeviceContext()->IASetInputLayout(GetShader(shadertype)->GetVertexLayout());
	GetDeviceContext()->VSSetShader(GetShader(shadertype)->GetVertexShader(), NULL, 0);
	GetDeviceContext()->PSSetShader(GetShader(shadertype)->GetPixelShader(), NULL, 0);

	XMMATRIX View = pCamera->GetView();
	XMMATRIX Projection = pCamera->GetProjection();

	XMMATRIX TranslationMatrix = XMMatrixTranslation(pos.x, pos.y, pos.z);
	XMMATRIX ScalingMatrix = XMMatrixScaling(scale.x, scale.y, scale.z);

	XMMATRIX World = ScalingMatrix * rotation * TranslationMatrix;

	SetWorldMatrix(World);
	SetViewMatrix(View);
	SetProjectionMatrix(Projection);

	ID3D11DeviceContext* pContext = GetDeviceContext();
	SetDefaultSampler();
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	for (unsigned int m = 0; m < (unsigned int)m_Meshes.size(); m++)
	{
		GlbMesh& mesh = m_Meshes[m];

		if (!mesh.pVertexBuffer || !mesh.pIndexBuffer || mesh.indexCount == 0)
			continue;
		if (mesh.batchRanges.empty() &&
			mesh.batchId >= 0 &&
			m_HiddenBatchIds.find(mesh.batchId) != m_HiddenBatchIds.end())
			continue;
		// メッシュ単位の保守的な視錐台カリング。
		// ワールドAABBはシャドウパスと共有のキャッシュを使う。
		if (mesh.hasBounds)
		{
			UpdateShadowWorldBounds(mesh, World);
			if (!IsSphereVisibleFromCamera(
				mesh.worldCenter,
				mesh.worldRadius,
				pCamera))
			{
				continue;
			}
		}

		XMFLOAT4 finalColor;
		if (useColorReplace)
		{
			finalColor = color;
		}
		else
		{
			if (color.w == 0.0f)
			{
				finalColor = mesh.diffuseColor;
			}
			else
			{
				finalColor = XMFLOAT4(
					mesh.diffuseColor.x * color.x,
					mesh.diffuseColor.y * color.y,
					mesh.diffuseColor.z * color.z,
					mesh.diffuseColor.w * color.w
				);
			}
		}
		MATERIAL material = {};
		material.Diffuse = finalColor;
		material.Ambient = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		material.Specular = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		material.Emission = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		material.Shininess = 50.0f;
		SetMaterial(material);

		if (m_EnablePreparedPbr)
		{
			const bool hasNormalMap = mesh.pNormalSRV != nullptr;
			const bool hasPackedOrm = mesh.pMetallicRoughnessSRV != nullptr;
			const bool hasMaterialMaps =
				hasNormalMap || hasPackedOrm || mesh.pEmissiveSRV != nullptr;
			float roughness = mesh.roughnessFactor;
			float metallic = mesh.metallicFactor;
			float texMode = hasPackedOrm ? 2.0f
				: (hasMaterialMaps ? 0.5f : 0.0f);
			if (m_MirrorEnv)
			{
				roughness = 0.08f;
				metallic = 1.0f;
				texMode = 3.0f;
				finalColor = XMFLOAT4(0.92f, 0.94f, 0.96f, 1.0f);
				material.Diffuse = finalColor;
				SetMaterial(material);
			}
			else if (!hasMaterialMaps)
			{
				roughness = 0.81f * 0.6f + mesh.roughnessFactor * 0.4f;
				metallic = 0.0f;
			}
			SetParameter(XMFLOAT4(
				roughness,
				metallic,
				texMode,
				receiveShadow));
		}

		ID3D11ShaderResourceView* pSRV = mesh.pTextureSRV ? mesh.pTextureSRV : m_pWhiteTexture;
		if (m_MirrorEnv && m_pWhiteTexture)
		{
			pSRV = m_pWhiteTexture;
		}
		pContext->PSSetShaderResources(0, 1, &pSRV);
		if (m_EnablePreparedPbr)
		{
			ID3D11ShaderResourceView* pNormal =
				mesh.pNormalSRV ? mesh.pNormalSRV : m_pFlatNormalTexture;
			ID3D11ShaderResourceView* pMetallicRoughness =
				mesh.pMetallicRoughnessSRV ? mesh.pMetallicRoughnessSRV : m_pWhiteTexture;
			ID3D11ShaderResourceView* pEmissive =
				mesh.pEmissiveSRV ? mesh.pEmissiveSRV : m_pBlackTexture;
			pContext->PSSetShaderResources(2, 1, &pNormal);
			pContext->PSSetShaderResources(3, 1, &pMetallicRoughness);
			pContext->PSSetShaderResources(4, 1, &pMetallicRoughness);
			pContext->PSSetShaderResources(5, 1, &pEmissive);
		}

		UINT stride = sizeof(Vertex3D);
		UINT offset = 0;
		pContext->IASetVertexBuffers(0, 1, &mesh.pVertexBuffer, &stride, &offset);
		if (m_MainPassCellCulling &&
			m_HiddenBatchIds.empty() &&
			!mesh.shadowCells.empty() &&
			mesh.pShadowIndexBuffer &&
			mesh.shadowIndexCount > 0)
		{
			unsigned int rangeCount = 0;
			bool inRange = false;
			for (const GlbShadowCell& cell : mesh.shadowCells)
			{
				const bool visible =
					cell.indexCount > 0 &&
					IsSphereVisibleFromCamera(
						cell.worldCenter,
						cell.worldRadius,
						pCamera);
				if (!visible)
				{
					inRange = false;
					continue;
				}
				if (!inRange)
				{
					rangeCount += 1;
					inRange = true;
				}
			}
			if (rangeCount == 0)
			{
				continue;
			}
			if (rangeCount > GLB_MAX_CELL_DRAW_INDEXED)
			{
				pContext->IASetIndexBuffer(
					mesh.pIndexBuffer,
					DXGI_FORMAT_R32_UINT,
					0);
				DrawIndexed(mesh.indexCount, 0, 0);
				continue;
			}
			pContext->IASetIndexBuffer(
				mesh.pShadowIndexBuffer,
				DXGI_FORMAT_R32_UINT,
				0);
			unsigned int drawOffset = 0;
			unsigned int drawCount = 0;
			bool hasDrawRange = false;
			auto flushDrawRange = [&]()
			{
				if (hasDrawRange && drawCount > 0)
				{
					DrawIndexed(drawCount, drawOffset, 0);
				}
				hasDrawRange = false;
				drawOffset = 0;
				drawCount = 0;
			};
			for (const GlbShadowCell& cell : mesh.shadowCells)
			{
				if (cell.indexCount == 0 ||
					!IsSphereVisibleFromCamera(
						cell.worldCenter,
						cell.worldRadius,
						pCamera))
				{
					flushDrawRange();
					continue;
				}
				const bool isAdjacent =
					hasDrawRange &&
					cell.indexOffset == drawOffset + drawCount;
				if (!isAdjacent)
				{
					flushDrawRange();
					drawOffset = cell.indexOffset;
					hasDrawRange = true;
				}
				drawCount += cell.indexCount;
			}
			flushDrawRange();
		}
		else if (!m_HiddenBatchIds.empty() &&
			mesh.pVisibleIndexBuffer &&
			mesh.visibleIndexCount > 0)
		{
			pContext->IASetIndexBuffer(
				mesh.pVisibleIndexBuffer,
				DXGI_FORMAT_R32_UINT,
				0);
			DrawIndexed(mesh.visibleIndexCount, 0, 0);
		}
		else if (!m_HiddenBatchIds.empty() && !mesh.sourceIndices.empty())
		{
			continue;
		}
		else if (!m_HiddenBatchIds.empty() && !mesh.batchRanges.empty())
		{
			pContext->IASetIndexBuffer(mesh.pIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
			unsigned int drawOffset = 0;
			unsigned int drawCount = 0;
			bool hasDrawRange = false;
			auto flushDrawRange = [&]()
			{
				if (hasDrawRange && drawCount > 0)
				{
					DrawIndexed(drawCount, drawOffset, 0);
				}
				hasDrawRange = false;
				drawOffset = 0;
				drawCount = 0;
			};
			for (const GlbBatchRange& range : mesh.batchRanges)
			{
				if (range.indexCount == 0 ||
					(range.batchId >= 0 &&
						m_HiddenBatchIds.find(range.batchId) !=
							m_HiddenBatchIds.end()))
				{
					flushDrawRange();
					continue;
				}
				const bool isAdjacent =
					hasDrawRange &&
					range.indexOffset == drawOffset + drawCount;
				if (!isAdjacent)
				{
					flushDrawRange();
					drawOffset = range.indexOffset;
					hasDrawRange = true;
				}
				drawCount += range.indexCount;
			}
			flushDrawRange();
		}
		else
		{
			pContext->IASetIndexBuffer(mesh.pIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
			DrawIndexed(mesh.indexCount, 0, 0);
		}
	}

	if (m_EnablePreparedPbr)
	{
		SetParameter(savedParameter);
		SetParameterW(savedParameter.w);
	}
}

void GlbModel::DrawShadowMap(XMFLOAT3 pos, const XMMATRIX& rotation, XMFLOAT3 scale,
	const XMMATRIX& lightView, const XMMATRIX& lightProjection,
	XMFLOAT3 focus, float radius)
{
	if (!m_IsLoaded || radius <= 0.0f)
	{
		return;
	}

	GetDeviceContext()->IASetInputLayout(GetShader(S_SHADOW_MAP)->GetVertexLayout());
	GetDeviceContext()->VSSetShader(GetShader(S_SHADOW_MAP)->GetVertexShader(), NULL, 0);
	GetDeviceContext()->PSSetShader(GetShader(S_SHADOW_MAP)->GetPixelShader(), NULL, 0);

	const XMMATRIX TranslationMatrix = XMMatrixTranslation(pos.x, pos.y, pos.z);
	const XMMATRIX ScalingMatrix = XMMatrixScaling(scale.x, scale.y, scale.z);
	const XMMATRIX World = ScalingMatrix * rotation * TranslationMatrix;

	SetWorldMatrix(World);
	SetViewMatrix(lightView);
	SetProjectionMatrix(lightProjection);

	ID3D11DeviceContext* pContext = GetDeviceContext();
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	const XMFLOAT3 cullMin = {
		focus.x - radius, focus.y - radius, focus.z - radius
	};
	const XMFLOAT3 cullMax = {
		focus.x + radius, focus.y + radius, focus.z + radius
	};

	if (m_pCombinedShadowVertexBuffer &&
		m_pCombinedShadowIndexBuffer &&
		m_CombinedShadowIndexCount > 0)
	{
		XMFLOAT3 worldMin = {};
		XMFLOAT3 worldMax = {};
		TransformAabb(m_BoundsMin, m_BoundsMax, World, &worldMin, &worldMax);
		if (!WorldAabbOverlaps(worldMin, worldMax, cullMin, cullMax))
		{
			return;
		}
		UINT stride = sizeof(Vertex3D);
		UINT offset = 0;
		pContext->IASetVertexBuffers(
			0, 1, &m_pCombinedShadowVertexBuffer, &stride, &offset);
		pContext->IASetIndexBuffer(
			m_pCombinedShadowIndexBuffer,
			DXGI_FORMAT_R32_UINT,
			0);
		DrawIndexed(m_CombinedShadowIndexCount, 0, 0);
		return;
	}

	for (unsigned int m = 0; m < (unsigned int)m_Meshes.size(); m++)
	{
		GlbMesh& mesh = m_Meshes[m];
		if (!mesh.pVertexBuffer || !mesh.pIndexBuffer || mesh.indexCount == 0)
		{
			continue;
		}
		UpdateShadowWorldBounds(mesh, World);
		if (mesh.hasBounds &&
			!WorldAabbOverlaps(
				mesh.worldBoundsMin,
				mesh.worldBoundsMax,
				cullMin,
				cullMax))
		{
			continue;
		}

		UINT stride = sizeof(Vertex3D);
		UINT offset = 0;
		pContext->IASetVertexBuffers(0, 1, &mesh.pVertexBuffer, &stride, &offset);
		pContext->IASetIndexBuffer(mesh.pIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
		DrawIndexed(mesh.indexCount, 0, 0);
	}
}
