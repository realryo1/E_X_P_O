#include "title.h"
#include "define.h"
#include "font.h"
#include "ClickFont.h"
#include "input_manager.h"
#include "fade.h"
#include "scene.h"
#include "main.h"

using namespace DirectX;

static DrawFont* g_pTitleText = nullptr;
static DrawFont* g_pHintText = nullptr;
static ClickFont* g_pDebugButton = nullptr;

void Title_Initialize(void)
{
	g_pTitleText = new DrawFont(
		{ SCREEN_X / 2.0f, SCREEN_Y / 2.0f - 40.0f },
		48.0f,
		0.0f,
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		"TITLE"
	);

	g_pHintText = new DrawFont(
		{ SCREEN_X / 2.0f, SCREEN_Y / 2.0f + 40.0f },
		28.0f,
		0.0f,
		{ 0.8f, 0.8f, 0.8f, 1.0f },
		"Press Decide"
	);

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
}

void Title_Update(void)
{
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
}

void Title_Draw(void)
{
	if (g_pTitleText) g_pTitleText->Draw();
	if (g_pHintText) g_pHintText->Draw();
	if (g_pDebugButton) g_pDebugButton->Draw();
}

void Title_Finalize(void)
{
	SAFE_DELETE(g_pTitleText);
	SAFE_DELETE(g_pHintText);
	SAFE_DELETE(g_pDebugButton);
}
