#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
using namespace DirectX;

void DebugCamera_Initialize(DirectX::XMFLOAT3 startPos = { 0.0f, 2.0f, 0.0f }, float startYaw = 0.0f, float startPitch = 0.0f);
void DebugCamera_Update(void);
void DebugCamera_Draw(void);
void DebugCamera_Finalize(void);
float DebugCamera_GetMoveSpeed(void);
void DebugCamera_SetMoveSpeed(float speed);
void DebugCamera_ResetMoveSpeed(void);
