#include "scene.h"
#include "game.h"
#include "renderer.h"
#include "keyboard.h"
#include "texture.h"
#include "title.h"
#include "result.h"
#if defined(_DEBUG)
#include "debugscene.h"
#endif
#include "define.h"
#include "main.h"
#include <Windows.h>
using namespace DirectX;

static SCENE scene = SCENE_GAME;

#if !defined(_DEBUG)
static void AlertDebugSceneUnavailable(const wchar_t* detail)
{
	MessageBoxW(
		NULL,
		detail,
		L"Scene Transition Cancelled",
		MB_OK | MB_ICONWARNING);
}
#endif

void Init( void )
{
#if !defined(_DEBUG)
	if (scene == SCENE_DEBUG)
	{
		AlertDebugSceneUnavailable(
			L"初期シーンが SCENE_DEBUG に設定されていますが、\n"
			L"Release ビルドでは利用できません。\n"
			L"SCENE_TITLE にフォールバックします。");
		scene = SCENE_TITLE;
	}
#endif

	switch ( scene )
	{
		case SCENE_TITLE:
		Title_Initialize();
		break;
		case SCENE_GAME:
		Game_Initialize();
		break;
		case SCENE_RESULT:
		Result_Initialize();
		break;
#if defined(_DEBUG)
		case SCENE_DEBUG:
		DebugScene_Initialize();
		break;
#endif
		default:
		break;
	}
}

void Update( void )
{
	if (Keyboard_IsKeyDownTrigger(KK_F2))
	{
		TakeScreenshot();
	}

	switch ( scene )
	{
		case SCENE_TITLE:
		Title_Update();
		break;
		case SCENE_GAME:
		Game_Update();
		break;
		case SCENE_RESULT:
		Result_Update();
		break;
#if defined(_DEBUG)
		case SCENE_DEBUG:
		DebugScene_Update();
		break;
#endif
		default:
		break;
	}
}

void Draw( void )
{
	switch ( scene )
	{
		case SCENE_TITLE:
		Title_Draw();
		break;
		case SCENE_GAME:
		Game_Draw();
		break;
		case SCENE_RESULT:
		Result_Draw();
		break;
#if defined(_DEBUG)
		case SCENE_DEBUG:
		DebugScene_Draw();
		break;
#endif
		default:
		break;
	}
}

void Finalize( void )
{
	switch ( scene )
	{
		case SCENE_TITLE:
		Title_Finalize();
		break;
		case SCENE_GAME:
		Game_Finalize();
		break;
		case SCENE_RESULT:
		Result_Finalize();
		break;
#if defined(_DEBUG)
		case SCENE_DEBUG:
		DebugScene_Finalize();
		break;
#endif
		default:
		break;
	}
}

void ApplySceneInternal( SCENE id )
{
#if !defined(_DEBUG)
	if (id == SCENE_DEBUG)
	{
		AlertDebugSceneUnavailable(
			L"SCENE_DEBUG は Release ビルドでは利用できません。\n"
			L"シーン遷移をキャンセルしました。");
		return;
	}
#endif

	Finalize();

	scene = id;

	Init();
	RequestRedraw();
}

SCENE GetScene( void )
{
	return scene;
}
