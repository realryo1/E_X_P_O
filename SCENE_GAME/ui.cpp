#include "ui.h"
#include "define.h"
#include "font.h"
#include "field.h"
#include "collision.h"
#include "player.h"
#include "sprite2d.h"
#include "main.h"
#include "renderer.h"
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

#if defined(_DEBUG)
static DrawFont* g_pModelStatusText = nullptr;
static DrawFont* g_pCollisionStatusText = nullptr;
static DrawFont* g_pHitModelText = nullptr;
static DrawFont* g_pMemoryStatusText = nullptr;
static std::chrono::steady_clock::time_point g_LastMemoryStatusUpdate;
static bool g_HasMemoryStatusUpdate = false;

static void ApplyLoadStatus(void)
{
	if (!g_pModelStatusText)
	{
		return;
	}

	char statusLabel[768] = {};
	if (Field_IsLoadComplete())
	{
		Field_GetFinishedStatus(statusLabel, sizeof(statusLabel));
	}
	else
	{
		Field_GetLoadStatus(statusLabel, sizeof(statusLabel));
	}
	const std::string status(statusLabel);
	const std::string marker = "衝突 AABB";
	const size_t collisionAt = status.find(marker);
	if (collisionAt == std::string::npos)
	{
		g_pModelStatusText->SetText(status);
		g_pCollisionStatusText->SetText("");
		return;
	}

	size_t modelEnd = collisionAt;
	while (modelEnd > 0 &&
		(status[modelEnd - 1] == ' ' || status[modelEnd - 1] == '/'))
	{
		--modelEnd;
	}
	g_pModelStatusText->SetText(status.substr(0, modelEnd));
	g_pCollisionStatusText->SetText(status.substr(collisionAt));
}

static void UpdateMemoryStatus(void)
{
	if (!g_pMemoryStatusText)
	{
		return;
	}
	const std::chrono::steady_clock::time_point now =
		std::chrono::steady_clock::now();
	if (g_HasMemoryStatusUpdate &&
		now - g_LastMemoryStatusUpdate < std::chrono::milliseconds(250))
	{
		return;
	}
	g_LastMemoryStatusUpdate = now;
	g_HasMemoryStatusUpdate = true;

	char memoryStatus[256] = {};
	Field_GetMemoryStatus(memoryStatus, sizeof(memoryStatus));
	g_pMemoryStatusText->SetText(memoryStatus);
}

static void UpdateHitModelStatus(void)
{
	if (!g_pHitModelText)
	{
		return;
	}
	char hitStatus[384] = {};
	Collision_GetLastHitStatus(hitStatus, sizeof(hitStatus));
	char nearestName[128] = {};
	if (Player_IsReady() &&
		Field_GetNearestHighDetailName(
			Player_GetPos(),
			nearestName,
			sizeof(nearestName)))
	{
		const size_t used = strlen(hitStatus);
		if (used + strlen(nearestName) + 4 < sizeof(hitStatus))
		{
			strcat_s(hitStatus, " / ");
			strcat_s(hitStatus, nearestName);
		}
	}
	g_pHitModelText->SetText(hitStatus);
}
#endif

// パビリオン名のトースト。近くの建物が変わったとき、その名前を一定時間出す
static DrawFont* g_pPavilionToastText = nullptr;
static Sprite2D* g_pPavilionToastBack = nullptr;
static char g_PavilionToastShown[512] = {};
static float g_PavilionToastWidth = 220.0f;
static float g_PavilionToastTimer = 0.0f;
static float g_PavilionToastCheckTimer = 0.0f;
static const float PAVILION_TOAST_RANGE = 18.0f;
static const float PAVILION_TOAST_SECONDS = 2.6f;
static const float PAVILION_TOAST_FADE_IN_SECONDS = 0.3f;
static const float PAVILION_TOAST_FADE_OUT_SECONDS = 0.4f;
static const float PAVILION_TOAST_CHECK_SECONDS = 0.2f;
static const float PAVILION_TOAST_Y = 112.0f;

// 表示用に1行へ詰める。改行は区切りとして切り、長い名前は文字数で切って「…」を付ける
static std::string MakeToastLabel(const char* name, int* outCodePoints)
{
	const int kMaxCodePoints = 22;
	std::string label;
	int codePoints = 0;
	const unsigned char* bytes = reinterpret_cast<const unsigned char*>(name);
	while (*bytes != '\0' && *bytes != '\n')
	{
		if (codePoints == kMaxCodePoints)
		{
			label += "\xE2\x80\xA6";
			break;
		}
		const unsigned char lead = *bytes;
		const size_t length =
			lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
		size_t i = 0;
		for (; i < length && bytes[i] != '\0'; ++i)
		{
			label.push_back(static_cast<char>(bytes[i]));
		}
		if (i < length)
		{
			break;
		}
		bytes += length;
		++codePoints;
	}
	if (outCodePoints)
	{
		*outCodePoints = codePoints;
	}
	return label;
}

static void UpdatePavilionToast(void)
{
	const float frameSeconds = 1.0f / FPS;
	if (g_PavilionToastTimer > 0.0f)
	{
		g_PavilionToastTimer = fmaxf(g_PavilionToastTimer - frameSeconds, 0.0f);
	}
	g_PavilionToastCheckTimer -= frameSeconds;
	if (g_PavilionToastCheckTimer > 0.0f)
	{
		return;
	}
	g_PavilionToastCheckTimer = PAVILION_TOAST_CHECK_SECONDS;
	if (!g_pPavilionToastText || !Field_IsLoadComplete() || !Player_IsReady())
	{
		return;
	}

	char name[512] = {};
	float distance = FLT_MAX;
	if (!Field_GetNearestPavilionLabel(Player_GetPos(), name, sizeof(name), &distance) ||
		distance > PAVILION_TOAST_RANGE)
	{
		// 範囲外へ出たら、次に近づいたとき同じ建物でも再表示する
		g_PavilionToastShown[0] = '\0';
		return;
	}
	if (strcmp(name, g_PavilionToastShown) == 0)
	{
		return;
	}
	strncpy_s(g_PavilionToastShown, sizeof(g_PavilionToastShown), name, _TRUNCATE);
	int codePoints = 0;
	g_pPavilionToastText->SetText(MakeToastLabel(name, &codePoints));
	g_PavilionToastWidth = fminf(fmaxf(120.0f + 22.0f * codePoints, 220.0f), 820.0f);
	g_PavilionToastTimer = PAVILION_TOAST_SECONDS;
}

static void DrawPavilionToast(void)
{
	if (!g_pPavilionToastText || !g_pPavilionToastBack || g_PavilionToastTimer <= 0.0f)
	{
		return;
	}

	// 出現時は下から少し上がって拡大し、終わりに帯が薄れる（文字は色を変えられないので表示・非表示で扱う）
	const float age = PAVILION_TOAST_SECONDS - g_PavilionToastTimer;
	const float rise = fminf(age / PAVILION_TOAST_FADE_IN_SECONDS, 1.0f);
	const float popRest = 1.0f - rise;
	const float fadeOut = fminf(g_PavilionToastTimer / PAVILION_TOAST_FADE_OUT_SECONDS, 1.0f);
	const float y = PAVILION_TOAST_Y - 12.0f * popRest * popRest;
	const float textScale = 1.0f + 0.2f * popRest * popRest;
	g_pPavilionToastBack->SetPos({ SCREEN_X * 0.5f, y });
	g_pPavilionToastBack->SetSize({ g_PavilionToastWidth, 52.0f });
	g_pPavilionToastBack->SetColor({ 0.0f, 0.0f, 0.0f, 0.55f * rise * fadeOut });
	g_pPavilionToastBack->Draw();

	if (g_PavilionToastTimer > PAVILION_TOAST_FADE_OUT_SECONDS * 0.5f)
	{
		g_pPavilionToastText->SetPos({ SCREEN_X * 0.5f, y });
		g_pPavilionToastText->SetSize({ textScale, textScale });
		g_pPavilionToastText->Draw();
	}
}

void Ui_Initialize(void)
{
	g_PavilionToastShown[0] = '\0';
	g_PavilionToastTimer = 0.0f;
	g_PavilionToastCheckTimer = 0.0f;
	g_pPavilionToastBack = new Sprite2D(
		{ SCREEN_X * 0.5f, PAVILION_TOAST_Y },
		{ 0.0f, 0.0f },
		0.0f,
		{ 0.0f, 0.0f, 0.0f, 0.0f },
		BLENDSTATE_ALFA,
		L"asset\\texture\\fade.png");
	g_pPavilionToastText = new DrawFont(
		{ SCREEN_X * 0.5f, PAVILION_TOAST_Y },
		26.0f,
		0.0f,
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		"",
		TA_MIDDLE);

#if defined(_DEBUG)
	g_HasMemoryStatusUpdate = false;
	g_pModelStatusText = new DrawFont(
		{ SCREEN_X / 2.0f, SCREEN_Y - 130.0f },
		22.0f,
		0.0f,
		{ 0.9f, 0.5f, 0.6f, 1.0f },
		"読込中 0/0"
	);

	g_pCollisionStatusText = new DrawFont(
		{ SCREEN_X / 2.0f, SCREEN_Y - 98.0f },
		18.0f,
		0.0f,
		{ 0.9f, 0.7f, 0.4f, 1.0f },
		"衝突 AABB なし"
	);

	g_pHitModelText = new DrawFont(
		{ SCREEN_X / 2.0f, 36.0f },
		22.0f,
		0.0f,
		{ 1.0f, 0.85f, 0.2f, 1.0f },
		"衝突なし"
	);

	g_pMemoryStatusText = new DrawFont(
		{ SCREEN_X / 2.0f, SCREEN_Y - 66.0f },
		16.0f,
		0.0f,
		{ 0.1f, 0.6f, 0.9f, 1.0f },
		"メモリ"
	);

	ApplyLoadStatus();
#endif
}

void Ui_Finalize(void)
{
#if defined(_DEBUG)
	SAFE_DELETE(g_pModelStatusText);
	SAFE_DELETE(g_pCollisionStatusText);
	SAFE_DELETE(g_pHitModelText);
	SAFE_DELETE(g_pMemoryStatusText);
#endif
	SAFE_DELETE(g_pPavilionToastText);
	SAFE_DELETE(g_pPavilionToastBack);
}

void Ui_Update(void)
{
	UpdatePavilionToast();
#if defined(_DEBUG)
	ApplyLoadStatus();
	UpdateHitModelStatus();
	UpdateMemoryStatus();
#endif
}

void Ui_ResetMaterial(void)
{
	MATERIAL material = {};
	material.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	material.Ambient = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	SetMaterial(material);
}

void Ui_Draw(void)
{
	DrawPavilionToast();
#if defined(_DEBUG)
	if (g_pModelStatusText) g_pModelStatusText->Draw();
	if (g_pCollisionStatusText) g_pCollisionStatusText->Draw();
	if (g_pHitModelText) g_pHitModelText->Draw();
	if (g_pMemoryStatusText) g_pMemoryStatusText->Draw();
#endif
}
