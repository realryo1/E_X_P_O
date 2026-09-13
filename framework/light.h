/*==============================================================================

   ライト管理[light.h]

==============================================================================*/
#pragma once

#include "main.h"
#include "renderer.h"

using namespace DirectX;

//==============================================================================
// アンビエントライト
//==============================================================================
class AmbientLight
{
private:
	XMFLOAT4 m_Color;

public:
	AmbientLight(XMFLOAT4 color) : m_Color(color) {}

	XMFLOAT4 GetColor() const { return m_Color; }
	void     SetColor(XMFLOAT4 color) { m_Color = color; }
};

//==============================================================================
// ポイントライト
//==============================================================================
class PointLight
{
private:
	BOOL     m_Enable;
	XMFLOAT4 m_Position;
	XMFLOAT4 m_Direction;
	XMFLOAT4 m_Diffuse;
	float    m_Range;
	float    m_Intensity;

public:
	PointLight(
		BOOL     enable,
		XMFLOAT4 position,
		XMFLOAT4 diffuse,
		float    range,
		float    intensity)
		: PointLight(
			enable,
			position,
			XMFLOAT4(0.0f, -1.0f, 0.0f, 0.0f),
			diffuse,
			range,
			intensity)
	{}

	PointLight(
		BOOL     enable,
		XMFLOAT4 position,
		XMFLOAT4 direction,
		XMFLOAT4 diffuse,
		float    range,
		float    intensity)
		: m_Enable(enable)
		, m_Position(position)
		, m_Direction(direction)
		, m_Diffuse(diffuse)
		, m_Range(range)
		, m_Intensity(intensity)
	{}

	BOOL     GetEnable()    const { return m_Enable; }
	XMFLOAT4 GetPosition()  const { return m_Position; }
	XMFLOAT4 GetDirection() const { return m_Direction; }
	XMFLOAT4 GetDiffuse()   const { return m_Diffuse; }
	float    GetRange()     const { return m_Range; }
	float    GetIntensity() const { return m_Intensity; }

	void SetEnable(BOOL enable)          { m_Enable    = enable; }
	void SetPosition(XMFLOAT4 position)  { m_Position  = position; }
	void SetDirection(XMFLOAT4 direction){ m_Direction = direction; }
	void SetDiffuse(XMFLOAT4 diffuse)    { m_Diffuse   = diffuse; }
	void SetRange(float range)           { m_Range     = range; }
	void SetIntensity(float intensity)   { m_Intensity = intensity; }

	// LIGHT構造体へ変換（Ambient はオプション）
	LIGHT ToLIGHT(XMFLOAT4 ambient = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f)) const
	{
		LIGHT light = {};
		light.Enable         = m_Enable;
		light.Position       = m_Position;
		light.Direction      = m_Direction;
		light.Diffuse        = m_Diffuse;
		light.Ambient        = ambient;
		light.PointLightParam = XMFLOAT4(m_Range, m_Intensity, 0.0f, 0.0f);
		return light;
	}

	// SetLight まで一括で行う
	void Apply(const AmbientLight& ambient) const
	{
		SetLight(ToLIGHT(ambient.GetColor()));
	}
};
