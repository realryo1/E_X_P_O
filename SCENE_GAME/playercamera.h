#pragma once

void PlayerCamera_Initialize(float startYaw = 0.0f, float startPitch = 20.0f);
void PlayerCamera_LockMouse(void);
void PlayerCamera_UpdateInput(void);
void PlayerCamera_Update(void);
void PlayerCamera_Draw(void);
void PlayerCamera_DrawDebug(void);
void PlayerCamera_Finalize(void);
float PlayerCamera_GetYaw(void);
bool PlayerCamera_IsDebugActive(void);
