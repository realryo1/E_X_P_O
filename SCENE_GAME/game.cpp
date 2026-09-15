#include "game.h"
#include "field.h"
#include "course.h"
#include "gameaudio.h"
#include "player.h"
#include "playercamera.h"
#include "renderer.h"
#include "sunlight.h"
#include "envprobe.h"
#include "ui.h"
#include "mouse.h"
#include "fade.h"
#include "keyboard.h"
#include "../framework/debug_ostream.h"
#include "imgui/imgui.h"
#include <chrono>

static bool g_PrevLeftButton = false;
static bool g_InitialLoadNotified = false;
#if defined(_DEBUG)
static bool g_EnableLocalShadowPass = true;
static bool g_EnableSkybox = true;
#endif

#if defined(_DEBUG)
static void Game_UpdateDebugRenderToggles(void)
{
	if (Keyboard_IsKeyDownTrigger(KK_F5))
	{
		g_EnableLocalShadowPass = !g_EnableLocalShadowPass;
		hal::dout << "[Debug Render] Local shadow pass: "
			<< (g_EnableLocalShadowPass ? "ON" : "OFF") << std::endl;
		RequestRedraw();
	}
	if (Keyboard_IsKeyDownTrigger(KK_F6))
	{
		g_EnableSkybox = !g_EnableSkybox;
		Field_SetSkyboxEnabled(g_EnableSkybox);
		hal::dout << "[Debug Render] Skybox: "
			<< (g_EnableSkybox ? "ON" : "OFF") << std::endl;
		RequestRedraw();
	}
}
#endif

static void Game_UpdateMouseLock(void)
{
	if (Course_IsMenuOpen() || PlayerCamera_IsDebugActive())
	{
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
	Fade_HoldUntilReady();
	PlayerCamera_Initialize();
	Field_Initialize();
	GameAudio_Initialize();
	Course_Initialize();
	Sunlight_Initialize();
	EnvProbe_Initialize();
	Ui_Initialize();
	g_PrevLeftButton = false;
	g_InitialLoadNotified = false;
#if defined(_DEBUG)
	g_EnableLocalShadowPass = true;
	g_EnableSkybox = true;
	Field_SetSkyboxEnabled(true);
#endif
}

void Game_Update(void)
{
#if defined(_DEBUG)
	Game_UpdateDebugRenderToggles();
#endif
	static const auto shaderTimeOrigin = std::chrono::steady_clock::now();
	const float shaderSeconds = std::chrono::duration<float>(
		std::chrono::steady_clock::now() - shaderTimeOrigin).count();
	SetShaderTime(shaderSeconds);
#if defined(_DEBUG)
	Direct3D_DebugStageBegin(DIRECT3D_DEBUG_STAGE_PUMP);
#endif
	Field_PumpLoad();
	GameAudio_Pump();
#if defined(_DEBUG)
	Direct3D_DebugStageEnd(DIRECT3D_DEBUG_STAGE_PUMP);
#endif
	const bool fieldReady = Field_IsLoadComplete();
	if (fieldReady)
	{
		if (!g_InitialLoadNotified)
		{
			Fade_NotifyReady();
			g_InitialLoadNotified = true;
		}
	}
	else
	{
		Fade_SetLoadProgress(Field_GetInitialLoadProgress());
		UnLockMouse();
	}

	if (fieldReady)
	{
		Game_UpdateMouseLock();
	}
	PlayerCamera_UpdateInput();
	Player_Update();
	if (fieldReady)
	{
		Course_Update();
	}
	PlayerCamera_Update();
	Ui_Update();
	Sunlight_Update();
}

void Game_PumpAfterPresent(double lastDrawMs, float lastGpuMs)
{
#if defined(_DEBUG)
	Direct3D_DebugStageBegin(DIRECT3D_DEBUG_STAGE_PUMP);
#endif
	Field_PumpAfterPresent(lastDrawMs, lastGpuMs);
#if defined(_DEBUG)
	Direct3D_DebugStageEnd(DIRECT3D_DEBUG_STAGE_PUMP);
#endif
}

void Game_Draw(void)
{
	if (!Field_IsLoadComplete())
	{
		Field_Draw();
		return;
	}

	Direct3D_BeginScene();

	PlayerCamera_Draw();

	SetDepthEnable(true);
	Sunlight_Apply();
#if defined(_DEBUG)
	const bool drawLocalShadow = g_EnableLocalShadowPass;
#else
	const bool drawLocalShadow = true;
#endif
	XMMATRIX shadowView[NUM_SHADOW_CASCADES] = {};
	XMMATRIX shadowProjection[NUM_SHADOW_CASCADES] = {};
	XMFLOAT3 shadowFocus[NUM_SHADOW_CASCADES] = {};
	float shadowRadius[NUM_SHADOW_CASCADES] = {};
#if defined(_DEBUG)
	Direct3D_DebugStageBegin(DIRECT3D_DEBUG_STAGE_SHADOW);
#endif
	if (drawLocalShadow && Sunlight_BeginLocalShadow(
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
#if defined(_DEBUG)
	Direct3D_DebugStageEnd(DIRECT3D_DEBUG_STAGE_SHADOW);
	Direct3D_DebugStageBegin(DIRECT3D_DEBUG_STAGE_FIELD);
#endif
	EnvProbe_CaptureOneFace();
	Field_Draw();
#if defined(_DEBUG)
	Direct3D_DebugStageEnd(DIRECT3D_DEBUG_STAGE_FIELD);
	Direct3D_DebugStageBegin(DIRECT3D_DEBUG_STAGE_OBJECTS);
#endif
	Player_Draw();
	Course_Draw();
	Direct3D_ApplySsao();
#if defined(_DEBUG)
	Direct3D_DebugStageEnd(DIRECT3D_DEBUG_STAGE_OBJECTS);
	Direct3D_DebugStageBegin(DIRECT3D_DEBUG_STAGE_UI);
#endif

	Ui_ResetMaterial();
	SetDepthEnable(false);
	Ui_Draw();
	Course_DrawHud();
	Player_DrawDebug();
	Field_DrawDebug();
	Course_DrawMenu();
	Sunlight_DrawDebug();
	PlayerCamera_DrawDebug();
#if defined(_DEBUG)
	Direct3D_DebugStageEnd(DIRECT3D_DEBUG_STAGE_UI);
#endif
}

void Game_Finalize(void)
{
	Course_Finalize();
	Player_Finalize();
	GameAudio_Finalize();
	PlayerCamera_Finalize();
	Sunlight_Finalize();
	EnvProbe_Finalize();
	Field_Finalize();
	Ui_Finalize();
}
