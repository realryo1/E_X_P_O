#include "debugcamera.h"
#include "sprite2d.h"
#include "texture.h"
#include "keyboard.h"
#include "fade.h"
#include "debug_ostream.h"
#include "define.h"

#include "renderer.h"
#include "font.h"
#include "mouse.h"
#include "sound.h"
#include "ClickFont.h"
#include "movie.h"
#include "camera.h"
#include "main.h"
#include "imgui/imgui.h"
#include <cmath>

using namespace DirectX;

static XMFLOAT3 g_DebugCameraPos = { 0.0f, 0.0f, -5.0f };
static float g_DebugCameraYaw = 0.0f;
static float g_DebugCameraPitch = 0.0f;
static bool g_IsLooking = false;
static float g_DebugCameraMoveSpeed = 0.1f;
static const float DEBUG_CAMERA_MOVE_SPEED_DEFAULT = 0.1f;

void DebugCamera_Initialize(DirectX::XMFLOAT3 startPos, float startYaw, float startPitch)
{
	Camera_Initialize();
	g_DebugCameraPos = startPos;
	g_DebugCameraYaw = startYaw;
	g_DebugCameraPitch = startPitch;
	g_IsLooking = false;
	g_DebugCameraMoveSpeed = DEBUG_CAMERA_MOVE_SPEED_DEFAULT;
	UnLockMouse();
}

float DebugCamera_GetMoveSpeed(void)
{
	return g_DebugCameraMoveSpeed;
}

void DebugCamera_SetMoveSpeed(float speed)
{
	if (speed < 0.01f)
	{
		speed = 0.01f;
	}
	if (speed > 5.0f)
	{
		speed = 5.0f;
	}
	g_DebugCameraMoveSpeed = speed;
}

void DebugCamera_ResetMoveSpeed(void)
{
	g_DebugCameraMoveSpeed = DEBUG_CAMERA_MOVE_SPEED_DEFAULT;
}

void DebugCamera_Update(void)
{
	const XMFLOAT3 prevPos = g_DebugCameraPos;
	const float prevYaw = g_DebugCameraYaw;
	const float prevPitch = g_DebugCameraPitch;
	const bool prevLooking = g_IsLooking;

	Mouse_State mouseState;
	Mouse_GetState(&mouseState);

	// ImGui 上での右クリックはカメラ操作を開始しない（操作中は継続）
	const bool imguiBlocksStart =
		!g_IsLooking &&
		ImGui::GetCurrentContext() != nullptr &&
		ImGui::GetIO().WantCaptureMouse;

	const bool wantLook = mouseState.rightButton && !imguiBlocksStart;

	if (wantLook)
	{
		if (!g_IsLooking)
		{
			// 絶対座標(UI) → 相対座標(視点)へ。切り替わり直後のデルタは捨てる
			LockMouse();
			g_IsLooking = true;
		}
		else
		{
			g_DebugCameraYaw += static_cast<float>(mouseState.dx) * 0.1f;
			g_DebugCameraPitch += static_cast<float>(mouseState.dy) * 0.1f;
		}
	}
	else if (g_IsLooking)
	{
		UnLockMouse();
		g_IsLooking = false;
	}

	if (g_DebugCameraPitch > 89.0f) g_DebugCameraPitch = 89.0f;
	if (g_DebugCameraPitch < -89.0f) g_DebugCameraPitch = -89.0f;

	float yawRad = XMConvertToRadians(g_DebugCameraYaw);
	float pitchRad = XMConvertToRadians(g_DebugCameraPitch);

	XMVECTOR forward = XMVectorSet(sinf(yawRad), 0.0f, cosf(yawRad), 0.0f);
	XMVECTOR right = XMVectorSet(cosf(yawRad), 0.0f, -sinf(yawRad), 0.0f);
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	
	XMVECTOR moveDir = XMVectorZero();

	if (Keyboard_IsKeyDown(KK_W)) moveDir = XMVectorAdd(moveDir, forward);
	if (Keyboard_IsKeyDown(KK_S)) moveDir = XMVectorSubtract(moveDir, forward);
	if (Keyboard_IsKeyDown(KK_D)) moveDir = XMVectorAdd(moveDir, right);
	if (Keyboard_IsKeyDown(KK_A)) moveDir = XMVectorSubtract(moveDir, right);

	if (Keyboard_IsKeyDown(KK_SPACE)) moveDir = XMVectorAdd(moveDir, up);
	if (Keyboard_IsKeyDown(KK_LEFTSHIFT) || Keyboard_IsKeyDown(KK_RIGHTSHIFT)) moveDir = XMVectorSubtract(moveDir, up);

	if (!XMVector3Equal(moveDir, XMVectorZero()))
	{
		moveDir = XMVector3Normalize(moveDir);
		moveDir = XMVectorScale(moveDir, g_DebugCameraMoveSpeed);
		
		XMVECTOR pos = XMLoadFloat3(&g_DebugCameraPos);
		pos = XMVectorAdd(pos, moveDir);
		XMStoreFloat3(&g_DebugCameraPos, pos);
	}

	XMVECTOR lookDir = XMVectorSet(
		sinf(yawRad) * cosf(pitchRad),
		-sinf(pitchRad),
		cosf(yawRad) * cosf(pitchRad),
		0.0f
	);

	XMVECTOR posVec = XMLoadFloat3(&g_DebugCameraPos);
	XMVECTOR atVec = XMVectorAdd(posVec, lookDir);
	
	XMFLOAT3 atPos;
	XMStoreFloat3(&atPos, atVec);

	if (GetCamera()) {
		GetCamera()->UpdateView(g_DebugCameraPos, atPos);
	}

	const bool moved =
		prevPos.x != g_DebugCameraPos.x ||
		prevPos.y != g_DebugCameraPos.y ||
		prevPos.z != g_DebugCameraPos.z ||
		prevYaw != g_DebugCameraYaw ||
		prevPitch != g_DebugCameraPitch ||
		prevLooking != g_IsLooking;
	if (moved)
	{
		RequestRedraw();
	}
}

void DebugCamera_Draw(void)
{
	// No longer draws a 2D sprite
}

void DebugCamera_Finalize(void)
{
	if (g_IsLooking)
	{
		UnLockMouse();
		g_IsLooking = false;
	}
}
