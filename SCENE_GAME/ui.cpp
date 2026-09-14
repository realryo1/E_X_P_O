#include "ui.h"
#include "define.h"
#include "font.h"
#include "field.h"
#include "main.h"
#include "renderer.h"
#include <chrono>

static DrawFont* g_pModelStatusText = nullptr;
static DrawFont* g_pMemoryStatusText = nullptr;
static std::chrono::steady_clock::time_point g_LastMemoryStatusUpdate;
static bool g_HasMemoryStatusUpdate = false;

static void ApplyLoadStatus(void)
{
	if (!g_pModelStatusText)
	{
		return;
	}

	char statusLabel[256] = {};
	if (Field_IsLoadComplete())
	{
		Field_GetFinishedStatus(statusLabel, sizeof(statusLabel));
	}
	else
	{
		Field_GetLoadStatus(statusLabel, sizeof(statusLabel));
	}
	g_pModelStatusText->SetText(statusLabel);
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

void Ui_Initialize(void)
{
	g_HasMemoryStatusUpdate = false;
	g_pModelStatusText = new DrawFont(
		{ SCREEN_X / 2.0f, SCREEN_Y - 90.0f },
		22.0f,
		0.0f,
		{ 0.9f, 0.5f, 0.6f, 1.0f },
		"読込中 0/0"
	);

	g_pMemoryStatusText = new DrawFont(
		{ SCREEN_X / 2.0f, SCREEN_Y - 50.0f },
		16.0f,
		0.0f,
		{ 0.1f, 0.6f, 0.9f, 1.0f },
		"メモリ"
	);

	ApplyLoadStatus();
}

void Ui_Finalize(void)
{
	SAFE_DELETE(g_pModelStatusText);
	SAFE_DELETE(g_pMemoryStatusText);
}

void Ui_Update(void)
{
	ApplyLoadStatus();
	UpdateMemoryStatus();
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
	if (g_pModelStatusText) g_pModelStatusText->Draw();
	if (g_pMemoryStatusText) g_pMemoryStatusText->Draw();
}
