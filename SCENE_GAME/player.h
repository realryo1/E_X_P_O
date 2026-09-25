#pragma once

#include <DirectXMath.h>

void Player_Initialize(DirectX::XMFLOAT3 startPos);
void Player_Finalize(void);
void Player_Update(void);
void Player_Draw(void);
void Player_DrawLocalShadow(
	const DirectX::XMMATRIX& lightView,
	const DirectX::XMMATRIX& lightProjection,
	DirectX::XMFLOAT3 focus,
	float radius);
void Player_DrawDebug(void);
bool Player_IsReady(void);
DirectX::XMFLOAT3 Player_GetPos(void);
DirectX::XMFLOAT3 Player_GetHalfExtents(void);
float Player_GetMoveSpeed(void);
void Player_SetMoveSpeed(float speed);
void Player_ResetMoveSpeed(void);
void Player_WarpToStart(void);
void Player_WarpTo(DirectX::XMFLOAT3 pos);
void Player_SetFacingYaw(float yaw);
void Player_SetControlEnabled(bool enabled);
void Player_ActivateDash(void);
void Player_SetVisible(bool visible);
