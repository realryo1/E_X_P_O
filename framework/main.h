#pragma once
//==================================
//マクロ定義
//==================================
#include <SDKDDKVer.h> //利用できる最も上位のWindowsプラットフォームが定義される
#include <windows.h>

#pragma warning(push)
#pragma warning(disable:4005)

#define _CRT_SECURE_NO_WARNINGS			// scanf のwarning防止
#include <stdio.h>

#include <d3d11.h>
#include <d3dcompiler.h>

#define DIRECTINPUT_VERSION 0x0800		// 警告対処
#include "dinput.h"
#include "mmsystem.h"

#pragma warning(pop)

#include <DirectXMath.h>

#include "DirectXTex.h"//<<<<

#if _DEBUG
#pragma comment(lib, "framework/library/DirectXTex_Debug.lib")
#else
#pragma comment(lib, "framework/library/DirectXTex_Release.lib")
#endif

using namespace DirectX;

#ifndef SAFE_DELETE
#define SAFE_DELETE(p) do { if ((p) != nullptr) { delete (p); (p) = nullptr; } } while (0)
#endif

//==================================
//プロトタイプ宣言
//==================================
//ウィンドウプロシージャ
//コールバック関数は他人が呼び出すもの
LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// FPS設定関数
void SetFPS(int fps);
int GetGamePad();

// 次フレームの描画を要求（静止シーンの間引き解除用）
void RequestRedraw(void);