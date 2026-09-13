//--------------------------------------------------------------------------------------
// File: mouse.h
//
// 便利なマウスモジュール
//
//--------------------------------------------------------------------------------------
// 2020/02/11
//     DirectXTKより、なんちゃってC言語用にシェイプアップ改変
//
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkId=248929
// http://go.microsoft.com/fwlink/?LinkID=615561
//--------------------------------------------------------------------------------------
#ifndef HAL_YOUHEI_MOUSE_H
#define HAL_YOUHEI_MOUSE_H
#pragma once


#include <windows.h>
#include <memory>


// マウスモード
typedef enum Mouse_PositionMode_tag
{
    MOUSE_POSITION_MODE_ABSOLUTE, // 絶対座標モード
    MOUSE_POSITION_MODE_RELATIVE, // 相対座標モード
} Mouse_PositionMode;


// マウス状態構造体
typedef struct MouseState_tag
{
    bool leftButton;
    bool middleButton;
    bool rightButton;
    bool xButton1;
    bool xButton2;
    int x;   // 絶対座標モード時のウィンドウ内X座標
    int y;   // 絶対座標モード時のウィンドウ内Y座標
    int dx;  // 相対移動量X
    int dy;  // 相対移動量Y
    int scrollWheelValue;
    Mouse_PositionMode positionMode;
} Mouse_State;


// マウスモジュールの初期化
void Mouse_Initialize(HWND window);

// マウスモジュールの終了処理
void Mouse_Finalize(void);

// マウスの状態を取得する
void Mouse_GetState(Mouse_State* pState);

// 累積したマウススクロールホイール値をリセットする
void Mouse_ResetScrollWheelValue(void);

// 今回分のホイール移動量を取得し、内部累積を0にする
int Mouse_ConsumeScrollWheel(void);

// マウスのポジションモードを設定する（デフォルトは絶対座標モード）
void Mouse_SetMode(Mouse_PositionMode mode);

// マウスの接続を検出する
bool Mouse_IsConnected(void);

// マウスカーソルが表示されているか確認する
bool Mouse_IsVisible(void);

// マウスカーソル表示を設定する
void Mouse_SetVisible(bool visible);

// マウス制御のためのウィンドウメッセージプロシージャフック関数
void Mouse_ProcessMessage(UINT message, WPARAM wParam, LPARAM lParam);

// マウス内部状態をデバッグログに出力する（毎フレーム呼び出し用）
void Mouse_DebugLog(void);

// フォーカスを再取得し、RELATIVEモードでの入力を再開する
void Mouse_ReacquireFocus(void);


// 3Dカメラ操作時用マウスロック
void LockMouse(void);

// マウスを開放
void UnLockMouse(void);


// 導入方法
//
// 対象のウィンドウが生成されたらそのウィンドウハンドルを引数に初期化関数を呼ぶ
//
// Mouse_Initialize(hwnd);
//
// ウィンドウメッセージプロシージャからマウス制御用フック関数を呼び出す
//
// LResult CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
// {
//     switch (message)
//     {
//     case WM_ACTIVATEAPP:
//     case WM_INPUT:
//     case WM_MOUSEMOVE:
//     case WM_LBUTTONDOWN:
//     case WM_LBUTTONUP:
//     case WM_RBUTTONDOWN:
//     case WM_RBUTTONUP:
//     case WM_MBUTTONDOWN:
//     case WM_MBUTTONUP:
//     case WM_MOUSEWHEEL:
//     case WM_XBUTTONDOWN:
//     case WM_XBUTTONUP:
//     case WM_MOUSEHOVER:
//         Mouse_ProcessMessage(message, wParam, lParam);
//         break;
//
//     }
// }
//

#endif // HAL_YOUHEI_MOUSE_H
