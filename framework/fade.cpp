// =========================================================
// fade.cpp フェード制御
// =========================================================
#include "fade.h"
#include "sprite2d.h"
#include "scene.h"
#include "texture.h"

#include "define.h"
#include "renderer.h"
#include "mouse.h"
#include "debug_ostream.h"
#include <Windows.h>
#include <psapi.h>
using namespace DirectX;


// モジュール内の単一インスタンス
static Fade* g_pFade = nullptr;

// =========================================================
// Fadeクラス メンバ関数の実装
// =========================================================

// コンストラクタ
Fade::Fade()
	: Sprite2D(
		XMFLOAT2(SCREEN_X / 2.0f, SCREEN_Y / 2.0f),	// 位置
		XMFLOAT2(SCREEN_X, SCREEN_Y),					// サイズ
		0.0f,													// 回転
		XMFLOAT4(1.0f, 1.0f, 1.0f, 0.0f),						// 色（RGBA）アルファ値0
		BLENDSTATE_ALFA,										// ブレンドステート
		L"asset\\texture\\fade.png"								// テクスチャパス
	),
	m_State(FADE_NONE),
	m_NextScene(SCENE_NONE),
	m_WarmupFrames(6)
{
}

// デストラクタ
Fade::~Fade()
{
	// Spriteのデストラクタが自動的にテクスチャを解放
}

// 更新処理
void Fade::Update()
{
	switch (m_State)
	{
	case FADE_NONE:
		// 何もしない
		break;

	case FADE_OUT:
		// 暗くしていく
		m_Color.w += 0.05f;
		if (m_Color.w >= 1.0f)
		{
			m_Color.w = 1.0f;

			// シーン遷移指定がある場合（タイトルへ戻るなど）
			if (m_NextScene != SCENE_NONE)
			{
				// 完全白で1フレーム描画してからロードする
				m_State = FADE_WAIT_LOAD;
			}
			// シーン遷移がない場合（マップ移動など）
			else
			{
				// 勝手に明るくせず、真っ暗なまま待機させる
				m_State = FADE_MAX;
			}
		}
		break;

	case FADE_WAIT_LOAD:
		// 前フレームで完全白を描画済み → ここでシーン遷移・ロード
		m_Color.w = 1.0f;
		m_State = FADE_WARMUP;
		m_WarmupFrames = 6; // 6フレーム空回しして、初回描画（GPU構築）などの負荷を暗転中に消化させる
		ApplySceneInternal(m_NextScene);
		break;

	case FADE_WARMUP:
		// 真っ暗な状態をキープ
		m_Color.w = 1.0f;
		if (m_WarmupFrames > 0)
		{
			m_WarmupFrames--;
		}
		else
		{
			m_State = FADE_IN;
		}
		break;

	case FADE_IN:
		// 明るくしていく
		m_Color.w -= 0.05f;
		if (m_Color.w <= 0.0f)
		{
			m_Color.w = 0.0f;
			m_State = FADE_NONE;
		}
		break;

	case FADE_MAX:
		// 真っ暗なまま待機
		m_Color.w = 1.0f;
		break;

	default:
		break;
	}
}

// フェードアウト開始
void Fade::SetSceneFade(SCENE next)
{
#if !defined(_DEBUG)
	if (next == SCENE_DEBUG)
	{
		MessageBoxW(
			NULL,
			L"SCENE_DEBUG は Release ビルドでは利用できません。\nシーン遷移をキャンセルしました。",
			L"Scene Transition Cancelled",
			MB_OK | MB_ICONWARNING);
		return;
	}
#endif

	if (m_State == FADE_NONE)
	{
		m_Color.w = 0.0f;
		m_State = FADE_OUT;
		m_NextScene = next;

		// 現在のメモリ使用率をデバッグ出力
		PROCESS_MEMORY_COUNTERS pmc;
		if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
		{
			double physicalMemMB = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
			hal::dout << "[Memory Log] SetSceneFade -> Target: " << next 
			          << " | RAM (Working Set): " << physicalMemMB << " MB" << std::endl;
		}
	}
}

// フェードイン開始（手動）★追加
void Fade::StartFadeIn()
{
	// 真っ暗待機中ならフェードインへ移行
	if (m_State == FADE_MAX || m_State == FADE_OUT)
	{
		m_State = FADE_IN;
		m_Color.w = 1.0f;
	}
}

// 状態取得
FADESTAT Fade::GetState() const
{
	return m_State;
}


// =========================================================
// グローバル関数（外部公開用）の実装
// =========================================================

void Fade_Initialize(void)
{
	if (g_pFade == nullptr) {
		g_pFade = new Fade();
	}
}

void Fade_Update(void)
{
	if (g_pFade) {
		g_pFade->Update();
	}
}

void Fade_Draw(void)
{
	if (!g_pFade) {
		return;
	}
	// α=0 の全画面スプライトは毎ピクセルαブレンドだけして負荷になるので描かない
	if (g_pFade->GetColor().w <= 0.0f) {
		return;
	}
	g_pFade->Draw();
}

void Fade_Finalize(void)
{
	if (g_pFade) {
		delete g_pFade;
		g_pFade = nullptr;
	}
}

void SetSceneFade(SCENE ns)
{
	if (g_pFade) {
		g_pFade->SetSceneFade(ns);
	}
}

void Fade_StartIn(void)
{
	if (g_pFade) {
		g_pFade->StartFadeIn();
	}
}

FADESTAT GetFadeState(void)
{
	if (g_pFade) {
		return g_pFade->GetState();
	}
	return FADE_NONE;
}