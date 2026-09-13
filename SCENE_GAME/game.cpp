#include "game.h"
#include "field.h"
#include "course.h"
#include "player.h"
#include "playercamera.h"
#include "renderer.h"
#include "sunlight.h"
#include "ui.h"
#include "keyboard.h"
#include "mouse.h"
#include "imgui/imgui.h"

static bool g_PrevLeftButton = false;

static void Game_UpdateMouseLock(void)
{
	if (Keyboard_IsKeyDownTrigger(KK_ESCAPE))
	{
		UnLockMouse();
		return;
	}

	// 相対モード中に GetState すると視点用の dx/dy を消費する。
	if (!Mouse_IsVisible())
	{
		return;
	}

	Mouse_State mouseState;
	Mouse_GetState(&mouseState);
	const bool leftTrigger = mouseState.leftButton && !g_PrevLeftButton;
	g_PrevLeftButton = mouseState.leftButton;

	const bool imguiBlocks =
		ImGui::GetCurrentContext() != nullptr &&
		ImGui::GetIO().WantCaptureMouse;
	if (leftTrigger && !imguiBlocks)
	{
		LockMouse();
	}
}

void Game_Initialize(void)
{
	PlayerCamera_Initialize();
	Field_Initialize();
	Course_Initialize();
	Sunlight_Initialize();
	Ui_Initialize();
	g_PrevLeftButton = false;
}

void Game_Update(void)
{
	Field_PumpLoad();

	Game_UpdateMouseLock();
	PlayerCamera_UpdateInput();
	Player_Update();
	Course_Update();
	PlayerCamera_Update();
	Ui_Update();
	Sunlight_Update();
}

void Game_Draw(void)
{
	PlayerCamera_Draw();

	SetDepthEnable(true);
	Sunlight_Apply();
	XMMATRIX shadowView[NUM_SHADOW_CASCADES] = {};
	XMMATRIX shadowProjection[NUM_SHADOW_CASCADES] = {};
	XMFLOAT3 shadowFocus[NUM_SHADOW_CASCADES] = {};
	float shadowRadius[NUM_SHADOW_CASCADES] = {};
	if (Sunlight_BeginLocalShadow(
		shadowView,
		shadowProjection,
		shadowFocus,
		shadowRadius))
	{
		Field_DrawLocalShadow(
			shadowView[0],
			shadowProjection[0],
			shadowFocus[0],
			shadowRadius[0]);
		Player_DrawLocalShadow(
			shadowView[0],
			shadowProjection[0],
			shadowFocus[0],
			shadowRadius[0]);
		for (int i = 1; i < NUM_SHADOW_CASCADES; ++i)
		{
			BeginShadowMapSlice(i);
			Field_DrawLocalShadow(
				shadowView[i],
				shadowProjection[i],
				shadowFocus[i],
				shadowRadius[i]);
			Player_DrawLocalShadow(
				shadowView[i],
				shadowProjection[i],
				shadowFocus[i],
				shadowRadius[i]);
		}
		Sunlight_EndLocalShadow();
	}
	Field_Draw();
	Player_Draw();
	Course_Draw();

	Ui_ResetMaterial();
	SetDepthEnable(false);
	Ui_Draw();
	Course_DrawHud();
	Player_DrawDebug();
	Course_DrawDebug();
	Sunlight_DrawDebug();
}

void Game_Finalize(void)
{
	Course_Finalize();
	Player_Finalize();
	PlayerCamera_Finalize();
	Sunlight_Finalize();
	Field_Finalize();
	Ui_Finalize();
}
