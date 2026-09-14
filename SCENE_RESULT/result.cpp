#include "result.h"
#include "define.h"
#include "font.h"
#include "input_manager.h"
#include "fade.h"
#include "scene.h"
#include "main.h"

using namespace DirectX;

static DrawFont* g_pResultText = nullptr;
static DrawFont* g_pHintText = nullptr;

void Result_Initialize(void)
{
	g_pResultText = new DrawFont(
		{ SCREEN_X / 2.0f, SCREEN_Y / 2.0f - 40.0f },
		48.0f,
		0.0f,
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		"リザルト"
	);

	g_pHintText = new DrawFont(
		{ SCREEN_X / 2.0f, SCREEN_Y / 2.0f + 40.0f },
		28.0f,
		0.0f,
		{ 0.8f, 0.8f, 0.8f, 1.0f },
		"決定でタイトルへ"
	);
}

void Result_Update(void)
{
	if (Input_IsActionTrigger(INPUT_ACTION_DECIDE))
	{
		SetSceneFade(SCENE_TITLE);
	}
}

void Result_Draw(void)
{
	if (g_pResultText) g_pResultText->Draw();
	if (g_pHintText) g_pHintText->Draw();
}

void Result_Finalize(void)
{
	SAFE_DELETE(g_pResultText);
	SAFE_DELETE(g_pHintText);
}
