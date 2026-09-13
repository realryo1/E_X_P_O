#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include <string>
#include "renderer.h"
#include "shadermanager.h"
using namespace DirectX;



struct BILLBOARD_VERTEX
{
	XMFLOAT3 pos;
	XMFLOAT3 normal;
	XMFLOAT4 color;
	XMFLOAT2 tex;
};

class Billboard
{
public:
	Billboard();
	Billboard(XMFLOAT3 pos, XMFLOAT2 size, XMFLOAT3 rot, const char* texturePath = nullptr, bool isDoubleSided = false);
	~Billboard();

	// 初期化
	void Initialize(XMFLOAT3 pos, XMFLOAT2 size, XMFLOAT3 rot, const char* texturePath = nullptr, bool isDoubleSided = false);

	void Update(void);
	void Draw(void);
	void Draw(SHADERTYPE shadertype); // シェーダー指定版のDraw
	void DrawShadowMap(const XMMATRIX& lightView, const XMMATRIX& lightProjection);

	// --- 便利機能 ---

	// 任意の画像パスを指定したい場合用
	void SetTexture(const char* texturePath);

	// NormalMapを使いたい場合用。床のテストでは asset\texture\Normal.png を渡す。
	void SetNormalMap(const char* texturePath);


	// --- UVアニメーション ---
	// frameCount: 横方向のコマ数, interval: 1コマあたりの秒数
	void SetUVAnimation(int frameCount, float interval);
	// UVアニメーションを無効化する
	void DisableUVAnimation();


	// --- 基本セッター・ゲッター ---
	void SetPos(XMFLOAT3 pos) { m_Pos = pos; }
	XMFLOAT3 GetPos(void) { return m_Pos; }

	void SetUVScale(XMFLOAT2 scale) { m_UVScale = scale; CreateBuffer(); }
	XMFLOAT2 GetUVScale(void) const { return m_UVScale; }

	void SetSize(XMFLOAT2 size)
	{
		if (m_Size.x != size.x || m_Size.y != size.y)
		{
			m_Size = size;
			CreateBuffer();
		}
	}
	XMFLOAT2 GetSize(void) { return m_Size; }

	void SetRotation(XMFLOAT3 rot) { m_Rot = rot; }
	XMFLOAT3 GetRotation(void) { return m_Rot; }

	void SetColor(XMFLOAT4 color) { m_Color = color; CreateBuffer(); }
	XMFLOAT4 GetColor(void) { return m_Color; }

	// ライティング無効化オプション
	void SetIgnoreLighting(bool ignore) { m_IgnoreLighting = ignore; }
	bool GetIgnoreLighting(void) const { return m_IgnoreLighting; }

	// 壁越し半透明オプション（false にすると壁の裏でも透過しない）
	void SetWallFadeEnabled(bool enable) { m_WallFadeEnabled = enable; }
	bool GetWallFadeEnabled(void) const { return m_WallFadeEnabled; }

	// ビルボードモード切り替え（true=カメラ追従、false=固定板ポリゴン）
	void SetBillboardMode(bool enable) { m_IsBillboardMode = enable; }
	bool GetBillboardMode(void) const { return m_IsBillboardMode; }

	// ブレンドモード設定（デフォルト: BLENDSTATE_ALFA）
	void SetBlendMode(BLENDSTATE blend) { m_BlendMode = blend; }
	BLENDSTATE GetBlendMode(void) const { return m_BlendMode; }

	// trueにすると、このBillboardはShadowMapを読んで影を受ける。
	// 今は床タイル用に使う。
	void SetReceiveShadow(bool enable) { m_ReceiveShadow = enable; }
	bool GetReceiveShadow(void) const { return m_ReceiveShadow; }

protected:
	ID3D11Buffer* m_VertexBuffer;
	ID3D11ShaderResourceView* m_Texture;
	ID3D11ShaderResourceView* m_NormalTexture;
	std::string m_TexturePath; // 現在ロード済みのテクスチャパス（再ロード防止用）
	std::string m_NormalTexturePath; // 現在ロード済みのNormalMapパス

	XMFLOAT3 m_Pos;
	XMFLOAT2 m_Size;
	XMFLOAT3 m_Rot;
	XMFLOAT4 m_Color;

	bool m_IsDoubleSided;
	bool m_IgnoreLighting; // ライティング無効化フラグ
	int m_VertexCount;

	XMFLOAT2 m_UVScale; // UVタイリング用スケール

	// UVアニメーション
	bool  m_UVAnimEnabled;   // UVアニメーション有効フラグ
	int   m_UVFrameCount;    // 横方向のコマ数
	int   m_UVCurrentFrame;  // 現在のコマ番号
	float m_UVInterval;      // 1コマあたりの秒数
	float m_UVTimer;         // 経過時間

	void CreateBuffer(void);
	void CreateBufferWithUV(float uMin, float uMax); // UV指定バッファ作成
	void CreateBufferWithUV(float uMin, float uMax, float vMin, float vMax);

	bool m_IsBillboardMode;    // true=ビルボード、false=固定板ポリゴン
	bool m_WallFadeEnabled;    // true=壁越しで半透明、false=壁越しでも不透明
	bool m_ReceiveShadow;      // true=シャドウマップの影を受ける
	BLENDSTATE m_BlendMode;    // ブレンドモード
};
