#include "debugscene.h"
#include "debug_model_scene.h"
#include "debug_lighting_scene.h"
#include "debug_toon_scene.h"
#include "keyboard.h"
#include "mouse.h"
#include "fade.h"
#include "scene.h"

using namespace DirectX;

// デバッグシーンのサブタイプ
enum DEBUG_TYPE {
	DEBUG_MODEL = 0,
	DEBUG_LIGHTING,
	DEBUG_TOON,
	DEBUG_MAX
};

static DEBUG_TYPE g_type = DEBUG_MODEL;

void DebugScene_Initialize(void)
{
	switch (g_type)
	{
	case DEBUG_MODEL:
		DebugModelScene_Initialize();
		break;
	case DEBUG_LIGHTING:
		DebugLightingScene_Initialize();
		break;
	case DEBUG_TOON:
		DebugToonScene_Initialize();
		break;
	default:
		break;
	}
}

void DebugScene_Update(void)
{
	if (Keyboard_IsKeyDownTrigger(KK_ESCAPE))
	{
		UnLockMouse();
		SetSceneFade(SCENE_TITLE);
	}

	// Tab キーでデバッグシーン切り替え
	if (Keyboard_IsKeyDownTrigger(KK_TAB))
	{
		switch (g_type)
		{
		case DEBUG_MODEL:
			DebugModelScene_Finalize();
			break;
		case DEBUG_LIGHTING:
			DebugLightingScene_Finalize();
			break;
		case DEBUG_TOON:
			DebugToonScene_Finalize();
			break;
		default:
			break;
		}

		g_type = (DEBUG_TYPE)((g_type + 1) % DEBUG_MAX);

		switch (g_type)
		{
		case DEBUG_MODEL:
			DebugModelScene_Initialize();
			break;
		case DEBUG_LIGHTING:
			DebugLightingScene_Initialize();
			break;
		case DEBUG_TOON:
			DebugToonScene_Initialize();
			break;
		default:
			break;
		}
	}

	switch (g_type)
	{
	case DEBUG_MODEL:
		DebugModelScene_Update();
		break;
	case DEBUG_LIGHTING:
		DebugLightingScene_Update();
		break;
	case DEBUG_TOON:
		DebugToonScene_Update();
		break;
	default:
		break;
	}
}

void DebugScene_Draw(void)
{
	switch (g_type)
	{
	case DEBUG_MODEL:
		DebugModelScene_Draw();
		break;
	case DEBUG_LIGHTING:
		DebugLightingScene_Draw();
		break;
	case DEBUG_TOON:
		DebugToonScene_Draw();
		break;
	default:
		break;
	}
}

void DebugScene_Finalize(void)
{
	switch (g_type)
	{
	case DEBUG_MODEL:
		DebugModelScene_Finalize();
		break;
	case DEBUG_LIGHTING:
		DebugLightingScene_Finalize();
		break;
	case DEBUG_TOON:
		DebugToonScene_Finalize();
		break;
	default:
		break;
	}
}
