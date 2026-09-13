#pragma once

#include <DirectXMath.h>
#include "renderer.h"

void Sunlight_Initialize(void);
void Sunlight_Finalize(void);
void Sunlight_Update(void);
void Sunlight_Apply(void);
void Sunlight_DrawDebug(void);

bool Sunlight_BeginLocalShadow(
	DirectX::XMMATRIX outView[NUM_SHADOW_CASCADES],
	DirectX::XMMATRIX outProjection[NUM_SHADOW_CASCADES],
	DirectX::XMFLOAT3 outFocus[NUM_SHADOW_CASCADES],
	float outRadius[NUM_SHADOW_CASCADES]);
void Sunlight_EndLocalShadow(void);
