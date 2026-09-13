#include "player.h"
#include "playercamera.h"
#include "collision.h"
#include "field.h"
#include "input_manager.h"
#include "sprite3d.h"
#include "main.h"
#include "renderer.h"
#include "imgui/imgui.h"

#include <cmath>

using namespace DirectX;

static const char* PLAYER_MODEL_PATH = "asset\\model\\flytaxi.glb";
static const float PLAYER_TARGET_SIZE = 0.8f;
static const float PLAYER_FALLBACK_SCALE = 0.1f;
static const float PLAYER_MOVE_SPEED_DEFAULT = 0.12f;
static const float PLAYER_DASH_VELOCITY = 0.1f;//ダッシュ加算
static const float PLAYER_DASH_DURATION = 0.2f;//ダッシュ継続
static const float PLAYER_YAW_FOLLOW_RATE = 0.06f;
static const float PLAYER_FORWARD_ACCELERATION = 0.0015f;
static const float PLAYER_FORWARD_DECELERATION = 0.0008f;
static const float PLAYER_DASH_RELEASE = PLAYER_FORWARD_DECELERATION;
static const float PLAYER_VERTICAL_ACCELERATION = 0.0015f;
static const float PLAYER_VERTICAL_DECELERATION = 0.0008f;
static const float PLAYER_MAX_PITCH = 18.0f;
static const float PLAYER_PITCH_FOLLOW_RATE = 1.5f;
static const float PLAYER_MAX_ROLL = 20.0f;
static const float PLAYER_ROLL_PER_YAW = 0.8f;
static const float PLAYER_ROLL_FOLLOW_RATE = 1.5f;
static const float PLAYER_MIN_HALF_EXTENT = 0.01f;

static Sprite3D* g_PlayerModel = nullptr;
static XMFLOAT3 g_Pos = { 0.0f, 0.0f, 0.0f };
static XMFLOAT3 g_StartPos = { 0.0f, 0.0f, 0.0f };
static XMFLOAT3 g_HalfExtents = { 0.1f, 0.1f, 0.1f };
static float g_MoveSpeed = PLAYER_MOVE_SPEED_DEFAULT;
static float g_Yaw = 0.0f;
static float g_Pitch = 0.0f;
static float g_Roll = 0.0f;
static float g_ForwardSpeed = 0.0f;
static float g_VerticalSpeed = 0.0f;
static bool g_ControlEnabled = true;
static float g_DashTimer = 0.0f;
static float g_DashVelocity = 0.0f;

static float Approach(float current, float target, float amount)
{
	if (current < target)
	{
		return fminf(current + amount, target);
	}
	return fmaxf(current - amount, target);
}

static float NormalizeAngle(float angle)
{
	while (angle > 180.0f) angle -= 360.0f;
	while (angle < -180.0f) angle += 360.0f;
	return angle;
}

void Player_Initialize(XMFLOAT3 startPos)
{
	g_Pos = startPos;
	g_StartPos = startPos;
	g_MoveSpeed = PLAYER_MOVE_SPEED_DEFAULT;
	g_Yaw = PlayerCamera_GetYaw();
	g_Pitch = 0.0f;
	g_Roll = 0.0f;
	g_ForwardSpeed = 0.0f;
	g_VerticalSpeed = 0.0f;
	g_ControlEnabled = true;
	g_DashTimer = 0.0f;
	g_DashVelocity = 0.0f;
	g_PlayerModel = new Sprite3D(
		startPos,
		{ 1.0f, 1.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f },
		PLAYER_MODEL_PATH,
		S_PBR
	);

	const XMFLOAT3 modelSize = g_PlayerModel->GetModelSize();
	const float largestModelSize = fmaxf(modelSize.x, fmaxf(modelSize.y, modelSize.z));
	const float playerScale = largestModelSize > PLAYER_MIN_HALF_EXTENT
		? PLAYER_TARGET_SIZE / largestModelSize
		: PLAYER_FALLBACK_SCALE;
	g_PlayerModel->SetSize({ playerScale, playerScale, playerScale });
	g_PlayerModel->SetCastShadow(true);
	g_PlayerModel->SetReceiveShadow(true);

	const XMFLOAT3 display = g_PlayerModel->GetDisplaySize();
	g_HalfExtents.x = display.x * 0.5f;
	g_HalfExtents.y = display.y * 0.5f;
	g_HalfExtents.z = display.z * 0.5f;
	if (g_HalfExtents.x < PLAYER_MIN_HALF_EXTENT) g_HalfExtents.x = PLAYER_MIN_HALF_EXTENT;
	if (g_HalfExtents.y < PLAYER_MIN_HALF_EXTENT) g_HalfExtents.y = PLAYER_MIN_HALF_EXTENT;
	if (g_HalfExtents.z < PLAYER_MIN_HALF_EXTENT) g_HalfExtents.z = PLAYER_MIN_HALF_EXTENT;
}

void Player_Finalize(void)
{
	g_ControlEnabled = true;
	g_DashTimer = 0.0f;
	g_DashVelocity = 0.0f;
	SAFE_DELETE(g_PlayerModel);
}

bool Player_IsReady(void)
{
	return g_PlayerModel != nullptr;
}

XMFLOAT3 Player_GetPos(void)
{
	return g_Pos;
}

float Player_GetMoveSpeed(void)
{
	return g_MoveSpeed;
}

void Player_SetMoveSpeed(float speed)
{
	if (speed < 0.01f) speed = 0.01f;
	if (speed > 5.0f) speed = 5.0f;
	g_MoveSpeed = speed;
}

void Player_ResetMoveSpeed(void)
{
	g_MoveSpeed = PLAYER_MOVE_SPEED_DEFAULT;
}

void Player_ActivateDash(void)
{
	g_DashTimer = PLAYER_DASH_DURATION;
	g_DashVelocity = PLAYER_DASH_VELOCITY;
}

void Player_WarpTo(XMFLOAT3 pos)
{
	g_Pos = pos;
	g_ForwardSpeed = 0.0f;
	g_VerticalSpeed = 0.0f;
	if (g_PlayerModel)
	{
		g_PlayerModel->SetPos(g_Pos);
	}
	RequestRedraw();
}

void Player_WarpToStart(void)
{
	Player_WarpTo(g_StartPos);
}

void Player_SetControlEnabled(bool enabled)
{
	g_ControlEnabled = enabled;
	if (!enabled)
	{
		g_ForwardSpeed = 0.0f;
		g_VerticalSpeed = 0.0f;
		g_DashTimer = 0.0f;
		g_DashVelocity = 0.0f;
	}
}

void Player_Update(void)
{
	if (!Player_IsReady())
	{
		if (!Field_IsLoadComplete())
		{
			return;
		}
		Player_Initialize(Field_GetSpawnPos());
		PlayerCamera_LockMouse();
	}

	if (!g_ControlEnabled)
	{
		g_ForwardSpeed = 0.0f;
		g_VerticalSpeed = 0.0f;
		return;
	}

	if (g_DashTimer > 0.0f)
	{
		g_DashTimer -= 1.0f / FPS;
		if (g_DashTimer < 0.0f)
		{
			g_DashTimer = 0.0f;
		}
	}

	const float targetDashVelocity =
		g_DashTimer > 0.0f ? PLAYER_DASH_VELOCITY : 0.0f;
	g_DashVelocity = Approach(
		g_DashVelocity,
		targetDashVelocity,
		PLAYER_DASH_RELEASE);
	const XMFLOAT3 prevPos = g_Pos;
	const float yawDelta = NormalizeAngle(PlayerCamera_GetYaw() - g_Yaw);
	g_Yaw = NormalizeAngle(g_Yaw + yawDelta * PLAYER_YAW_FOLLOW_RATE);

	const float moveYawRad = XMConvertToRadians(PlayerCamera_GetYaw());
	const Input_Vector2 move = Input_GetMoveVector();

	const float forwardInput = fmaxf(move.y, 0.0f);
	const float targetForwardSpeed = forwardInput * g_MoveSpeed;
	const float forwardSpeedStep = targetForwardSpeed > g_ForwardSpeed
		? PLAYER_FORWARD_ACCELERATION
		: PLAYER_FORWARD_DECELERATION;
	g_ForwardSpeed = Approach(
		g_ForwardSpeed,
		targetForwardSpeed,
		forwardSpeedStep);

	float verticalInput = 0.0f;
	if (Input_IsActionDown(INPUT_ACTION_JUMP))
	{
		verticalInput += 1.0f;
	}
	if (Input_IsActionDown(INPUT_ACTION_DESCEND))
	{
		verticalInput -= 1.0f;
	}
	const float targetVerticalSpeed = verticalInput * g_MoveSpeed;
	const float verticalSpeedStep = targetVerticalSpeed > g_VerticalSpeed
		? PLAYER_VERTICAL_ACCELERATION
		: PLAYER_VERTICAL_DECELERATION;
	g_VerticalSpeed = Approach(
		g_VerticalSpeed,
		targetVerticalSpeed,
		verticalSpeedStep);

	XMFLOAT3 delta = { 0.0f, 0.0f, 0.0f };
	const float fx = sinf(moveYawRad);
	const float fz = cosf(moveYawRad);
	delta.x = fx * (g_ForwardSpeed + g_DashVelocity);
	delta.y = g_VerticalSpeed;
	delta.z = fz * (g_ForwardSpeed + g_DashVelocity);

	const float verticalRatio = fmaxf(
		-1.0f,
		fminf(1.0f, g_VerticalSpeed / g_MoveSpeed));
	// 機体の前方（+Z）を上へ向けるため、ピッチは上昇時に負方向へ回す。
	const float targetPitch = -verticalRatio * PLAYER_MAX_PITCH;
	g_Pitch = Approach(g_Pitch, targetPitch, PLAYER_PITCH_FOLLOW_RATE);

	// 正のヨー旋回では右へバンクするため、ロールは反対符号にする。
	const float targetRoll = fmaxf(
		-PLAYER_MAX_ROLL,
		fminf(PLAYER_MAX_ROLL, -yawDelta * PLAYER_ROLL_PER_YAW));
	g_Roll = Approach(g_Roll, targetRoll, PLAYER_ROLL_FOLLOW_RATE);

	XMFLOAT3 nextPos = g_Pos;
	Collision_MoveAABB(g_Pos, g_HalfExtents, delta, &nextPos, nullptr);
	g_Pos = nextPos;

	if (g_PlayerModel)
	{
		g_PlayerModel->SetPos(g_Pos);
		g_PlayerModel->SetRot({ g_Pitch, g_Yaw, g_Roll });
	}

	if (prevPos.x != g_Pos.x || prevPos.y != g_Pos.y || prevPos.z != g_Pos.z)
	{
		RequestRedraw();
	}
}

void Player_Draw(void)
{
	if (g_PlayerModel)
	{
		g_PlayerModel->Draw();
	}
}

void Player_DrawLocalShadow(
	const XMMATRIX& lightView,
	const XMMATRIX& lightProjection,
	XMFLOAT3 focus,
	float radius)
{
	if (g_PlayerModel)
	{
		g_PlayerModel->DrawShadowMap(
			lightView,
			lightProjection,
			focus,
			radius);
	}
}

void Player_DrawDebug(void)
{
	if (!Player_IsReady() || Direct3D_IsTakingScreenshot())
	{
		return;
	}

	ImGui::Begin("Expo Player");

	float moveSpeed = Player_GetMoveSpeed();
	if (ImGui::DragFloat("Move Speed", &moveSpeed, 0.01f, 0.01f, 5.0f, "%.3f"))
	{
		Player_SetMoveSpeed(moveSpeed);
	}
	if (ImGui::Button("Reset Speed"))
	{
		Player_ResetMoveSpeed();
	}
	if (ImGui::Button("原点に戻る"))
	{
		Player_WarpToStart();
	}

	const int ringCollisionId = Field_GetRingCollisionId();
	if (ringCollisionId >= 0 && ImGui::Button("スロープへ"))
	{
		XMFLOAT3 bmin = {};
		XMFLOAT3 bmax = {};
		if (Collision_GetBounds(ringCollisionId, &bmin, &bmax))
		{
			XMFLOAT3 pos;
			pos.x = bmin.x * 0.2f + bmax.x * 0.8f;
			pos.z = 0.5f * (bmin.z + bmax.z);
			pos.y = bmax.y + 0.4f;
			Player_WarpTo(pos);
		}
	}

	ImGui::End();
}
