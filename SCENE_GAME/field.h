#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <cstddef>

void Field_Initialize(void);
void Field_Finalize(void);
void Field_PumpLoad(void);
void Field_PumpAfterPresent(double lastDrawMs, float lastGpuMs);
bool Field_IsLoadComplete(void);
float Field_GetInitialLoadProgress(void);
void Field_Draw(void);
void Field_DrawProbeScene(void);
bool Field_TryGetNull2ProbeCenter(DirectX::XMFLOAT3* outCenter);
void Field_DrawLocalShadow(
	const DirectX::XMMATRIX& lightView,
	const DirectX::XMMATRIX& lightProjection,
	DirectX::XMFLOAT3 focus,
	float radius);

DirectX::XMFLOAT3 Field_GetSpawnPos(void);
DirectX::XMFLOAT3 Field_GetLookTarget(void);
bool Field_HasFloor(void);
int Field_GetRingCollisionId(void);

void Field_GetLoadStatus(char* out, size_t outSize);
void Field_GetFinishedStatus(char* out, size_t outSize);
void Field_GetMemoryStatus(char* out, size_t outSize);
void Field_SetSkyboxYaw(float yawDegrees);
void Field_SetSkyboxTexture(ID3D11ShaderResourceView* texture);
void Field_SetSkyboxEnabled(bool enabled);
