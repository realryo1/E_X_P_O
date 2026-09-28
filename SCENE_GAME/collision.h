#pragma once

#include <DirectXMath.h>
#include <cstddef>
#include <vector>

struct CollisionBatchYOffset
{
	unsigned int batchId = 0xFFFFFFFFu;
	float yOffset = 0.0f;
};

enum CollisionPumpResult
{
	COLLISION_PUMP_IDLE = 0,
	COLLISION_PUMP_BUSY,
	COLLISION_PUMP_DONE,
	COLLISION_PUMP_FAILED
};

enum CollisionPumpStage
{
	COLLISION_STAGE_IDLE = 0,
	COLLISION_STAGE_LOAD,
	COLLISION_STAGE_TRANSFORM,
	COLLISION_STAGE_GRID
};

bool Collision_StartAdd(
	const char* glbPath,
	const DirectX::XMMATRIX& world,
	bool filterLattice,
	const std::vector<CollisionBatchYOffset>* batchYOffsets = nullptr);
CollisionPumpResult Collision_Pump(int* outMeshId);
void Collision_GetPumpProgress(size_t* done, size_t* total, int* stage);
void Collision_GetSourceStatus(char* out, size_t outSize);
void Collision_GetLastHitStatus(char* out, size_t outSize);
void Collision_SetWorld(int meshId, const DirectX::XMMATRIX& world);
bool Collision_AdjustBatchY(int meshId, unsigned int batchId, float deltaY);
void Collision_Clear(void);
bool Collision_GetBounds(int meshId, DirectX::XMFLOAT3* bmin, DirectX::XMFLOAT3* bmax);
bool Collision_SampleTopY(int meshId, float x, float z, float* outY);
bool Collision_MoveAABB(
	DirectX::XMFLOAT3 center,
	DirectX::XMFLOAT3 halfExtents,
	DirectX::XMFLOAT3 delta,
	DirectX::XMFLOAT3* outCenter,
	bool* grounded);
void Collision_DrawWire(
	DirectX::XMFLOAT3 center,
	DirectX::XMFLOAT3 halfExtents);
void Collision_DrawDebug(void);
