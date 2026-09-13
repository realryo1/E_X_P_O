#pragma once
#include "billboard.h"

class SplitBilBoard : public Billboard
{
public:
	SplitBilBoard();
	SplitBilBoard(int cols, int rows);
	SplitBilBoard(int cols, int rows, XMFLOAT3 pos, XMFLOAT2 size, XMFLOAT3 rot, const char* texturePath = nullptr, bool isDoubleSided = false);
	~SplitBilBoard();

	// アニメーションFPSの設定
	void SetFPS(float fps);

	// フレームの更新
	void Update(void);

	// アニメーションのON/OFF
	void SetAnimationEnabled(bool enable) { m_AnimEnabled = enable; }
	bool IsAnimationEnabled(void) const { return m_AnimEnabled; }

	// ループ設定
	void SetLoop(bool loop) { m_Loop = loop; }
	bool IsLoop(void) const { return m_Loop; }

	// テクスチャ番号のセッター・ゲッター
	void SetTextureIndex(int index);
	int GetTextureIndex(void) const { return m_TextureIndex; }

private:
	int m_Cols;             // 横分割数
	int m_Rows;             // 縦分割数
	int m_TextureIndex;     // 現在のテクスチャ番号（0 〜 cols*rows - 1）

	bool  m_AnimEnabled;    // アニメーションON/OFFフラグ
	bool  m_Loop;           // ループ再生フラグ
	float m_FPS;            // 1秒あたりのコマ数
	float m_AnimTimer;      // アニメーション経過タイマー

	// 指定されたテクスチャインデックスに基づきUVを計算し、バッファを再構築する
	void UpdateUV(void);

	// 縦横UV指定付きの頂点バッファ作成
	void CreateBufferWithSplitUV(float uMin, float uMax, float vMin, float vMax);
};
