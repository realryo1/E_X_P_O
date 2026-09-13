#pragma once

#include <d3d11.h>
#include "renderer.h"
#include "texture.h"
#include "component.h"
#include "model.h"
#include "glb_model.h"
#include "debug_ostream.h"
#include <DirectXMath.h>
#include "camera.h"
#include <algorithm>
#include <cmath>
using namespace DirectX;

class Sprite3D : public Transform3D
{
protected:
	MODEL* m_Model;
	GlbModel* m_GlbModel;     // GLB用モデル (.glb 拡張子の場合に使用)
	bool m_IsGlb;              // GLBモデルかどうか
	XMFLOAT3 m_ModelSize;
	XMFLOAT3 m_ModelCenter;
	XMFLOAT4 m_Color;
	XMFLOAT4 m_OriginalColor;
	bool m_UseOriginalColor;
	SHADERTYPE m_ShaderType;
	ID3D11ShaderResourceView* m_CustomTexture; // カスタムテクスチャ
	bool m_HasRotationMatrix;
	XMMATRIX m_RotationMatrix;
	bool m_CastShadow;
	bool m_ReceiveShadow;
	bool m_PhotoAlbedo;
	bool m_MainPassCellCulling;
public:
	Sprite3D() : Transform3D(XMFLOAT3(0.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, 0.0f), XMFLOAT3(1.0f, 1.0f, 1.0f)), m_Model(nullptr), m_GlbModel(nullptr), m_IsGlb(false),
		  m_ModelSize(0.0f, 0.0f, 0.0f), m_ModelCenter(0.0f, 0.0f, 0.0f),
		  m_Color(1.0f, 1.0f, 1.0f, 1.0f),
		  m_OriginalColor(1.0f, 1.0f, 1.0f, 1.0f), m_UseOriginalColor(true), m_ShaderType(S_UNLIT), m_CustomTexture(nullptr),
		  m_HasRotationMatrix(false), m_RotationMatrix(XMMatrixIdentity()),
		  m_CastShadow(false), m_ReceiveShadow(false), m_PhotoAlbedo(false),
		  m_MainPassCellCulling(false)
	{
	}

	Sprite3D(const XMFLOAT3& pos, const XMFLOAT3& scale, const XMFLOAT3& rot, const char* pass, SHADERTYPE st)
		: Transform3D(pos, rot, scale), m_Model(nullptr), m_GlbModel(nullptr), m_IsGlb(false),
		  m_ModelSize(0.0f, 0.0f, 0.0f), m_ModelCenter(0.0f, 0.0f, 0.0f),
		  m_Color(1.0f, 1.0f, 1.0f, 1.0f),
		  m_OriginalColor(1.0f, 1.0f, 1.0f, 1.0f), m_UseOriginalColor(true), m_ShaderType(st), m_CustomTexture(nullptr),
		  m_HasRotationMatrix(false), m_RotationMatrix(XMMatrixIdentity()),
		  m_CastShadow(false), m_ReceiveShadow(false), m_PhotoAlbedo(false),
		  m_MainPassCellCulling(false)
	{
		// 拡張子で読み込みを分岐
		if (IsGlbFile(pass))
		{
			m_IsGlb = true;
			m_GlbModel = new GlbModel();
			m_GlbModel->Load(pass, GetDevice(), GetDeviceContext());
			// GlbModel::Load() 内で aiProcess_GlobalScale (100倍) 適用済みなのでそのまま使用
			m_ModelSize = m_GlbModel->GetSize();
			m_GlbModel->GetBounds(nullptr, nullptr, &m_ModelCenter);
			m_OriginalColor = m_GlbModel->GetAverageMaterialColor();
		}
		else
		{
			m_IsGlb = false;
			m_Model = ModelLoad(pass);
			m_ModelSize = ModelGetSize(m_Model);
			ModelGetBounds(m_Model, nullptr, nullptr, &m_ModelCenter);
			m_OriginalColor = ModelGetAverageMaterialColor(m_Model);
		}
	}
	~Sprite3D()
	{
		if (m_IsGlb)
		{
			if (m_GlbModel)
			{
				m_GlbModel->Release();
				delete m_GlbModel;
				m_GlbModel = nullptr;
			}
		}
		else
		{
			ModelRelease(m_Model);
		}
	}

	bool IsVisibleFromCamera(void) const
	{
		Camera* camera = GetCamera();
		if (!camera)
		{
			return true;
		}

		const float maxScale = (std::max)(
			std::fabs(m_Scale.x),
			(std::max)(std::fabs(m_Scale.y), std::fabs(m_Scale.z)));
		const float modelRadius = 0.5f * std::sqrt(
			m_ModelSize.x * m_ModelSize.x +
			m_ModelSize.y * m_ModelSize.y +
			m_ModelSize.z * m_ModelSize.z) * maxScale;
		if (modelRadius <= 0.001f)
		{
			return true;
		}

		const XMMATRIX rotation = m_HasRotationMatrix
			? m_RotationMatrix
			: XMMatrixRotationRollPitchYaw(
				XMConvertToRadians(m_Rotation.x),
				XMConvertToRadians(m_Rotation.y),
				XMConvertToRadians(m_Rotation.z));
		const XMMATRIX world = XMMatrixScaling(m_Scale.x, m_Scale.y, m_Scale.z)
			* rotation
			* XMMatrixTranslation(m_Position.x, m_Position.y, m_Position.z);
		const XMVECTOR viewPosition = XMVector3TransformCoord(
			XMLoadFloat3(&m_ModelCenter),
			world * camera->GetView());
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

		if (viewZ + modelRadius < nearPlane || viewZ - modelRadius > farPlane)
		{
			return false;
		}

		// 最遠側の深度を使って保守的に判定し、見える可能性のあるモデルを誤って捨てない。
		const float sideDepth = (std::max)(viewZ + modelRadius, nearPlane);
		if (std::fabs(viewX) - modelRadius > sideDepth / xScale)
		{
			return false;
		}
		if (std::fabs(viewY) - modelRadius > sideDepth / yScale)
		{
			return false;
		}
		return true;
	}

	// モデルの境界球全体がカメラ背面にあるかを判定する簡易カリング。
	bool IsModelInFrontOfCamera(void) const
	{
		Camera* camera = GetCamera();
		if (!camera)
		{
			return true;
		}

		const float maxScale = (std::max)(
			std::fabs(m_Scale.x),
			(std::max)(std::fabs(m_Scale.y), std::fabs(m_Scale.z)));
		const float modelRadius = 0.5f * std::sqrt(
			m_ModelSize.x * m_ModelSize.x +
			m_ModelSize.y * m_ModelSize.y +
			m_ModelSize.z * m_ModelSize.z) * maxScale;
		if (modelRadius <= 0.001f)
		{
			return true;
		}

		const XMMATRIX rotation = m_HasRotationMatrix
			? m_RotationMatrix
			: XMMatrixRotationRollPitchYaw(
				XMConvertToRadians(m_Rotation.x),
				XMConvertToRadians(m_Rotation.y),
				XMConvertToRadians(m_Rotation.z));
		const XMMATRIX world = XMMatrixScaling(m_Scale.x, m_Scale.y, m_Scale.z)
			* rotation
			* XMMatrixTranslation(m_Position.x, m_Position.y, m_Position.z);
		const XMVECTOR viewPosition = XMVector3TransformCoord(
			XMLoadFloat3(&m_ModelCenter),
			world * camera->GetView());
		return XMVectorGetZ(viewPosition) + modelRadius > 0.0f;
	}

	void SetCastShadow(bool enable) { m_CastShadow = enable; }
	void SetReceiveShadow(bool enable) { m_ReceiveShadow = enable; }
	void SetPhotoAlbedo(bool enable)
	{
		m_PhotoAlbedo = enable;
		if (m_GlbModel)
		{
			m_GlbModel->SetPhotoAlbedo(enable);
		}
	}
	void SetMainPassCellCulling(bool enable)
	{
		m_MainPassCellCulling = enable;
		if (m_GlbModel)
		{
			m_GlbModel->SetMainPassCellCulling(enable);
		}
	}

	// ShadowMapへ影(深度)を描く。静的モデル用。
	// スキニングモデルは AnimSprite3D 側でオーバーライドする。
	virtual void DrawShadowMap(const XMMATRIX& lightView, const XMMATRIX& lightProjection)
	{
		DrawShadowMap(lightView, lightProjection, XMFLOAT3(0.0f, 0.0f, 0.0f), 0.0f);
	}

	virtual void DrawShadowMap(
		const XMMATRIX& lightView,
		const XMMATRIX& lightProjection,
		XMFLOAT3 focus,
		float radius)
	{
		if (!m_CastShadow)
		{
			return;
		}
		if (m_IsGlb)
		{
			if (m_GlbModel && m_GlbModel->IsLoaded())
			{
				const XMMATRIX rotation = m_HasRotationMatrix
					? m_RotationMatrix
					: XMMatrixRotationRollPitchYaw(
						XMConvertToRadians(GetRot().x),
						XMConvertToRadians(GetRot().y),
						XMConvertToRadians(GetRot().z));
				m_GlbModel->DrawShadowMap(
					GetPos(), rotation, GetScale(),
					lightView, lightProjection, focus, radius);
			}
			return;
		}
		if (m_Model)
		{
			ModelDrawShadowMap(m_Model, GetPos(), GetRot(), GetScale(), lightView, lightProjection);
		}
	}

	virtual void Draw(void)
	{
		if (!IsVisibleFromCamera())
		{
			return;
		}

		// 使用する色を決定
		XMFLOAT4 drawColor = m_UseOriginalColor ? m_OriginalColor : m_Color;
		bool shouldApplyColorReplace = !m_UseOriginalColor;

		if (!shouldApplyColorReplace)
		{
			drawColor.w = 0.0f;
		}

		const bool usePbrParameter = !m_IsGlb && m_ShaderType == S_PBR;
		const XMFLOAT4 savedParameter = usePbrParameter
			? GetParameter()
			: XMFLOAT4();
		if (usePbrParameter)
		{
			SetParameterW(m_ReceiveShadow ? 1.0f : 0.0f);
		}

		if (m_IsGlb)
		{
			if (m_GlbModel && m_GlbModel->IsLoaded())
			{
				m_GlbModel->SetReceiveShadow(m_ReceiveShadow);
				m_GlbModel->SetPhotoAlbedo(m_PhotoAlbedo);
				// GlbModel::Load() 内で aiProcess_GlobalScale (100倍) 適用済みなので追加スケール不要
				if (m_HasRotationMatrix)
				{
					m_GlbModel->Draw(
						GetPos(),
						m_RotationMatrix,
						GetScale(),
						drawColor,
						shouldApplyColorReplace,
						m_ShaderType
					);
				}
				else
				{
					m_GlbModel->Draw(
						GetPos(),
						GetRot(),
						GetScale(),
						drawColor,
						shouldApplyColorReplace,
						m_ShaderType
					);
				}
			}
			else
			{
			}
		}
		else
		{
			if (m_Model)
			{
				ModelDraw(
					m_Model,
					GetPos(),
					GetRot(),
					GetScale(),
					drawColor,
					shouldApplyColorReplace,
					m_ShaderType,
					m_CustomTexture
				);
			}
			else
			{
			}
		}

		if (usePbrParameter)
		{
			SetParameter(savedParameter);
		}
	}

	void ClearGlbHiddenBatchIds(void)
	{
		if (m_GlbModel)
		{
			m_GlbModel->ClearHiddenBatchIds();
		}
	}

	void HideGlbBatchId(int batchId)
	{
		if (m_GlbModel)
		{
			m_GlbModel->HideBatchId(batchId);
		}
	}

	// 色を設定
	void SetColor(const XMFLOAT4& color)
	{
		m_Color = color;
		m_UseOriginalColor = false;  // カスタム色を使用中
	}

	// 色を設定（R, G, B, A）
	void SetColor(float r, float g, float b, float a = 1.0f)
	{
		m_Color = XMFLOAT4(r, g, b, a);
		m_UseOriginalColor = false;  // カスタム色を使用中
	}

	// 色を取得
	XMFLOAT4 GetColor(void) const
	{
		return m_Color;
	}

	// 元の色を設定（初期化時に呼び出す）
	void SetOriginalColor(const XMFLOAT4& color)
	{
		m_OriginalColor = color;
	}

	// 元の色を設定（R, G, B, A）
	void SetOriginalColor(float r, float g, float b, float a = 1.0f)
	{
		m_OriginalColor = XMFLOAT4(r, g, b, a);
	}

	// 色をリセット（元の色に戻す）
	void ResetColor(void)
	{
		m_UseOriginalColor = true;
	}

	// カスタムテクスチャをパスから設定する
	void SetCustomTexture(const char* texturePath)
	{
		if (texturePath)
		{
			int len = (int)strlen(texturePath);
			std::wstring wpath(len, L'\0');
			MultiByteToWideChar(CP_ACP, 0, texturePath, len, &wpath[0], len);
			m_CustomTexture = LoadTexture(wpath.c_str());
		}
		else
		{
			m_CustomTexture = nullptr;
		}
	}

	// カスタムテクスチャを直接設定する
	void SetCustomTexture(ID3D11ShaderResourceView* texture)
	{
		m_CustomTexture = texture;
	}

	// 色を変更するメソッド（R, G, B, A 個別指定）
	void SetColorRed(float r) { m_Color.x = r; m_UseOriginalColor = false; }
	void SetColorGreen(float g) { m_Color.y = g; m_UseOriginalColor = false; }
	void SetColorBlue(float b) { m_Color.z = b; m_UseOriginalColor = false; }
	void SetColorAlpha(float a) { m_Color.w = a; m_UseOriginalColor = false; }

	// 色を取得するメソッド（R, G, B, A 個別取得）
	float GetColorRed(void) const { return m_Color.x; }
	float GetColorGreen(void) const { return m_Color.y; }
	float GetColorBlue(void) const { return m_Color.z; }
	float GetColorAlpha(void) const { return m_Color.w; }

	void SetShaderType(SHADERTYPE st)
	{
		m_ShaderType = st;
	}

	void AdoptGlbModel(GlbModel* model)
	{
		if (m_GlbModel && m_GlbModel != model)
		{
			m_GlbModel->Release();
			delete m_GlbModel;
		}
		m_IsGlb = true;
		m_GlbModel = model;
		if (m_GlbModel)
		{
			m_GlbModel->SetPhotoAlbedo(m_PhotoAlbedo);
			m_GlbModel->SetReceiveShadow(m_ReceiveShadow);
			m_GlbModel->SetMainPassCellCulling(m_MainPassCellCulling);
		}
		if (m_GlbModel && m_GlbModel->IsLoaded())
		{
			m_ModelSize = m_GlbModel->GetSize();
			m_GlbModel->GetBounds(nullptr, nullptr, &m_ModelCenter);
			m_OriginalColor = m_GlbModel->GetAverageMaterialColor();
		}
	}

	void SetRotationMatrix(const XMMATRIX& rotation)
	{
		m_RotationMatrix = rotation;
		m_HasRotationMatrix = true;
		RequestRedraw();
	}

	XMFLOAT3 GetModelSize(void) const { return m_ModelSize; }
	XMFLOAT3 GetDisplaySize(void) const 
	{ 
		XMFLOAT3 scale = GetScale();
		return XMFLOAT3(
			m_ModelSize.x * scale.x,
			m_ModelSize.y * scale.y,
			m_ModelSize.z * scale.z
		);
	}

	XMFLOAT4 GetModelColor(void) const
	{
		if (m_IsGlb)
		{
			return m_GlbModel ? m_GlbModel->GetAverageMaterialColor() : XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		}
		return ModelGetAverageMaterialColor(m_Model);
	}
};
