#pragma once

#include <DirectXMath.h>

// 画面座標と床コリジョンの変換。植生エディタと水面エディタで共用する。
// 画面座標は ImGui の DisplaySize 基準。

// 画面座標からカメラのレイを作る。
bool GroundPick_BuildRay(
	float screenX,
	float screenY,
	DirectX::XMVECTOR* origin,
	DirectX::XMVECTOR* direction);

// レイを step ずつ進めて、最初に床より下へ入る点を返す。
bool GroundPick_Pick(
	float screenX,
	float screenY,
	float maxDistance,
	float step,
	DirectX::XMFLOAT3* outPoint);

// ワールド座標を画面座標へ。カメラの後ろや深度範囲外なら false。
bool GroundPick_Project(const DirectX::XMFLOAT3& world, DirectX::XMFLOAT2* outScreen);
