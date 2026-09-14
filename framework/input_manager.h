#pragma once

#include "gamepad.h"

typedef enum Input_Action_tag
{
    INPUT_ACTION_DECIDE = 0,        // 決定 (Space, Enter / Aボタン)
    INPUT_ACTION_CANCEL,            // キャンセル / 戻る (BackSpace / Bボタン)
    INPUT_ACTION_MENU_UP,           // メニュー上移動 (W, UP, DPad-UP, LStick-UP)
    INPUT_ACTION_MENU_DOWN,         // メニュー下移動 (S, DOWN, DPad-DOWN, LStick-DOWN)
    INPUT_ACTION_MENU_LEFT,         // メニュー左移動 (A, LEFT, DPad-LEFT, LStick-LEFT)
    INPUT_ACTION_MENU_RIGHT,        // メニュー右移動 (D, RIGHT, DPad-RIGHT, LStick-RIGHT)
    INPUT_ACTION_PAUSE,             // ポーズ画面 (Escape / STARTボタン)
    INPUT_ACTION_BACK,              // 一つ戻る (Escape, X / Xボタン)
    INPUT_ACTION_JUMP,              // 上昇 (Space / Bボタン)
    INPUT_ACTION_DESCEND,           // 下降 (Shift)
} Input_Action;

typedef struct Input_Vector2_tag
{
    float x;
    float y;
} Input_Vector2;

void Input_Initialize(void);
void Input_Finalize(void);
void Input_Update(void);

bool Input_IsActionDown(Input_Action action);
bool Input_IsActionTrigger(Input_Action action);
void Input_PlayDecideSe(void);

Input_Vector2 Input_GetMoveVector(void);
Input_Vector2 Input_GetLookVector(void);
float Input_GetZoomDelta(void);

void Input_SetRumble(float leftMotor, float rightMotor);

void Input_SetGamepadLayout(Gamepad_Layout layout);
Gamepad_Layout Input_GetGamepadLayout(void);
