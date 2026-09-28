#include "title.h"
#include "define.h"
#include "font.h"
#include "ClickFont.h"
#include "input_manager.h"
#include "fade.h"
#include "scene.h"
#include "sprite2d.h"
#include "sound.h"
#include "field.h"
#include "sunlight.h"
#include "main.h"
#include <cmath>
#include <Windows.h>
#include <fstream>
#include <string>

using namespace DirectX;

enum TitleAnimPhase
{
	TITLE_ANIM_WAIT = 0,
	TITLE_ANIM_TAXI,
	TITLE_ANIM_EXPO,
	TITLE_ANIM_RACE,
	TITLE_ANIM_IDLE
};

static Sprite2D* g_pBackground = nullptr;
static Sprite2D* g_pTaxi = nullptr;
static DrawFont* g_pExpoText = nullptr;
static DrawFont* g_pRaceText = nullptr;
static DrawFont* g_pHintText = nullptr;
#if defined(_DEBUG)
static DrawFont* g_pLoadDebugText = nullptr;
#endif
static ClickFont* g_pDebugButton = nullptr;
static SoundData* g_pTitleAppearSe = nullptr;
static SoundData* g_pTitleBgm = nullptr;

static TitleAnimPhase g_AnimPhase = TITLE_ANIM_WAIT;
static float g_AnimT = 0.0f;
static float g_BobTime = 0.0f;

static const XMFLOAT2 kTaxiSize = { 500.0f, 500.0f };
static const XMFLOAT2 kTaxiRest = { 700.0f, 500.0f };
static const XMFLOAT2 kExpoRest = { SCREEN_X / 4.0f, 160.0f };
static const XMFLOAT2 kRaceRest = { SCREEN_X / 4.0f * 3.0f, 160.0f };
static const float kTaxiStartX = SCREEN_X + kTaxiSize.x * 0.5f;
static const float kExpoStartX = -400.0f;
static const float kRaceStartX = SCREEN_X + 400.0f;
static const float kWaitSec = 1.0f;
static const float kFrameInSec = 0.70f;
static const float kBobAmp = 14.0f;
static const float kBobPeriod = 2.0f;

static float Title_EaseOutCubic(float t)
{
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	const float inv = 1.0f - t;
	return 1.0f - inv * inv * inv;
}

static float Title_Lerp(float a, float b, float t)
{
	return a + (b - a) * t;
}

void Title_Initialize(void)
{
	g_AnimPhase = TITLE_ANIM_WAIT;
	g_AnimT = 0.0f;
	g_BobTime = 0.0f;
	Field_Initialize();
	Sunlight_PrepareAssets();

	g_pBackground = new Sprite2D(
		{ SCREEN_X / 2.0f, SCREEN_Y / 2.0f },
		{ SCREEN_X, SCREEN_Y },
		0.0f,
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		BLENDSTATE_NONE,
		L"asset\\texture\\title.png"
	);

	g_pTaxi = new Sprite2D(
		{ kTaxiStartX, kTaxiRest.y },
		kTaxiSize,
		0.0f,
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		BLENDSTATE_ALFA,
		L"asset\\texture\\taxi.png"
	);

	g_pExpoText = new DrawFont(
		{ kExpoStartX, kExpoRest.y },
		200.0f,
		0.0f,
		{ 1.0f, 0.12f, 0.12f, 1.0f },
		"EXPO"
	);

	g_pRaceText = new DrawFont(
		{ kRaceStartX, kRaceRest.y },
		200.0f,
		0.0f,
		{ 0.18f, 0.35f, 1.0f, 1.0f },
		"RACE"
	);

	g_pHintText = new DrawFont(
		{ SCREEN_X / 2.0f, SCREEN_Y - 48.0f },
		24.0f,
		0.0f,
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		"Enterでスタート"
	);

#if defined(_DEBUG)
	g_pLoadDebugText = new DrawFont(
		{ 28.0f, SCREEN_Y - 82.0f },
		16.0f,
		0.0f,
		{ 1.0f, 0.85f, 0.25f, 1.0f },
		"TITLE PRELOAD",
		TA_START);
#endif

#if defined(_DEBUG)
	g_pDebugButton = new ClickFont(
		{ SCREEN_X - 80.0f, 40.0f },
		24.0f,
		0.0f,
		{ 0.7f, 0.7f, 0.7f, 1.0f },
		{ 1.0f, 1.0f, 0.4f, 1.0f },
		"DEBUG"
	);
#endif

	g_pTitleAppearSe = LoadMP3(L"asset\\sound\\se\\boost.mp3");
	g_pTitleBgm = LoadMP3(L"asset\\sound\\bgm\\race.mp3");
}

void Title_Update(void)
{
	Field_PumpPreload();

#if defined(_DEBUG)
	if (g_pLoadDebugText)
	{
		char status[768] = {};
		if (Field_IsLoadComplete())
		{
			Field_GetFinishedStatus(status, sizeof(status));
		}
		else
		{
			Field_GetLoadStatus(status, sizeof(status));
		}
		g_pLoadDebugText->SetText(
			std::string("TITLE PRELOAD | ") + status);
	}
#endif

	if (Input_IsActionTrigger(INPUT_ACTION_DECIDE))
	{
		SetSceneFade(SCENE_GAME);
	}

#if defined(_DEBUG)
	if (g_pDebugButton)
	{
		g_pDebugButton->Update();
		if (g_pDebugButton->IsClick())
		{
			SetSceneFade(SCENE_DEBUG);
		}
	}
#endif

	const float dt = 1.0f / FPS;

	if (g_AnimPhase == TITLE_ANIM_WAIT)
	{
		g_AnimT += dt;
		if (g_AnimT >= kWaitSec)
		{
			g_AnimT = 0.0f;
			g_AnimPhase = TITLE_ANIM_TAXI;
			PlaySound(g_pTitleAppearSe, false);
		}
	}
	else if (g_AnimPhase != TITLE_ANIM_IDLE)
	{
		g_AnimT += dt / kFrameInSec;
		const float t = Title_EaseOutCubic(g_AnimT);

		if (g_AnimPhase == TITLE_ANIM_TAXI && g_pTaxi)
		{
			g_pTaxi->SetPos({ Title_Lerp(kTaxiStartX, kTaxiRest.x, t), kTaxiRest.y });
		}
		else if (g_AnimPhase == TITLE_ANIM_EXPO && g_pExpoText)
		{
			g_pExpoText->SetPos({ Title_Lerp(kExpoStartX, kExpoRest.x, t), kExpoRest.y });
		}
		else if (g_AnimPhase == TITLE_ANIM_RACE && g_pRaceText)
		{
			g_pRaceText->SetPos({ Title_Lerp(kRaceStartX, kRaceRest.x, t), kRaceRest.y });
		}

		if (g_AnimT >= 1.0f)
		{
			g_AnimT = 0.0f;
			if (g_AnimPhase == TITLE_ANIM_TAXI && g_pTaxi)
			{
				g_pTaxi->SetPos(kTaxiRest);
			}
			else if (g_AnimPhase == TITLE_ANIM_EXPO && g_pExpoText)
			{
				g_pExpoText->SetPos(kExpoRest);
			}
			else if (g_AnimPhase == TITLE_ANIM_RACE && g_pRaceText)
			{
				g_pRaceText->SetPos(kRaceRest);
			}
			g_AnimPhase = static_cast<TitleAnimPhase>(g_AnimPhase + 1);
			if (g_AnimPhase == TITLE_ANIM_EXPO || g_AnimPhase == TITLE_ANIM_RACE)
			{
				PlaySound(g_pTitleAppearSe, false);
			}
			else if (g_AnimPhase == TITLE_ANIM_IDLE)
			{
				PlaySound(g_pTitleBgm, true);
			}
		}
	}

	if (g_AnimPhase >= TITLE_ANIM_EXPO && g_pTaxi)
	{
		g_BobTime += dt;
		const float bob = sinf(g_BobTime * (2.0f * 3.14159265f) / kBobPeriod) * kBobAmp;
		g_pTaxi->SetPos({ kTaxiRest.x, kTaxiRest.y + bob });
	}

	RequestRedraw();
}

void Title_Draw(void)
{
	if (g_pBackground) g_pBackground->Draw();
	if (g_pTaxi) g_pTaxi->Draw();
	if (g_pExpoText) g_pExpoText->Draw();
	if (g_pRaceText) g_pRaceText->Draw();
	if (g_pHintText) g_pHintText->Draw();
#if defined(_DEBUG)
	if (g_pLoadDebugText) g_pLoadDebugText->Draw();
#endif
	if (g_pDebugButton) g_pDebugButton->Draw();
}

void Title_Finalize(void)
{
	SAFE_DELETE(g_pBackground);
	SAFE_DELETE(g_pTaxi);
	SAFE_DELETE(g_pExpoText);
	SAFE_DELETE(g_pRaceText);
	SAFE_DELETE(g_pHintText);
#if defined(_DEBUG)
	SAFE_DELETE(g_pLoadDebugText);
#endif
	SAFE_DELETE(g_pDebugButton);
	StopSound(g_pTitleBgm);
	UnloadSound(g_pTitleAppearSe);
	UnloadSound(g_pTitleBgm);
	g_pTitleAppearSe = nullptr;
	g_pTitleBgm = nullptr;
}
