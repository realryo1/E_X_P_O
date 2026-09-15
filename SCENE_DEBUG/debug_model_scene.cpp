#pragma execution_character_set("utf-8")
/*==============================================================================
   デバッグモデルビューアシーン [debug_model_scene.cpp]
   asset\model と asset\expomodel のモデル候補を列挙し、
   選択中の1体だけを読み込んで表示する。
==============================================================================*/

#include <d3d11.h>
#include <DirectXMath.h>
#include <windows.h>
#include <string>
#include <vector>
#include "debug_model_scene.h"
#include "renderer.h"
#include "light.h"
#include "camera.h"
#include "model.h"
#include "billboard.h"
#include "font.h"
#include "keyboard.h"
#include "mouse.h"
#include "define.h"
#include "debugcamera.h"

using namespace DirectX;

struct DebugModelEntry
{
	std::string fileName;
	std::string filePath;
	MODEL* pModel = nullptr;
};

static std::vector<DebugModelEntry> g_Entries;
static int g_CurrentIndex = -1;
static AmbientLight* g_pAmbientLight = nullptr;
static PointLight* g_pFloorLight = nullptr;
static DrawFont* g_pModelNameFont = nullptr;
static DrawFont* g_pSubInfoFont = nullptr;
static DrawFont* g_pControlHintFont = nullptr;
static Billboard* g_pFloorBillboard = nullptr;
static MODEL* g_pCubeModel = nullptr;
static bool g_ShowOriginCube = false;
static bool isMouseLock = true;

static const float RIM_LIGHT_BRIGHTNESS = 1.0f;
static const float MODEL_VIEW_SCALE = 0.01f;
static const float SHADOW_BIAS = 0.004f;
static const float SHADOW_BRIGHTNESS = 0.55f;

static float GetCurrentModelScale()
{
	if (g_CurrentIndex >= 0 && g_CurrentIndex < (int)g_Entries.size() &&
		g_Entries[g_CurrentIndex].fileName == "cube.fbx")
	{
		return 1.0f;
	}
	return MODEL_VIEW_SCALE;
}

static void EnumerateModels()
{
	const char* patterns[] = {
		"asset\\model\\*.fbx",
		"asset\\model\\*.glb",
		"asset\\expomodel\\*.fbx",
		"asset\\expomodel\\*.glb",
	};
	const char* roots[] = {
		"asset\\model\\",
		"asset\\model\\",
		"asset\\expomodel\\",
		"asset\\expomodel\\",
	};

	for (size_t patternIndex = 0; patternIndex < _countof(patterns); ++patternIndex)
	{
		WIN32_FIND_DATAA fd = {};
		HANDLE hFind = FindFirstFileA(patterns[patternIndex], &fd);
		if (hFind == INVALID_HANDLE_VALUE)
		{
			continue;
		}

		do
		{
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				continue;
			}

			DebugModelEntry entry;
			entry.fileName = fd.cFileName;
			entry.filePath = roots[patternIndex] + entry.fileName;
			g_Entries.push_back(entry);
		} while (FindNextFileA(hFind, &fd));

		FindClose(hFind);
	}
}

static void ReleaseCurrentModel()
{
	if (g_CurrentIndex < 0 || g_CurrentIndex >= (int)g_Entries.size())
	{
		return;
	}

	if (g_Entries[g_CurrentIndex].pModel)
	{
		ModelRelease(g_Entries[g_CurrentIndex].pModel);
		g_Entries[g_CurrentIndex].pModel = nullptr;
	}
}

static void LoadCurrentModel()
{
	if (g_CurrentIndex < 0 || g_CurrentIndex >= (int)g_Entries.size())
	{
		return;
	}

	g_Entries[g_CurrentIndex].pModel =
		ModelLoad(g_Entries[g_CurrentIndex].filePath.c_str());
}

static void SelectModel(int index)
{
	if (g_Entries.empty())
	{
		g_CurrentIndex = -1;
		return;
	}

	ReleaseCurrentModel();
	const int count = (int)g_Entries.size();
	g_CurrentIndex = (index % count + count) % count;
	LoadCurrentModel();
}

static void ReloadAllModels()
{
	const int selectedIndex = g_CurrentIndex;
	ReleaseCurrentModel();
	g_Entries.clear();
	EnumerateModels();

	if (g_Entries.empty())
	{
		g_CurrentIndex = -1;
		return;
	}

	g_CurrentIndex = selectedIndex;
	if (g_CurrentIndex < 0 || g_CurrentIndex >= (int)g_Entries.size())
	{
		g_CurrentIndex = 0;
	}
	LoadCurrentModel();
}

void DebugModelScene_Initialize(void)
{
	isMouseLock = true;
	g_ShowOriginCube = false;

	g_pAmbientLight = new AmbientLight(XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f));
	g_pFloorLight = new PointLight(
		TRUE,
		XMFLOAT4(0.0f, 5.0f, -5.0f, 1.0f),
		XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
		50.0f,
		1.0f
	);

	g_pFloorBillboard = new Billboard(
		XMFLOAT3(0.0f, -0.5f, 0.0f),
		XMFLOAT2(1.0f, 1.0f),
		XMFLOAT3(90.0f, 0.0f, 0.0f),
		"asset\\texture\\notfound_thumbnail.png",
		false
	);
	g_pFloorBillboard->SetBillboardMode(false);
	g_pFloorBillboard->SetReceiveShadow(true);

	g_pCubeModel = ModelLoad("asset\\model\\cube.fbx");

	EnumerateModels();
	g_CurrentIndex = g_Entries.empty() ? -1 : 0;
	LoadCurrentModel();

	DebugCamera_Initialize();

	g_pModelNameFont = new DrawFont(
		{ SCREEN_X / 2.0f, SCREEN_Y - 140.0f },
		36.0f,
		0.0f,
		{ 1, 1, 1, 1 },
		""
	);
	g_pSubInfoFont = new DrawFont(
		{ SCREEN_X / 2.0f, SCREEN_Y - 90.0f },
		28.0f,
		0.0f,
		{ 0.8f, 0.8f, 0.2f, 1 },
		""
	);
	g_pControlHintFont = new DrawFont(
		{ SCREEN_X / 2.0f, 30.0f },
		22.0f,
		0.0f,
		{ 0.6f, 0.6f, 0.6f, 1 },
		"WASD:Move  Mouse:Look  Left/Right:Model  U:Mouse  B:OriginCube  R:Reload"
	);
	g_pControlHintFont->PreCacheGlyphs();
}

void DebugModelScene_Update(void)
{
	if (Keyboard_IsKeyDownTrigger(KK_LEFT))
	{
		SelectModel(g_CurrentIndex - 1);
	}
	if (Keyboard_IsKeyDownTrigger(KK_RIGHT))
	{
		SelectModel(g_CurrentIndex + 1);
	}

	if (Keyboard_IsKeyDownTrigger(KK_U))
	{
		if (isMouseLock)
		{
			UnLockMouse();
			isMouseLock = false;
		}
		else
		{
			LockMouse();
			isMouseLock = true;
		}
	}
	if (Keyboard_IsKeyDownTrigger(KK_B))
	{
		g_ShowOriginCube = !g_ShowOriginCube;
	}
	if (Keyboard_IsKeyDownTrigger(KK_R))
	{
		ReloadAllModels();
	}

	DebugCamera_Update();
	if (GetCamera())
	{
		SetCameraPosition(GetCamera()->GetPos());
	}

	if (g_pModelNameFont && g_pSubInfoFont)
	{
		if (g_CurrentIndex >= 0 && g_CurrentIndex < (int)g_Entries.size())
		{
			g_pModelNameFont->SetText(g_Entries[g_CurrentIndex].fileName);
			g_pSubInfoFont->SetText(
				std::to_string(g_CurrentIndex + 1) + " / " +
				std::to_string(g_Entries.size()) +
				(g_Entries[g_CurrentIndex].pModel ? "" : "  [LOAD FAILED]")
			);
		}
		else
		{
			g_pModelNameFont->SetText("No model found");
			g_pSubInfoFont->SetText("asset\\model / asset\\expomodel");
		}
		g_pModelNameFont->PreCacheGlyphs();
		g_pSubInfoFont->PreCacheGlyphs();
	}
}

void DebugModelScene_Draw(void)
{
	SetDepthEnable(true);
	if (g_pFloorLight && g_pAmbientLight)
	{
		g_pFloorLight->Apply(*g_pAmbientLight);
	}

	XMFLOAT4 lightPositionValue =
		g_pFloorLight
		? g_pFloorLight->GetPosition()
		: XMFLOAT4(0.0f, 5.0f, -5.0f, 1.0f);
	XMVECTOR shadowEye = XMLoadFloat4(&lightPositionValue);
	XMVECTOR shadowCenter = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	XMMATRIX lightView = XMMatrixLookAtLH(
		shadowEye,
		shadowCenter,
		XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)
	);
	float shadowFarZ = g_pFloorLight ? g_pFloorLight->GetRange() + 20.0f : 80.0f;
	XMMATRIX lightProjection = XMMatrixPerspectiveFovLH(
		XMConvertToRadians(50.0f),
		1.0f,
		0.5f,
		shadowFarZ
	);
	SetShadowMatrix(
		lightView * lightProjection,
		XMFLOAT4(SHADOW_BIAS, SHADOW_BRIGHTNESS, 0.0f, 0.0f)
	);

	BeginShadowMap();
	SetCullState(CULLSTATE_BACK);
	const float currentModelScale = GetCurrentModelScale();
	if (g_CurrentIndex >= 0 && g_CurrentIndex < (int)g_Entries.size() &&
		g_Entries[g_CurrentIndex].pModel)
	{
		ModelDrawShadowMap(
			g_Entries[g_CurrentIndex].pModel,
			{ 0.0f, 0.0f, 0.0f },
			{ 0.0f, 0.0f, 0.0f },
			{ currentModelScale, currentModelScale, currentModelScale },
			lightView,
			lightProjection
		);
	}
	SetCullState(CULLSTATE_NONE);
	EndShadowMap();

	if (g_pFloorBillboard)
	{
		for (int z = -8; z < 8; ++z)
		{
			for (int x = -8; x < 8; ++x)
			{
				g_pFloorBillboard->SetPos(
					{ (float)x + 0.5f, -0.5f, (float)z + 0.5f }
				);
				g_pFloorBillboard->SetSize({ 1.0f, 1.0f });
				g_pFloorBillboard->Draw();
			}
		}
	}

	SetParameter(XMFLOAT4(RIM_LIGHT_BRIGHTNESS, 0.0f, 0.0f, 0.0f));
	if (g_CurrentIndex >= 0 && g_CurrentIndex < (int)g_Entries.size() &&
		g_Entries[g_CurrentIndex].pModel)
	{
		ModelDraw(
			g_Entries[g_CurrentIndex].pModel,
			{ 0.0f, 0.0f, 0.0f },
			{ 0.0f, 0.0f, 0.0f },
			{ currentModelScale, currentModelScale, currentModelScale },
			{ 1.0f, 1.0f, 1.0f, 1.0f },
			false,
			S_PHONG
		);
	}

	if (g_ShowOriginCube && g_pCubeModel)
	{
		ModelDraw(
			g_pCubeModel,
			{ 0.0f, 0.0f, 0.0f },
			{ 0.0f, 0.0f, 0.0f },
			{ 1.0f, 1.0f, 1.0f },
			{ 1.0f, 1.0f, 1.0f, 1.0f },
			false,
			S_PHONG
		);
	}

	SetDepthEnable(false);
	if (g_pModelNameFont) g_pModelNameFont->Draw();
	if (g_pSubInfoFont) g_pSubInfoFont->Draw();
	if (g_pControlHintFont) g_pControlHintFont->Draw();
	DebugCamera_Draw();
}

void DebugModelScene_Finalize(void)
{
	isMouseLock = false;
	UnLockMouse();
	ReleaseCurrentModel();
	g_Entries.clear();
	g_CurrentIndex = -1;

	DebugCamera_Finalize();
	Camera_Finalize();

	if (g_pAmbientLight) { delete g_pAmbientLight; g_pAmbientLight = nullptr; }
	if (g_pFloorLight) { delete g_pFloorLight; g_pFloorLight = nullptr; }
	if (g_pCubeModel) { ModelRelease(g_pCubeModel); g_pCubeModel = nullptr; }
	if (g_pFloorBillboard) { delete g_pFloorBillboard; g_pFloorBillboard = nullptr; }
	if (g_pModelNameFont) { delete g_pModelNameFont; g_pModelNameFont = nullptr; }
	if (g_pSubInfoFont) { delete g_pSubInfoFont; g_pSubInfoFont = nullptr; }
	if (g_pControlHintFont) { delete g_pControlHintFont; g_pControlHintFont = nullptr; }
}
