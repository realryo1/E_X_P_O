//==================================================
//main.cpp
//制作日：2025/05/09
//==================================================

#define _CRT_SECURE_NO_WARNINGS

#include <SDKDDKVer.h> //利用できる最も上位のWindowsプラットフォームが定義される
#define WIN32_LEAN_AND_MEAN	//32bitアプリには不要な情報を無視
#include <iostream>
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <shellapi.h>
#include "main.h"
#include "define.h"
#include "scene.h"
#include "renderer.h"
#include "debug_ostream.h"
#include "keyboard.h"
#include "mouse.h"
#include "font.h"
#include "sprite2d.h"
#include "fade.h"
#include "texture.h"
#include "sound.h"
#include "input_manager.h"
#include "input_monitor_console.h"
#include "gamepad.h"
#include "shadermanager.h"
#include "camera.h"
#include "../framework/imgui/imgui.h"
#include "../framework/imgui/imgui_impl_win32.h"
#include "../framework/imgui/imgui_impl_dx11.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,

	LPARAM lParam);


#pragma	comment (lib, "d3d11.lib")
#pragma	comment (lib, "d3dcompiler.lib")
#pragma	comment (lib, "winmm.lib")
#pragma	comment (lib, "dxguid.lib")
#pragma	comment (lib, "dinput8.lib")
#pragma	comment (lib, "shell32.lib")

using namespace DirectX;

// ハイパフォーマンスGPUを使用するためのヒント
extern "C" {
	_declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
	_declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

//==================================
//グローバル変数
//==================================

int g_CountFPS;       // 描画FPS（Draw/Present の呼び出し回数/秒）
int g_CountUpdateFPS; // 論理更新FPS（Update の呼び出し回数/秒）
long long g_UpdateTime = 0;
long long g_DrawTime = 0;
static double g_PresentTimeMs = 0.0;
#if defined(_DEBUG)
static std::ofstream g_DebugFramePerfLog;
static unsigned long long g_DebugFrameLogIndex = 0;
#endif
wchar_t g_DebugStr[2048];
static int g_TargetFPS = FPS;  // 目標FPS（デフォルトは FPS マクロの値）
static bool g_RequestRedraw = true; // 起動直後は必ず1回描画する
static int g_StartupWarmupPresents = 8; // 起動直後の冷えた Draw を暗転なしで消化する
int pad;//未接続:-1、接続中:0

#pragma comment(lib, "winmm.lib")

//==================================
//SetFPS関数
//==================================
void SetFPS(int fps)
{
	if (fps > 0)
	{
		g_TargetFPS = fps;
	}
}

int GetGamePad() {
	return pad;
}

#if defined(_DEBUG)
static void DebugFramePerfLogInitialize(void)
{
	g_DebugFramePerfLog.open(
		"debug-frame-perf.log",
		std::ios::out | std::ios::trunc);
	g_DebugFrameLogIndex = 0;
	if (g_DebugFramePerfLog)
	{
		g_DebugFramePerfLog
			<< "frame,totalUs,updUs,drwUs,prsMs,gpuMs,mapCount,mapMs,idx,"
			<< "shdMs,fldMs,objMs,uiMs,pumpMs\n";
	}
}

static void DebugFramePerfLogWrite(float gpuFrameMs)
{
	if (!g_DebugFramePerfLog)
	{
		return;
	}

	g_DebugFramePerfLog
		<< g_DebugFrameLogIndex++ << ','
		<< (g_UpdateTime + g_DrawTime) << ','
		<< g_UpdateTime << ','
		<< g_DrawTime << ','
		<< std::fixed << std::setprecision(3) << g_PresentTimeMs << ','
		<< gpuFrameMs << ','
		<< Direct3D_DebugGetMapCount() << ','
		<< Direct3D_DebugGetMapMs() << ','
		<< Direct3D_DebugGetDrawIndexedCount() << ','
		<< Direct3D_DebugGetStageMs(DIRECT3D_DEBUG_STAGE_SHADOW) << ','
		<< Direct3D_DebugGetStageMs(DIRECT3D_DEBUG_STAGE_FIELD) << ','
		<< Direct3D_DebugGetStageMs(DIRECT3D_DEBUG_STAGE_OBJECTS) << ','
		<< Direct3D_DebugGetStageMs(DIRECT3D_DEBUG_STAGE_UI) << ','
		<< Direct3D_DebugGetStageMs(DIRECT3D_DEBUG_STAGE_PUMP) << '\n';

	if ((g_DebugFrameLogIndex % 60) == 0)
	{
		g_DebugFramePerfLog.flush();
	}
}

static void DebugFramePerfLogFinalize(void)
{
	if (g_DebugFramePerfLog)
	{
		g_DebugFramePerfLog.flush();
		g_DebugFramePerfLog.close();
	}
}
#endif

void RequestRedraw(void)
{
	g_RequestRedraw = true;
}

// 変化のない静止シーンは Clear/Present を間引き、GPU を休ませる
static bool NeedsPresent(void)
{
	if (g_StartupWarmupPresents > 0) {
		return true;
	}
	if (g_RequestRedraw) {
		return true;
	}
	if (GetFadeState() != FADE_NONE) {
		return true;
	}
#if defined(_DEBUG)
	// デバッグシーンは ImGui / カメラ等で毎フレーム変化する
	if (GetScene() == SCENE_DEBUG) {
		return true;
	}
#endif
	return false;
}

static bool PathExistsW(const wchar_t* path)
{
	return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static bool LooksLikeProjectRoot(const wchar_t* root)
{
	wchar_t batPath[MAX_PATH];
	if (swprintf_s(batPath, L"%s\\tool\\download_expo_assets.bat", root) < 0)
	{
		return false;
	}
	return PathExistsW(batPath);
}

static bool TryCopyRoot(wchar_t* outRoot, size_t outCap, const wchar_t* candidate)
{
	if (!LooksLikeProjectRoot(candidate))
	{
		return false;
	}
	wcsncpy_s(outRoot, outCap, candidate, _TRUNCATE);
	return true;
}

static bool ResolveProjectRoot(wchar_t* outRoot, size_t outCap)
{
	wchar_t cwd[MAX_PATH];
	if (GetCurrentDirectoryW(MAX_PATH, cwd) != 0)
	{
		if (TryCopyRoot(outRoot, outCap, cwd))
		{
			return true;
		}
	}

	wchar_t exeDir[MAX_PATH];
	if (GetModuleFileNameW(NULL, exeDir, MAX_PATH) == 0)
	{
		return false;
	}

	wchar_t* slash = wcsrchr(exeDir, L'\\');
	if (slash != nullptr)
	{
		*slash = L'\0';
	}
	if (TryCopyRoot(outRoot, outCap, exeDir))
	{
		return true;
	}

	for (int climb = 0; climb < 2; ++climb)
	{
		slash = wcsrchr(exeDir, L'\\');
		if (slash == nullptr)
		{
			return false;
		}
		*slash = L'\0';
		if (TryCopyRoot(outRoot, outCap, exeDir))
		{
			return true;
		}
	}

	return false;
}

static bool ExpoModelDirectoryIsEmpty(const wchar_t* dir)
{
	const DWORD attr = GetFileAttributesW(dir);
	if (attr == INVALID_FILE_ATTRIBUTES)
	{
		return true;
	}
	if ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
	{
		return true;
	}

	wchar_t pattern[MAX_PATH];
	if (swprintf_s(pattern, L"%s\\*", dir) < 0)
	{
		return true;
	}

	WIN32_FIND_DATAW findData;
	const HANDLE findHandle = FindFirstFileW(pattern, &findData);
	if (findHandle == INVALID_HANDLE_VALUE)
	{
		return true;
	}

	bool empty = true;
	do
	{
		if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
		{
			continue;
		}
		empty = false;
		break;
	} while (FindNextFileW(findHandle, &findData));

	FindClose(findHandle);
	return empty;
}

// 続行するなら true。ダウンロード起動後に終了するなら false。
static bool EnsureExpoAssetsOrPrompt(void)
{
	wchar_t root[MAX_PATH];
	if (!ResolveProjectRoot(root, MAX_PATH))
	{
		return true;
	}

	wchar_t expoDir[MAX_PATH];
	if (swprintf_s(expoDir, L"%s\\asset\\expomodel", root) < 0)
	{
		return true;
	}
	if (!ExpoModelDirectoryIsEmpty(expoDir))
	{
		return true;
	}

	const int answer = MessageBoxW(
		NULL,
		L"asset\\expomodel に万博モデルがありません。\n"
		L"ゲームを終了して tool\\download_expo_assets.bat を実行しますか？\n\n"
		L"実行後、コンソールで利用条件を確認し AGREE と入力してください。\n"
		L"完了したらゲームを再起動してください。",
		L"万博アセット未配置",
		MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND | MB_TOPMOST);

	if (answer != IDYES)
	{
		return true;
	}

	wchar_t batPath[MAX_PATH];
	if (swprintf_s(batPath, L"%s\\tool\\download_expo_assets.bat", root) < 0 || !PathExistsW(batPath))
	{
		MessageBoxW(
			NULL,
			L"tool\\download_expo_assets.bat が見つかりません。",
			L"万博アセット未配置",
			MB_OK | MB_ICONERROR);
		return false;
	}

	const HINSTANCE launched = ShellExecuteW(
		NULL,
		L"open",
		batPath,
		NULL,
		root,
		SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(launched) <= 32)
	{
		MessageBoxW(
			NULL,
			L"download_expo_assets.bat を起動できませんでした。\n"
			L"プロジェクトルートで tool\\download_expo_assets.bat を実行してください。",
			L"万博アセット未配置",
			MB_OK | MB_ICONERROR);
	}

	return false;
}

//==================================
//メイン関数
//==================================
//==================================  
int APIENTRY WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	// モニターごとのDPI認識を有効化（DWMによる拡大描画を防ぎ、物理ピクセルでウィンドウ管理する）
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	if (!EnsureExpoAssetsOrPrompt())
	{
		return 0;
	}

	//フレームレート計測用変数（steady_clock ベース）
	using SteadyClock = std::chrono::steady_clock;
	using Seconds = std::chrono::duration<double>;
	constexpr double FIXED_STEP = 1.0 / FPS;
	double accumulator  = 0.0;
	auto prevFrameTime  = SteadyClock::now();
	auto fpsLastTime    = SteadyClock::now();
	auto titleLastTime  = SteadyClock::now();
	int  frameCount     = 0; // 描画回数カウント
	int  updateCount    = 0; // 論理更新回数カウント

	//COMコンポーネントの準備（機能を部品化して外部のプログラムから共有利用する仕組み）
	HRESULT hr = CoInitializeEx(nullptr, COINITBASE_MULTITHREADED);

	// ゲームウィンドウより先に出す。後から AllocConsole するとゲームが背面に回る。
	//InputMonitorConsole_Initialize();

	//ウィンドウクラスの登録
	WNDCLASS wc;//構造体を定義
	ZeroMemory(&wc, sizeof(WNDCLASS));//構造体初期化
	wc.lpfnWndProc = WndProc;//初期化
	wc.lpszClassName = CLASS_NAME;//仕様書の名前
	wc.hInstance = hInstance;//このアプリのこと
	wc.hIcon = LoadIcon(hInstance, L"IDI_ICON1");
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);//cursorの種類
	wc.hbrBackground = (HBRUSH)(COLOR_BACKGROUND);//背景色
	RegisterClass(&wc);//構構体をwindowsにセット

	//ウィンドウサイズの調整
	//クライアント領域（描画領域）のサイズを表す矩形
	RECT window_rect = { 0, 0, (LONG)DRAW_SCREEN_X, (LONG)DRAW_SCREEN_Y };
	//ウィンドウスタイルの設定
	DWORD window_style = WS_OVERLAPPEDWINDOW;
	//指定のクライアント領域＋ウィンドウスタイルでの全体のサイズを計算
	AdjustWindowRect(&window_rect, window_style, FALSE);
	//矩形の横と縦のサイズを計算
	int window_width = window_rect.right - window_rect.left;
	int window_height = window_rect.bottom - window_rect.top;

	// 作業領域（タスクバーを除いた画面）に収まるようクランプ（16:9比率を保持）
	RECT workArea;
	SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
	// ウィンドウボーダー分を計算（AdjustWindowRect済みの全体サイズ - クライアントサイズ）
	int border_W = window_width  - (int)DRAW_SCREEN_X;
	int border_H = window_height - (int)DRAW_SCREEN_Y;
	// 作業領域の70%に収まる最大クライアントサイズ
	int max_client_W = (int)((workArea.right  - workArea.left) * 0.7f) - border_W;
	int max_client_H = (int)((workArea.bottom - workArea.top)  * 0.7f) - border_H;
	if (max_client_W < 1) max_client_W = 1;
	if (max_client_H < 1) max_client_H = 1;
	// 16:9（DRAW_SCREEN比率）を保ちながら縮小スケールを計算
	float scale_W = (float)max_client_W / DRAW_SCREEN_X;
	float scale_H = (float)max_client_H / DRAW_SCREEN_Y;
	float client_scale = (scale_W < scale_H) ? scale_W : scale_H;
	if (client_scale > 1.0f) client_scale = 1.0f;
	window_width  = (int)(DRAW_SCREEN_X  * client_scale) + border_W;
	window_height = (int)(DRAW_SCREEN_Y * client_scale) + border_H;

	//ウィンドウの作成
	HWND hWnd = CreateWindow(
		CLASS_NAME,
		WINDOW_CAPTION,
		window_style, // リサイズ可能なウィンドウ
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		window_width,//ウィンドウの幅
		window_height,//ウィンドウの高さ
		NULL,
		NULL,
		hInstance,	//アプリのハンドル
		NULL
	);

	ShowWindow(hWnd, nCmdShow);//引数に従って表示非表示
	SetForegroundWindow(hWnd);

	//ウィンドウ内部の更新要求
	UpdateWindow(hWnd);
	InitRenderer(hInstance, hWnd, TRUE);

	//ImGui の Win32 と DirectX 11 バックエンドを初期化
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(hWnd);
	ImGui_ImplDX11_Init(GetDevice(), GetDeviceContext());

	// 初期ウィンドウクライアントサイズをDirect3Dに通知
	{
		RECT cr;
		GetClientRect(hWnd, &cr);
		Direct3D_ResizeWindow((unsigned int)(cr.right - cr.left), (unsigned int)(cr.bottom - cr.top));
		SetWorldViewProjection2D();
	}

	// 全画面・ウィンドウ切替用のキー入力を受け取るために Direct3D_Initialize の後に行う
	// (スワップチェーンへのアクセスが必要な場合があるため)

	Keyboard_Initialize();
	Mouse_Initialize(hWnd);
	InitSound();
	Input_Initialize();
	InitShader();
	Font_InitializeGlobalData();
	Sprite_Initialize();
	Fade_Initialize();
	Gamepad_Initialize();

	Init();
#if defined(_DEBUG)
	DebugFramePerfLogInitialize();
#endif

	//メッセージループ
	MSG msg;
	ZeroMemory(&msg, sizeof(MSG));

	// prevFrameTime / fpsLastTime / titleLastTime は上記の初期化で設定済み
	// （timeBeginPeriod / timeGetTime は使用しない）

	do
	{
		//終了メッセージが来るまでループ （Windowsからのメッセージはそのまま使えない）
		//while (GetMessage(&msg, NULL, 0, 0))　ゲ－ム向きではないらしい
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))	//余計なことをしないので早い
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg); //WndProcが呼び出される
		}
		//ゲームの処理
		else
		{
			auto now = SteadyClock::now();
			double delta = std::chrono::duration_cast<Seconds>(now - prevFrameTime).count();
			prevFrameTime = now;

			// デバッガ停止などの極端な遅延をクランプ
			if (delta > 0.25) delta = 0.25;

			// FPSカウンタ更新（1秒ごと）
			if (std::chrono::duration_cast<Seconds>(now - fpsLastTime).count() >= 1.0)
			{
				g_CountFPS       = frameCount;
				g_CountUpdateFPS = updateCount;
				fpsLastTime  = now;
				frameCount   = 0;
				updateCount  = 0;
			}

			// 論理更新：固定ステップを消化する（最大5ステップ）
			accumulator += delta;
			int steps = 0;
#if defined(_DEBUG)
			Direct3D_DebugResetFrameCounters();
#endif
			while (accumulator >= FIXED_STEP && steps < 5)
			{
				Gamepad_Update();
				Input_Update();
				InputMonitorConsole_Update();

				if(pad<0) pad = Gamepad_FindConnectedPlayer();

				//// ウィンドウ操作（論理ステップ内で判定：keycopy後に正しいトリガーを参照できる）
				//// Alt+Enterで全画面切り替え
				//if (Keyboard_IsKeyDown(KK_LEFTALT) || Keyboard_IsKeyDown(KK_RIGHTALT))
				//{
				//	if (Keyboard_IsKeyDownTrigger(KK_ENTER))
				//	{
				//		// 全画面切り替え処理
				//		static bool isFullScreen = false;
				//		isFullScreen = !isFullScreen;
				//		IDXGISwapChain* pSwapChain = nullptr;
				//	}
				//}

				// F11キーでボーダレスウィンドウ切り替え
				if (Keyboard_IsKeyDownTrigger(KK_F11))
				{
					static bool isBorderless = false;
					static RECT prevWindowRect = {};
					isBorderless = !isBorderless;

					if (isBorderless)
					{
						// 現在のウィンドウ位置・サイズを保存
						GetWindowRect(hWnd, &prevWindowRect);

						// モニターのサイズを取得
						HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
						MONITORINFO mi = {};
						mi.cbSize = sizeof(mi);
						GetMonitorInfo(hMonitor, &mi);

						// ボーダレス（フレームなし）スタイルに変更
						SetWindowLong(hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
						SetWindowPos(hWnd, HWND_TOP,
							mi.rcMonitor.left, mi.rcMonitor.top,
							mi.rcMonitor.right - mi.rcMonitor.left,
							mi.rcMonitor.bottom - mi.rcMonitor.top,
							SWP_FRAMECHANGED);
					}
					else
					{
						// 通常ウィンドウスタイルに戻す
						DWORD restored_style = WS_OVERLAPPEDWINDOW;
						SetWindowLong(hWnd, GWL_STYLE, restored_style | WS_VISIBLE);
						SetWindowPos(hWnd, HWND_TOP,
							prevWindowRect.left, prevWindowRect.top,
							prevWindowRect.right - prevWindowRect.left,
							prevWindowRect.bottom - prevWindowRect.top,
							SWP_FRAMECHANGED);
					}
				}

				// 更新時間の計測
				auto startUpdate = std::chrono::high_resolution_clock::now();
				Fade_Update();
				UpdateTextureCache();
				UpdateSoundCache();
				Update();
				auto endUpdate = std::chrono::high_resolution_clock::now();
				g_UpdateTime = std::chrono::duration_cast<std::chrono::microseconds>(endUpdate - startUpdate).count();

				// キー状態を末尾でコピー：Update() が読んだ後に gStateOld を更新
				// → 次ステップで IsKeyDownTrigger が正しく「前回との差分」を検出できる
				keycopy();

				accumulator -= FIXED_STEP;
				steps++;
			}
			updateCount += steps; // 今回のループで実行した論理ステップ数を加算

			if (steps > 0 && NeedsPresent())
			{
				// 描画時間の計測（Present/VSync 待ちは含めない）
				auto startDraw = std::chrono::high_resolution_clock::now();

				// ImGui フレーム開始（Draw 内の ImGui::* 呼び出しより前に必須）
				ImGui_ImplDX11_NewFrame();
				ImGui_ImplWin32_NewFrame();
				ImGui::NewFrame();

				Clear();//バッファのクリア

				SetWorldViewProjection2D(); // 2D専用シーン向けにデフォルトで2D行列を設定
				Draw();

				SetDepthEnable(false);
				Fade_Draw();

				// ImGui 描画（頂点が無いフレームは GPU へ送らない）
				ImGui::Render();
				ImDrawData* drawData = ImGui::GetDrawData();
				if (drawData && drawData->TotalVtxCount > 0)
				{
					ImGui_ImplDX11_RenderDrawData(drawData);
				}

				auto endDraw = std::chrono::high_resolution_clock::now();
				g_DrawTime = std::chrono::duration_cast<std::chrono::microseconds>(endDraw - startDraw).count();

				const auto startPresent = std::chrono::high_resolution_clock::now();
				Present();//バッファの表示（VSync 待ちはここ）
				const auto endPresent = std::chrono::high_resolution_clock::now();
				g_PresentTimeMs =
					std::chrono::duration<double, std::milli>(
						endPresent - startPresent).count();
				g_RequestRedraw = false;
				if (g_StartupWarmupPresents > 0)
				{
					g_StartupWarmupPresents--;
				}
				PumpAfterPresent(
					static_cast<double>(g_DrawTime) / 1000.0,
					Direct3D_GetLastGpuFrameMs());
#if defined(_DEBUG)
				DebugFramePerfLogWrite(Direct3D_GetLastGpuFrameMs());
#endif
				frameCount++; // Present() 1回 = 描画1フレーム
			}
			else
			{
				// Present していないフレームは前回の冷えた計測値を引きずるので 0 にする
				g_DrawTime = 0;
				g_PresentTimeMs = 0.0;

				// 論理ステップ待ち、または静止シーンで Present 不要なときの空回し防止
				const double remaining = FIXED_STEP - accumulator;
				if (remaining > 0.002)
				{
					DWORD sleepMs = (DWORD)((remaining - 0.001) * 1000.0);
					if (sleepMs > 0)
					{
						Sleep(sleepMs);
					}
				}
				else
				{
					Sleep(0); // 残り僅かのときは他スレッドへ譲る
				}
			}

#if defined(_DEBUG)
			//ウィンドウキャプションへ情報を表示（0.2秒に1回更新）
			if (std::chrono::duration_cast<Seconds>(now - titleLastTime).count() >= 0.2)
			{
				titleLastTime = now;
				const float gpuFrameMs = Direct3D_GetLastGpuFrameMs();
				const unsigned long long mapCount =
					Direct3D_DebugGetMapCount();
				const double mapMs = Direct3D_DebugGetMapMs();
				const unsigned long long drawIndexedCount =
					Direct3D_DebugGetDrawIndexedCount();
				const double shadowMs = Direct3D_DebugGetStageMs(
					DIRECT3D_DEBUG_STAGE_SHADOW);
				const double fieldMs = Direct3D_DebugGetStageMs(
					DIRECT3D_DEBUG_STAGE_FIELD);
				const double objectsMs = Direct3D_DebugGetStageMs(
					DIRECT3D_DEBUG_STAGE_OBJECTS);
				const double uiMs = Direct3D_DebugGetStageMs(
					DIRECT3D_DEBUG_STAGE_UI);
				const double pumpMs = Direct3D_DebugGetStageMs(
					DIRECT3D_DEBUG_STAGE_PUMP);
				if (gpuFrameMs >= 0.0f)
				{
					swprintf(g_DebugStr, sizeof(g_DebugStr) / sizeof(wchar_t),
						L"Draw: %dfps | Logic: %dfps | Total: %lldus | Upd: %lldus | Drw: %lldus | Prs: %.1fms | GPU: %.1fms | Map: %llu/%.2fms | Idx: %llu | Shd: %.1f | Fld: %.1f | Obj: %.1f | UI: %.1f | Pump: %.1f",
						g_CountFPS,
						g_CountUpdateFPS,
						g_UpdateTime + g_DrawTime,
						g_UpdateTime,
						g_DrawTime,
						g_PresentTimeMs,
						gpuFrameMs,
						mapCount,
						mapMs,
						drawIndexedCount,
						shadowMs,
						fieldMs,
						objectsMs,
						uiMs,
						pumpMs);
				}
				else
				{
					swprintf(g_DebugStr, sizeof(g_DebugStr) / sizeof(wchar_t),
						L"Draw: %dfps | Logic: %dfps | Total: %lldus | Upd: %lldus | Drw: %lldus | Prs: %.1fms | GPU: n/a | Map: %llu/%.2fms | Idx: %llu | Shd: %.1f | Fld: %.1f | Obj: %.1f | UI: %.1f | Pump: %.1f",
						g_CountFPS,
						g_CountUpdateFPS,
						g_UpdateTime + g_DrawTime,
						g_UpdateTime,
						g_DrawTime,
						g_PresentTimeMs,
						mapCount,
						mapMs,
						drawIndexedCount,
						shadowMs,
						fieldMs,
						objectsMs,
						uiMs,
						pumpMs);
				}
				SetWindowText(hWnd, g_DebugStr);
			}
#endif
		}

	} while (msg.message != WM_QUIT);//windowsから終了メッセージが来たらループ終了

#if defined(_DEBUG)
	DebugFramePerfLogFinalize();
#endif

	//ImGui のクリーンアップ
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	Finalize();
	ReleaseAllTextures();
	ReleaseAllSounds();
	Gamepad_Finalize();
	Input_Finalize();
	InputMonitorConsole_Finalize();
	UninitSound();
	Fade_Finalize();
	Font_FinalizeGlobalData();
	Sprite_Finalize();
	FinalizeShader();
	FinalizeRenderer();


	//終了
	return (int)msg.wParam;
}

//==================================
//ウィンドウプロシージャ
//メッセージループ内で呼び出し
//==================================
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	//HGDIOBJ hbrWhite, hbrGray;

	//HDC hdc;			//デバイスコンテキスト
	//PAINTSTRUCT ps;		//ウィンドウ画面の大きさなど描画関連の情報

	//ImGui の Win32 メッセージハンドラを最初に呼び出す（ImGuiが処理したいメッセージを先に処理させる）
	if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
		return true;

	Mouse_ProcessMessage(uMsg, wParam, lParam);
	Gamepad_ProcessMessage(uMsg, wParam, lParam);

	switch (uMsg)
	{
	case WM_ACTIVATEAPP:

		break;
	case WM_SIZE:
		// ウィンドウサイズが変更された（全画面化など）場合にバックバッファを再構成する
		if (wParam != SIZE_MINIMIZED && GetDeviceContext() != NULL)
		{
			unsigned int newW = LOWORD(lParam);
			unsigned int newH = HIWORD(lParam);
			Direct3D_ResizeWindow(newW, newH);
			Direct3D_Resize(newW, newH); // バックバッファをウィンドウサイズ（ネイティブ解像度）に合わせる
			if (newH > 0)
			{
				Camera_SetAspect(static_cast<float>(newW) / static_cast<float>(newH));
			}
			RequestRedraw();
		}
		break;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		BeginPaint(hWnd, &ps);
		EndPaint(hWnd, &ps);
		RequestRedraw();
		return 0;
	}
	case WM_SYSKEYDOWN:
		// Alt+Enterキーの全画面切り替え無効化（手動制御に変更）
		if (wParam == VK_RETURN && (lParam & 0x20000000))
		{
			// Alt+Enterの全画面切り替えを無視（イベントを処理してreturnする）
			return 0;
		}
		Keyboard_ProcessMessage(uMsg, wParam, lParam);
		break;
	case WM_KEYUP:
		Keyboard_ProcessMessage(uMsg, wParam, lParam);
		break;
	case WM_SYSKEYUP:
		Keyboard_ProcessMessage(uMsg, wParam, lParam);
		break;
	case WM_KEYDOWN:	//キーが押された
		Keyboard_ProcessMessage(uMsg, wParam, lParam);

		break;
	case WM_CLOSE:
		if (true)
		{
			//OKが押された
			DestroyWindow(hWnd);
		}
		else
		{
			//終わらない
			return 0;
		}
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	}

	//必要のないメッセージは適当に処理するらしい
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}
