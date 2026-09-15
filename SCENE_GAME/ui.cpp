#include "ui.h"
#include "define.h"
#include "font.h"
#include "field.h"
#include "main.h"
#include "renderer.h"
#include <chrono>
#include <cstring>
#include <string>

#if defined(_DEBUG)
static DrawFont* g_pModelStatusText = nullptr;
static DrawFont* g_pCollisionStatusText = nullptr;
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
#endif

void Ui_Initialize(void)
{
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
	SAFE_DELETE(g_pMemoryStatusText);
#endif
}

void Ui_Update(void)
{
#if defined(_DEBUG)
	ApplyLoadStatus();
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
#if defined(_DEBUG)
	if (g_pModelStatusText) g_pModelStatusText->Draw();
	if (g_pCollisionStatusText) g_pCollisionStatusText->Draw();
	if (g_pMemoryStatusText) g_pMemoryStatusText->Draw();
#endif
}
