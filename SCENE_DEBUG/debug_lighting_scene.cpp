#include "debug_lighting_scene.h"
#include "light.h"
#include "keyboard.h"
#include "renderer.h"
#include "sprite3d.h"
#include "camera.h"
#include "debugcamera.h"
#include "texture.h"
#include "mouse.h"
#include "billboard.h"
#include "imgui/imgui.h"
#include <DirectXMath.h>

using namespace DirectX;

// 移植シェーダー検証用モデル群（cube / 床ビルボード）
static Sprite3D* g_pLambertModel = nullptr;
static Sprite3D* g_pCookTorranceModel = nullptr;
static Sprite3D* g_pDisneyPBRModel = nullptr;
static Sprite3D* g_pHemisphereModel = nullptr;
static Sprite3D* g_pPointLightModel = nullptr;
static Sprite3D* g_pSpotLightModel = nullptr;
static Billboard* g_pNormalMapFloor = nullptr;

// ディズニーPBR用バッファ構造体
struct DISNEY_LIGHT_BUFFER
{
	LIGHT DisneyLights[10];
	int LightCount;
	float Dummy[3];
};
static DISNEY_LIGHT_BUFFER g_DisneyLightBufferData;
static ID3D11Buffer* g_pDisneyLightBuffer = nullptr;

// 法線マップ用テクスチャ
static ID3D11ShaderResourceView* g_pNormalTexture = nullptr;

// アンビエントライト
static AmbientLight g_ambientLight(XMFLOAT4(0.15f, 0.15f, 0.15f, 1.0f));

// 1. 平行光 (S_LAMBERT, S_NORMAL_MAP, S_HEMISPHERE 用)
static XMFLOAT4 g_dirLightDir(0.5f, -1.0f, 0.5f, 0.0f);
static XMFLOAT4 g_dirLightDiffuse(1.0f, 1.0f, 1.0f, 1.0f);

// 半球ライト色
static XMFLOAT4 g_skyColor(0.4f, 0.55f, 0.9f, 1.0f);
static XMFLOAT4 g_groundColor(0.35f, 0.25f, 0.15f, 1.0f);
static XMFLOAT4 g_groundNormal(0.0f, 1.0f, 0.0f, 0.0f);

// 2. 点光源 (S_POINT_LIGHT / CookTorrance 用)
static XMFLOAT4 g_pointLightPos(2.0f, 2.0f, 3.0f, 1.0f);
static XMFLOAT4 g_pointLightDiffuse(1.0f, 0.5f, 0.0f, 1.0f);
static float g_pointLightRange = 5.0f;

// 3. スポットライト (S_SPOT_LIGHT 用)
static XMFLOAT4 g_spotLightPos(4.0f, 3.0f, 3.0f, 1.0f);
static XMFLOAT4 g_spotLightDir(0.0f, -1.0f, 0.5f, 0.0f);
static XMFLOAT4 g_spotLightDiffuse(0.0f, 1.0f, 1.0f, 1.0f);
static float g_spotLightRange = 10.0f;
static float g_spotLightAngle = 30.0f;
static float g_spotLightPower = 2.0f;

// PBR調整用パラメータ
static float g_PBRSmooth = 0.5f;
static float g_PBRMetallic = 0.5f;
static float g_CookSmooth = 0.5f;
static float g_CookMetallic = 0.5f;

// 床
static XMFLOAT3 g_floorPos = { 0.0f, -1.0f, 5.0f };
static XMFLOAT3 g_floorRot = { -90.0f, 0.0f, 0.0f };
static XMFLOAT2 g_floorSize = { 20.0f, 20.0f };

static float g_RotationY = 0.0f;
static bool g_showModels = true;

void DebugLightingScene_Initialize(void)
{
	g_DisneyLightBufferData.LightCount = 1;
	for (int i = 0; i < 10; i++)
	{
		ZeroMemory(&g_DisneyLightBufferData.DisneyLights[i], sizeof(LIGHT));
		g_DisneyLightBufferData.DisneyLights[i].Enable = (i == 0) ? TRUE : FALSE;
		g_DisneyLightBufferData.DisneyLights[i].Position = XMFLOAT4(-2.0f + (float)i * 0.5f, 2.0f, 3.0f, 1.0f);
		g_DisneyLightBufferData.DisneyLights[i].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		g_DisneyLightBufferData.DisneyLights[i].Ambient = XMFLOAT4(0.15f, 0.15f, 0.15f, 1.0f);
		g_DisneyLightBufferData.DisneyLights[i].PointLightParam = XMFLOAT4(5.0f, 1.0f, 0.0f, 0.0f);
	}

	g_pLambertModel = new Sprite3D(
		{ -5.0f, 0.0f, 5.0f },
		{ 1.0f, 1.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f },
		"asset\\model\\cube.fbx",
		S_LAMBERT
	);

	g_pCookTorranceModel = new Sprite3D(
		{ -3.0f, 0.0f, 5.0f },
		{ 1.0f, 1.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f },
		"asset\\model\\cube.fbx",
		S_COOK_TORRANCE
	);

	g_pDisneyPBRModel = new Sprite3D(
		{ -1.0f, 0.0f, 5.0f },
		{ 1.0f, 1.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f },
		"asset\\model\\cube.fbx",
		S_DISNEY_PBR
	);

	g_pHemisphereModel = new Sprite3D(
		{ 1.0f, 0.0f, 5.0f },
		{ 1.0f, 1.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f },
		"asset\\model\\cube.fbx",
		S_HEMISPHERE
	);

	g_pPointLightModel = new Sprite3D(
		{ 3.0f, 0.0f, 5.0f },
		{ 1.0f, 1.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f },
		"asset\\model\\cube.fbx",
		S_POINT_LIGHT
	);

	g_pSpotLightModel = new Sprite3D(
		{ 5.0f, 0.0f, 5.0f },
		{ 1.0f, 1.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f },
		"asset\\model\\cube.fbx",
		S_SPOT_LIGHT
	);

	g_pNormalMapFloor = new Billboard(
		g_floorPos,
		g_floorSize,
		g_floorRot,
		"asset\\texture\\renga.png",
		false
	);
	g_pNormalMapFloor->SetBillboardMode(false);
	g_pNormalMapFloor->SetIgnoreLighting(false);
	g_pNormalMapFloor->SetUVScale({ 4.0f, 4.0f });
	g_pNormalMapFloor->SetNormalMap("asset\\texture\\renga_normal.png");

	g_pNormalTexture = LoadTextureLinear(L"asset\\texture\\renga_normal.png");

	D3D11_BUFFER_DESC hBufferDesc = {};
	hBufferDesc.ByteWidth = sizeof(DISNEY_LIGHT_BUFFER);
	hBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	hBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	hBufferDesc.CPUAccessFlags = 0;
	hBufferDesc.MiscFlags = 0;
	hBufferDesc.StructureByteStride = 0;
	GetDevice()->CreateBuffer(&hBufferDesc, NULL, &g_pDisneyLightBuffer);

	DebugCamera_Initialize({ 0.0f, 4.0f, -6.0f }, 0.0f, 15.0f);
	if (GetCamera())
	{
		GetCamera()->SetTargetPos({ 0.0f, 0.0f, 5.0f });
		SetCameraPosition(GetCamera()->GetPos());
	}

	UnLockMouse();
}

void DebugLightingScene_Update(void)
{
	DebugCamera_Update();

	if (GetCamera())
	{
		SetCameraPosition(GetCamera()->GetPos());
	}

	g_RotationY += 0.4f;
	if (g_RotationY >= 360.0f)
	{
		g_RotationY -= 360.0f;
	}

	XMFLOAT3 rot = { 0.0f, g_RotationY, 0.0f };
	if (g_pLambertModel) g_pLambertModel->SetRot(rot);
	if (g_pCookTorranceModel) g_pCookTorranceModel->SetRot(rot);
	if (g_pDisneyPBRModel) g_pDisneyPBRModel->SetRot(rot);
	if (g_pHemisphereModel) g_pHemisphereModel->SetRot(rot);
	if (g_pPointLightModel) g_pPointLightModel->SetRot(rot);
	if (g_pSpotLightModel) g_pSpotLightModel->SetRot(rot);
}

void DebugLightingScene_Draw(void)
{
	SetDepthEnable(true);

	if (GetCamera())
	{
		SetCameraPosition(GetCamera()->GetPos());
	}

	// 1. S_LAMBERT
	if (g_pLambertModel && g_showModels)
	{
		LIGHT light = {};
		light.Enable = TRUE;
		light.Direction = g_dirLightDir;
		light.Diffuse = g_dirLightDiffuse;
		light.Ambient = g_ambientLight.GetColor();
		SetLight(light);
		g_pLambertModel->Draw();
	}

	// 2. S_COOK_TORRANCE
	if (g_pCookTorranceModel && g_showModels)
	{
		LIGHT light = {};
		light.Enable = TRUE;
		light.Position = g_pointLightPos;
		light.Diffuse = g_pointLightDiffuse;
		light.Ambient = g_ambientLight.GetColor();
		light.PointLightParam.x = g_pointLightRange;
		SetLight(light);
		SetParameter(XMFLOAT4(g_CookSmooth, g_CookMetallic, 0.0f, 0.0f));
		g_pCookTorranceModel->Draw();
	}

	// 3. S_DISNEY_PBR (定数バッファは b10)
	if (g_pDisneyPBRModel && g_pDisneyLightBuffer && g_showModels)
	{
		SetParameter(XMFLOAT4(g_PBRSmooth, g_PBRMetallic, 0.0f, 0.0f));
		GetDeviceContext()->UpdateSubresource(g_pDisneyLightBuffer, 0, NULL, &g_DisneyLightBufferData, 0, 0);
		GetDeviceContext()->PSSetConstantBuffers(10, 1, &g_pDisneyLightBuffer);
		g_pDisneyPBRModel->Draw();
	}

	// 4. S_HEMISPHERE
	if (g_pHemisphereModel && g_showModels)
	{
		LIGHT light = {};
		light.Enable = TRUE;
		light.Direction = g_dirLightDir;
		light.Diffuse = g_dirLightDiffuse;
		light.Ambient = g_ambientLight.GetColor();
		light.SkyColor = g_skyColor;
		light.GroundColor = g_groundColor;
		light.GroundNormal = g_groundNormal;
		SetLight(light);
		g_pHemisphereModel->Draw();
	}

	// 5. S_NORMAL_MAP 床
	if (g_pNormalMapFloor)
	{
		LIGHT light = {};
		light.Enable = TRUE;
		light.Direction = g_dirLightDir;
		light.Diffuse = g_dirLightDiffuse;
		light.Ambient = g_ambientLight.GetColor();
		SetLight(light);

		if (g_pNormalTexture)
		{
			// NormalMapPS は t2
			GetDeviceContext()->PSSetShaderResources(2, 1, &g_pNormalTexture);
		}
		g_pNormalMapFloor->Draw(S_NORMAL_MAP);
		ID3D11ShaderResourceView* nullSRV = nullptr;
		GetDeviceContext()->PSSetShaderResources(2, 1, &nullSRV);
	}

	// 6. S_POINT_LIGHT
	if (g_pPointLightModel && g_showModels)
	{
		LIGHT light = {};
		light.Enable = TRUE;
		light.Position = g_pointLightPos;
		light.Diffuse = g_pointLightDiffuse;
		light.Ambient = g_ambientLight.GetColor();
		light.PointLightParam.x = g_pointLightRange;
		SetLight(light);
		g_pPointLightModel->Draw();
	}

	// 7. S_SPOT_LIGHT
	if (g_pSpotLightModel && g_showModels)
	{
		LIGHT light = {};
		light.Enable = TRUE;
		light.Position = g_spotLightPos;
		light.Direction = g_spotLightDir;
		light.Diffuse = g_spotLightDiffuse;
		light.Ambient = g_ambientLight.GetColor();
		light.Angle.x = XMConvertToRadians(g_spotLightAngle);
		light.PointLightParam.x = g_spotLightRange;
		light.PointLightParam.y = g_spotLightPower;
		SetLight(light);
		g_pSpotLightModel->Draw();
	}

	if (Direct3D_IsTakingScreenshot())
	{
		return;
	}

	ImGui::Begin("Lighting Control Panel");
	ImGui::Checkbox("Show Models", &g_showModels);
	ImGui::Separator();

	if (ImGui::CollapsingHeader("Ambient Light"))
	{
		XMFLOAT4 ambientColor = g_ambientLight.GetColor();
		float col[4] = { ambientColor.x, ambientColor.y, ambientColor.z, ambientColor.w };
		if (ImGui::ColorEdit4("Ambient Color", col))
		{
			g_ambientLight.SetColor(XMFLOAT4(col[0], col[1], col[2], col[3]));
		}
	}

	if (ImGui::CollapsingHeader("Directional / Hemisphere"))
	{
		float dir[3] = { g_dirLightDir.x, g_dirLightDir.y, g_dirLightDir.z };
		if (ImGui::DragFloat3("Direction", dir, 0.05f, -1.0f, 1.0f))
		{
			g_dirLightDir = XMFLOAT4(dir[0], dir[1], dir[2], 0.0f);
		}
		float col[3] = { g_dirLightDiffuse.x, g_dirLightDiffuse.y, g_dirLightDiffuse.z };
		if (ImGui::ColorEdit3("Diffuse", col))
		{
			g_dirLightDiffuse = XMFLOAT4(col[0], col[1], col[2], 1.0f);
		}
		float sky[3] = { g_skyColor.x, g_skyColor.y, g_skyColor.z };
		if (ImGui::ColorEdit3("Sky Color", sky))
		{
			g_skyColor = XMFLOAT4(sky[0], sky[1], sky[2], 1.0f);
		}
		float ground[3] = { g_groundColor.x, g_groundColor.y, g_groundColor.z };
		if (ImGui::ColorEdit3("Ground Color", ground))
		{
			g_groundColor = XMFLOAT4(ground[0], ground[1], ground[2], 1.0f);
		}
	}

	if (ImGui::CollapsingHeader("Cook-Torrance"))
	{
		ImGui::SliderFloat("Smoothness##Cook", &g_CookSmooth, 0.0f, 1.0f);
		ImGui::SliderFloat("Metallic##Cook", &g_CookMetallic, 0.0f, 1.0f);
	}

	if (ImGui::CollapsingHeader("Disney PBR"))
	{
		ImGui::SliderFloat("Smoothness##PBR", &g_PBRSmooth, 0.0f, 1.0f);
		ImGui::SliderFloat("Metallicness##PBR", &g_PBRMetallic, 0.0f, 1.0f);
		ImGui::SliderInt("Light Count##PBR", &g_DisneyLightBufferData.LightCount, 1, 10);

		static int selectedLightIdx = 0;
		if (selectedLightIdx >= g_DisneyLightBufferData.LightCount)
			selectedLightIdx = g_DisneyLightBufferData.LightCount - 1;
		ImGui::SliderInt("Edit Light ID##PBR", &selectedLightIdx, 0, g_DisneyLightBufferData.LightCount - 1);

		LIGHT& dLight = g_DisneyLightBufferData.DisneyLights[selectedLightIdx];
		bool enable = dLight.Enable ? true : false;
		if (ImGui::Checkbox("Enable##PBRLight", &enable))
		{
			dLight.Enable = enable ? TRUE : FALSE;
		}
		float pos[3] = { dLight.Position.x, dLight.Position.y, dLight.Position.z };
		if (ImGui::DragFloat3("Position##PBRLight", pos, 0.1f))
		{
			dLight.Position = XMFLOAT4(pos[0], pos[1], pos[2], 1.0f);
		}
		float dcol[3] = { dLight.Diffuse.x, dLight.Diffuse.y, dLight.Diffuse.z };
		if (ImGui::ColorEdit3("Color##PBRLight", dcol))
		{
			dLight.Diffuse = XMFLOAT4(dcol[0], dcol[1], dcol[2], 1.0f);
		}
		ImGui::SliderFloat("Range##PBRLight", &dLight.PointLightParam.x, 0.1f, 50.0f);
	}

	if (ImGui::CollapsingHeader("Point Light"))
	{
		float pos[3] = { g_pointLightPos.x, g_pointLightPos.y, g_pointLightPos.z };
		if (ImGui::DragFloat3("Position##Point", pos, 0.1f))
		{
			g_pointLightPos = XMFLOAT4(pos[0], pos[1], pos[2], 1.0f);
		}
		float col[3] = { g_pointLightDiffuse.x, g_pointLightDiffuse.y, g_pointLightDiffuse.z };
		if (ImGui::ColorEdit3("Color##Point", col))
		{
			g_pointLightDiffuse = XMFLOAT4(col[0], col[1], col[2], 1.0f);
		}
		ImGui::SliderFloat("Range##Point", &g_pointLightRange, 0.1f, 50.0f);
	}

	if (ImGui::CollapsingHeader("Spot Light"))
	{
		float pos[3] = { g_spotLightPos.x, g_spotLightPos.y, g_spotLightPos.z };
		if (ImGui::DragFloat3("Position##Spot", pos, 0.1f))
		{
			g_spotLightPos = XMFLOAT4(pos[0], pos[1], pos[2], 1.0f);
		}
		float dir[3] = { g_spotLightDir.x, g_spotLightDir.y, g_spotLightDir.z };
		if (ImGui::DragFloat3("Direction##Spot", dir, 0.05f, -1.0f, 1.0f))
		{
			g_spotLightDir = XMFLOAT4(dir[0], dir[1], dir[2], 0.0f);
		}
		float col[3] = { g_spotLightDiffuse.x, g_spotLightDiffuse.y, g_spotLightDiffuse.z };
		if (ImGui::ColorEdit3("Color##Spot", col))
		{
			g_spotLightDiffuse = XMFLOAT4(col[0], col[1], col[2], 1.0f);
		}
		ImGui::SliderFloat("Angle(deg)##Spot", &g_spotLightAngle, 1.0f, 90.0f);
		ImGui::SliderFloat("Range##Spot", &g_spotLightRange, 0.1f, 50.0f);
		ImGui::SliderFloat("Power##Spot", &g_spotLightPower, 0.1f, 16.0f);
	}

	ImGui::End();
}

void DebugLightingScene_Finalize(void)
{
	delete g_pLambertModel;
	g_pLambertModel = nullptr;
	delete g_pCookTorranceModel;
	g_pCookTorranceModel = nullptr;
	delete g_pDisneyPBRModel;
	g_pDisneyPBRModel = nullptr;
	delete g_pHemisphereModel;
	g_pHemisphereModel = nullptr;
	delete g_pPointLightModel;
	g_pPointLightModel = nullptr;
	delete g_pSpotLightModel;
	g_pSpotLightModel = nullptr;
	delete g_pNormalMapFloor;
	g_pNormalMapFloor = nullptr;

	g_pNormalTexture = nullptr;
	SAFE_RELEASE(g_pDisneyLightBuffer);

	DebugCamera_Finalize();
}
