#include "envprobe.h"
#include "field.h"
#include "player.h"
#include "playercamera.h"
#include "camera.h"
#include "renderer.h"

using namespace DirectX;

static const float ENV_PROBE_RADIUS = 72.0f;
static const float ENV_PROBE_FOV = 90.0f;
static const float ENV_PROBE_NEAR = 0.2f;
static const float ENV_PROBE_FAR = 800.0f;

static const XMFLOAT3 ENV_CUBE_LOOK[ENV_CUBE_FACE_COUNT] = {
	{ 1.0f, 0.0f, 0.0f },
	{ -1.0f, 0.0f, 0.0f },
	{ 0.0f, 1.0f, 0.0f },
	{ 0.0f, -1.0f, 0.0f },
	{ 0.0f, 0.0f, 1.0f },
	{ 0.0f, 0.0f, -1.0f }
};
static const XMFLOAT3 ENV_CUBE_UP[ENV_CUBE_FACE_COUNT] = {
	{ 0.0f, 1.0f, 0.0f },
	{ 0.0f, 1.0f, 0.0f },
	{ 0.0f, 0.0f, -1.0f },
	{ 0.0f, 0.0f, 1.0f },
	{ 0.0f, 1.0f, 0.0f },
	{ 0.0f, 1.0f, 0.0f }
};

static int g_NextFace = 0;
static int g_CapturedFaces = 0;
static bool g_Ready = false;

void EnvProbe_Initialize(void)
{
	g_NextFace = 0;
	g_CapturedFaces = 0;
	g_Ready = false;
}

void EnvProbe_Finalize(void)
{
	g_NextFace = 0;
	g_CapturedFaces = 0;
	g_Ready = false;
}

bool EnvProbe_IsReady(void)
{
	return g_Ready;
}

void EnvProbe_CaptureOneFace(void)
{
	if (g_Ready)
	{
		return;
	}
	XMFLOAT3 probeCenter = {};
	if (!Field_TryGetNull2ProbeCenter(&probeCenter))
	{
		return;
	}

	const XMFLOAT3 playerPos = Player_IsReady() ? Player_GetPos() : probeCenter;
	const float dx = playerPos.x - probeCenter.x;
	const float dy = playerPos.y - probeCenter.y;
	const float dz = playerPos.z - probeCenter.z;
	if ((dx * dx + dy * dy + dz * dz) >
		ENV_PROBE_RADIUS * ENV_PROBE_RADIUS)
	{
		return;
	}

	Camera* camera = GetCamera();
	if (!camera)
	{
		return;
	}

	const int face = g_NextFace;
	if (!BeginEnvCubeFace(face))
	{
		return;
	}

	const XMFLOAT3 savedPos = camera->GetPos();
	const XMFLOAT3 savedAt = camera->GetAtPos();
	const XMFLOAT3 savedUp = camera->GetUpVec();
	const float savedFov = camera->GetFov();
	const float savedAspect = camera->GetAspect();
	const float savedNear = camera->GetNear();
	const float savedFar = camera->GetFar();

	const XMFLOAT3 look = ENV_CUBE_LOOK[face];
	const XMFLOAT3 at = {
		probeCenter.x + look.x,
		probeCenter.y + look.y,
		probeCenter.z + look.z
	};
	camera->SetUpVec(ENV_CUBE_UP[face]);
	camera->SetNear(ENV_PROBE_NEAR);
	camera->SetFar(ENV_PROBE_FAR);
	camera->SetFov(ENV_PROBE_FOV);
	camera->SetAspect(1.0f);
	camera->UpdateView(probeCenter, at);
	SetCameraPosition(probeCenter);
	SetViewMatrix(camera->GetView());
	SetProjectionMatrix(camera->GetProjection());

	Field_DrawProbeScene();
	Player_Draw();

	camera->SetUpVec(savedUp);
	camera->SetNear(savedNear);
	camera->SetFar(savedFar);
	camera->SetFov(savedFov);
	camera->SetAspect(savedAspect);
	camera->UpdateView(savedPos, savedAt);
	PlayerCamera_Draw();
	SetViewMatrix(camera->GetView());
	SetProjectionMatrix(camera->GetProjection());

	EndEnvCubeFace();

	g_NextFace = (g_NextFace + 1) % ENV_CUBE_FACE_COUNT;
	if (g_CapturedFaces < ENV_CUBE_FACE_COUNT)
	{
		g_CapturedFaces += 1;
	}
	if (g_CapturedFaces >= ENV_CUBE_FACE_COUNT)
	{
		g_Ready = true;
		if (g_NextFace == 0)
		{
			GenerateEnvCubeMips();
		}
	}
}
