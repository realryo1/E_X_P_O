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

using namespace DirectX;

static const float EXPO_CAMERA_FAR = 2000.0f;
static float g_Yaw = 0.0f;
static float g_Pitch = 20.0f;
static const float PLAYER_CAMERA_DISTANCE = 3.0f;
static const float PLAYER_CAMERA_LOOK_Y = 0.2f;
static const float PLAYER_CAMERA_STICK_LOOK = 2.5f;

static void SyncRendererCamera(void)
{
	if (GetCamera())
	{
		SetCameraPosition(GetCamera()->GetPos());
	}
}

void PlayerCamera_Initialize(float startYaw, float startPitch)
{
	g_Yaw = startYaw;
	g_Pitch = startPitch;
	Camera_Initialize();
	Camera_SetFar(EXPO_CAMERA_FAR);
}

void PlayerCamera_LockMouse(void)
{
	LockMouse();
}

float PlayerCamera_GetYaw(void)
{
	return g_Yaw;
}

void PlayerCamera_UpdateInput(void)
{
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
	XMFLOAT3 camPos;
	XMStoreFloat3(&camPos, posVec);

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

void PlayerCamera_Finalize(void)
{
	UnLockMouse();
	Camera_Finalize();
}
