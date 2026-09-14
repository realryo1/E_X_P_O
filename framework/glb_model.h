#pragma once
//==============================================================================
// GLBモデル読み込み・描画クラス [glb_model.h]
// Assimp + DirectX 11 による .glb (glTF Binary) 専用ローダー
//==============================================================================

#include <d3d11.h>
#include <DirectXMath.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <mutex>

#include "assimp/cimport.h"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#pragma comment(lib, "assimp-vc143-mt.lib")

#include "model.h"
#include "renderer.h"
#include <memory>

using namespace DirectX;

struct GlbImportOptions
{
	bool genNormals = true;
	bool joinVertices = true;
};

struct GlbDecodedTexture;
struct GlbPreparedData;

//==============================================================================
// メッシュ単位のデータ
//==============================================================================
struct GlbShadowCell
{
	XMFLOAT3 aabbMin = { 0.0f, 0.0f, 0.0f };
	XMFLOAT3 aabbMax = { 0.0f, 0.0f, 0.0f };
	unsigned int indexOffset = 0;
	unsigned int indexCount = 0;
	XMFLOAT3 worldAabbMin = { 0.0f, 0.0f, 0.0f };
	XMFLOAT3 worldAabbMax = { 0.0f, 0.0f, 0.0f };
	XMFLOAT3 worldCenter = { 0.0f, 0.0f, 0.0f };
	float worldRadius = 0.0f;
};

struct GlbBatchRange
{
	int batchId = -1;
	unsigned int indexOffset = 0;
	unsigned int indexCount = 0;
	XMFLOAT3 boundsMin = { 0.0f, 0.0f, 0.0f };
	XMFLOAT3 boundsMax = { 0.0f, 0.0f, 0.0f };
	bool hasBounds = false;
};

struct GlbMesh
{
	ID3D11Buffer* pVertexBuffer = nullptr;
	ID3D11Buffer* pIndexBuffer = nullptr;
	ID3D11Buffer* pShadowIndexBuffer = nullptr;
	unsigned int  indexCount = 0;
	unsigned int  shadowIndexCount = 0;
	std::size_t vertexUploadOffset = 0;
	std::size_t indexUploadOffset = 0;
	std::size_t shadowIndexUploadOffset = 0;
	std::vector<GlbShadowCell> shadowCells;
	XMFLOAT3 boundsMin = { 0.0f, 0.0f, 0.0f };
	XMFLOAT3 boundsMax = { 0.0f, 0.0f, 0.0f };
	bool hasBounds = false;
	XMFLOAT3 worldBoundsMin = { 0.0f, 0.0f, 0.0f };
	XMFLOAT3 worldBoundsMax = { 0.0f, 0.0f, 0.0f };
	XMFLOAT3 worldCenter = { 0.0f, 0.0f, 0.0f };
	float worldRadius = 0.0f;
	XMMATRIX shadowWorld = {};
	bool shadowBoundsValid = false;

	// マテリアル情報
	XMFLOAT4 diffuseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	ID3D11ShaderResourceView* pTextureSRV = nullptr;  // テクスチャ (nullなら白テクスチャを使用)
	ID3D11ShaderResourceView* pNormalSRV = nullptr;
	ID3D11ShaderResourceView* pMetallicRoughnessSRV = nullptr;
	ID3D11ShaderResourceView* pEmissiveSRV = nullptr;
	float metallicFactor = 1.0f;
	float roughnessFactor = 1.0f;
	int batchId = -1;
	std::vector<GlbBatchRange> batchRanges;
};

//==============================================================================
// GLBモデルクラス
//==============================================================================
class GlbModel
{
public:
	GlbModel();
	~GlbModel();

	// モデル読み込み (.glb ファイルパス)
	bool Load(const char* filePath, ID3D11Device* pDevice, ID3D11DeviceContext* pContext);

	// ワーカーから呼ぶ。Assimpシーンの所有権を返す。失敗時は nullptr。
	static const aiScene* ImportSceneFile(const char* filePath, const GlbImportOptions* options = nullptr);

	// ImportSceneFile の結果を受け取り、GPU化の準備をする。シーンの所有権を移す。
	bool AttachImportedScene(const aiScene* scene);

	// 万博の静的GLBを、Accessor/BufferViewを直接解釈してCPUデータへ展開する。
	// 返されたデータの所有権は呼び出し側が持ち、AttachPreparedDataへ渡す。
	static GlbPreparedData* ImportPreparedFile(const char* filePath);
	bool AttachPreparedData(GlbPreparedData* data);
	void MergePreparedMeshesByMaterial(void);

	// 影パス用に、準備済み頂点をモデル空間の XZ セルへ分割する。ワーカーから呼ぶ。
	void PrepareShadowCells(float modelSpaceCellSize);

	// ワーカーから呼ぶ。埋め込みテクスチャを CPU デコードする。D3D は触らない。
	// skipTextureResize=true の場合は長辺2048pxへの縮小を行わない。
	bool DecodeEmbeddedTextures(bool skipTextureResize = false);

	// テクスチャまたはメッシュを itemBudget 個まで GPU 化する。0=継続, 1=完了, -1=失敗。
	int PumpGpu(ID3D11Device* pDevice, int itemBudget);

	void GetGpuProgress(unsigned int* done, unsigned int* total) const;

	// リソース解放
	void Release();

	// 描画 (pos/rot/scale指定版 — Sprite3Dと同じインターフェース)
	void Draw(XMFLOAT3 pos, XMFLOAT3 rot, XMFLOAT3 scale,
		const XMFLOAT4& color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
		bool useColorReplace = false,SHADERTYPE shadertype = S_LAMBERT);

	void Draw(XMFLOAT3 pos, const XMMATRIX& rotation, XMFLOAT3 scale,
		const XMFLOAT4& color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
		bool useColorReplace = false, SHADERTYPE shadertype = S_LAMBERT);

	void DrawShadowMap(XMFLOAT3 pos, const XMMATRIX& rotation, XMFLOAT3 scale,
		const XMMATRIX& lightView, const XMMATRIX& lightProjection,
		XMFLOAT3 focus, float radius);

	void SetReceiveShadow(bool enable) { m_ReceiveShadow = enable; }
	bool GetReceiveShadow(void) const { return m_ReceiveShadow; }
	void SetPhotoAlbedo(bool enable) { m_PhotoAlbedo = enable; }
	void SetMainPassCellCulling(bool enable) { m_MainPassCellCulling = enable; }

	// 遠景補完など、GLB内のバッチ単位で描画を一時的に抑制する。
	void ClearHiddenBatchIds(void);
	void HideBatchId(int batchId);

	// メッシュ数を取得
	unsigned int GetMeshCount() const { return (unsigned int)m_Meshes.size(); }

	// 読み込み済みかどうか
	bool IsLoaded() const { return m_IsLoaded; }

	// モデルのバウンディングボックスサイズを取得
	XMFLOAT3 GetSize() const;
	void GetBounds(XMFLOAT3* minBounds, XMFLOAT3* maxBounds, XMFLOAT3* center) const;

	// 全マテリアルの平均色を取得
	XMFLOAT4 GetAverageMaterialColor() const;

private:
	// Assimpシーンからメッシュデータを抽出しDX11バッファを作成
	int ProcessOneMesh(unsigned int meshIndex, ID3D11Device* pDevice);
	bool LoadOneEmbeddedTexture(unsigned int textureIndex, ID3D11Device* pDevice);
	int CreateSrvFromDecoded(unsigned int textureIndex, ID3D11Device* pDevice);

	// マテリアルからテクスチャパスを取得し、埋め込みテクスチャとマッピング
	void SetupMeshMaterials(const aiScene* pScene);
	void SetupPreparedMeshMaterials(void);

private:
	bool m_IsLoaded = false;
	bool m_ReceiveShadow = false;
	bool m_PhotoAlbedo = false;
	bool m_MainPassCellCulling = false;
	const aiScene* m_pScene = nullptr;
	std::unique_ptr<GlbPreparedData> m_pPreparedData;
	int m_GpuPhase = 0;
	unsigned int m_GpuIndex = 0;

	std::vector<GlbMesh> m_Meshes;
	std::vector<DirectX::XMMATRIX> m_MeshNodeTransforms;
	std::vector<std::unique_ptr<GlbDecodedTexture>> m_DecodedTextures;
	bool m_TexturesDecoded = false;

	// 埋め込みテクスチャのキャッシュ (テクスチャ名 → SRV)
	std::unordered_map<std::string, ID3D11ShaderResourceView*> m_EmbeddedTextures;
	std::unordered_set<int> m_HiddenBatchIds;

	// テクスチャなしメッシュ用の白テクスチャ
	ID3D11ShaderResourceView* m_pWhiteTexture = nullptr;
	ID3D11ShaderResourceView* m_pBlackTexture = nullptr;
	ID3D11ShaderResourceView* m_pFlatNormalTexture = nullptr;
	bool m_EnablePreparedPbr = false;

	// 視錐台カリング用のモデル空間境界ボックス。
	XMFLOAT3 m_BoundsMin = XMFLOAT3(0.0f, 0.0f, 0.0f);
	XMFLOAT3 m_BoundsMax = XMFLOAT3(0.0f, 0.0f, 0.0f);
	XMFLOAT3 m_BoundsCenter = XMFLOAT3(0.0f, 0.0f, 0.0f);
};

// 拡張子が .glb かどうかを判定するユーティリティ関数
bool IsGlbFile(const char* filePath);

// Assimpのインポート処理をプロセス内で直列化するための共有ロック。
std::mutex& Glb_GetAssimpMutex(void);
