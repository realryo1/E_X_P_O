// =========================================================
// fade.h フェード制御
// =========================================================
#pragma once

#include "sprite2d.h"
#include "scene.h"

// =========================================================
// 列挙体宣言
// =========================================================
enum FADESTAT
{
	FADE_NONE = 0,
	FADE_OUT,		// 暗くなる
	FADE_IN,		// 明るくなる
	FADE_MAX,		// 真っ暗で待機
	FADE_WAIT_LOAD,	// 完全白で1フレーム待機後にロード
	FADE_WARMUP		// ロード完了後、真っ暗なまま数フレーム待機（初期化処理スパイク逃がし用）
};

// =========================================================
// Spriteを継承したFadeクラス
// =========================================================
class Fade : public Sprite2D
{
private:
	FADESTAT m_State;
	SCENE m_NextScene;
	int m_WarmupFrames; // 空回しフレーム数カウンタ

public:
	// コンストラクタ・デストラクタ
	Fade();
	~Fade();

	// 更新処理
	void Update();

	// フェード開始
	void SetSceneFade(SCENE next = SCENE_NONE);

	// フェードイン開始
	void StartFadeIn();

	// ゲッター
	FADESTAT GetState() const;
};

// =========================================================
// モジュール関数（グローバル関数）
// =========================================================
void Fade_Initialize(void);
void Fade_Update(void);
void Fade_Draw(void);
void Fade_Finalize(void);

void SetSceneFade(SCENE ns = SCENE_NONE);
void Fade_StartIn(void);
FADESTAT GetFadeState(void);