#include "field.h"
#include "define.h"
#include "main.h"
#include "sprite3d.h"
#include "glb_model.h"
#include "collision.h"
#include "renderer.h"
#include "camera.h"
#include "playercamera.h"
#include "input_manager.h"
#include "fade.h"
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <utility>
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "Psapi.lib")

using namespace DirectX;

static const int EXPO_TILE_MAX = 8;
static const char* EXPO_TILE_SET_LOD2_PATH = "asset\\expomodel\\expo_tiles_lod2.txt";
static const char* EXPO_FIELD_PATH = "asset\\expomodel\\expo_field.txt";
static const char* EXPO_MODEL_LOD2_PATH = "asset\\expomodel\\expo_tile_lod2.glb";
static const char* EXPO_MODEL_LOD1_PATH = "asset\\expomodel\\expo_tile.glb";
static const float EXPO_MODEL_SCALE = 0.002f;
static const float EXPO_SHADOW_CELL_WORLD = 8.0f;
static const float EXPO_GLB_GLOBAL_SCALE = 100.0f;
static const float EXPO_FLOOR_SINK = 0.08f;
static const float EXPO_BUILDING_Y_OFFSET = -9.010f;
static const float EXPO_RING_Y_OFFSET = -1.550f;
static const char* EXPO_SKYBOX_PATH = "asset\\model\\basic_skybox_3d.fbx";
static const float EXPO_SKYBOX_SCALE = 1000.0f;
static const float EXPO_SKYBOX_PITCH = 0.0f;
static const float EXPO_PAVILION_LOAD_RADIUS = 24.0f;
static const float EXPO_PAVILION_UNLOAD_RADIUS = 36.0f;
static const float EXPO_PAVILION_PREFETCH_DISTANCE = 24.0f;
static const double EXPO_LOAD_BUDGET_MILLISECONDS = 6.0;
static const double EXPO_GRS80_A = 6378137.0;
static const double EXPO_GRS80_F = 1.0 / 298.257222101;
static const double EXPO_GRS80_E2 =
	EXPO_GRS80_F * (2.0 - EXPO_GRS80_F);
static const char* EXPO_PLACEHOLDER_MODEL_PATH = "asset\\model\\cube.fbx";

struct ExpoTileDesc
{
	char path[MAX_PATH];
	double rtcX;
	double rtcY;
	double rtcZ;
	bool hasRegion;
	double region[6];
	bool hasWorldBounds;
	XMFLOAT3 worldBoundsCenter;
	float worldBoundsRadius;
};

struct ExpoFarBatchMap
{
	char pavilionPath[MAX_PATH];
	char farPath[MAX_PATH];
	int batchId;
};

struct ExpoTileSet
{
	double refRtcX;
	double refRtcY;
	double refRtcZ;
	float modelScale;
	float glbGlobalScale;
	bool lhsFlipZ;
	int count;
	ExpoTileDesc tiles[EXPO_TILE_MAX];
	bool hasFloor;
	ExpoTileDesc floor;
	bool hasRing;
	ExpoTileDesc ring;
	int farCount;
	ExpoTileDesc farTiles[EXPO_TILE_MAX];
	std::vector<ExpoFarBatchMap> farBatchMaps;
	std::vector<ExpoTileDesc> pavilions;
};

static Sprite3D* g_ExpoFloor = nullptr;
static std::vector<Sprite3D*> g_ExpoTiles;
static std::vector<Sprite3D*> g_ExpoFarTiles;
static std::vector<Sprite3D*> g_ExpoPavilions;
static Sprite3D* g_ExpoRing = nullptr;
static Sprite3D* g_ExpoSkybox = nullptr;
static float g_ExpoSkyboxYaw = 0.0f;
static std::vector<float> g_TileBaseY;
static std::vector<float> g_FarTileBaseY;
static std::vector<float> g_PavilionBaseY;
static float g_RingBaseY = 0.0f;
static XMMATRIX g_MeshRotation = XMMatrixIdentity();
static bool g_HasMeshRotation = false;
static int g_FloorCollisionId = -1;
static int g_RingCollisionId = -1;
static ExpoTileSet g_TileSet = {};
static bool g_HasLod2 = false;
static XMMATRIX g_EcefToEnu = XMMatrixIdentity();
static bool g_HasEcefToEnu = false;
static bool g_LoadComplete = false;
static bool g_PumpedThisFrame = false;
static int g_LoadedTiles = 0;
static int g_LoadedFarTiles = 0;
static int g_LoadedPavilions = 0;
static int g_ExpectedTiles = 0;
static int g_ExpectedFarTiles = 0;
static int g_ExpectedPavilions = 0;
static int g_FallbackLod = 0;

enum class ExpoDrawKind
{
	Floor,
	Lod2,
	Lod2Far,
	Pavilion,
	Ring,
	FallbackLod2,
	FallbackLod1
};

struct ExpoDrawJob
{
	ExpoDrawKind kind = ExpoDrawKind::Floor;
	int index = 0;
	const char* label = "";
	std::string path;
	XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
	XMMATRIX rotation = {};
	bool hasRotation = false;
	bool started = false;
	bool joined = false;
	bool finished = false;
	bool failed = false;
	std::thread worker;
	std::atomic<bool> workerDone{ false };
	std::atomic<bool> workerFailed{ false };
	std::atomic<bool> workerCancelled{ false };
	std::atomic<unsigned int> generation{ 0 };
	std::string failureReason;
	GlbModel* gpuModel = nullptr;
	Sprite3D* placeholder = nullptr;
	Sprite3D* result = nullptr;
	float streamPadding = 0.0f;
	std::vector<std::pair<int, int>> farBatchRefs;
};

static std::vector<std::unique_ptr<ExpoDrawJob>> g_DrawJobs;
static bool g_LoadStarted = false;
static bool g_TriedFallbackLod1 = false;
static bool g_CollisionFinished = false;
static int g_CollisionSlot = 0;
static int g_CollisionTargetCount = 0;
static int* g_CollisionTargets[2] = {};
static XMFLOAT3 g_PreviousCameraPos = { 0.0f, 0.0f, 0.0f };
static bool g_HasPreviousCameraPos = false;
static XMFLOAT3 g_PrefetchPosition = { 0.0f, 0.0f, 0.0f };
static bool g_HasPrefetchPosition = false;

static void ConfigureExpoShadowModel(Sprite3D* model, bool castShadow)
{
	if (!model)
	{
		return;
	}
	model->SetCastShadow(castShadow);
	model->SetReceiveShadow(true);
}

static bool FileExists(const char* path)
{
	if (!path) return false;
	const DWORD attrib = GetFileAttributesA(path);
	return attrib != INVALID_FILE_ATTRIBUTES && (attrib & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static LONGLONG GetPerformanceCounter(void)
{
	LARGE_INTEGER counter = {};
	QueryPerformanceCounter(&counter);
	return counter.QuadPart;
}

static LONGLONG GetLoadDeadline(void)
{
	static LONGLONG frequency = 0;
	if (frequency == 0)
	{
		LARGE_INTEGER value = {};
		QueryPerformanceFrequency(&value);
		frequency = value.QuadPart;
	}
	if (frequency <= 0)
	{
		return GetPerformanceCounter() + 1;
	}

	const double budgetTicks =
		static_cast<double>(frequency) * EXPO_LOAD_BUDGET_MILLISECONDS / 1000.0;
	return GetPerformanceCounter() + static_cast<LONGLONG>((std::max)(1.0, budgetTicks));
}

static bool HasLoadTime(const LONGLONG deadline)
{
	return GetPerformanceCounter() < deadline;
}

static void UpdatePrefetchPosition(void)
{
	Camera* camera = GetCamera();
	if (!camera)
	{
		g_HasPrefetchPosition = false;
		return;
	}

	const XMFLOAT3 cameraPos = camera->GetPos();
	Input_Vector2 move = Input_GetMoveVector();
	const float yawRad = XMConvertToRadians(PlayerCamera_GetYaw());
	const float forwardX = sinf(yawRad);
	const float forwardZ = cosf(yawRad);
	const float forwardInput = fmaxf(move.y, 0.0f);
	float directionX = forwardX * forwardInput;
	float directionZ = forwardZ * forwardInput;
	float directionLength = sqrtf(directionX * directionX + directionZ * directionZ);

	if (directionLength <= 0.001f && g_HasPreviousCameraPos)
	{
		directionX = cameraPos.x - g_PreviousCameraPos.x;
		directionZ = cameraPos.z - g_PreviousCameraPos.z;
		directionLength = sqrtf(directionX * directionX + directionZ * directionZ);
	}

	if (directionLength > 0.001f)
	{
		directionX /= directionLength;
		directionZ /= directionLength;
		g_PrefetchPosition = {
			cameraPos.x + directionX * EXPO_PAVILION_PREFETCH_DISTANCE,
			cameraPos.y,
			cameraPos.z + directionZ * EXPO_PAVILION_PREFETCH_DISTANCE
		};
		g_HasPrefetchPosition = true;
	}
	else
	{
		g_PrefetchPosition = cameraPos;
		g_HasPrefetchPosition = true;
	}

	g_PreviousCameraPos = cameraPos;
	g_HasPreviousCameraPos = true;
}

static void ResetDrawJobs(void)
{
	for (std::unique_ptr<ExpoDrawJob>& job : g_DrawJobs)
	{
		if (!job)
		{
			continue;
		}
		if (job->worker.joinable())
		{
			job->worker.join();
		}
		if (job->gpuModel)
		{
			delete job->gpuModel;
			job->gpuModel = nullptr;
		}
		SAFE_DELETE(job->placeholder);
		if (job->result && job->result != g_ExpoFloor && job->result != g_ExpoRing)
		{
			bool owned = false;
			for (Sprite3D* model : g_ExpoTiles)
			{
				if (model == job->result) owned = true;
			}
			for (Sprite3D* model : g_ExpoFarTiles)
			{
				if (model == job->result) owned = true;
			}
			for (Sprite3D* model : g_ExpoPavilions)
			{
				if (model == job->result) owned = true;
			}
			if (!owned)
			{
				SAFE_DELETE(job->result);
			}
		}
	}
	g_DrawJobs.clear();
}

static bool IsModelLoaded(Sprite3D* model)
{
	if (!model) return false;
	const XMFLOAT3 modelSize = model->GetModelSize();
	return modelSize.x > 0.0f && modelSize.y > 0.0f && modelSize.z > 0.0f;
}

static void ClearExpoTiles(void)
{
	ResetDrawJobs();
	SAFE_DELETE(g_ExpoFloor);
	for (Sprite3D* model : g_ExpoTiles)
	{
		SAFE_DELETE(model);
	}
	g_ExpoTiles.clear();
	g_TileBaseY.clear();
	for (Sprite3D* model : g_ExpoFarTiles)
	{
		SAFE_DELETE(model);
	}
	g_ExpoFarTiles.clear();
	g_FarTileBaseY.clear();
	for (Sprite3D* model : g_ExpoPavilions)
	{
		SAFE_DELETE(model);
	}
	g_ExpoPavilions.clear();
	g_PavilionBaseY.clear();
	SAFE_DELETE(g_ExpoRing);
	SAFE_DELETE(g_ExpoSkybox);
	g_ExpoSkyboxYaw = 0.0f;
	g_RingBaseY = 0.0f;
	Collision_Clear();
	g_FloorCollisionId = -1;
	g_RingCollisionId = -1;
	g_LoadStarted = false;
	g_TriedFallbackLod1 = false;
	g_CollisionFinished = false;
	g_CollisionSlot = 0;
	g_CollisionTargetCount = 0;
	g_LoadedFarTiles = 0;
}

static bool ParseRtcLine(const char* line, ExpoTileDesc* outDesc)
{
	if (!line || !outDesc)
	{
		return false;
	}
	const char* values = strchr(line, ' ');
	if (!values)
	{
		return false;
	}
	outDesc->hasRegion = false;
	outDesc->hasWorldBounds = false;
	const int parsed = sscanf_s(
		values + 1,
		"%259s %lf %lf %lf %lf %lf %lf %lf %lf %lf",
		outDesc->path,
		(unsigned)_countof(outDesc->path),
		&outDesc->rtcX,
		&outDesc->rtcY,
		&outDesc->rtcZ,
		&outDesc->region[0],
		&outDesc->region[1],
		&outDesc->region[2],
		&outDesc->region[3],
		&outDesc->region[4],
		&outDesc->region[5]);
	if (parsed != 4 && parsed != 10)
	{
		return false;
	}
	if (parsed == 10 &&
		std::isfinite(outDesc->region[0]) &&
		std::isfinite(outDesc->region[1]) &&
		std::isfinite(outDesc->region[2]) &&
		std::isfinite(outDesc->region[3]) &&
		std::isfinite(outDesc->region[4]) &&
		std::isfinite(outDesc->region[5]) &&
		outDesc->region[0] <= outDesc->region[2] &&
		outDesc->region[1] <= outDesc->region[3] &&
		outDesc->region[4] <= outDesc->region[5])
	{
		outDesc->hasRegion = true;
	}
	return true;
}

static void InitTileSetDefaults(ExpoTileSet* outSet)
{
	if (!outSet)
	{
		return;
	}
	outSet->refRtcX = 0.0;
	outSet->refRtcY = 0.0;
	outSet->refRtcZ = 0.0;
	outSet->modelScale = EXPO_MODEL_SCALE;
	outSet->glbGlobalScale = EXPO_GLB_GLOBAL_SCALE;
	outSet->lhsFlipZ = true;
	outSet->count = 0;
	memset(outSet->tiles, 0, sizeof(outSet->tiles));
	outSet->hasFloor = false;
	memset(&outSet->floor, 0, sizeof(outSet->floor));
	outSet->hasRing = false;
	memset(&outSet->ring, 0, sizeof(outSet->ring));
	outSet->farCount = 0;
	memset(outSet->farTiles, 0, sizeof(outSet->farTiles));
	outSet->farBatchMaps.clear();
	outSet->pavilions.clear();
}

static bool ParseExpoTileSet(const char* path, ExpoTileSet* outSet)
{
	if (!outSet || !FileExists(path))
	{
		return false;
	}

	FILE* file = nullptr;
	if (fopen_s(&file, path, "r") != 0 || !file)
	{
		return false;
	}

	InitTileSetDefaults(outSet);

	char line[1024];
	bool hasHeader = false;
	while (fgets(line, sizeof(line), file))
	{
		if (line[0] == '\0' || line[0] == '#' || line[0] == '\n' || line[0] == '\r')
		{
			continue;
		}

		if (!hasHeader)
		{
			int version = 0;
			if (sscanf_s(line, "EXPO_TILE_SET %d", &version) != 1 ||
				(version != 1 && version != 2))
			{
				fclose(file);
				return false;
			}
			hasHeader = true;
			continue;
		}

		if (strncmp(line, "ref_rtc ", 8) == 0)
		{
			if (sscanf_s(line, "ref_rtc %lf %lf %lf", &outSet->refRtcX, &outSet->refRtcY, &outSet->refRtcZ) != 3)
			{
				fclose(file);
				return false;
			}
			continue;
		}
		if (strncmp(line, "model_scale ", 12) == 0)
		{
			sscanf_s(line, "model_scale %f", &outSet->modelScale);
			continue;
		}
		if (strncmp(line, "glb_global_scale ", 17) == 0)
		{
			sscanf_s(line, "glb_global_scale %f", &outSet->glbGlobalScale);
			continue;
		}
		if (strncmp(line, "lhs_flip_z ", 11) == 0)
		{
			int flag = 1;
			sscanf_s(line, "lhs_flip_z %d", &flag);
			outSet->lhsFlipZ = (flag != 0);
			continue;
		}
		if (strncmp(line, "count ", 6) == 0)
		{
			continue;
		}
		if (strncmp(line, "tile ", 5) == 0)
		{
			if (outSet->count >= EXPO_TILE_MAX)
			{
				continue;
			}
			if (!ParseRtcLine(line, &outSet->tiles[outSet->count]))
			{
				fclose(file);
				return false;
			}
			outSet->count += 1;
		}
	}

	fclose(file);
	return hasHeader && outSet->count > 0;
}

static bool ParseExpoField(const char* path, ExpoTileSet* outSet)
{
	if (!outSet || !FileExists(path))
	{
		return false;
	}

	FILE* file = nullptr;
	if (fopen_s(&file, path, "r") != 0 || !file)
	{
		return false;
	}

	char line[1024];
	bool hasHeader = false;
	while (fgets(line, sizeof(line), file))
	{
		if (line[0] == '\0' || line[0] == '#' || line[0] == '\n' || line[0] == '\r')
		{
			continue;
		}
		if (!hasHeader)
		{
			int version = 0;
			if (sscanf_s(line, "EXPO_FIELD %d", &version) != 1 || version != 1)
			{
				fclose(file);
				return false;
			}
			hasHeader = true;
			continue;
		}
		if (strncmp(line, "ref_rtc ", 8) == 0)
		{
			if (sscanf_s(line, "ref_rtc %lf %lf %lf", &outSet->refRtcX, &outSet->refRtcY, &outSet->refRtcZ) != 3)
			{
				fclose(file);
				return false;
			}
			continue;
		}
		if (strncmp(line, "floor ", 6) == 0)
		{
			if (ParseRtcLine(line, &outSet->floor))
			{
				outSet->hasFloor = true;
			}
			continue;
		}
		if (strncmp(line, "ring ", 5) == 0)
		{
			if (ParseRtcLine(line, &outSet->ring))
			{
				outSet->hasRing = true;
			}
			continue;
		}
		if (strncmp(line, "lod2far ", 8) == 0)
		{
			if (outSet->farCount < EXPO_TILE_MAX &&
				ParseRtcLine(line, &outSet->farTiles[outSet->farCount]))
			{
				outSet->farCount += 1;
			}
			continue;
		}
		if (strncmp(line, "lod2far_batch ", 14) == 0)
		{
			ExpoFarBatchMap map = {};
			if (sscanf_s(
				line,
				"lod2far_batch %259s %259s %d",
				map.pavilionPath,
				(unsigned)_countof(map.pavilionPath),
				map.farPath,
				(unsigned)_countof(map.farPath),
				&map.batchId) == 3)
			{
				outSet->farBatchMaps.push_back(map);
			}
			continue;
		}
		if (strncmp(line, "pavilion ", 9) == 0)
		{
			ExpoTileDesc desc = {};
			if (ParseRtcLine(line, &desc))
			{
				outSet->pavilions.push_back(desc);
			}
		}
	}

	fclose(file);
	return hasHeader && (outSet->hasFloor || outSet->hasRing || !outSet->pavilions.empty());
}

static XMMATRIX BuildEcefToEnuMatrix(double x, double y, double z)
{
	const double lon = atan2(y, x);
	const double lat = atan2(z, sqrt(x * x + y * y));
	const double sinLon = sin(lon);
	const double cosLon = cos(lon);
	const double sinLat = sin(lat);
	const double cosLat = cos(lat);

	const XMFLOAT3 east(-(float)sinLon, (float)cosLon, 0.0f);
	const XMFLOAT3 north(
		(float)(-sinLat * cosLon),
		(float)(-sinLat * sinLon),
		(float)cosLat);
	const XMFLOAT3 up(
		(float)(cosLat * cosLon),
		(float)(cosLat * sinLon),
		(float)sinLat);

	return XMMatrixSet(
		east.x, up.x, north.x, 0.0f,
		east.y, up.y, north.y, 0.0f,
		east.z, up.z, north.z, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f);
}

static XMMATRIX BuildMeshRotation(const XMMATRIX& ecefToEnu, bool lhsFlipZ)
{
	const XMMATRIX yUpToZUp = XMMatrixRotationX(XMConvertToRadians(90.0f));
	XMMATRIX rotation = yUpToZUp * ecefToEnu;
	if (lhsFlipZ)
	{
		rotation = XMMatrixScaling(1.0f, 1.0f, -1.0f) * rotation;
	}
	return rotation;
}

static XMFLOAT3 RtcToWorldPosition(
	const ExpoTileSet& tileSet,
	const ExpoTileDesc& tile,
	const XMMATRIX& ecefToEnu)
{
	const double unitScale = static_cast<double>(tileSet.modelScale) * static_cast<double>(tileSet.glbGlobalScale);
	const XMVECTOR delta = XMVectorSet(
		static_cast<float>((tile.rtcX - tileSet.refRtcX) * unitScale),
		static_cast<float>((tile.rtcY - tileSet.refRtcY) * unitScale),
		static_cast<float>((tile.rtcZ - tileSet.refRtcZ) * unitScale),
		0.0f);
	const XMVECTOR world = XMVector3TransformNormal(delta, ecefToEnu);
	XMFLOAT3 position;
	XMStoreFloat3(&position, world);
	return position;
}

static bool RegionPointToWorld(
	const ExpoTileSet& tileSet,
	double longitude,
	double latitude,
	double height,
	XMFLOAT3* outPoint)
{
	if (!outPoint ||
		!std::isfinite(longitude) ||
		!std::isfinite(latitude) ||
		!std::isfinite(height))
	{
		return false;
	}

	const double sinLatitude = sin(latitude);
	const double cosLatitude = cos(latitude);
	const double sinLongitude = sin(longitude);
	const double cosLongitude = cos(longitude);
	const double primeVerticalRadius =
		EXPO_GRS80_A / sqrt(1.0 - EXPO_GRS80_E2 * sinLatitude * sinLatitude);
	const double ecefX =
		(primeVerticalRadius + height) * cosLatitude * cosLongitude;
	const double ecefY =
		(primeVerticalRadius + height) * cosLatitude * sinLongitude;
	const double ecefZ =
		(primeVerticalRadius * (1.0 - EXPO_GRS80_E2) + height) * sinLatitude;

	const double referenceLongitude = atan2(
		tileSet.refRtcY,
		tileSet.refRtcX);
	const double referenceLatitude = atan2(
		tileSet.refRtcZ,
		sqrt(
			tileSet.refRtcX * tileSet.refRtcX +
			tileSet.refRtcY * tileSet.refRtcY));
	const double eastX = -sin(referenceLongitude);
	const double eastY = cos(referenceLongitude);
	const double northX = -sin(referenceLatitude) * cos(referenceLongitude);
	const double northY = -sin(referenceLatitude) * sin(referenceLongitude);
	const double northZ = cos(referenceLatitude);
	const double upX = cos(referenceLatitude) * cos(referenceLongitude);
	const double upY = cos(referenceLatitude) * sin(referenceLongitude);
	const double upZ = sin(referenceLatitude);
	const double deltaX = ecefX - tileSet.refRtcX;
	const double deltaY = ecefY - tileSet.refRtcY;
	const double deltaZ = ecefZ - tileSet.refRtcZ;
	const double east = deltaX * eastX + deltaY * eastY;
	const double north = deltaX * northX + deltaY * northY + deltaZ * northZ;
	const double up = deltaX * upX + deltaY * upY + deltaZ * upZ;
	const double unitScale =
		static_cast<double>(tileSet.modelScale) *
		static_cast<double>(tileSet.glbGlobalScale);

	const double worldX = east * unitScale;
	const double worldY = up * unitScale + EXPO_BUILDING_Y_OFFSET;
	const double worldZ = north * unitScale;
	if (!std::isfinite(worldX) ||
		!std::isfinite(worldY) ||
		!std::isfinite(worldZ))
	{
		return false;
	}
	*outPoint = XMFLOAT3(
		static_cast<float>(worldX),
		static_cast<float>(worldY),
		static_cast<float>(worldZ));
	return true;
}

static bool BuildWorldBoundsFromRegion(
	const ExpoTileSet& tileSet,
	ExpoTileDesc* tile)
{
	if (!tile || !tile->hasRegion)
	{
		return false;
	}

	XMFLOAT3 boundsMin(
		FLT_MAX,
		FLT_MAX,
		FLT_MAX);
	XMFLOAT3 boundsMax(
		-FLT_MAX,
		-FLT_MAX,
		-FLT_MAX);
	for (int longitudeIndex = 0; longitudeIndex < 2; ++longitudeIndex)
	{
		for (int latitudeIndex = 0; latitudeIndex < 2; ++latitudeIndex)
		{
			for (int heightIndex = 0; heightIndex < 2; ++heightIndex)
			{
				XMFLOAT3 point;
				if (!RegionPointToWorld(
					tileSet,
					tile->region[longitudeIndex == 0 ? 0 : 2],
					tile->region[latitudeIndex == 0 ? 1 : 3],
					tile->region[heightIndex == 0 ? 4 : 5],
					&point))
				{
					tile->hasWorldBounds = false;
					return false;
				}
				boundsMin.x = (std::min)(boundsMin.x, point.x);
				boundsMin.y = (std::min)(boundsMin.y, point.y);
				boundsMin.z = (std::min)(boundsMin.z, point.z);
				boundsMax.x = (std::max)(boundsMax.x, point.x);
				boundsMax.y = (std::max)(boundsMax.y, point.y);
				boundsMax.z = (std::max)(boundsMax.z, point.z);
			}
		}
	}

	tile->worldBoundsCenter = XMFLOAT3(
		(boundsMin.x + boundsMax.x) * 0.5f,
		(boundsMin.y + boundsMax.y) * 0.5f,
		(boundsMin.z + boundsMax.z) * 0.5f);
	const float halfX = (boundsMax.x - boundsMin.x) * 0.5f;
	const float halfY = (boundsMax.y - boundsMin.y) * 0.5f;
	const float halfZ = (boundsMax.z - boundsMin.z) * 0.5f;
	tile->worldBoundsRadius = sqrtf(
		halfX * halfX +
		halfY * halfY +
		halfZ * halfZ);
	if (!std::isfinite(tile->worldBoundsRadius) ||
		tile->worldBoundsRadius <= 0.0f)
	{
		tile->hasWorldBounds = false;
		return false;
	}
	tile->hasWorldBounds = true;
	return true;
}

static ExpoTileDesc* FindLod2TileByPath(const char* path)
{
	if (!path)
	{
		return nullptr;
	}
	for (int i = 0; i < g_TileSet.count; ++i)
	{
		if (strcmp(g_TileSet.tiles[i].path, path) == 0)
		{
			return &g_TileSet.tiles[i];
		}
	}
	return nullptr;
}

static void BuildTileWorldBounds(void)
{
	for (int i = 0; i < g_TileSet.count; ++i)
	{
		BuildWorldBoundsFromRegion(g_TileSet, &g_TileSet.tiles[i]);
	}
	for (int i = 0; i < g_TileSet.farCount; ++i)
	{
		ExpoTileDesc* farTile = &g_TileSet.farTiles[i];
		ExpoTileDesc* lod2Tile = FindLod2TileByPath(farTile->path);
		if (lod2Tile && lod2Tile->hasRegion)
		{
			farTile->hasRegion = true;
			memcpy(farTile->region, lod2Tile->region, sizeof(farTile->region));
		}
		BuildWorldBoundsFromRegion(g_TileSet, farTile);
	}
}

static bool IsTileVisibleFromCamera(const ExpoTileDesc& tile)
{
	if (!tile.hasWorldBounds)
	{
		return true;
	}
	Camera* camera = GetCamera();
	if (!camera)
	{
		return true;
	}

	const XMVECTOR viewPosition = XMVector3TransformCoord(
		XMLoadFloat3(&tile.worldBoundsCenter),
		camera->GetView());
	const float viewX = XMVectorGetX(viewPosition);
	const float viewY = XMVectorGetY(viewPosition);
	const float viewZ = XMVectorGetZ(viewPosition);
	const XMMATRIX projection = camera->GetProjection();
	const float xScale = XMVectorGetX(projection.r[0]);
	const float yScale = XMVectorGetY(projection.r[1]);
	if (xScale <= 0.0f || yScale <= 0.0f)
	{
		return true;
	}

	const float nearPlane = (std::max)(0.0f, camera->GetNear());
	const float farPlane = (std::max)(nearPlane, camera->GetFar());
	const float radius = tile.worldBoundsRadius;
	if (viewZ + radius < nearPlane || viewZ - radius > farPlane)
	{
		return false;
	}

	const float sideDepth = (std::max)(viewZ + radius, nearPlane);
	if (fabsf(viewX) - radius > sideDepth / xScale)
	{
		return false;
	}
	if (fabsf(viewY) - radius > sideDepth / yScale)
	{
		return false;
	}
	return true;
}

static XMMATRIX MakeModelWorld(Sprite3D* model, const XMMATRIX& rotation)
{
	if (!model)
	{
		return XMMatrixIdentity();
	}
	const XMFLOAT3 pos = model->GetPos();
	const XMFLOAT3 scale = model->GetScale();
	return XMMatrixScaling(scale.x, scale.y, scale.z) * rotation * XMMatrixTranslation(pos.x, pos.y, pos.z);
}

static XMMATRIX MakeWorldAt(const XMFLOAT3& position, const XMMATRIX& rotation)
{
	return XMMatrixScaling(EXPO_MODEL_SCALE, EXPO_MODEL_SCALE, EXPO_MODEL_SCALE)
		* rotation
		* XMMatrixTranslation(position.x, position.y, position.z);
}

static void ApplyFixedYOffsets(void)
{
	const size_t count = (g_ExpoTiles.size() < g_TileBaseY.size()) ? g_ExpoTiles.size() : g_TileBaseY.size();
	for (size_t i = 0; i < count; ++i)
	{
		if (!g_ExpoTiles[i])
		{
			continue;
		}
		g_ExpoTiles[i]->SetPosY(g_TileBaseY[i] + EXPO_BUILDING_Y_OFFSET);
	}
	const size_t farCount = (g_ExpoFarTiles.size() < g_FarTileBaseY.size())
		? g_ExpoFarTiles.size()
		: g_FarTileBaseY.size();
	for (size_t i = 0; i < farCount; ++i)
	{
		if (!g_ExpoFarTiles[i])
		{
			continue;
		}
		g_ExpoFarTiles[i]->SetPosY(g_FarTileBaseY[i] + EXPO_BUILDING_Y_OFFSET);
	}
	const size_t pavilionCount = (g_ExpoPavilions.size() < g_PavilionBaseY.size()) ? g_ExpoPavilions.size() : g_PavilionBaseY.size();
	for (size_t i = 0; i < pavilionCount; ++i)
	{
		if (!g_ExpoPavilions[i])
		{
			continue;
		}
		g_ExpoPavilions[i]->SetPosY(g_PavilionBaseY[i] + EXPO_BUILDING_Y_OFFSET);
	}
	if (g_ExpoRing)
	{
		g_ExpoRing->SetPosY(g_RingBaseY + EXPO_RING_Y_OFFSET);
		if (g_RingCollisionId >= 0 && g_HasMeshRotation)
		{
			Collision_SetWorld(g_RingCollisionId, MakeModelWorld(g_ExpoRing, g_MeshRotation));
		}
	}
}

static ExpoDrawJob* AddDrawJob(
	ExpoDrawKind kind,
	int index,
	const char* label,
	const char* path,
	const XMFLOAT3& position,
	const XMMATRIX* rotation)
{
	std::unique_ptr<ExpoDrawJob> job(new ExpoDrawJob());
	job->kind = kind;
	job->index = index;
	job->label = label ? label : "";
	job->path = path ? path : "";
	job->position = position;
	job->hasRotation = (rotation != nullptr);
	if (rotation)
	{
		job->rotation = *rotation;
	}
	ExpoDrawJob* raw = job.get();
	g_DrawJobs.push_back(std::move(job));
	return raw;
}

static void AssignFarBatchRefs(ExpoDrawJob* job)
{
	if (!job || job->kind != ExpoDrawKind::Pavilion)
	{
		return;
	}
	for (const ExpoFarBatchMap& map : g_TileSet.farBatchMaps)
	{
		if (job->path != map.pavilionPath)
		{
			continue;
		}
		for (int i = 0; i < g_TileSet.farCount; ++i)
		{
			if (strcmp(g_TileSet.farTiles[i].path, map.farPath) == 0)
			{
				job->farBatchRefs.emplace_back(i, map.batchId);
				break;
			}
		}
	}
}

static void CommitDrawJob(ExpoDrawJob* job)
{
	if (!job || !job->result)
	{
		return;
	}
	Sprite3D* model = job->result;
	SAFE_DELETE(job->placeholder);
	ConfigureExpoShadowModel(
		model,
		job->kind != ExpoDrawKind::Pavilion);
	switch (job->kind)
	{
	case ExpoDrawKind::Floor:
		g_ExpoFloor = model;
		break;
	case ExpoDrawKind::Lod2:
	case ExpoDrawKind::FallbackLod2:
	case ExpoDrawKind::FallbackLod1:
		if (g_ExpoTiles.size() <= static_cast<size_t>(job->index))
		{
			g_ExpoTiles.resize(static_cast<size_t>(job->index) + 1, nullptr);
			g_TileBaseY.resize(static_cast<size_t>(job->index) + 1, 0.0f);
		}
		g_ExpoTiles[static_cast<size_t>(job->index)] = model;
		g_TileBaseY[static_cast<size_t>(job->index)] = model->GetPos().y;
		g_LoadedTiles += 1;
		if (job->kind == ExpoDrawKind::FallbackLod2)
		{
			g_FallbackLod = 2;
		}
		if (job->kind == ExpoDrawKind::FallbackLod1)
		{
			g_FallbackLod = 1;
		}
		break;
	case ExpoDrawKind::Lod2Far:
		if (g_ExpoFarTiles.size() <= static_cast<size_t>(job->index))
		{
			g_ExpoFarTiles.resize(static_cast<size_t>(job->index) + 1, nullptr);
			g_FarTileBaseY.resize(static_cast<size_t>(job->index) + 1, 0.0f);
		}
		g_ExpoFarTiles[static_cast<size_t>(job->index)] = model;
		g_FarTileBaseY[static_cast<size_t>(job->index)] = model->GetPos().y;
		model->SetPosY(g_FarTileBaseY[static_cast<size_t>(job->index)] + EXPO_BUILDING_Y_OFFSET);
		g_LoadedFarTiles += 1;
		break;
	case ExpoDrawKind::Pavilion:
		if (g_ExpoPavilions.size() <= static_cast<size_t>(job->index))
		{
			g_ExpoPavilions.resize(static_cast<size_t>(job->index) + 1, nullptr);
			g_PavilionBaseY.resize(static_cast<size_t>(job->index) + 1, 0.0f);
		}
		g_ExpoPavilions[static_cast<size_t>(job->index)] = model;
		g_PavilionBaseY[static_cast<size_t>(job->index)] = model->GetPos().y;
		model->SetPosY(g_PavilionBaseY[static_cast<size_t>(job->index)] + EXPO_BUILDING_Y_OFFSET);
		g_LoadedPavilions += 1;
		break;
	case ExpoDrawKind::Ring:
		model->SetMainPassCellCulling(true);
		g_ExpoRing = model;
		g_RingBaseY = model->GetPos().y;
		break;
	}
}

static int MaxImportWorkers(void)
{
	// UMA環境ではインポート中のCPUシーンと画像デコードが共有メモリを
	// 圧迫するため、同時実行数を固定してピーク使用量を抑える。
	int n = 2;
	const int jobCount = static_cast<int>(g_DrawJobs.size());
	if (n > jobCount)
	{
		n = jobCount;
	}
	if (n < 1)
	{
		n = 1;
	}
	return n;
}

static bool IsCoreDrawJob(const ExpoDrawJob* job)
{
	return job && job->kind != ExpoDrawKind::Pavilion;
}

static float GetDistanceSquaredXZ(const XMFLOAT3& left, const XMFLOAT3& right)
{
	const float dx = left.x - right.x;
	const float dz = left.z - right.z;
	return dx * dx + dz * dz;
}

static float GetPavilionLoadScore(const ExpoDrawJob* job)
{
	if (!job || job->kind != ExpoDrawKind::Pavilion)
	{
		return 0.0f;
	}
	Camera* camera = GetCamera();
	if (!camera)
	{
		return FLT_MAX;
	}
	const XMFLOAT3 cameraPos = camera->GetPos();
	float score = GetDistanceSquaredXZ(job->position, cameraPos);
	if (g_HasPrefetchPosition)
	{
		score = (std::min)(score, GetDistanceSquaredXZ(job->position, g_PrefetchPosition));
	}
	return score;
}

static void RememberPavilionStreamPadding(ExpoDrawJob* job, const XMFLOAT3& modelSize)
{
	if (!job || job->kind != ExpoDrawKind::Pavilion)
	{
		return;
	}
	const float worldX = fabsf(modelSize.x) * EXPO_MODEL_SCALE;
	const float worldZ = fabsf(modelSize.z) * EXPO_MODEL_SCALE;
	const float padding = 0.5f * (std::max)(worldX, worldZ);
	if (padding > job->streamPadding)
	{
		job->streamPadding = padding;
	}
}

static bool IsPavilionInLoadRange(const ExpoDrawJob* job, float radius)
{
	if (!job || job->kind != ExpoDrawKind::Pavilion)
	{
		return true;
	}
	Camera* camera = GetCamera();
	if (!camera)
	{
		return false;
	}
	const float paddedRadius = radius + job->streamPadding;
	const float radiusSquared = paddedRadius * paddedRadius;
	if (GetDistanceSquaredXZ(job->position, camera->GetPos()) <= radiusSquared)
	{
		return true;
	}
	return g_HasPrefetchPosition &&
		GetDistanceSquaredXZ(job->position, g_PrefetchPosition) <= radiusSquared;
}

static void StartDrawWorker(ExpoDrawJob* job)
{
	if (!job || job->started)
	{
		return;
	}
	if (job->path.empty() || !FileExists(job->path.c_str()))
	{
		job->started = true;
		job->failed = true;
		job->finished = true;
		job->workerDone = true;
		job->workerFailed = true;
		job->failureReason = "ファイルなし";
		return;
	}
	job->started = true;
	job->joined = false;
	job->workerDone = false;
	job->workerFailed = false;
	job->workerCancelled = false;
	job->failureReason.clear();
	ExpoDrawJob* raw = job;
	const unsigned int generation = job->generation.load();
	job->worker = std::thread([raw, generation]()
	{
		GlbPreparedData* prepared = GlbModel::ImportPreparedFile(raw->path.c_str());
		if (!prepared)
		{
			raw->failureReason = "GLB直接解析失敗";
			raw->workerFailed = true;
			raw->workerDone = true;
			return;
		}
		GlbModel* model = new GlbModel();
		if (!model->AttachPreparedData(prepared))
		{
			raw->failureReason = "CPUメッシュ接続失敗";
			delete model;
			raw->workerFailed = true;
			raw->workerDone = true;
			return;
		}
		if (raw->kind != ExpoDrawKind::Pavilion)
		{
			model->PrepareShadowCells(EXPO_SHADOW_CELL_WORLD / EXPO_MODEL_SCALE);
		}
		if (!model->DecodeEmbeddedTextures(raw->kind == ExpoDrawKind::Floor))
		{
			raw->failureReason = "テクスチャCPUデコード失敗";
			delete model;
			raw->workerFailed = true;
			raw->workerDone = true;
			return;
		}
		if (raw->generation.load() != generation)
		{
			delete model;
			raw->failureReason = "範囲外キャンセル";
			raw->workerCancelled = true;
			raw->workerDone = true;
			return;
		}
		raw->gpuModel = model;
		raw->failureReason.clear();
		raw->workerDone = true;
	});
}

static bool AllDrawJobsSettled(void);

static void StartPendingImports(void)
{
	int inFlight = 0;
	int ready = 0;
	for (std::unique_ptr<ExpoDrawJob>& job : g_DrawJobs)
	{
		if (job && job->started && !job->finished && !job->workerDone)
		{
			inFlight += 1;
		}
		if (job && job->workerDone && job->gpuModel && !job->finished &&
			IsPavilionInLoadRange(job.get(), EXPO_PAVILION_UNLOAD_RADIUS))
		{
			ready += 1;
		}
	}
	const int maxWorkers = MaxImportWorkers();
	const int maxReadyModels = maxWorkers;

	// CPUインポート中のワーカー数と、GPU待ちの完成データ数を分ける。
	// CPU準備済みモデルが増えすぎる場合だけ新しいインポートを抑える。
	// まず床・LOD2・リングをすべて開始し、コアのGPU化完了後にパビリオンへ進む。
	for (std::unique_ptr<ExpoDrawJob>& job : g_DrawJobs)
	{
		if (inFlight >= maxWorkers || ready >= maxReadyModels)
		{
			break;
		}
		if (!job || job->started || job->finished)
		{
			continue;
		}
		if (!IsCoreDrawJob(job.get()))
		{
			continue;
		}
		StartDrawWorker(job.get());
		if (job->started && !job->workerDone)
		{
			inFlight += 1;
		}
		if (job->gpuModel)
		{
			ready += 1;
		}
	}
	if (!AllDrawJobsSettled())
	{
		return;
	}

	// パビリオンはカメラまたは移動先予測点に近い順で開始する。
	while (inFlight < maxWorkers && ready < maxReadyModels)
	{
		ExpoDrawJob* bestJob = nullptr;
		float bestScore = FLT_MAX;
		for (std::unique_ptr<ExpoDrawJob>& holder : g_DrawJobs)
		{
			ExpoDrawJob* job = holder.get();
			if (!job || job->kind != ExpoDrawKind::Pavilion ||
				job->started || job->finished ||
				!IsPavilionInLoadRange(job, EXPO_PAVILION_LOAD_RADIUS))
			{
				continue;
			}
			const float score = GetPavilionLoadScore(job);
			if (score < bestScore)
			{
				bestScore = score;
				bestJob = job;
			}
		}
		if (!bestJob)
		{
			break;
		}
		StartDrawWorker(bestJob);
		if (bestJob->started && !bestJob->workerDone)
		{
			inFlight += 1;
		}
		else if (bestJob->gpuModel)
		{
			ready += 1;
		}
		else if (bestJob->failed)
		{
			bestJob->finished = true;
		}
	}
}

static bool AllDrawJobsSettled(void)
{
	if (g_DrawJobs.empty())
	{
		return true;
	}
	for (std::unique_ptr<ExpoDrawJob>& job : g_DrawJobs)
	{
		if (IsCoreDrawJob(job.get()) && !job->finished)
		{
			return false;
		}
	}
	return true;
}

static void PumpPavilionStreaming(const LONGLONG deadline)
{
	Camera* camera = GetCamera();
	if (!camera)
	{
		return;
	}
	for (std::unique_ptr<ExpoDrawJob>& holder : g_DrawJobs)
	{
		ExpoDrawJob* job = holder.get();
		if (!job || job->kind != ExpoDrawKind::Pavilion)
		{
			continue;
		}
		if (IsPavilionInLoadRange(job, EXPO_PAVILION_UNLOAD_RADIUS))
		{
			if (job->workerDone && job->workerCancelled)
			{
				if (job->worker.joinable())
				{
					job->worker.join();
				}
				job->workerDone = false;
				job->workerFailed = false;
				job->workerCancelled = false;
				job->started = false;
				job->joined = false;
				job->failureReason.clear();
			}
			else if (job->failed && job->finished && !job->result &&
				job->failureReason != "ファイルなし" &&
				IsPavilionInLoadRange(job, EXPO_PAVILION_LOAD_RADIUS))
			{
				if (job->worker.joinable())
				{
					job->worker.join();
				}
				delete job->gpuModel;
				job->gpuModel = nullptr;
				job->started = false;
				job->joined = false;
				job->finished = false;
				job->failed = false;
				job->workerDone = false;
				job->workerFailed = false;
				job->workerCancelled = false;
				job->failureReason.clear();
			}
			continue;
		}

		if (job->started && !job->workerDone)
		{
			// 範囲外になったCPUインポートは結果を採用しない。
			// スレッド自体は完了まで待ち、同じジョブを二重起動しない。
			job->generation.fetch_add(1);
			continue;
		}

		if (job->workerDone && job->workerCancelled)
		{
			if (job->worker.joinable())
			{
				job->worker.join();
			}
			job->workerDone = false;
			job->workerFailed = false;
			job->workerCancelled = false;
			job->started = false;
			job->joined = false;
			continue;
		}

		// 破棄は一度に一棟だけにし、GPU解放が同じフレームに集中しないようにする。
		if (job->result)
		{
			if (!HasLoadTime(deadline))
			{
				return;
			}
			Sprite3D* old = job->result;
			if (job->index >= 0 && job->index < static_cast<int>(g_ExpoPavilions.size()) &&
				g_ExpoPavilions[job->index] == old)
			{
				g_ExpoPavilions[job->index] = nullptr;
			}
			SAFE_DELETE(old);
			job->result = nullptr;
			if (g_LoadedPavilions > 0)
			{
				--g_LoadedPavilions;
			}
			job->finished = false;
			job->started = false;
			job->joined = false;
			job->failed = false;
			job->workerCancelled = false;
			return;
		}

		if (job->placeholder)
		{
			if (!HasLoadTime(deadline))
			{
				return;
			}
			SAFE_DELETE(job->placeholder);
			if (job->workerDone && job->gpuModel)
			{
				if (job->worker.joinable())
				{
					job->worker.join();
				}
				delete job->gpuModel;
				job->gpuModel = nullptr;
			}
			job->finished = false;
			job->started = false;
			job->joined = false;
			job->failed = false;
			job->workerDone = false;
			job->workerFailed = false;
			job->workerCancelled = false;
			return;
		}

		if (job->workerDone && job->gpuModel)
		{
			if (!HasLoadTime(deadline))
			{
				return;
			}
			if (job->worker.joinable())
			{
				job->worker.join();
			}
			delete job->gpuModel;
			job->gpuModel = nullptr;
			job->finished = false;
			job->started = false;
			job->joined = false;
			job->failed = false;
			job->workerDone = false;
			job->workerFailed = false;
			job->workerCancelled = false;
			return;
		}
	}
}

static void PumpCollisionLoad(void)
{
	if (g_CollisionFinished)
	{
		return;
	}
	int meshId = -1;
	const CollisionPumpResult result = Collision_Pump(&meshId);
	if (result == COLLISION_PUMP_DONE)
	{
		if (g_CollisionSlot < g_CollisionTargetCount && g_CollisionTargets[g_CollisionSlot])
		{
			*g_CollisionTargets[g_CollisionSlot] = meshId;
		}
		g_CollisionSlot += 1;
	}
	else if (result == COLLISION_PUMP_FAILED)
	{
		g_CollisionSlot += 1;
	}
	else if (result == COLLISION_PUMP_IDLE)
	{
		g_CollisionFinished = true;
	}
}

static bool HasReadyFarFallback(const ExpoDrawJob* job)
{
	if (!job || job->kind != ExpoDrawKind::Pavilion)
	{
		return false;
	}
	for (const std::pair<int, int>& ref : job->farBatchRefs)
	{
		if (ref.first >= 0 &&
			ref.first < static_cast<int>(g_ExpoFarTiles.size()) &&
			g_ExpoFarTiles[ref.first])
		{
			return true;
		}
	}
	return false;
}

static void EnsurePavilionPlaceholder(ExpoDrawJob* job)
{
	if (!job || job->kind != ExpoDrawKind::Pavilion ||
		job->placeholder || !job->gpuModel ||
		HasReadyFarFallback(job))
	{
		return;
	}

	const XMFLOAT3 modelSize = job->gpuModel->GetSize();
	RememberPavilionStreamPadding(job, modelSize);
	Sprite3D* placeholder = new Sprite3D(
		job->position,
		{ 1.0f, 1.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f },
		EXPO_PLACEHOLDER_MODEL_PATH,
		S_PBR);
	if (!IsModelLoaded(placeholder))
	{
		SAFE_DELETE(placeholder);
		return;
	}

	const XMFLOAT3 placeholderSize = placeholder->GetModelSize();
	XMFLOAT3 placeholderScale = {
		EXPO_MODEL_SCALE,
		EXPO_MODEL_SCALE,
		EXPO_MODEL_SCALE
	};
	if (placeholderSize.x > 0.001f && placeholderSize.y > 0.001f && placeholderSize.z > 0.001f)
	{
		placeholderScale = {
			modelSize.x * EXPO_MODEL_SCALE / placeholderSize.x,
			modelSize.y * EXPO_MODEL_SCALE / placeholderSize.y,
			modelSize.z * EXPO_MODEL_SCALE / placeholderSize.z
		};
	}
	placeholder->SetSize(placeholderScale);
	placeholder->SetPosY(job->position.y + EXPO_BUILDING_Y_OFFSET);
	placeholder->SetColor(0.35f, 0.38f, 0.42f, 1.0f);
	ConfigureExpoShadowModel(placeholder, false);
	if (job->hasRotation)
	{
		placeholder->SetRotationMatrix(job->rotation);
	}
	job->placeholder = placeholder;
}

static ExpoDrawJob* FindNextGpuJob(void)
{
	ExpoDrawJob* bestJob = nullptr;
	float bestScore = FLT_MAX;
	for (std::unique_ptr<ExpoDrawJob>& holder : g_DrawJobs)
	{
		ExpoDrawJob* job = holder.get();
		if (!job || job->finished || !job->workerDone ||
			(!job->gpuModel && !job->workerFailed))
		{
			continue;
		}
		if (!g_LoadComplete && IsCoreDrawJob(job))
		{
			return job;
		}
		if (job->kind == ExpoDrawKind::Pavilion)
		{
			// 開始はロード半径、GPU化は破棄半径まで進める。
			// ヒステリシス帯でREADYが残ると、新しい近景のインポート枠を塞ぐ。
			if (!IsPavilionInLoadRange(job, EXPO_PAVILION_UNLOAD_RADIUS))
			{
				continue;
			}
			float score = GetPavilionLoadScore(job);
			if (!IsPavilionInLoadRange(job, EXPO_PAVILION_LOAD_RADIUS))
			{
				score += EXPO_PAVILION_UNLOAD_RADIUS * EXPO_PAVILION_UNLOAD_RADIUS;
			}
			if (score < bestScore)
			{
				bestScore = score;
				bestJob = job;
			}
		}
		else if (!bestJob)
		{
			bestJob = job;
		}
	}
	return bestJob;
}

static void PumpOneGpu(const LONGLONG deadline)
{
	while (HasLoadTime(deadline))
	{
		ExpoDrawJob* job = FindNextGpuJob();
		if (!job)
		{
			return;
		}
		if (!job->joined)
		{
			if (job->worker.joinable())
			{
				job->worker.join();
			}
			job->joined = true;
			if (job->workerFailed || !job->gpuModel)
			{
				delete job->gpuModel;
				job->gpuModel = nullptr;
				job->failed = true;
				job->finished = true;
				continue;
			}
		}

		EnsurePavilionPlaceholder(job);
		const int gpu = job->gpuModel->PumpGpu(GetDevice(), 1);
		if (gpu == 0)
		{
			// 1メッシュ／1転送チャンクだけ進め、残りは同じフレームの
			// 6ms予算内で次のループへ渡す。
			continue;
		}
		if (gpu < 0)
		{
			delete job->gpuModel;
			job->gpuModel = nullptr;
			job->failed = true;
			job->finished = true;
			continue;
		}

		Sprite3D* model = new Sprite3D();
		model->SetPos(job->position);
		model->SetSize({ EXPO_MODEL_SCALE, EXPO_MODEL_SCALE, EXPO_MODEL_SCALE });
		model->SetShaderType(S_PBR);
		model->AdoptGlbModel(job->gpuModel);
		job->gpuModel = nullptr;
		if (job->hasRotation)
		{
			model->SetRotationMatrix(job->rotation);
		}
		if (!IsModelLoaded(model))
		{
			SAFE_DELETE(model);
			job->failed = true;
			job->finished = true;
			continue;
		}
		job->result = model;
		RememberPavilionStreamPadding(job, model->GetModelSize());
		CommitDrawJob(job);
		job->finished = true;
	}
}

static bool HasAnyModel(void)
{
	return g_ExpoFloor || g_LoadedTiles > 0 || g_LoadedFarTiles > 0 ||
		g_LoadedPavilions > 0 || g_ExpoRing;
}

static void FinishLoad(void)
{
	if (g_LoadComplete)
	{
		return;
	}
	g_LoadComplete = true;
}

static void KickoffLoad(void)
{
	g_CollisionSlot = 0;
	g_CollisionTargetCount = 0;
	g_CollisionFinished = false;
	if (g_HasEcefToEnu && g_HasMeshRotation)
	{
		if (g_TileSet.hasFloor && FileExists(g_TileSet.floor.path))
		{
			XMFLOAT3 position = RtcToWorldPosition(g_TileSet, g_TileSet.floor, g_EcefToEnu);
			position.y -= EXPO_FLOOR_SINK;
			if (Collision_StartAdd(g_TileSet.floor.path, MakeWorldAt(position, g_MeshRotation)))
			{
				g_CollisionTargets[g_CollisionTargetCount++] = &g_FloorCollisionId;
			}
		}
		if (g_TileSet.hasRing && FileExists(g_TileSet.ring.path))
		{
			const XMFLOAT3 position = RtcToWorldPosition(g_TileSet, g_TileSet.ring, g_EcefToEnu);
			if (Collision_StartAdd(g_TileSet.ring.path, MakeWorldAt(position, g_MeshRotation)))
			{
				g_CollisionTargets[g_CollisionTargetCount++] = &g_RingCollisionId;
			}
		}
	}
	if (g_CollisionTargetCount == 0)
	{
		g_CollisionFinished = true;
	}
	StartPendingImports();
}

static void BuildDrawJobs(void)
{
	g_DrawJobs.clear();
	g_LoadStarted = false;
	g_TriedFallbackLod1 = false;

	if (g_HasEcefToEnu)
	{
		if (g_TileSet.hasFloor)
		{
			XMFLOAT3 position = RtcToWorldPosition(g_TileSet, g_TileSet.floor, g_EcefToEnu);
			position.y -= EXPO_FLOOR_SINK;
			AddDrawJob(ExpoDrawKind::Floor, 0, "FLOOR", g_TileSet.floor.path, position, &g_MeshRotation);
		}
		if (g_HasLod2)
		{
			for (int i = 0; i < g_TileSet.count; ++i)
			{
				const XMFLOAT3 position = RtcToWorldPosition(g_TileSet, g_TileSet.tiles[i], g_EcefToEnu);
				AddDrawJob(ExpoDrawKind::Lod2, i, "LOD2", g_TileSet.tiles[i].path, position, &g_MeshRotation);
			}
		}
		for (int i = 0; i < g_TileSet.farCount; ++i)
		{
			const XMFLOAT3 position = RtcToWorldPosition(g_TileSet, g_TileSet.farTiles[i], g_EcefToEnu);
			AddDrawJob(
				ExpoDrawKind::Lod2Far,
				i,
				"LOD2 FAR",
				g_TileSet.farTiles[i].path,
				position,
				&g_MeshRotation);
		}
		for (int i = 0; i < static_cast<int>(g_TileSet.pavilions.size()); ++i)
		{
			const XMFLOAT3 position = RtcToWorldPosition(g_TileSet, g_TileSet.pavilions[i], g_EcefToEnu);
			ExpoDrawJob* job = AddDrawJob(
				ExpoDrawKind::Pavilion,
				i,
				"PAV",
				g_TileSet.pavilions[i].path,
				position,
				&g_MeshRotation);
			AssignFarBatchRefs(job);
		}
		if (g_TileSet.hasRing)
		{
			const XMFLOAT3 position = RtcToWorldPosition(g_TileSet, g_TileSet.ring, g_EcefToEnu);
			AddDrawJob(ExpoDrawKind::Ring, 0, "RING", g_TileSet.ring.path, position, &g_MeshRotation);
		}
	}
	else
	{
		AddDrawJob(
			ExpoDrawKind::FallbackLod2,
			0,
			"LOD2 TILE",
			EXPO_MODEL_LOD2_PATH,
			{ 0.0f, 0.0f, 0.0f },
			nullptr);
	}
}

void Field_GetLoadStatus(char* out, size_t outSize)
{
	if (!out || outSize == 0)
	{
		return;
	}

	int importDone = 0;
	const int importTotal = static_cast<int>(g_DrawJobs.size());
	int importing = 0;
	int ready = 0;
	ExpoDrawJob* gpuJob = nullptr;
	ExpoDrawJob* failedJob = nullptr;
	for (std::unique_ptr<ExpoDrawJob>& job : g_DrawJobs)
	{
		if (!job)
		{
			continue;
		}
		if (job->workerDone || job->finished || job->failed)
		{
			importDone += 1;
		}
		if (job->started && !job->workerDone)
		{
			importing += 1;
		}
		if (job->workerDone && job->gpuModel && !job->finished)
		{
			ready += 1;
		}
		if (!failedJob && job->workerFailed && !job->failureReason.empty())
		{
			failedJob = job.get();
		}
		if (!gpuJob && job->workerDone && !job->finished && !job->failed && job->gpuModel)
		{
			gpuJob = job.get();
		}
	}

	char extra[128] = {};
	if (gpuJob)
	{
		unsigned int gpuDone = 0;
		unsigned int gpuTotal = 0;
		gpuJob->gpuModel->GetGpuProgress(&gpuDone, &gpuTotal);
		sprintf_s(extra, " GPU %s %u/%u", gpuJob->label, gpuDone, gpuTotal);
	}

	char col[64] = {};
	if (!g_CollisionFinished)
	{
		size_t done = 0;
		size_t total = 0;
		int stage = COLLISION_STAGE_IDLE;
		Collision_GetPumpProgress(&done, &total, &stage);
		if (stage != COLLISION_STAGE_IDLE && total > 0)
		{
			const unsigned pct = static_cast<unsigned>((done * 100) / total);
			sprintf_s(col, " COL %u%%", pct);
		}
		else if (stage != COLLISION_STAGE_IDLE)
		{
			strcpy_s(col, " COL");
		}
	}

	char failure[128] = {};
	if (failedJob)
	{
		sprintf_s(
			failure,
			" FAIL %s %s",
			failedJob->label,
			failedJob->failureReason.c_str());
	}

	sprintf_s(
		out,
		outSize,
		"LOADING IMPORT %d/%d IN %d READY %d%s%s",
		importDone,
		importTotal,
		importing,
		ready,
		extra,
		col);
	if (failure[0] != '\0')
	{
		const size_t used = strlen(out);
		if (used + strlen(failure) + 1 < outSize)
		{
			strcat_s(out, outSize, failure);
		}
	}
}

void Field_GetFinishedStatus(char* out, size_t outSize)
{
	if (!out || outSize == 0)
	{
		return;
	}

	if (g_HasEcefToEnu && HasAnyModel())
	{
		sprintf_s(
			out,
			outSize,
			"PLATEAU EXPO / FLOOR %s / LOD2 %d/%d / FAR %d/%d / PAV %d/%d / RING %s",
			g_ExpoFloor ? "ON" : "OFF",
			g_LoadedTiles,
			g_ExpectedTiles,
			g_LoadedFarTiles,
			g_ExpectedFarTiles,
			g_LoadedPavilions,
			g_ExpectedPavilions,
			g_ExpoRing ? "ON" : "OFF");
		return;
	}
	if (g_FallbackLod == 2)
	{
		strcpy_s(out, outSize, "PLATEAU EXPO / LOD2 TILE");
		return;
	}
	if (g_FallbackLod == 1)
	{
		strcpy_s(out, outSize, "PLATEAU EXPO / LOD1 TILE");
		return;
	}
	strcpy_s(out, outSize, "EXPO MODEL LOAD FAILED");
}

void Field_GetMemoryStatus(char* out, size_t outSize)
{
	if (!out || outSize == 0)
	{
		return;
	}
	PROCESS_MEMORY_COUNTERS counters = {};
	const BOOL hasMemory = GetProcessMemoryInfo(
		GetCurrentProcess(), &counters, sizeof(counters));
	SIZE_T workingSetMb = hasMemory ? counters.WorkingSetSize / (1024 * 1024) : 0;
	SIZE_T privateMb = hasMemory ? counters.PagefileUsage / (1024 * 1024) : 0;
	unsigned long long localBudget = 0;
	unsigned long long localUsage = 0;
	unsigned long long nonLocalBudget = 0;
	unsigned long long nonLocalUsage = 0;
	Direct3D_GetMemoryInfo(
		&localBudget, &localUsage, &nonLocalBudget, &nonLocalUsage);
	int inFlight = 0;
	int gpuWaiting = 0;
	int failed = 0;
	int resident = 0;
	int placeholders = 0;
	for (const std::unique_ptr<ExpoDrawJob>& holder : g_DrawJobs)
	{
		const ExpoDrawJob* job = holder.get();
		if (!job) continue;
		if (job->started && !job->workerDone) ++inFlight;
		if (job->gpuModel && !job->finished) ++gpuWaiting;
		if (job->kind == ExpoDrawKind::Pavilion && job->result) ++resident;
		if (job->placeholder) ++placeholders;
		if (job->failed) ++failed;
	}
	sprintf_s(
		out,
		outSize,
		"Expo Memory WS %llu MB Private %llu MB Local %llu/%llu MB NonLocal %llu/%llu MB PAV %d PH %d IN %d GPUQ %d FAIL %d",
		static_cast<unsigned long long>(workingSetMb),
		static_cast<unsigned long long>(privateMb),
		localUsage,
		localBudget,
		nonLocalUsage,
		nonLocalBudget,
		resident,
		placeholders,
		inFlight,
		gpuWaiting,
		failed);
}

static bool FadeHidesInitialLoad(void)
{
	const FADESTAT state = GetFadeState();
	return state == FADE_WAIT_LOAD || state == FADE_WARMUP || state == FADE_OUT || state == FADE_MAX;
}

void Field_PumpLoad(void)
{
	if (g_PumpedThisFrame)
	{
		return;
	}
	if (!g_LoadComplete && FadeHidesInitialLoad())
	{
		return;
	}
	g_PumpedThisFrame = true;

	const LONGLONG deadline = GetLoadDeadline();
	UpdatePrefetchPosition();
	if (g_LoadComplete)
	{
		PumpPavilionStreaming(deadline);
		StartPendingImports();
		PumpOneGpu(deadline);
		return;
	}
	if (g_DrawJobs.empty() && g_CollisionFinished)
	{
		FinishLoad();
		return;
	}

	if (!g_LoadStarted)
	{
		g_LoadStarted = true;
		KickoffLoad();
	}

	StartPendingImports();
	PumpCollisionLoad();
	PumpOneGpu(deadline);

	if (!g_HasEcefToEnu && AllDrawJobsSettled() && !g_TriedFallbackLod1)
	{
		g_TriedFallbackLod1 = true;
		if (!HasAnyModel() && FileExists(EXPO_MODEL_LOD1_PATH))
		{
			AddDrawJob(
				ExpoDrawKind::FallbackLod1,
				0,
				"LOD1 TILE",
				EXPO_MODEL_LOD1_PATH,
				{ 0.0f, 0.0f, 0.0f },
				nullptr);
			return;
		}
	}

	if (AllDrawJobsSettled() && g_CollisionFinished)
	{
		ApplyFixedYOffsets();
		FinishLoad();
	}
}

void Field_Initialize(void)
{
	g_LoadComplete = false;
	g_PumpedThisFrame = false;
	g_LoadStarted = false;
	g_TriedFallbackLod1 = false;
	g_CollisionFinished = false;
	g_CollisionSlot = 0;
	g_CollisionTargetCount = 0;
	g_LoadedTiles = 0;
	g_LoadedFarTiles = 0;
	g_LoadedPavilions = 0;
	g_ExpectedTiles = 0;
	g_ExpectedFarTiles = 0;
	g_ExpectedPavilions = 0;
	g_FallbackLod = 0;
	g_HasLod2 = false;
	g_HasEcefToEnu = false;
	g_HasMeshRotation = false;
	g_HasPreviousCameraPos = false;
	g_HasPrefetchPosition = false;
	g_MeshRotation = XMMatrixIdentity();
	g_EcefToEnu = XMMatrixIdentity();
	InitTileSetDefaults(&g_TileSet);

	if (FileExists(EXPO_SKYBOX_PATH))
	{
		g_ExpoSkybox = new Sprite3D(
			{ 0.0f, 0.0f, 0.0f },
			{ EXPO_SKYBOX_SCALE, EXPO_SKYBOX_SCALE, EXPO_SKYBOX_SCALE },
			{ EXPO_SKYBOX_PITCH, 0.0f, 0.0f },
			EXPO_SKYBOX_PATH,
			S_SKYBOX);
	}

	g_HasLod2 = ParseExpoTileSet(EXPO_TILE_SET_LOD2_PATH, &g_TileSet);
	if (!g_HasLod2)
	{
		InitTileSetDefaults(&g_TileSet);
	}
	ParseExpoField(EXPO_FIELD_PATH, &g_TileSet);

	g_ExpectedTiles = g_TileSet.count;
	g_ExpectedFarTiles = g_TileSet.farCount;
	g_ExpectedPavilions = static_cast<int>(g_TileSet.pavilions.size());

	const bool hasRefRtc =
		g_HasLod2 || g_TileSet.farCount > 0 || g_TileSet.hasFloor ||
		g_TileSet.hasRing || !g_TileSet.pavilions.empty();
	if (hasRefRtc)
	{
		g_EcefToEnu = BuildEcefToEnuMatrix(
			g_TileSet.refRtcX, g_TileSet.refRtcY, g_TileSet.refRtcZ);
		g_MeshRotation = BuildMeshRotation(g_EcefToEnu, g_TileSet.lhsFlipZ);
		g_HasMeshRotation = true;
		g_HasEcefToEnu = true;
	}

	BuildTileWorldBounds();
	BuildDrawJobs();
}

void Field_Finalize(void)
{
	ClearExpoTiles();
}

bool Field_IsLoadComplete(void)
{
	return g_LoadComplete;
}

void Field_SetSkyboxYaw(float yawDegrees)
{
	g_ExpoSkyboxYaw = yawDegrees;
	if (g_ExpoSkybox)
	{
		g_ExpoSkybox->SetRot({ EXPO_SKYBOX_PITCH, yawDegrees, 0.0f });
	}
}

void Field_SetSkyboxTexture(ID3D11ShaderResourceView* texture)
{
	if (!g_ExpoSkybox || !texture)
	{
		return;
	}
	g_ExpoSkybox->SetCustomTexture(texture);
	g_ExpoSkybox->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

static void ClearFarBatchVisibility(void)
{
	for (Sprite3D* model : g_ExpoFarTiles)
	{
		if (model)
		{
			model->ClearGlbHiddenBatchIds();
		}
	}
}

static void UpdateFarBatchVisibility(void)
{
	ClearFarBatchVisibility();
	for (const std::unique_ptr<ExpoDrawJob>& holder : g_DrawJobs)
	{
		const ExpoDrawJob* job = holder.get();
		if (!job || job->kind != ExpoDrawKind::Pavilion || !job->result)
		{
			continue;
		}
		for (const std::pair<int, int>& ref : job->farBatchRefs)
		{
			if (ref.first >= 0 &&
				ref.first < static_cast<int>(g_ExpoFarTiles.size()) &&
				g_ExpoFarTiles[ref.first])
			{
				g_ExpoFarTiles[ref.first]->HideGlbBatchId(ref.second);
			}
		}
	}
}

void Field_DrawLocalShadow(
	const XMMATRIX& lightView,
	const XMMATRIX& lightProjection,
	XMFLOAT3 focus,
	float radius)
{
	if (g_ExpoFloor)
	{
		g_ExpoFloor->DrawShadowMap(lightView, lightProjection, focus, radius);
	}
	for (Sprite3D* model : g_ExpoTiles)
	{
		if (model)
		{
			model->DrawShadowMap(lightView, lightProjection, focus, radius);
		}
	}
	for (Sprite3D* model : g_ExpoFarTiles)
	{
		if (model)
		{
			model->DrawShadowMap(lightView, lightProjection, focus, radius);
		}
	}
	if (g_ExpoRing)
	{
		g_ExpoRing->DrawShadowMap(lightView, lightProjection, focus, radius);
	}
}

void Field_Draw(void)
{
	if (g_ExpoFloor)
	{
		g_ExpoFloor->Draw();
	}
	for (size_t i = 0; i < g_ExpoTiles.size(); ++i)
	{
		Sprite3D* model = g_ExpoTiles[i];
		if (model &&
			(i >= static_cast<size_t>(g_TileSet.count) ||
				IsTileVisibleFromCamera(g_TileSet.tiles[i])))
		{
			model->Draw();
		}
	}
	UpdateFarBatchVisibility();
	for (size_t i = 0; i < g_ExpoFarTiles.size(); ++i)
	{
		Sprite3D* model = g_ExpoFarTiles[i];
		if (model)
		{
			if (i >= static_cast<size_t>(g_TileSet.farCount) ||
				IsTileVisibleFromCamera(g_TileSet.farTiles[i]))
			{
				model->Draw();
			}
		}
	}
	for (Sprite3D* model : g_ExpoPavilions)
	{
		if (model && model->IsModelInFrontOfCamera())
		{
			model->Draw();
		}
	}
	for (const std::unique_ptr<ExpoDrawJob>& holder : g_DrawJobs)
	{
		if (holder && holder->placeholder &&
			!HasReadyFarFallback(holder.get()) &&
			holder->placeholder->IsModelInFrontOfCamera())
		{
			holder->placeholder->Draw();
		}
	}
	if (g_ExpoRing)
	{
		g_ExpoRing->Draw();
	}
	if (g_ExpoSkybox)
	{
		if (Camera* camera = GetCamera())
		{
			g_ExpoSkybox->SetPos(camera->GetPos());
		}
		const XMFLOAT4 savedParameter = GetParameter();
		SetParameterW(g_ExpoSkyboxYaw);
		g_ExpoSkybox->Draw();
		SetParameter(savedParameter);
	}
	g_PumpedThisFrame = false;
}

XMFLOAT3 Field_GetSpawnPos(void)
{
	XMFLOAT3 spawnPos = { 0.0f, 8.0f, 0.0f };
	if (g_ExpoFloor)
	{
		spawnPos = g_ExpoFloor->GetPos();
		spawnPos.y += 4.0f;
	}
	return spawnPos;
}

XMFLOAT3 Field_GetLookTarget(void)
{
	if (g_ExpoFloor)
	{
		return g_ExpoFloor->GetPos();
	}
	return { 0.0f, 0.0f, 0.0f };
}

bool Field_HasFloor(void)
{
	return g_ExpoFloor != nullptr;
}

int Field_GetRingCollisionId(void)
{
	return g_RingCollisionId;
}

