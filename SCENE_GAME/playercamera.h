#pragma once

void PlayerCamera_Initialize(float startYaw = 0.0f, float startPitch = 20.0f);
void PlayerCamera_SetLookAngles(float yaw, float pitch);
void PlayerCamera_LockMouse(void);
void PlayerCamera_UpdateInput(void);
void PlayerCamera_Update(void);
void PlayerCamera_Draw(void);
void PlayerCamera_DrawDebug(void);
void PlayerCamera_Finalize(void);
void PlayerCamera_SetFreeCameraActive(bool active);
void PlayerCamera_SetFreeCameraFov(float fov);
float PlayerCamera_GetFreeCameraMoveSpeed(void);
void PlayerCamera_SetFreeCameraMoveSpeed(float speed);
void PlayerCamera_SnapFreeCameraToPlayer(void);
float PlayerCamera_GetYaw(void);
bool PlayerCamera_IsDebugActive(void);
