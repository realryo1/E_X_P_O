#pragma once

#include "font.h"
#include <DirectXMath.h>
#include <string>

using namespace DirectX;

class BillboardDrawFont : public DrawFont
{
public:
	BillboardDrawFont(
		XMFLOAT3 worldPos,
		float worldHeight,
		XMFLOAT4 color,
		const std::string& text);
	~BillboardDrawFont() = default;

	void Draw() override;
	void SetWorldPos(XMFLOAT3 pos);
	XMFLOAT3 GetWorldPos() const { return m_WorldPos; }
	void SetWorldHeight(float height);
	float GetWorldHeight() const { return m_WorldHeight; }

private:
	static const float kPixelSize;
	XMFLOAT3 m_WorldPos;
	float m_WorldHeight;
};
