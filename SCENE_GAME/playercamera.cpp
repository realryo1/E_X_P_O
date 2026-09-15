#include "playercamera.h"

#include "camera.h"
#include "define.h"
#include "field.h"
#include "input_manager.h"
#include "main.h"
#include "mouse.h"
#include "player.h"
#include "renderer.h"
#include <cmath>
#if defined(_DEBUG)
#include "course.h"
#include "keyboard.h"
#include "imgui/imgui.h"
#endif

using namespace DirectX;

static const float EXPO_CAMERA_FAR = 2000.0f;
static const float EXPO_CAMERA_FOV = 55.0f;
static float g_Yaw = 0.0f;
static float g_Pitch = 20.0f;
static const float PLAYER_CAMERA_DISTANCE = 3.0f;
static const float PLAYER_CAMERA_LOOK_Y = 0.55f;
static const float PLAYER_CAMERA_STICK_LOOK = 2.5f;

#if defined(_DEBUG)
static const float DEBUG_CAMERA_MOVE_SPEED_DEFAULT = 0.5f;
static bool g_DebugActive = false;
static bool g_DebugLooking = false;
static XMFLOAT3 g_DebugPos = { 0.0f, 0.0f, 0.0f };
static float g_DebugYaw = 0.0f;
static float g_DebugPitch = 20.0f;
static float g_DebugMoveSpeed = DEBUG_CAMERA_MOVE_SPEED_DEFAULT;
#endif

static void SyncRendererCamera(void)
{
	if (GetCamera())
	{
		SetCameraPosition(GetCamera()->GetPos());
	}
}

static void ComputeFollowCamera(XMFLOAT3* outPos, XMFLOAT3* outLookAt)
{
	const XMFLOAT3 targetPos = Player_IsReady() ? Player_GetPos() : Field_GetLookTarget();
	const float yawRad = XMConvertToRadians(g_Yaw);
	const float pitchRad = XMConvertToRadians(g_Pitch);
	const XMVECTOR lookDir = XMVectorSet(
		sinf(yawRad) * cosf(pitchRad),
		-sinf(pitchRad),
		cosf(yawRad) * cosf(pitchRad),
		0.0f
	);
	const XMFLOAT3 lookAt = {
		targetPos.x,
		targetPos.y + PLAYER_CAMERA_LOOK_Y,
		targetPos.z
	};
	const XMVECTOR atVec = XMLoadFloat3(&lookAt);
	const XMVECTOR posVec = XMVectorSubtract(atVec, XMVectorScale(lookDir, PLAYER_CAMERA_DISTANCE));
	XMStoreFloat3(outPos, posVec);
	*outLookAt = lookAt;
}

#if defined(_DEBUG)
static void ClampDebugPitch(void)
{
	if (g_DebugPitch > 89.0f) g_DebugPitch = 89.0f;
	if (g_DebugPitch < -89.0f) g_DebugPitch = -89.0f;
}

static void ApplyDebugView(void)
{
	ClampDebugPitch();
	const float yawRad = XMConvertToRadians(g_DebugYaw);
	const float pitchRad = XMConvertToRadians(g_DebugPitch);
	const XMVECTOR lookDir = XMVectorSet(
		sinf(yawRad) * cosf(pitchRad),
		-sinf(pitchRad),
		cosf(yawRad) * cosf(pitchRad),
		0.0f
	);
	const XMVECTOR posVec = XMLoadFloat3(&g_DebugPos);
	const XMVECTOR atVec = XMVectorAdd(posVec, lookDir);
	XMFLOAT3 atPos;
	XMStoreFloat3(&atPos, atVec);
	if (GetCamera())
	{
		GetCamera()->UpdateView(g_DebugPos, atPos);
	}
	SyncRendererCamera();
}

static void StopDebugLook(void)
{
	if (g_DebugLooking)
	{
		UnLockMouse();
		g_DebugLooking = false;
	}
}

static void SnapDebugToPlayerCamera(void)
{
	XMFLOAT3 lookAt;
	ComputeFollowCamera(&g_DebugPos, &lookAt);
	g_DebugYaw = g_Yaw;
	g_DebugPitch = g_Pitch;
	ClampDebugPitch();
}

static void SetDebugActive(bool active)
{
	if (g_DebugActive == active)
	{
		return;
	}

	g_DebugActive = active;
	if (active)
	{
		SnapDebugToPlayerCamera();
		if (GetCamera())
		{
			g_DebugPos = GetCamera()->GetPos();
		}
		StopDebugLook();
		UnLockMouse();
	}
	else
	{
		StopDebugLook();
	}
	RequestRedraw();
}

static void UpdateDebugCamera(void)
{
	if (!Course_IsMenuOpen())
	{
		Mouse_State mouseState;
		Mouse_GetState(&mouseState);

		const bool imguiWantMouse =
			ImGui::GetCurrentContext() != nullptr &&
			ImGui::GetIO().WantCaptureMouse;
		const bool imguiWantKeyboard =
			ImGui::GetCurrentContext() != nullptr &&
			ImGui::GetIO().WantCaptureKeyboard;

		const bool wantLook =
			mouseState.rightButton && (g_DebugLooking || !imguiWantMouse);
		if (wantLook)
		{
			if (!g_DebugLooking)
			{
				LockMouse();
				g_DebugLooking = true;
			}
			else
			{
				g_DebugYaw += static_cast<float>(mouseState.dx) * 0.1f;
				g_DebugPitch += static_cast<float>(mouseState.dy) * 0.1f;
			}
		}
		else if (g_DebugLooking)
		{
			UnLockMouse();
			g_DebugLooking = false;
		}
		else if (!Mouse_IsVisible())
		{
			// メニュー閉鎖などで相対モードに入ったら、ImGui操作用に戻す
			UnLockMouse();
		}

		if (!imguiWantKeyboard)
		{
			const float yawRad = XMConvertToRadians(g_DebugYaw);
			XMVECTOR forward = XMVectorSet(sinf(yawRad), 0.0f, cosf(yawRad), 0.0f);
			XMVECTOR right = XMVectorSet(cosf(yawRad), 0.0f, -sinf(yawRad), 0.0f);
			XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
			XMVECTOR moveDir = XMVectorZero();

			if (Keyboard_IsKeyDown(KK_W)) moveDir = XMVectorAdd(moveDir, forward);
			if (Keyboard_IsKeyDown(KK_S)) moveDir = XMVectorSubtract(moveDir, forward);
			if (Keyboard_IsKeyDown(KK_D)) moveDir = XMVectorAdd(moveDir, right);
			if (Keyboard_IsKeyDown(KK_A)) moveDir = XMVectorSubtract(moveDir, right);
			if (Keyboard_IsKeyDown(KK_SPACE)) moveDir = XMVectorAdd(moveDir, up);
			if (Keyboard_IsKeyDown(KK_LEFTSHIFT) || Keyboard_IsKeyDown(KK_RIGHTSHIFT))
			{
				moveDir = XMVectorSubtract(moveDir, up);
			}

			if (!XMVector3Equal(moveDir, XMVectorZero()))
			{
				moveDir = XMVector3Normalize(moveDir);
				moveDir = XMVectorScale(moveDir, g_DebugMoveSpeed);
				XMVECTOR pos = XMLoadFloat3(&g_DebugPos);
				pos = XMVectorAdd(pos, moveDir);
				XMStoreFloat3(&g_DebugPos, pos);
			}
		}
	}
	else
	{
		StopDebugLook();
	}

	ApplyDebugView();
	RequestRedraw();
}
#endif

void PlayerCamera_Initialize(float startYaw, float startPitch)
{
	g_Yaw = startYaw;
	g_Pitch = startPitch;
	Camera_Initialize();
	Camera_SetFar(EXPO_CAMERA_FAR);
	Camera_SetFov(EXPO_CAMERA_FOV);
#if defined(_DEBUG)
	g_DebugActive = false;
	g_DebugLooking = false;
	g_DebugMoveSpeed = DEBUG_CAMERA_MOVE_SPEED_DEFAULT;
#endif
}

void PlayerCamera_SetLookAngles(float yaw, float pitch)
{
	g_Yaw = yaw;
	g_Pitch = pitch;
	if (g_Pitch > 89.0f) g_Pitch = 89.0f;
	if (g_Pitch < -89.0f) g_Pitch = -89.0f;
	RequestRedraw();
}

void PlayerCamera_LockMouse(void)
{
#if defined(_DEBUG)
	if (g_DebugActive)
	{
		return;
	}
#endif
	LockMouse();
}

float PlayerCamera_GetYaw(void)
{
#if defined(_DEBUG)
	if (g_DebugActive)
	{
		return g_DebugYaw;
	}
#endif
	return g_Yaw;
}

bool PlayerCamera_IsDebugActive(void)
{
#if defined(_DEBUG)
	return g_DebugActive;
#else
	return false;
#endif
}

void PlayerCamera_UpdateInput(void)
{
#if defined(_DEBUG)
	if (g_DebugActive)
	{
		return;
	}
#endif

	Mouse_State mouseState;
	Mouse_GetState(&mouseState);
	if (mouseState.positionMode == MOUSE_POSITION_MODE_RELATIVE)
	{
		g_Yaw += static_cast<float>(mouseState.dx) * 0.1f;
		g_Pitch += static_cast<float>(mouseState.dy) * 0.1f;
	}

	const Input_Vector2 look = Input_GetLookVector();
	g_Yaw += look.x * PLAYER_CAMERA_STICK_LOOK;
	g_Pitch -= look.y * PLAYER_CAMERA_STICK_LOOK;

	if (g_Pitch > 89.0f) g_Pitch = 89.0f;
	if (g_Pitch < -89.0f) g_Pitch = -89.0f;
}

void PlayerCamera_Update(void)
{
#if defined(_DEBUG)
	if (g_DebugActive)
	{
		UpdateDebugCamera();
		return;
	}
#endif

	XMFLOAT3 camPos;
	XMFLOAT3 lookAt;
	ComputeFollowCamera(&camPos, &lookAt);

	if (GetCamera())
	{
		GetCamera()->UpdateView(camPos, lookAt);
	}
	SyncRendererCamera();
	RequestRedraw();
}

void PlayerCamera_Draw(void)
{
	SyncRendererCamera();
}

void PlayerCamera_DrawDebug(void)
{
#if defined(_DEBUG)
	if (Direct3D_IsTakingScreenshot())
	{
		return;
	}

	ImGui::Begin("Expo Debug Camera");

	bool enabled = g_DebugActive;
	if (ImGui::Checkbox("Enable Free Camera", &enabled))
	{
		SetDebugActive(enabled);
	}

	ImGui::TextUnformatted("WASD: move  Space/Shift: up/down  RMB: look");

	if (g_DebugActive)
	{
		ImGui::DragFloat3("Position", &g_DebugPos.x, 0.1f, 0.0f, 0.0f, "%.3f");
		ImGui::DragFloat("Yaw", &g_DebugYaw, 0.5f, 0.0f, 0.0f, "%.1f");
		if (ImGui::SliderFloat("Pitch", &g_DebugPitch, -89.0f, 89.0f, "%.1f"))
		{
			ClampDebugPitch();
		}
		if (ImGui::DragFloat("Move Speed", &g_DebugMoveSpeed, 0.01f, 0.01f, 20.0f, "%.3f"))
		{
			if (g_DebugMoveSpeed < 0.01f) g_DebugMoveSpeed = 0.01f;
			if (g_DebugMoveSpeed > 20.0f) g_DebugMoveSpeed = 20.0f;
		}
		if (ImGui::Button("Reset Speed"))
		{
			g_DebugMoveSpeed = DEBUG_CAMERA_MOVE_SPEED_DEFAULT;
		}
		if (ImGui::Button("プレイヤーカメラへ合わせる"))
		{
			SnapDebugToPlayerCamera();
		}
		ApplyDebugView();
		RequestRedraw();
	}

	ImGui::End();
#endif
}

void PlayerCamera_Finalize(void)
{
#if defined(_DEBUG)
	StopDebugLook();
	g_DebugActive = false;
#endif
	UnLockMouse();
	Camera_Finalize();
}
