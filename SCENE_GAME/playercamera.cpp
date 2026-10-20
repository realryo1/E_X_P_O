#include "playercamera.h"

#include "camera.h"
#include "define.h"
#include "field.h"
#include "input_manager.h"
#include "main.h"
#include "mouse.h"
#include "player.h"
#include "imgui/imgui.h"
#include "renderer.h"
#include <cmath>
#include "course.h"

using namespace DirectX;

static const float EXPO_CAMERA_FAR = 2000.0f;
static const float EXPO_CAMERA_FOV = 55.0f;
static float g_Yaw = 0.0f;
static float g_Pitch = 20.0f;
static const float PLAYER_CAMERA_DISTANCE = 3.0f;
static const float PLAYER_CAMERA_LOOK_Y = 0.55f;
static const float PLAYER_CAMERA_STICK_LOOK = 2.5f;

static const float DEBUG_CAMERA_MOVE_SPEED_DEFAULT = 0.5f;
static bool g_DebugActive = false;
static bool g_DebugLooking = false;
static XMFLOAT3 g_DebugPos = { 0.0f, 0.0f, 0.0f };
static float g_DebugYaw = 0.0f;
static float g_DebugPitch = 20.0f;
static float g_DebugMoveSpeed = DEBUG_CAMERA_MOVE_SPEED_DEFAULT;

static const float PLAYER_CAMERA_FOV_KICK_DEGREES = 7.0f;
static const float PLAYER_CAMERA_FOV_KICK_SECONDS = 0.35f;
static const float PLAYER_CAMERA_SHAKE_SECONDS = 0.25f;
static float g_FovKickTimer = 0.0f;
static float g_ShakeTimer = 0.0f;
static float g_ShakeAmplitude = 0.0f;
static float g_ShakeTime = 0.0f;

static void SyncRendererCamera(void)
{
	if (GetCamera())
	{
		SetCameraPosition(GetCamera()->GetPos());
	}
}

static float ClampPitch(float pitch)
{
	if (pitch > 89.0f) pitch = 89.0f;
	if (pitch < -89.0f) pitch = -89.0f;
	return pitch;
}

// yaw/pitch（度）から視線方向ベクトルを求める
static XMVECTOR ComputeLookDir(float yawDeg, float pitchDeg)
{
	const float yawRad = XMConvertToRadians(yawDeg);
	const float pitchRad = XMConvertToRadians(pitchDeg);
	return XMVectorSet(
		sinf(yawRad) * cosf(pitchRad),
		-sinf(pitchRad),
		cosf(yawRad) * cosf(pitchRad),
		0.0f
	);
}

// 追従カメラ専用の演出。FOVキックは二次減衰、揺れは残り時間に比例して弱める
static void UpdateFollowEffects(void)
{
	const float frameSeconds = 1.0f / FPS;
	g_FovKickTimer = fmaxf(g_FovKickTimer - frameSeconds, 0.0f);
	g_ShakeTimer = fmaxf(g_ShakeTimer - frameSeconds, 0.0f);
	g_ShakeTime += frameSeconds;
	if (g_ShakeTimer <= 0.0f)
	{
		g_ShakeAmplitude = 0.0f;
	}

	const float kickRate = g_FovKickTimer / PLAYER_CAMERA_FOV_KICK_SECONDS;
	const float fov = EXPO_CAMERA_FOV + PLAYER_CAMERA_FOV_KICK_DEGREES * kickRate * kickRate;
	if (GetCamera())
	{
		GetCamera()->SetFov(fov);
	}
}

static XMFLOAT2 GetShakeOffset(void)
{
	if (g_ShakeTimer <= 0.0f)
	{
		return { 0.0f, 0.0f };
	}
	const float strength = g_ShakeAmplitude * (g_ShakeTimer / PLAYER_CAMERA_SHAKE_SECONDS);
	return {
		strength * sinf(g_ShakeTime * 71.0f),
		strength * cosf(g_ShakeTime * 53.0f) };
}

static void ComputeFollowCamera(XMFLOAT3* outPos, XMFLOAT3* outLookAt)
{
	const XMFLOAT3 targetPos = Player_IsReady() ? Player_GetPos() : Field_GetLookTarget();
	const XMVECTOR lookDir = ComputeLookDir(g_Yaw, g_Pitch);
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

static void ApplyDebugView(void)
{
	g_DebugPitch = ClampPitch(g_DebugPitch);
	const XMVECTOR lookDir = ComputeLookDir(g_DebugYaw, g_DebugPitch);
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
	g_DebugPitch = ClampPitch(g_Pitch);
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

			const Input_Vector2 move = Input_GetMoveVector();
			moveDir = XMVectorAdd(
				moveDir,
				XMVectorScale(forward, move.y));
			moveDir = XMVectorAdd(
				moveDir,
				XMVectorScale(right, move.x));
			if (Input_IsActionDown(INPUT_ACTION_JUMP))
			{
				moveDir = XMVectorAdd(moveDir, up);
			}
			if (Input_IsActionDown(INPUT_ACTION_DESCEND))
			{
				moveDir = XMVectorSubtract(moveDir, up);
			}
			const Input_Vector2 look = Input_GetLookVector();
			g_DebugYaw += look.x * PLAYER_CAMERA_STICK_LOOK;
			g_DebugPitch -= look.y * PLAYER_CAMERA_STICK_LOOK;

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

void PlayerCamera_Initialize(float startYaw, float startPitch)
{
	g_Yaw = startYaw;
	g_Pitch = startPitch;
	Camera_Initialize();
	Camera_SetFar(EXPO_CAMERA_FAR);
	Camera_SetFov(EXPO_CAMERA_FOV);
	g_DebugActive = false;
	g_DebugLooking = false;
	g_DebugMoveSpeed = DEBUG_CAMERA_MOVE_SPEED_DEFAULT;
	g_FovKickTimer = 0.0f;
	g_ShakeTimer = 0.0f;
	g_ShakeAmplitude = 0.0f;
	g_ShakeTime = 0.0f;
}

void PlayerCamera_AddFovKick(void)
{
	g_FovKickTimer = PLAYER_CAMERA_FOV_KICK_SECONDS;
}

void PlayerCamera_AddShake(float amplitude)
{
	const float current = g_ShakeTimer > 0.0f ? g_ShakeAmplitude : 0.0f;
	g_ShakeAmplitude = fmaxf(current, amplitude);
	g_ShakeTimer = PLAYER_CAMERA_SHAKE_SECONDS;
}

void PlayerCamera_SetLookAngles(float yaw, float pitch)
{
	g_Yaw = yaw;
	g_Pitch = ClampPitch(pitch);
	RequestRedraw();
}

void PlayerCamera_LockMouse(void)
{
	if (g_DebugActive)
	{
		return;
	}
	LockMouse();
}

float PlayerCamera_GetYaw(void)
{
	if (g_DebugActive)
	{
		return g_DebugYaw;
	}
	return g_Yaw;
}

bool PlayerCamera_IsDebugActive(void)
{
	return g_DebugActive;
}

void PlayerCamera_UpdateInput(void)
{
	if (g_DebugActive)
	{
		return;
	}

	Mouse_State mouseState;
	Mouse_GetState(&mouseState);
	if (mouseState.positionMode == MOUSE_POSITION_MODE_RELATIVE)
	{
		g_Yaw += static_cast<float>(mouseState.dx) * 0.1f;
		g_Pitch += static_cast<float>(mouseState.dy) * 0.1f;
	}

	const Input_Vector2 look = Input_GetLookVector();
	g_Yaw += look.x * PLAYER_CAMERA_STICK_LOOK;
	g_Pitch = ClampPitch(g_Pitch - look.y * PLAYER_CAMERA_STICK_LOOK);
}

void PlayerCamera_Update(void)
{
	if (g_DebugActive)
	{
		UpdateDebugCamera();
		return;
	}

	XMFLOAT3 camPos;
	XMFLOAT3 lookAt;
	ComputeFollowCamera(&camPos, &lookAt);
	UpdateFollowEffects();
	const XMFLOAT2 shake = GetShakeOffset();
	camPos.x += shake.x;
	camPos.y += shake.y;
	lookAt.x += shake.x;
	lookAt.y += shake.y;

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
}

void PlayerCamera_Finalize(void)
{
	StopDebugLook();
	g_DebugActive = false;
	UnLockMouse();
	Camera_Finalize();
}

void PlayerCamera_SetFreeCameraActive(bool active)
{
	SetDebugActive(active);
}

void PlayerCamera_SetFreeCameraFov(float fov)
{
	if (GetCamera())
	{
		GetCamera()->SetFov(fov);
	}
}

float PlayerCamera_GetFreeCameraMoveSpeed(void)
{
	return g_DebugMoveSpeed;
}

void PlayerCamera_SetFreeCameraMoveSpeed(float speed)
{
	if (speed < 0.01f) speed = 0.01f;
	if (speed > 20.0f) speed = 20.0f;
	g_DebugMoveSpeed = speed;
}

void PlayerCamera_SnapFreeCameraToPlayer(void)
{
	SnapDebugToPlayerCamera();
	ApplyDebugView();
}
