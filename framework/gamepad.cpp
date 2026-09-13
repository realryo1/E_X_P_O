#include "gamepad.h"

#include <cmath>
#include <cstring>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <hidsdi.h>
#include <hidusage.h>
#include <vector>
#include <xinput.h>

#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "hid.lib")

namespace
{
    const int kRawMaxDevices = 8;
    const int kRawMaxButtonCaps = 32;
    const int kRawMaxValueCaps = 64;
    const int kDirectMaxDevices = 4;
    const int kFallbackPlayerIndex = 0;
    const float kActivityStickThreshold = 0.12f;
    const float kActivityTriggerThreshold = 0.08f;

    struct RawDevice
    {
        HANDLE handle;
        bool connected;
        bool activity;
        PHIDP_PREPARSED_DATA preparsed;
        HIDP_CAPS caps;
        HIDP_BUTTON_CAPS buttonCaps[kRawMaxButtonCaps];
        USHORT buttonCapCount;
        HIDP_VALUE_CAPS valueCaps[kRawMaxValueCaps];
        USHORT valueCapCount;
        XINPUT_GAMEPAD current;
        XINPUT_GAMEPAD previous;
    };

    struct DirectDevice
    {
        IDirectInputDevice8W* device;
        GUID guid;
        bool connected;
        bool activity;
        DIJOYSTATE2 current;
        DIJOYSTATE2 previous;
    };

    XINPUT_STATE gCurrentStates[XUSER_MAX_COUNT] = {};
    XINPUT_STATE gPreviousStates[XUSER_MAX_COUNT] = {};
    bool gConnected[XUSER_MAX_COUNT] = {};

    Gamepad_Layout gLayout = GAMEPAD_LAYOUT_SWITCH_ABXY;
    Gamepad_InputBackend gActiveBackend = GAMEPAD_INPUT_BACKEND_NONE;
    Gamepad_InputBackend gFallbackWinner = GAMEPAD_INPUT_BACKEND_NONE;

    HWND gWindow = NULL;
    bool gRawRegistered = false;
    RawDevice gRawDevices[kRawMaxDevices] = {};
    std::vector<BYTE> gRawBuffer;

    IDirectInput8W* gDirectInput = nullptr;
    DirectDevice gDirectDevices[kDirectMaxDevices] = {};

    XINPUT_GAMEPAD gRawMerged = {};
    XINPUT_GAMEPAD gRawMergedPrev = {};
    bool gRawMergedConnected = false;

    XINPUT_GAMEPAD gDirectMerged = {};
    XINPUT_GAMEPAD gDirectMergedPrev = {};
    bool gDirectMergedConnected = false;

    unsigned long long gActivitySeqCounter = 0;
    unsigned long long gRawLastActivitySeq = 0;
    unsigned long long gDirectLastActivitySeq = 0;

    int ClampInt(int value, int minValue, int maxValue)
    {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    float Clamp01(float value)
    {
        if (value < 0.0f) return 0.0f;
        if (value > 1.0f) return 1.0f;
        return value;
    }

    bool IsValidPlayerIndex(int playerIndex)
    {
        return playerIndex >= 0 && playerIndex < XUSER_MAX_COUNT;
    }

    WORD ToPhysicalMask(Gamepad_Button button)
    {
        switch (button)
        {
        case GPB_A: return (gLayout == GAMEPAD_LAYOUT_SWITCH_ABXY) ? XINPUT_GAMEPAD_B : XINPUT_GAMEPAD_A;
        case GPB_B: return (gLayout == GAMEPAD_LAYOUT_SWITCH_ABXY) ? XINPUT_GAMEPAD_A : XINPUT_GAMEPAD_B;
        case GPB_X: return (gLayout == GAMEPAD_LAYOUT_SWITCH_ABXY) ? XINPUT_GAMEPAD_Y : XINPUT_GAMEPAD_X;
        case GPB_Y: return (gLayout == GAMEPAD_LAYOUT_SWITCH_ABXY) ? XINPUT_GAMEPAD_X : XINPUT_GAMEPAD_Y;
        case GPB_DPAD_UP: return XINPUT_GAMEPAD_DPAD_UP;
        case GPB_DPAD_DOWN: return XINPUT_GAMEPAD_DPAD_DOWN;
        case GPB_DPAD_LEFT: return XINPUT_GAMEPAD_DPAD_LEFT;
        case GPB_DPAD_RIGHT: return XINPUT_GAMEPAD_DPAD_RIGHT;
        case GPB_LEFT_SHOULDER: return XINPUT_GAMEPAD_LEFT_SHOULDER;
        case GPB_RIGHT_SHOULDER: return XINPUT_GAMEPAD_RIGHT_SHOULDER;
        case GPB_START: return XINPUT_GAMEPAD_START;
        case GPB_BACK: return XINPUT_GAMEPAD_BACK;
        case GPB_LEFT_STICK: return XINPUT_GAMEPAD_LEFT_THUMB;
        case GPB_RIGHT_STICK: return XINPUT_GAMEPAD_RIGHT_THUMB;
        default: return 0;
        }
    }

    float NormalizeAxis(SHORT raw, SHORT deadZone)
    {
        const int value = static_cast<int>(raw);
        const int absValue = std::abs(value);
        if (absValue <= deadZone)
        {
            return 0.0f;
        }

        const int range = 32767 - deadZone;
        const int adjusted = ClampInt(absValue - deadZone, 0, range);
        const float normalized = (range > 0) ? (static_cast<float>(adjusted) / static_cast<float>(range)) : 0.0f;
        return (value < 0) ? -normalized : normalized;
    }

    float NormalizeTrigger(BYTE raw)
    {
        if (raw <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
        {
            return 0.0f;
        }

        const int range = 255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
        const int adjusted = ClampInt(static_cast<int>(raw) - XINPUT_GAMEPAD_TRIGGER_THRESHOLD, 0, range);
        return (range > 0) ? (static_cast<float>(adjusted) / static_cast<float>(range)) : 0.0f;
    }

    SHORT ToShortAxis(float value)
    {
        const float clamped = (value < -1.0f) ? -1.0f : ((value > 1.0f) ? 1.0f : value);
        return static_cast<SHORT>(clamped * 32767.0f);
    }

    BYTE ToByteTrigger(float value)
    {
        return static_cast<BYTE>(Clamp01(value) * 255.0f);
    }

    bool IsPadActive(const XINPUT_GAMEPAD& current, const XINPUT_GAMEPAD& previous)
    {
        if (current.wButtons != previous.wButtons)
        {
            return true;
        }

        const int stickThreshold = static_cast<int>(kActivityStickThreshold * 32767.0f);
        if (std::abs(static_cast<int>(current.sThumbLX)) > stickThreshold) return true;
        if (std::abs(static_cast<int>(current.sThumbLY)) > stickThreshold) return true;
        if (std::abs(static_cast<int>(current.sThumbRX)) > stickThreshold) return true;
        if (std::abs(static_cast<int>(current.sThumbRY)) > stickThreshold) return true;

        const BYTE triggerThreshold = static_cast<BYTE>(kActivityTriggerThreshold * 255.0f);
        if (current.bLeftTrigger > triggerThreshold) return true;
        if (current.bRightTrigger > triggerThreshold) return true;

        return false;
    }

    void ClearPlayers(void)
    {
        for (int i = 0; i < XUSER_MAX_COUNT; ++i)
        {
            gConnected[i] = false;
            ZeroMemory(&gCurrentStates[i], sizeof(XINPUT_STATE));
        }
    }

    void ApplyFallbackPlayer(const XINPUT_GAMEPAD& pad)
    {
        ClearPlayers();
        gConnected[kFallbackPlayerIndex] = true;
        gCurrentStates[kFallbackPlayerIndex].dwPacketNumber = gPreviousStates[kFallbackPlayerIndex].dwPacketNumber + 1;
        gCurrentStates[kFallbackPlayerIndex].Gamepad = pad;
    }

    WORD RawButtonUsageToMask(USAGE usage)
    {
        switch (usage)
        {
        case 1: return XINPUT_GAMEPAD_A;
        case 2: return XINPUT_GAMEPAD_B;
        case 3: return XINPUT_GAMEPAD_X;
        case 4: return XINPUT_GAMEPAD_Y;
        case 5: return XINPUT_GAMEPAD_LEFT_SHOULDER;
        case 6: return XINPUT_GAMEPAD_RIGHT_SHOULDER;
        case 9: return XINPUT_GAMEPAD_BACK;
        case 10: return XINPUT_GAMEPAD_START;
        case 11: return XINPUT_GAMEPAD_LEFT_THUMB;
        case 12: return XINPUT_GAMEPAD_RIGHT_THUMB;
        case 0x90: return XINPUT_GAMEPAD_DPAD_UP;
        case 0x91: return XINPUT_GAMEPAD_DPAD_DOWN;
        case 0x92: return XINPUT_GAMEPAD_DPAD_RIGHT;
        case 0x93: return XINPUT_GAMEPAD_DPAD_LEFT;
        default: return 0;
        }
    }

    void ApplyHatToDpad(ULONG hatValue, XINPUT_GAMEPAD* outPad)
    {
        outPad->wButtons &= static_cast<WORD>(~(XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT));
        if (hatValue > 7)
        {
            return;
        }

        if (hatValue == 0 || hatValue == 1 || hatValue == 7) outPad->wButtons |= XINPUT_GAMEPAD_DPAD_UP;
        if (hatValue == 3 || hatValue == 4 || hatValue == 5) outPad->wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
        if (hatValue == 5 || hatValue == 6 || hatValue == 7) outPad->wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
        if (hatValue == 1 || hatValue == 2 || hatValue == 3) outPad->wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
    }

    float NormalizeHidSigned(ULONG value, LONG logicalMin, LONG logicalMax)
    {
        if (logicalMax <= logicalMin)
        {
            return 0.0f;
        }
        const float range = static_cast<float>(logicalMax - logicalMin);
        const float normalized01 = static_cast<float>(static_cast<LONG>(value) - logicalMin) / range;
        return normalized01 * 2.0f - 1.0f;
    }

    float NormalizeHidUnsigned(ULONG value, LONG logicalMin, LONG logicalMax)
    {
        if (logicalMax <= logicalMin)
        {
            return 0.0f;
        }
        const float range = static_cast<float>(logicalMax - logicalMin);
        return Clamp01(static_cast<float>(static_cast<LONG>(value) - logicalMin) / range);
    }

    RawDevice* FindRawDevice(HANDLE handle)
    {
        for (int i = 0; i < kRawMaxDevices; ++i)
        {
            if (gRawDevices[i].handle == handle)
            {
                return &gRawDevices[i];
            }
        }
        return nullptr;
    }

    RawDevice* CreateRawDevice(HANDLE handle)
    {
        int slot = -1;
        for (int i = 0; i < kRawMaxDevices; ++i)
        {
            if (gRawDevices[i].handle == nullptr)
            {
                slot = i;
                break;
            }
        }
        if (slot < 0)
        {
            return nullptr;
        }

        RID_DEVICE_INFO info = {};
        info.cbSize = sizeof(info);
        UINT infoSize = sizeof(info);
        if (GetRawInputDeviceInfo(handle, RIDI_DEVICEINFO, &info, &infoSize) == static_cast<UINT>(-1))
        {
            return nullptr;
        }

        if (info.dwType != RIM_TYPEHID)
        {
            return nullptr;
        }
        if (info.hid.usUsagePage != 0x01 || (info.hid.usUsage != 0x04 && info.hid.usUsage != 0x05))
        {
            return nullptr;
        }

        UINT prepSize = 0;
        if (GetRawInputDeviceInfo(handle, RIDI_PREPARSEDDATA, nullptr, &prepSize) == static_cast<UINT>(-1) || prepSize == 0)
        {
            return nullptr;
        }

        BYTE* prepData = new BYTE[prepSize];
        if (GetRawInputDeviceInfo(handle, RIDI_PREPARSEDDATA, prepData, &prepSize) == static_cast<UINT>(-1))
        {
            delete[] prepData;
            return nullptr;
        }

        RawDevice& device = gRawDevices[slot];
        ZeroMemory(&device, sizeof(device));
        device.handle = handle;
        device.connected = true;
        device.preparsed = reinterpret_cast<PHIDP_PREPARSED_DATA>(prepData);

        if (HidP_GetCaps(device.preparsed, &device.caps) != HIDP_STATUS_SUCCESS)
        {
            delete[] prepData;
            ZeroMemory(&device, sizeof(device));
            return nullptr;
        }

        device.buttonCapCount = static_cast<USHORT>(ClampInt(device.caps.NumberInputButtonCaps, 0, kRawMaxButtonCaps));
        if (device.buttonCapCount > 0)
        {
            HidP_GetButtonCaps(HidP_Input, device.buttonCaps, &device.buttonCapCount, device.preparsed);
        }

        device.valueCapCount = static_cast<USHORT>(ClampInt(device.caps.NumberInputValueCaps, 0, kRawMaxValueCaps));
        if (device.valueCapCount > 0)
        {
            HidP_GetValueCaps(HidP_Input, device.valueCaps, &device.valueCapCount, device.preparsed);
        }

        return &device;
    }

    RawDevice* FindOrCreateRawDevice(HANDLE handle)
    {
        RawDevice* found = FindRawDevice(handle);
        if (found != nullptr)
        {
            return found;
        }
        return CreateRawDevice(handle);
    }

    void ParseRawReport(const RawDevice& device, const BYTE* report, ULONG reportSize, XINPUT_GAMEPAD* outPad)
    {
        if (device.preparsed == nullptr || report == nullptr || reportSize == 0 || outPad == nullptr)
        {
            return;
        }

        bool leftTriggerDigital = false;
        bool rightTriggerDigital = false;

        for (USHORT i = 0; i < device.buttonCapCount; ++i)
        {
            ULONG usageLength = 32;
            USAGE usages[32] = {};
            const HIDP_BUTTON_CAPS& cap = device.buttonCaps[i];
            if (HidP_GetUsages(HidP_Input, cap.UsagePage, cap.LinkCollection, usages, &usageLength, device.preparsed, reinterpret_cast<PCHAR>(const_cast<BYTE*>(report)), reportSize) != HIDP_STATUS_SUCCESS)
            {
                continue;
            }

            for (ULONG u = 0; u < usageLength; ++u)
            {
                if (usages[u] == 7)
                {
                    leftTriggerDigital = true;
                }
                else if (usages[u] == 8)
                {
                    rightTriggerDigital = true;
                }
                outPad->wButtons |= RawButtonUsageToMask(usages[u]);
            }
        }

        for (USHORT i = 0; i < device.valueCapCount; ++i)
        {
            const HIDP_VALUE_CAPS& cap = device.valueCaps[i];
            const USAGE usage = cap.IsRange ? cap.Range.UsageMin : cap.NotRange.Usage;

            ULONG value = 0;
            if (HidP_GetUsageValue(HidP_Input, cap.UsagePage, cap.LinkCollection, usage, &value, device.preparsed, reinterpret_cast<PCHAR>(const_cast<BYTE*>(report)), reportSize) != HIDP_STATUS_SUCCESS)
            {
                continue;
            }

            if (cap.UsagePage != HID_USAGE_PAGE_GENERIC)
            {
                continue;
            }

            switch (usage)
            {
            case HID_USAGE_GENERIC_X:
                outPad->sThumbLX = ToShortAxis(NormalizeHidSigned(value, cap.LogicalMin, cap.LogicalMax));
                break;
            case HID_USAGE_GENERIC_Y:
                outPad->sThumbLY = ToShortAxis(-NormalizeHidSigned(value, cap.LogicalMin, cap.LogicalMax));
                break;
            case HID_USAGE_GENERIC_RX:
                outPad->sThumbRX = ToShortAxis(NormalizeHidSigned(value, cap.LogicalMin, cap.LogicalMax));
                break;
            case HID_USAGE_GENERIC_RY:
                outPad->sThumbRY = ToShortAxis(-NormalizeHidSigned(value, cap.LogicalMin, cap.LogicalMax));
                break;
            case HID_USAGE_GENERIC_Z:
                outPad->bLeftTrigger = ToByteTrigger(NormalizeHidUnsigned(value, cap.LogicalMin, cap.LogicalMax));
                break;
            case HID_USAGE_GENERIC_RZ:
                outPad->bRightTrigger = ToByteTrigger(NormalizeHidUnsigned(value, cap.LogicalMin, cap.LogicalMax));
                break;
            case HID_USAGE_GENERIC_HATSWITCH:
                ApplyHatToDpad(value, outPad);
                break;
            default:
                break;
            }
        }

        if (leftTriggerDigital && outPad->bLeftTrigger < 250)
        {
            outPad->bLeftTrigger = 255;
        }
        if (rightTriggerDigital && outPad->bRightTrigger < 250)
        {
            outPad->bRightTrigger = 255;
        }
    }

    BOOL CALLBACK EnumDirectInputDevice(const DIDEVICEINSTANCEW* instance, VOID* context)
    {
        int* count = reinterpret_cast<int*>(context);
        if (instance == nullptr || count == nullptr)
        {
            return DIENUM_CONTINUE;
        }

        if (*count >= kDirectMaxDevices)
        {
            return DIENUM_STOP;
        }

        IDirectInputDevice8W* device = nullptr;
        if (FAILED(gDirectInput->CreateDevice(instance->guidInstance, &device, nullptr)))
        {
            return DIENUM_CONTINUE;
        }

        if (FAILED(device->SetDataFormat(&c_dfDIJoystick2)))
        {
            device->Release();
            return DIENUM_CONTINUE;
        }

        if (gWindow != NULL)
        {
            device->SetCooperativeLevel(gWindow, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
        }
        device->Acquire();

        DirectDevice& out = gDirectDevices[*count];
        ZeroMemory(&out, sizeof(out));
        out.device = device;
        out.guid = instance->guidInstance;
        out.connected = true;
        (*count)++;

        return DIENUM_CONTINUE;
    }

    void ReleaseDirectDevices(void)
    {
        for (int i = 0; i < kDirectMaxDevices; ++i)
        {
            if (gDirectDevices[i].device != nullptr)
            {
                gDirectDevices[i].device->Unacquire();
                gDirectDevices[i].device->Release();
                gDirectDevices[i].device = nullptr;
            }
            ZeroMemory(&gDirectDevices[i], sizeof(gDirectDevices[i]));
        }
    }

    XINPUT_GAMEPAD ConvertDirectState(const DIJOYSTATE2& js)
    {
        XINPUT_GAMEPAD out = {};
        out.sThumbLX = static_cast<SHORT>(ClampInt(static_cast<int>(js.lX), -32768, 32767));
        out.sThumbLY = static_cast<SHORT>(-ClampInt(static_cast<int>(js.lY), -32768, 32767));
        out.sThumbRX = static_cast<SHORT>(ClampInt(static_cast<int>(js.lRx), -32768, 32767));
        out.sThumbRY = static_cast<SHORT>(-ClampInt(static_cast<int>(js.lRy), -32768, 32767));

        float lt = Clamp01(static_cast<float>(js.rglSlider[0]) / 65535.0f);
        float rt = Clamp01(static_cast<float>(js.rglSlider[1]) / 65535.0f);
        if (lt <= 0.01f) lt = Clamp01(static_cast<float>(js.lZ + 32768) / 65535.0f);
        if (rt <= 0.01f) rt = Clamp01(static_cast<float>(js.lRz + 32768) / 65535.0f);
        out.bLeftTrigger = ToByteTrigger(lt);
        out.bRightTrigger = ToByteTrigger(rt);

        if (js.rgbButtons[0] & 0x80) out.wButtons |= XINPUT_GAMEPAD_A;
        if (js.rgbButtons[1] & 0x80) out.wButtons |= XINPUT_GAMEPAD_B;
        if (js.rgbButtons[2] & 0x80) out.wButtons |= XINPUT_GAMEPAD_X;
        if (js.rgbButtons[3] & 0x80) out.wButtons |= XINPUT_GAMEPAD_Y;
        if (js.rgbButtons[4] & 0x80) out.wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
        if (js.rgbButtons[5] & 0x80) out.wButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
        if (js.rgbButtons[6] & 0x80) out.bLeftTrigger = 255;
        if (js.rgbButtons[7] & 0x80) out.bRightTrigger = 255;
        if (js.rgbButtons[8] & 0x80) out.wButtons |= XINPUT_GAMEPAD_BACK;
        if (js.rgbButtons[9] & 0x80) out.wButtons |= XINPUT_GAMEPAD_START;
        if (js.rgbButtons[10] & 0x80) out.wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;
        if (js.rgbButtons[11] & 0x80) out.wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;

        const DWORD pov = js.rgdwPOV[0];
        if (pov != 0xFFFFFFFF)
        {
            const int angle = static_cast<int>(pov / 100);
            if (angle >= 315 || angle < 45) out.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
            if (angle >= 45 && angle < 135) out.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
            if (angle >= 135 && angle < 225) out.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
            if (angle >= 225 && angle < 315) out.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
        }

        return out;
    }

    void UpdateRawMerged(void)
    {
        gRawMergedPrev = gRawMerged;
        gRawMerged = {};
        gRawMergedConnected = false;

        int selected = -1;
        for (int i = 0; i < kRawMaxDevices; ++i)
        {
            RawDevice& d = gRawDevices[i];
            if (d.handle == nullptr || !d.connected)
            {
                continue;
            }

            if (selected < 0)
            {
                selected = i;
            }
            if (d.activity || IsPadActive(d.current, d.previous))
            {
                selected = i;
                break;
            }
        }

        if (selected >= 0)
        {
            gRawMerged = gRawDevices[selected].current;
            gRawMergedConnected = true;
        }

        for (int i = 0; i < kRawMaxDevices; ++i)
        {
            gRawDevices[i].activity = false;
        }
    }

    void UpdateDirectMerged(void)
    {
        gDirectMergedPrev = gDirectMerged;
        gDirectMerged = {};
        gDirectMergedConnected = false;

        int selected = -1;
        for (int i = 0; i < kDirectMaxDevices; ++i)
        {
            DirectDevice& d = gDirectDevices[i];
            if (d.device == nullptr)
            {
                continue;
            }

            d.previous = d.current;

            HRESULT hr = d.device->Poll();
            if (FAILED(hr))
            {
                d.device->Acquire();
                hr = d.device->Poll();
            }
            if (FAILED(hr) || FAILED(d.device->GetDeviceState(sizeof(DIJOYSTATE2), &d.current)))
            {
                d.connected = false;
                continue;
            }

            d.connected = true;
            const XINPUT_GAMEPAD now = ConvertDirectState(d.current);
            const XINPUT_GAMEPAD old = ConvertDirectState(d.previous);
            d.activity = IsPadActive(now, old);
            if (d.activity)
            {
                gDirectLastActivitySeq = ++gActivitySeqCounter;
            }

            if (selected < 0)
            {
                selected = i;
            }
            if (d.activity)
            {
                selected = i;
                break;
            }
        }

        if (selected >= 0)
        {
            gDirectMerged = ConvertDirectState(gDirectDevices[selected].current);
            gDirectMergedConnected = true;
        }
    }

    Gamepad_InputBackend PickFallbackBackend(void)
    {
        if (gFallbackWinner == GAMEPAD_INPUT_BACKEND_RAWINPUT && !gRawMergedConnected)
        {
            gFallbackWinner = GAMEPAD_INPUT_BACKEND_NONE;
        }
        if (gFallbackWinner == GAMEPAD_INPUT_BACKEND_DIRECTINPUT && !gDirectMergedConnected)
        {
            gFallbackWinner = GAMEPAD_INPUT_BACKEND_NONE;
        }

        if (gFallbackWinner == GAMEPAD_INPUT_BACKEND_NONE)
        {
            const bool rawActive = gRawMergedConnected && IsPadActive(gRawMerged, gRawMergedPrev);
            const bool directActive = gDirectMergedConnected && IsPadActive(gDirectMerged, gDirectMergedPrev);

            if (rawActive && !directActive)
            {
                gFallbackWinner = GAMEPAD_INPUT_BACKEND_RAWINPUT;
            }
            else if (directActive && !rawActive)
            {
                gFallbackWinner = GAMEPAD_INPUT_BACKEND_DIRECTINPUT;
            }
            else if (rawActive && directActive)
            {
                gFallbackWinner = (gRawLastActivitySeq >= gDirectLastActivitySeq) ? GAMEPAD_INPUT_BACKEND_RAWINPUT : GAMEPAD_INPUT_BACKEND_DIRECTINPUT;
            }
            else if (gRawMergedConnected && !gDirectMergedConnected)
            {
                gFallbackWinner = GAMEPAD_INPUT_BACKEND_RAWINPUT;
            }
            else if (gDirectMergedConnected && !gRawMergedConnected)
            {
                gFallbackWinner = GAMEPAD_INPUT_BACKEND_DIRECTINPUT;
            }
            else if (gRawMergedConnected && gDirectMergedConnected)
            {
                gFallbackWinner = (gRawLastActivitySeq >= gDirectLastActivitySeq) ? GAMEPAD_INPUT_BACKEND_RAWINPUT : GAMEPAD_INPUT_BACKEND_DIRECTINPUT;
            }
        }

        if (gFallbackWinner == GAMEPAD_INPUT_BACKEND_RAWINPUT && gRawMergedConnected)
        {
            return GAMEPAD_INPUT_BACKEND_RAWINPUT;
        }
        if (gFallbackWinner == GAMEPAD_INPUT_BACKEND_DIRECTINPUT && gDirectMergedConnected)
        {
            return GAMEPAD_INPUT_BACKEND_DIRECTINPUT;
        }
        return GAMEPAD_INPUT_BACKEND_NONE;
    }
}

void Gamepad_Initialize(void)
{
    ZeroMemory(gCurrentStates, sizeof(gCurrentStates));
    ZeroMemory(gPreviousStates, sizeof(gPreviousStates));
    ZeroMemory(gConnected, sizeof(gConnected));
    ZeroMemory(gRawDevices, sizeof(gRawDevices));
    ZeroMemory(gDirectDevices, sizeof(gDirectDevices));

    gLayout = GAMEPAD_LAYOUT_SWITCH_ABXY;
    gActiveBackend = GAMEPAD_INPUT_BACKEND_NONE;
    gFallbackWinner = GAMEPAD_INPUT_BACKEND_NONE;
    gRawMerged = {};
    gRawMergedPrev = {};
    gDirectMerged = {};
    gDirectMergedPrev = {};
    gRawMergedConnected = false;
    gDirectMergedConnected = false;
    gActivitySeqCounter = 0;
    gRawLastActivitySeq = 0;
    gDirectLastActivitySeq = 0;

    gWindow = GetForegroundWindow();
    if (gWindow == NULL)
    {
        gWindow = GetActiveWindow();
    }

    RAWINPUTDEVICE rid[2] = {};
    rid[0].usUsagePage = 0x01;
    rid[0].usUsage = 0x04;
    rid[0].dwFlags = (gWindow != NULL) ? RIDEV_INPUTSINK : 0;
    rid[0].hwndTarget = gWindow;
    rid[1].usUsagePage = 0x01;
    rid[1].usUsage = 0x05;
    rid[1].dwFlags = (gWindow != NULL) ? RIDEV_INPUTSINK : 0;
    rid[1].hwndTarget = gWindow;
    gRawRegistered = (RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE)) == TRUE);

    if (FAILED(DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, reinterpret_cast<void**>(&gDirectInput), nullptr)))
    {
        gDirectInput = nullptr;
    }
    else
    {
        int count = 0;
        gDirectInput->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumDirectInputDevice, &count, DIEDFL_ATTACHEDONLY);
    }
}

void Gamepad_Finalize(void)
{
    for (int i = 0; i < XUSER_MAX_COUNT; ++i)
    {
        Gamepad_SetVibration(i, 0.0f, 0.0f);
    }

    for (int i = 0; i < kRawMaxDevices; ++i)
    {
        if (gRawDevices[i].preparsed != nullptr)
        {
            delete[] reinterpret_cast<BYTE*>(gRawDevices[i].preparsed);
            gRawDevices[i].preparsed = nullptr;
        }
        ZeroMemory(&gRawDevices[i], sizeof(gRawDevices[i]));
    }

    ReleaseDirectDevices();
    if (gDirectInput != nullptr)
    {
        gDirectInput->Release();
        gDirectInput = nullptr;
    }

    gRawRegistered = false;
}

void Gamepad_ProcessMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    if (!gRawRegistered || message != WM_INPUT)
    {
        return;
    }

    UINT size = 0;
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1) || size == 0)
    {
        return;
    }

    if (gRawBuffer.size() < size)
    {
        gRawBuffer.resize(size);
    }

    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, gRawBuffer.data(), &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1))
    {
        return;
    }

    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(gRawBuffer.data());
    if (raw->header.dwType != RIM_TYPEHID)
    {
        return;
    }

    RawDevice* device = FindOrCreateRawDevice(raw->header.hDevice);
    if (device == nullptr)
    {
        return;
    }

    const ULONG reportCount = raw->data.hid.dwCount;
    const ULONG reportSize = raw->data.hid.dwSizeHid;
    const BYTE* reports = raw->data.hid.bRawData;

    for (ULONG i = 0; i < reportCount; ++i)
    {
        const BYTE* report = reports + (i * reportSize);
        XINPUT_GAMEPAD parsed = {};
        ParseRawReport(*device, report, reportSize, &parsed);

        device->previous = device->current;
        device->current = parsed;
        device->connected = true;
        device->activity = IsPadActive(device->current, device->previous);
        if (device->activity)
        {
            gRawLastActivitySeq = ++gActivitySeqCounter;
        }
    }
}

void Gamepad_Update(void)
{
    for (int i = 0; i < XUSER_MAX_COUNT; ++i)
    {
        gPreviousStates[i] = gCurrentStates[i];
    }

    bool anyXInput = false;
    for (int i = 0; i < XUSER_MAX_COUNT; ++i)
    {
        XINPUT_STATE state = {};
        const DWORD result = XInputGetState(i, &state);
        gConnected[i] = (result == ERROR_SUCCESS);
        if (gConnected[i])
        {
            anyXInput = true;
            gCurrentStates[i] = state;
        }
        else
        {
            ZeroMemory(&gCurrentStates[i], sizeof(XINPUT_STATE));
        }
    }

    if (anyXInput)
    {
        gActiveBackend = GAMEPAD_INPUT_BACKEND_XINPUT;
        gFallbackWinner = GAMEPAD_INPUT_BACKEND_NONE;
        return;
    }

    UpdateRawMerged();
    UpdateDirectMerged();

    const Gamepad_InputBackend fallback = PickFallbackBackend();
    if (fallback == GAMEPAD_INPUT_BACKEND_RAWINPUT)
    {
        ApplyFallbackPlayer(gRawMerged);
        gActiveBackend = GAMEPAD_INPUT_BACKEND_RAWINPUT;
        return;
    }

    if (fallback == GAMEPAD_INPUT_BACKEND_DIRECTINPUT)
    {
        ApplyFallbackPlayer(gDirectMerged);
        gActiveBackend = GAMEPAD_INPUT_BACKEND_DIRECTINPUT;
        return;
    }

    ClearPlayers();
    gActiveBackend = GAMEPAD_INPUT_BACKEND_NONE;
}

bool Gamepad_IsConnected(int playerIndex)
{
    if (!IsValidPlayerIndex(playerIndex))
    {
        return false;
    }
    return gConnected[playerIndex];
}

int Gamepad_FindConnectedPlayer(void)
{
    for (int i = 0; i < XUSER_MAX_COUNT; ++i)
    {
        if (gConnected[i])
        {
            return i;
        }
    }
    return -1;
}

unsigned int Gamepad_GetConnectedMask(void)
{
    unsigned int mask = 0;
    for (int i = 0; i < XUSER_MAX_COUNT; ++i)
    {
        if (gConnected[i])
        {
            mask |= (1u << i);
        }
    }
    return mask;
}

bool Gamepad_IsButtonDown(int playerIndex, Gamepad_Button button)
{
    if (!IsValidPlayerIndex(playerIndex) || !gConnected[playerIndex])
    {
        return false;
    }

    const WORD mask = ToPhysicalMask(button);
    if (mask == 0)
    {
        return false;
    }

    return (gCurrentStates[playerIndex].Gamepad.wButtons & mask) != 0;
}

bool Gamepad_IsButtonTrigger(int playerIndex, Gamepad_Button button)
{
    if (!IsValidPlayerIndex(playerIndex) || !gConnected[playerIndex])
    {
        return false;
    }

    const WORD mask = ToPhysicalMask(button);
    if (mask == 0)
    {
        return false;
    }

    const bool nowDown = (gCurrentStates[playerIndex].Gamepad.wButtons & mask) != 0;
    const bool prevDown = (gPreviousStates[playerIndex].Gamepad.wButtons & mask) != 0;
    return nowDown && !prevDown;
}

Gamepad_ThumbStick Gamepad_GetLeftStick(int playerIndex)
{
    Gamepad_ThumbStick out = { 0.0f, 0.0f };
    if (!IsValidPlayerIndex(playerIndex) || !gConnected[playerIndex])
    {
        return out;
    }

    out.x = NormalizeAxis(gCurrentStates[playerIndex].Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    out.y = NormalizeAxis(gCurrentStates[playerIndex].Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    return out;
}

Gamepad_ThumbStick Gamepad_GetRightStick(int playerIndex)
{
    Gamepad_ThumbStick out = { 0.0f, 0.0f };
    if (!IsValidPlayerIndex(playerIndex) || !gConnected[playerIndex])
    {
        return out;
    }

    out.x = NormalizeAxis(gCurrentStates[playerIndex].Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    out.y = NormalizeAxis(gCurrentStates[playerIndex].Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    return out;
}

float Gamepad_GetLeftTrigger(int playerIndex)
{
    if (!IsValidPlayerIndex(playerIndex) || !gConnected[playerIndex])
    {
        return 0.0f;
    }
    return NormalizeTrigger(gCurrentStates[playerIndex].Gamepad.bLeftTrigger);
}

float Gamepad_GetRightTrigger(int playerIndex)
{
    if (!IsValidPlayerIndex(playerIndex) || !gConnected[playerIndex])
    {
        return 0.0f;
    }
    return NormalizeTrigger(gCurrentStates[playerIndex].Gamepad.bRightTrigger);
}

void Gamepad_SetVibration(int playerIndex, float leftMotor, float rightMotor)
{
    if (!IsValidPlayerIndex(playerIndex))
    {
        return;
    }

    if (gActiveBackend != GAMEPAD_INPUT_BACKEND_XINPUT)
    {
        return;
    }

    XINPUT_VIBRATION vibration = {};
    vibration.wLeftMotorSpeed = static_cast<WORD>(Clamp01(leftMotor) * 65535.0f);
    vibration.wRightMotorSpeed = static_cast<WORD>(Clamp01(rightMotor) * 65535.0f);
    XInputSetState(playerIndex, &vibration);
}

void Gamepad_SetLayout(Gamepad_Layout layout)
{
    gLayout = layout;
}

Gamepad_Layout Gamepad_GetLayout(void)
{
    return gLayout;
}

Gamepad_InputBackend Gamepad_GetActiveBackend(void)
{
    return gActiveBackend;
}
