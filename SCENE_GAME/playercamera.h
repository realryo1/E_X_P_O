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
// ブースト時に視野角を一瞬広げる
void PlayerCamera_AddFovKick(void);
// 衝突時の揺れ。amplitude は最大オフセット（m）。揺れ中は大きい方を採用する
void PlayerCamera_AddShake(float amplitude);
