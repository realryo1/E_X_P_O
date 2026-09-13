#pragma execution_character_set("utf-8")
/*==============================================================================
   デバッグモデルビューアシーン [debug_model_scene.cpp]
   asset\model と asset\expomodel フォルダ内の .fbx / .glb を自動列挙し、
   グリッド状に配置して表示する。
   Ghost と家具憑依機能のみを残し、モデルに近づくとモデル名と
   block_definitions.json への登録状況を描画する。
==============================================================================*/

#include <d3d11.h>
#include <DirectXMath.h>
#include <windows.h>
#include <string>
#include <vector>
#include <cmath>
#include <fstream>
#include <map>
#include "debug_model_scene.h"
#include "renderer.h"
#include "light.h"
#include "camera.h"
#include "model.h"
#include "sprite3d.h"
#include "anim_sprite3d.h"
#include "sprite2d.h"
#include "billboard.h"
#include "font.h"
#include "keyboard.h"
#include "mouse.h"
#include "define.h"
#include "texture.h"
#include "debugcamera.h"
#include "debug_ostream.h"

using namespace DirectX;

// ======================================================
// 内部構造体
// ======================================================

// 展示モデル1体分の情報
struct DebugModelEntry
{
	std::string fileName;       // "bathtub.fbx" など
	std::string filePath;       // "asset\\model\\bathtub.fbx"
	XMFLOAT3    worldPos;       // 配置ワールド座標
	MODEL*      pModel;         // 読み込んだモデル
};

// ======================================================
// 静的変数
// ======================================================
static std::vector<DebugModelEntry> g_Entries;
static AmbientLight* g_pAmbientLight = nullptr;
static PointLight* g_pFloorLight = nullptr;

// 近接表示用フォント
static DrawFont* g_pModelNameFont = nullptr;
static DrawFont* g_pSubInfoFont = nullptr;
static DrawFont* g_pControlHintFont = nullptr;

// 床描画用
static Billboard* g_pFloorBillboard = nullptr;
//static AnimSprite3D* g_pKirbyAnim = nullptr;

// 配置パラメータ
static const float MODEL_SPACING = 5.0f;  // モデル同士の間隔
static const float LABEL_RANGE = 8.0f;  // この距離以内で名前を表示
static const float RIM_LIGHT_BRIGHTNESS = 1.0f; // リムライトの明るさ（距離減衰なしの定数値で表現）
static const float OUTLINE_WIDTH = 0.009f; // アウトラインの太さ
// ShadowMapの調整値。Biasは影のちらつき防止、Brightnessは影になった場所の明るさ。
static const float SHADOW_BIAS = 0.004f; // 影のちらつき防止用の深度補正
static const float SHADOW_BRIGHTNESS = 0.55f; // 影になった部分の明るさ

// 原点キューブ表示用
static MODEL* g_pCubeModel = nullptr;
static bool   g_ShowOriginCubes = false;
static bool   g_AnimationKeyHeld[10] = {};

static bool isMouseLock = true;

// リロード用フラグ
static bool g_ReloadRequested = false;

static int GetTriggeredAnimationSlot()
{
	struct KeyPair
	{
		Keyboard_Keys mainKey;
		Keyboard_Keys numpadKey;
	};

	static const KeyPair keyPairs[] = {
		{ KK_D1, KK_NUMPAD1 },
		{ KK_D2, KK_NUMPAD2 },
		{ KK_D3, KK_NUMPAD3 },
		{ KK_D4, KK_NUMPAD4 },
		{ KK_D5, KK_NUMPAD5 },
		{ KK_D6, KK_NUMPAD6 },
		{ KK_D7, KK_NUMPAD7 },
		{ KK_D8, KK_NUMPAD8 },
		{ KK_D9, KK_NUMPAD9 },
		{ KK_D0, KK_NUMPAD0 },
	};

	for (int i = 0; i < (int)ARRAYSIZE(keyPairs); i++)
	{
		bool isDown = Keyboard_IsKeyDown(keyPairs[i].mainKey) || Keyboard_IsKeyDown(keyPairs[i].numpadKey);
		bool triggered = isDown && !g_AnimationKeyHeld[i];
		g_AnimationKeyHeld[i] = isDown;
		if (triggered)
		{
			return i;
		}
	}

	return -1;
}

static void TryPlayKirbyAnimationFromKey()
{
	////if (!g_pKirbyAnim)
	////{
	////	return;
	////}

	//int animSlot = GetTriggeredAnimationSlot();
	//if (animSlot < 0)
	//{
	//	return;
	//}

	//unsigned int animCount = g_pKirbyAnim->GetAnimationCount();
	//if ((unsigned int)animSlot >= animCount)
	//{
	//	return;
	//}

	//if (g_pKirbyAnim->PlayAnimationByIndex((unsigned int)animSlot, true))
	//{
	//	g_pKirbyAnim->UpdateAnimation(0.0f);
	//	const char* animName = g_pKirbyAnim->GetAnimationName((unsigned int)animSlot);
	//	hal::dout << "[DebugModelScene] Play kirby animation index=" << animSlot
	//		<< " name=" << (animName ? animName : "<noname>") << std::endl;
	//}
}

// ======================================================
// asset\model と asset\expomodel フォルダを列挙して .fbx / .glb を集める
// ======================================================
static void EnumerateModels()
{
	// 検索する拡張子パターン一覧
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
		const char* pattern = patterns[patternIndex];
		WIN32_FIND_DATAA fd;
		HANDLE hFind = FindFirstFileA(pattern, &fd);
		if (hFind == INVALID_HANDLE_VALUE) continue;

		do
		{
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

			DebugModelEntry entry;
			entry.fileName = fd.cFileName;
			entry.filePath = roots[patternIndex] + entry.fileName;
			entry.worldPos = { 0, 0, 0 };
		entry.pModel = nullptr;

			// block_definitions.json にあるか確認

			g_Entries.push_back(entry);
		} while (FindNextFileA(hFind, &fd));

		FindClose(hFind);
	}
}

// ======================================================
// モデルの再読み込み（家具とキャッシュを破棄して再構築）
// ======================================================
static void ReloadAllModels()
{
	// ゴーストの現在位置を保持

	// 家具を全て破棄（内部で各モデルのModelRelease/GlbModel::Releaseも呼ばれる）
	for (int i = 0; i < (int)g_Entries.size(); i++)
	{
		if (g_Entries[i].pModel) { ModelRelease(g_Entries[i].pModel); }
	}

	// 原点キューブモデルを解放
	if (g_pCubeModel) { ModelRelease(g_pCubeModel); g_pCubeModel = nullptr; }

	// モデル一覧を再列挙
	g_Entries.clear();
	EnumerateModels();

	// グリッド配置 + 家具再生成
	int cols = 6;
	for (int i = 0; i < (int)g_Entries.size(); i++)
	{
		int row = i / cols;
		int col = i % cols;
		float x = (col - cols / 2.0f + 0.5f) * MODEL_SPACING;
		float z = (float)row * MODEL_SPACING;
		float y = 0.0f;

		g_Entries[i].worldPos = { x, y, z };
		g_Entries[i].pModel = ModelLoad(g_Entries[i].filePath.c_str());
	}

	// 原点キューブモデルを再読み込み
	g_pCubeModel = ModelLoad("asset\\model\\cube.fbx");

	// ゴーストの位置を復元

}

// ======================================================
// Initialize
// ======================================================
void DebugModelScene_Initialize(void)
{
	isMouseLock = true;

	// ライト
	g_pAmbientLight = new AmbientLight(XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f));
	g_pFloorLight = new PointLight(
		TRUE,
		XMFLOAT4(0.0f, 5.0f, -5.0f, 1.0f),
		XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
		50.0f,
		1.0f
	);

	// 床用バッファ・テクスチャの作成
	g_pFloorBillboard = new Billboard(XMFLOAT3(0.0f, -0.5f, 0.0f), XMFLOAT2(1.0f, 1.0f), XMFLOAT3(90.0f, 0.0f, 0.0f), "asset\\texture\\notfound_thumbnail.png", false);
	g_pFloorBillboard->SetBillboardMode(false);
	// 床だけShadowReceiveシェーダーを使い、ShadowMapから影を受け取る。
	g_pFloorBillboard->SetReceiveShadow(true);
	// NormalMapのテスト。床の法線をピクセルごとに変えて、ライトの当たり方に凹凸を出す。
	// Normal.pngは負荷軽減のため削除
	//g_pFloorBillboard->SetNormalMap("");

	// 原点表示用キューブモデルの読み込み
	g_pCubeModel = ModelLoad("asset\\model\\cube.fbx");
	g_ShowOriginCubes = false;
	for (int i = 0; i < 10; i++)
	for (int i = 0; i < 10; i++)
	{
		g_AnimationKeyHeld[i] = false;
	}

	//g_pKirbyAnim = new AnimSprite3D(
	//	{ 0.0f, -6.0f, 0.0f },
	//	{ 1.0f, 1.0f, 1.0f },
	//	{ 0.0f, 180.0f, 0.0f },
	//	"asset\\model\\kirbyanim.fbx",
	//	S_PHONG
	//);
	//if (g_pKirbyAnim)
	//{
	//	g_pKirbyAnim->SetAnimationBlendDuration(0.2);
	//	hal::dout << "[DebugModelScene] kirbyanim loaded. animationCount=" << g_pKirbyAnim->GetAnimationCount() << std::endl;
	//}

	// モデル列挙
	EnumerateModels();

	// グリッド配置: 1列あたりのモデル数
	int cols = 6;
	for (int i = 0; i < (int)g_Entries.size(); i++)
	{
		int row = i / cols;
		int col = i % cols;
		float x = (col - cols / 2.0f + 0.5f) * MODEL_SPACING;
		float z = (float)row * MODEL_SPACING;
		float y = 0.0f;

		g_Entries[i].worldPos = { x, y, z };
		g_Entries[i].pModel = ModelLoad(g_Entries[i].filePath.c_str());
	}

	// プレイヤー初期化 (内部でCamera_InitializeやLockMouseが呼ばれる)
	DebugCamera_Initialize();

	// フォント
	g_pModelNameFont = new DrawFont({ SCREEN_X / 2.0f, SCREEN_Y - 140.0f }, 36.0f, 0.0f, { 1,1,1,1 }, "");
	g_pSubInfoFont = new DrawFont({ SCREEN_X / 2.0f, SCREEN_Y - 90.0f }, 28.0f, 0.0f, { 0.8f,0.8f,0.2f,1 }, "");
	g_pControlHintFont = new DrawFont({ SCREEN_X / 2.0f,                  30.0f }, 22.0f, 0.0f, { 0.6f,0.6f,0.6f,1 }, "WASD:Move  Mouse:Look  SPACE:Possess  E:Release  B:OriginCube  R:Reload");
	g_pControlHintFont->PreCacheGlyphs();

}

// ======================================================
// Update
// ======================================================
void DebugModelScene_Update(void)
{

	// Bキーで原点キューブ表示切り替え
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


	// Bキーで原点キューブ表示切り替え
	if (Keyboard_IsKeyDownTrigger(KK_B))
	{
		g_ShowOriginCubes = !g_ShowOriginCubes;
	}

	// Rキーで全モデルを再読み込み
	if (Keyboard_IsKeyDownTrigger(KK_R))
	{
		ReloadAllModels();
	}

	//TryPlayKirbyAnimationFromKey();
	//if (g_pKirbyAnim)
	//{
	//	g_pKirbyAnim->UpdateAnimation(1.0f / 60.0f);
	//}

	// プレイヤー(カメラ)の更新
	DebugCamera_Update();
	SetCameraPosition(GetCamera()->GetPos());

	float nearestDist = FLT_MAX;
	int nearestIdx = -1;

	if (nearestIdx >= 0 && nearestDist < LABEL_RANGE)
	{
		const DebugModelEntry& e = g_Entries[nearestIdx];

		// モデル名（ファイル名）
		g_pModelNameFont->SetText(e.fileName);
		g_pModelNameFont->PreCacheGlyphs();
	}
	else
	{
		g_pModelNameFont->SetText("");
		g_pSubInfoFont->SetText("");
	}
}

// ======================================================
// Draw
// ======================================================
void DebugModelScene_Draw(void)
{
	// 3D 描画
	SetDepthEnable(true);
	if (g_pFloorLight && g_pAmbientLight)
	{
		g_pFloorLight->Apply(*g_pAmbientLight);
	}

	// 1. ShadowMap作成パス
	// ライト位置からシーンを見て、モデルの深度だけをShadowMapに保存する。
	int shadowCols = 6;
	int shadowRows = ((int)g_Entries.size() + shadowCols - 1) / shadowCols;
	if (shadowRows < 1) shadowRows = 1;

	// 展示モデル全体の中央あたりをライトが見るようにする。
	float shadowCenterZ = (shadowRows - 1) * MODEL_SPACING * 0.5f;

	XMFLOAT4 lightPositionValue = g_pFloorLight ? g_pFloorLight->GetPosition() : XMFLOAT4(0.0f, 5.0f, -5.0f, 1.0f);
	XMVECTOR shadowCenter = XMVectorSet(0.0f, 0.0f, shadowCenterZ, 1.0f);
	XMVECTOR shadowEye = XMLoadFloat4(&lightPositionValue);

	// 今のShadowMapはライト位置から見た1枚のカメラ。
	// PointLightの全方向影ではなく、この視野に入ったものだけが影を作る。
	XMMATRIX lightView = XMMatrixLookAtLH(shadowEye, shadowCenter, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
	float shadowFarZ = g_pFloorLight ? g_pFloorLight->GetRange() + 20.0f : 80.0f;
	XMMATRIX lightProjection = XMMatrixPerspectiveFovLH(XMConvertToRadians(50.0f), 1.0f, 0.5f, shadowFarZ);

	// ShadowReceiveシェーダーが同じライト視点で影判定できるように、行列をGPUへ渡す。
	SetShadowMatrix(lightView * lightProjection, XMFLOAT4(SHADOW_BIAS, SHADOW_BRIGHTNESS, 0.0f, 0.0f));

	// ここからEndShadowMapまでの描画は、画面ではなくShadowMapへ書かれる。
	BeginShadowMap();
	SetCullState(CULLSTATE_BACK);
	for (int i = 0; i < (int)g_Entries.size(); i++)
	{
		if (g_Entries[i].pModel)
		{
			ModelDrawShadowMap(
				g_Entries[i].pModel,
				g_Entries[i].worldPos,
				{ 0.0f, 0.0f, 0.0f },
				{ 1.0f, 1.0f, 1.0f },
				lightView,
				lightProjection
			);
		}
	}
	SetCullState(CULLSTATE_NONE);

	// ShadowMapへの書き込みを終えて、通常の画面描画に戻す。
	EndShadowMap();

	// --- 床の描画 ---
	if (g_pFloorBillboard)
	{
		// モデル配置範囲に合わせて床タイルを敷く
		int cols = 6;
		int rows = ((int)g_Entries.size() + cols - 1) / cols;
		float halfW = (cols / 2.0f) * MODEL_SPACING + MODEL_SPACING;
		float maxZ = (float)(rows) * MODEL_SPACING + MODEL_SPACING;
		float minZ = -MODEL_SPACING;

		float tileSize = 1.0f;
		int tilesX = (int)((halfW * 2.0f) / tileSize) + 2;
		int tilesZ = (int)((maxZ - minZ) / tileSize) + 2;
		float startX = -halfW;
		float startZ = minZ;

		for (int iz = 0; iz < tilesZ; iz++)
		{
			for (int ix = 0; ix < tilesX; ix++)
			{
				float x = startX + ix * tileSize + tileSize * 0.5f;
				float z = startZ + iz * tileSize + tileSize * 0.5f;
				float y = -0.5f;

				g_pFloorBillboard->SetPos({ x, y, z });
				g_pFloorBillboard->SetSize({ tileSize, tileSize });
				g_pFloorBillboard->Draw();
			}
		}
	}

	// 家具描画（展示モデル＋ビルボードアイコン含む）
	/*SetParameter(XMFLOAT4(OUTLINE_WIDTH, 0.0f, 0.0f, 0.0f));
	SetCullState(CULLSTATE_FRONT);
	for (int i = 0; i < (int)g_Entries.size(); i++)
	{
		if (g_Entries[i].pModel)
		{
			ModelDraw(
				g_Entries[i].pModel,
				g_Entries[i].worldPos,
				{ 0.0f, 0.0f, 0.0f },
				{ 1.0f, 1.0f, 1.0f },
				XMFLOAT4(0.02f, 0.025f, 0.035f, 1.0f),
				true,
				S_OUTLINE
			);
		}
	}
	SetCullState(CULLSTATE_NONE);*/

	SetParameter(XMFLOAT4(RIM_LIGHT_BRIGHTNESS, 0.0f, 0.0f, 0.0f));
	for (int i = 0; i < (int)g_Entries.size(); i++)
	{
		if (g_Entries[i].pModel)
		{
			ModelDraw(
				g_Entries[i].pModel,
				g_Entries[i].worldPos,
				{ 0.0f, 0.0f, 0.0f },
				{ 1.0f, 1.0f, 1.0f },
				XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
				false,
				S_PHONG
			);
		}
	}

	//if (g_pKirbyAnim)
	//{
	//	g_pKirbyAnim->Draw();
	//}

	// 原点キューブ描画
	if (g_ShowOriginCubes && g_pCubeModel)
	{
		for (int i = 0; i < (int)g_Entries.size(); i++)
		{
			ModelDraw(
				g_pCubeModel,
				g_Entries[i].worldPos,
				{ 0.0f, 0.0f, 0.0f },
				{ 1.0f, 1.0f, 1.0f },
				XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
				false,
				S_PHONG
			);
		}
	}
	// 2D 描画
	SetDepthEnable(false);

	if (g_pModelNameFont)   g_pModelNameFont->Draw();
	if (g_pSubInfoFont)     g_pSubInfoFont->Draw();
	if (g_pControlHintFont) g_pControlHintFont->Draw();

	// プレイヤー描画追加
	DebugCamera_Draw();

	// ポーズメニュー描画（2D描画内で最後に描く）
}

// ======================================================
// Finalize
// ======================================================
void DebugModelScene_Finalize(void)
{
	isMouseLock = false;
	UnLockMouse();

	for (int i = 0; i < (int)g_Entries.size(); i++)
	{
		if (g_Entries[i].pModel) { ModelRelease(g_Entries[i].pModel); }
	}
	g_Entries.clear();
	DebugCamera_Finalize();
	Camera_Finalize();

	if (g_pAmbientLight) { delete g_pAmbientLight; g_pAmbientLight = nullptr; }
	if (g_pFloorLight) { delete g_pFloorLight;   g_pFloorLight = nullptr; }

	// 原点キューブモデル解放
	if (g_pCubeModel) { ModelRelease(g_pCubeModel); g_pCubeModel = nullptr; }

	// 床リソース解放
	if (g_pFloorBillboard) { delete g_pFloorBillboard; g_pFloorBillboard = nullptr; }
	//if (g_pKirbyAnim) { delete g_pKirbyAnim; g_pKirbyAnim = nullptr; }

	if (g_pModelNameFont) { delete g_pModelNameFont;   g_pModelNameFont = nullptr; }
	if (g_pSubInfoFont) { delete g_pSubInfoFont;     g_pSubInfoFont = nullptr; }
	if (g_pControlHintFont) { delete g_pControlHintFont; g_pControlHintFont = nullptr; }
}
