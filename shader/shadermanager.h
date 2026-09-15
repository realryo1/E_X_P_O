#pragma once

#include "main.h"
#include <string>
#include "renderer.h"
using namespace DirectX;

enum SHADERTYPE {
	S_UNLIT = 0,
	S_LAMBERT,
	S_PHONG,
	S_PBR,
	S_RIM_LIGHT,
	S_OUTLINE,
	S_SHADOW_MAP,		// ShadowMap作成用。色ではなく深度だけを描く。
	S_BILLBOARD_SHADOW_MAP, // 透過ビルボードの形状に沿ったShadowMap作成用。
	S_SHADOW_RECEIVE,	// ShadowMapを読んで、床などに影を反映する。
	S_NORMAL_MAP_SHADOW_RECEIVE, // ShadowMapの落ち影とNormalMapの凹凸表現を同時に使う床テスト用。
	S_PHONG_SHADOW,		// Phong(点光源ランバート) + ShadowMap受け取り。フィールド用。
	S_CHROMAKEY,		// クロマキー透過用。
	// expogame から移植（既存 enum 値は崩さない）
	S_COOK_TORRANCE,
	S_DISNEY_PBR,
	S_HEMISPHERE,
	S_NORMAL_MAP,		// 影なし単体 NormalMap
	S_POINT_LIGHT,
	S_SPOT_LIGHT,
	S_TOON1,
	S_TOON2,
	S_SKYBOX,		// ワールド方向からHDR正距円筒UVを計算するスカイドーム用。
	S_SSAO,
	S_SSAO_BLUR,
	S_SSAO_COMPOSITE,
	S_MAX,
};

const std::string filenames[S_MAX] = {
	"UnlitTexture",
	"VertexDirectionalLighting",
	"PixelDirectionalLighting",
	"PBRShader",
	"RimLight",
	"Outline",
	"ShadowMap",
	"BillboardShadowMap",
	"ShadowReceive",
	// shader/NormalMapShadowReceiveVS.cso と PS.cso を読み込む。
	"NormalMapShadowReceive",
	"PhongShadow",
	"ChromaKey",
	"CookTorrance",
	"DisneyPBR",
	"HemiSphereLighting",
	"NormalMap",
	"PointPixelLighting",
	"SpotLighting",
	"Toon1",
	"Toon2",
	"SkyboxTexture",
	"Ssao",
	"SsaoBlur",
	"SsaoComposite"
};

class ShaderManager
{
private:
	ID3D11InputLayout* m_Layout = nullptr;
	ID3D11VertexShader* m_VS = nullptr;
	ID3D11PixelShader* m_PS = nullptr;

public:
	ShaderManager() = delete;
	ShaderManager(SHADERTYPE st)
	{
		std::string vsname = "shader/" + filenames[st] + "VS.cso";
		std::string psname = "shader/" + filenames[st] + "PS.cso";

		// シェーダー読み込み
		CreateVertexShader(&m_VS, &m_Layout, vsname.c_str());
		CreatePixelShader(&m_PS, psname.c_str());
	}
	~ShaderManager()
	{
		SAFE_RELEASE(m_Layout);
		SAFE_RELEASE(m_VS);
		SAFE_RELEASE(m_PS);
	}
	ID3D11InputLayout* GetVertexLayout() { return m_Layout; }
	ID3D11VertexShader* GetVertexShader() { return m_VS; }
	ID3D11PixelShader* GetPixelShader() { return m_PS; }
};

inline ShaderManager*& ShaderSlot(SHADERTYPE st)
{
	static ShaderManager* SMng[S_MAX] = {};
	return SMng[st];
}

inline void InitShader(void)
{
	for (int i = 0; i < S_MAX; i++)
	{
		ShaderSlot((SHADERTYPE)i) = new ShaderManager((SHADERTYPE)i);
	}
}

inline void FinalizeShader(void)
{
	for (int i = 0; i < S_MAX; i++)
	{
		delete ShaderSlot((SHADERTYPE)i);
		ShaderSlot((SHADERTYPE)i) = nullptr;
	}
}

inline ShaderManager* GetShader(SHADERTYPE st)
{
	return ShaderSlot(st);
}
