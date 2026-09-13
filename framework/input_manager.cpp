#include "input_manager.h"

#include "keyboard.h"
#include "sound.h"

namespace
{
    const float kMoveStickThreshold = 0.5f;

    // 前フレームのスティック状態
    float g_PrevLStickX = 0.0f;
    float g_PrevLStickY = 0.0f;

    // 今フレームの LStick トリガー状態
    bool g_LStickTriggerUp = false;
    bool g_LStickTriggerDown = false;
    bool g_LStickTriggerLeft = false;
    bool g_LStickTriggerRight = false;

    SoundData* g_pDecideSe = nullptr;

    int GetActivePlayerIndex()
    {
        return Gamepad_FindConnectedPlayer();
    }

    float MinFloat(float lhs, float rhs)
    {
        return (lhs < rhs) ? lhs : rhs;
    }

    float MaxFloat(float lhs, float rhs)
    {
        return (lhs > rhs) ? lhs : rhs;
    }
}

void Input_Initialize(void)
{
    Gamepad_Initialize();
    Gamepad_SetLayout(GAMEPAD_LAYOUT_SWITCH_ABXY);

    g_pDecideSe = LoadMP3("asset/sound/se/kettei.mp3");
}

void Input_Finalize(void)
{
    Gamepad_Finalize();

    UnloadSound(g_pDecideSe);   g_pDecideSe = nullptr;
}

void Input_Update(void)
{
    const int player = GetActivePlayerIndex();
    Gamepad_ThumbStick ls = Gamepad_GetLeftStick(player);

    // LStick トリガー更新
    g_LStickTriggerUp    = (ls.y >  kMoveStickThreshold) && (g_PrevLStickY <=  kMoveStickThreshold);
    g_LStickTriggerDown  = (ls.y < -kMoveStickThreshold) && (g_PrevLStickY >= -kMoveStickThreshold);
    g_LStickTriggerLeft  = (ls.x < -kMoveStickThreshold) && (g_PrevLStickX >= -kMoveStickThreshold);
    g_LStickTriggerRight = (ls.x >  kMoveStickThreshold) && (g_PrevLStickX <=  kMoveStickThreshold);

    // 状態の保存
    g_PrevLStickX = ls.x;
    g_PrevLStickY = ls.y;
}

bool Input_IsActionDown(Input_Action action)
{
    const int player = GetActivePlayerIndex();

    switch (action)
    {
    case INPUT_ACTION_DECIDE:
        return Keyboard_IsKeyDown(KK_ENTER) || Keyboard_IsKeyDown(KK_SPACE) || Gamepad_IsButtonDown(player, GPB_A);
    case INPUT_ACTION_CANCEL:
        return Keyboard_IsKeyDown(KK_BACK) || Gamepad_IsButtonDown(player, GPB_B);
    case INPUT_ACTION_MENU_UP:
        return Keyboard_IsKeyDown(KK_UP) || Keyboard_IsKeyDown(KK_W) || Gamepad_IsButtonDown(player, GPB_DPAD_UP) || Input_GetMoveVector().y > kMoveStickThreshold;
    case INPUT_ACTION_MENU_DOWN:
        return Keyboard_IsKeyDown(KK_DOWN) || Keyboard_IsKeyDown(KK_S) || Gamepad_IsButtonDown(player, GPB_DPAD_DOWN) || Input_GetMoveVector().y < -kMoveStickThreshold;
    case INPUT_ACTION_MENU_LEFT:
        return Keyboard_IsKeyDown(KK_LEFT) || Keyboard_IsKeyDown(KK_A) || Gamepad_IsButtonDown(player, GPB_DPAD_LEFT) || Input_GetMoveVector().x < -kMoveStickThreshold;
    case INPUT_ACTION_MENU_RIGHT:
        return Keyboard_IsKeyDown(KK_RIGHT) || Keyboard_IsKeyDown(KK_D) || Gamepad_IsButtonDown(player, GPB_DPAD_RIGHT) || Input_GetMoveVector().x > kMoveStickThreshold;
    case INPUT_ACTION_PAUSE:
        return Keyboard_IsKeyDown(KK_ESCAPE) || Gamepad_IsButtonDown(player, GPB_START);
    case INPUT_ACTION_BACK:
        return Keyboard_IsKeyDown(KK_ESCAPE) || Keyboard_IsKeyDown(KK_X) || Gamepad_IsButtonDown(player, GPB_X);
    case INPUT_ACTION_JUMP:
        return Keyboard_IsKeyDown(KK_SPACE) || Gamepad_IsButtonDown(player, GPB_B);
    case INPUT_ACTION_DESCEND:
        return Keyboard_IsKeyDown(KK_LEFTSHIFT) || Keyboard_IsKeyDown(KK_RIGHTSHIFT);
    default:
        return false;
    }
}

bool Input_IsActionTrigger(Input_Action action)
{
    const int player = GetActivePlayerIndex();
    bool triggered = false;

    switch (action)
    {
    case INPUT_ACTION_DECIDE:
        triggered = Keyboard_IsKeyDownTrigger(KK_ENTER) || Keyboard_IsKeyDownTrigger(KK_SPACE) || Gamepad_IsButtonTrigger(player, GPB_A);
        if (triggered && g_pDecideSe)
        {
            PlaySound(g_pDecideSe, false);
        }
        return triggered;
    case INPUT_ACTION_CANCEL:
        triggered = Keyboard_IsKeyDownTrigger(KK_BACK) || Gamepad_IsButtonTrigger(player, GPB_B);
        if (triggered && g_pDecideSe)
        {
            PlaySound(g_pDecideSe, false);//もどるボタンでも音だけなるように
        }
        return triggered;
    case INPUT_ACTION_MENU_UP:
        return Keyboard_IsKeyDownTrigger(KK_UP) || Keyboard_IsKeyDownTrigger(KK_W) || Gamepad_IsButtonTrigger(player, GPB_DPAD_UP) || g_LStickTriggerUp;
    case INPUT_ACTION_MENU_DOWN:
        return Keyboard_IsKeyDownTrigger(KK_DOWN) || Keyboard_IsKeyDownTrigger(KK_S) || Gamepad_IsButtonTrigger(player, GPB_DPAD_DOWN) || g_LStickTriggerDown;
    case INPUT_ACTION_MENU_LEFT:
        return Keyboard_IsKeyDownTrigger(KK_LEFT) || Keyboard_IsKeyDownTrigger(KK_A) || Gamepad_IsButtonTrigger(player, GPB_DPAD_LEFT) || g_LStickTriggerLeft;
    case INPUT_ACTION_MENU_RIGHT:
        return Keyboard_IsKeyDownTrigger(KK_RIGHT) || Keyboard_IsKeyDownTrigger(KK_D) || Gamepad_IsButtonTrigger(player, GPB_DPAD_RIGHT) || g_LStickTriggerRight;
    case INPUT_ACTION_PAUSE:
        return Keyboard_IsKeyDownTrigger(KK_ESCAPE) || Gamepad_IsButtonTrigger(player, GPB_START);
    case INPUT_ACTION_BACK:
        return Keyboard_IsKeyDownTrigger(KK_ESCAPE) || Keyboard_IsKeyDownTrigger(KK_X) || Gamepad_IsButtonTrigger(player, GPB_X);
    case INPUT_ACTION_JUMP:
        return Keyboard_IsKeyDownTrigger(KK_SPACE) || Gamepad_IsButtonTrigger(player, GPB_B);
    case INPUT_ACTION_DESCEND:
        return Keyboard_IsKeyDownTrigger(KK_LEFTSHIFT) || Keyboard_IsKeyDownTrigger(KK_RIGHTSHIFT);
    default:
        return false;
    }
}

Input_Vector2 Input_GetMoveVector(void)
{
    const int player = GetActivePlayerIndex();
    const Gamepad_ThumbStick leftStick = Gamepad_GetLeftStick(player);

    Input_Vector2 out = {};
    out.x = leftStick.x;
    out.y = leftStick.y;

    if (Keyboard_IsKeyDown(KK_A) || Gamepad_IsButtonDown(player, GPB_DPAD_LEFT)) out.x = MinFloat(out.x, -1.0f);
    if (Keyboard_IsKeyDown(KK_D) || Gamepad_IsButtonDown(player, GPB_DPAD_RIGHT)) out.x = MaxFloat(out.x, 1.0f);
    if (Keyboard_IsKeyDown(KK_W) || Gamepad_IsButtonDown(player, GPB_DPAD_UP)) out.y = MaxFloat(out.y, 1.0f);
    if (Keyboard_IsKeyDown(KK_S) || Gamepad_IsButtonDown(player, GPB_DPAD_DOWN)) out.y = MinFloat(out.y, -1.0f);

    return out;
}

Input_Vector2 Input_GetLookVector(void)
{
    const int player = GetActivePlayerIndex();
    const Gamepad_ThumbStick rightStick = Gamepad_GetRightStick(player);
    Input_Vector2 out = {};
    out.x = rightStick.x;
    out.y = rightStick.y;

    if (Keyboard_IsKeyDown(KK_LEFT)) out.x = MinFloat(out.x, -1.0f);
    if (Keyboard_IsKeyDown(KK_RIGHT)) out.x = MaxFloat(out.x, 1.0f);
    if (Keyboard_IsKeyDown(KK_UP)) out.y = MaxFloat(out.y, 1.0f);
    if (Keyboard_IsKeyDown(KK_DOWN)) out.y = MinFloat(out.y, -1.0f);

    return out;
}

float Input_GetZoomDelta(void)
{
    const int player = GetActivePlayerIndex();
    const float zoomIn = Gamepad_GetRightTrigger(player);
    const float zoomOut = Gamepad_GetLeftTrigger(player);
    return zoomIn - zoomOut;
}

void Input_SetRumble(float leftMotor, float rightMotor)
{
    const int player = GetActivePlayerIndex();
    if (player >= 0)
    {
        Gamepad_SetVibration(player, leftMotor, rightMotor);
    }
}

void Input_SetGamepadLayout(Gamepad_Layout layout)
{
    Gamepad_SetLayout(layout);
}

Gamepad_Layout Input_GetGamepadLayout(void)
{
    return Gamepad_GetLayout();
}
