#include "debug_toon_scene.h"
#include "keyboard.h"
#include "renderer.h"
#include "sprite3d.h"
#include "sprite2d.h"
#include "camera.h"
#include "debugcamera.h"
#include "texture.h"
#include "mouse.h"
#include "billboard.h"
#include "define.h"
#include "imgui/imgui.h"
#include <DirectXMath.h>

using namespace DirectX;

static Sprite3D* g_pToon1Model = nullptr;
static Sprite3D* g_pToon2Model = nullptr;
static Billboard* g_pFloor = nullptr;
static Sprite2D* g_pRampSprite = nullptr;

static float g_RotationY = 0.0f;
static float g_Level1 = 0.4f;
static float g_Level2 = 0.7f;
static float g_TexV = 0.05f;
static float g_EdgeThreshold = -0.25f;

static XMFLOAT4 g_pointLightPos = { 0.0f, 3.0f, 2.0f, 1.0f };
static XMFLOAT4 g_pointLightDiffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
static XMFLOAT4 g_ambientColor = { 0.15f, 0.15f, 0.15f, 1.0f };
static float g_pointLightRange = 20.0f;

void DebugToonScene_Initialize(void)
{
	g_pToon1Model = new Sprite3D(
		{ -2.0f, 0.0f, 5.0f },
		{ 1.0f, 1.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f },
		"asset\\model\\model.fbx",
		S_TOON1
	);

	g_pToon2Model = new Sprite3D(
		{ 2.0f, 0.0f, 5.0f },
		{ 1.0f, 1.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f },
		"asset\\model\\model.fbx",
		S_TOON2
	);

	g_pFloor = new Billboard(
		{ 0.0f, -0.5f, 5.0f },
		{ 12.0f, 12.0f },
		{ 90.0f, 0.0f, 0.0f },
		"asset\\texture\\tex.png",
		false
	);
	g_pFloor->SetBillboardMode(false);
	g_pFloor->SetIgnoreLighting(true);

	g_pRampSprite = new Sprite2D(
		{ 140.0f, 140.0f },
		{ 200.0f, 200.0f },
		0.0f,
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		BLENDSTATE_NONE,
		L"asset\\texture\\Toon2.png"
	);

	DebugCamera_Initialize({ 0.0f, 4.0f, -4.0f }, 0.0f, 15.0f);
	if (GetCamera())
	{
		GetCamera()->SetTargetPos({ 0.0f, 0.5f, 5.0f });
		SetCameraPosition(GetCamera()->GetPos());
	}

	UnLockMouse();
}

void DebugToonScene_Update(void)
{
	DebugCamera_Update();

	if (GetCamera())
	{
		SetCameraPosition(GetCamera()->GetPos());
	}

	g_RotationY += 0.5f;
	if (g_RotationY >= 360.0f)
	{
		g_RotationY -= 360.0f;
	}

	XMFLOAT3 rot = { 0.0f, g_RotationY, 0.0f };
	if (g_pToon1Model) g_pToon1Model->SetRot(rot);
	if (g_pToon2Model) g_pToon2Model->SetRot(rot);
}

void DebugToonScene_Draw(void)
{
	SetDepthEnable(true);

	LIGHT light = {};
	light.Enable = TRUE;
	light.Position = g_pointLightPos;
	light.Diffuse = g_pointLightDiffuse;
	light.Ambient = g_ambientColor;
	light.PointLightParam.x = g_pointLightRange;
	SetLight(light);

	if (GetCamera())
	{
		SetCameraPosition(GetCamera()->GetPos());
	}

	// Toon1: プログラム式段階分け
	if (g_pToon1Model)
	{
		SetParameter(XMFLOAT4(g_Level1, g_Level2, g_EdgeThreshold, 0.0f));
		g_pToon1Model->Draw();
	}

	// Toon2: Ramp テクスチャ (t2 — Common.hlsl の ShadowMap が t1)
	if (g_pToon2Model)
	{
		SetParameter(XMFLOAT4(g_TexV, 0.0f, g_EdgeThreshold, 0.0f));

		ID3D11ShaderResourceView* rampSRV = g_pRampSprite ? g_pRampSprite->GetTexture() : nullptr;
		if (rampSRV)
		{
			GetDeviceContext()->PSSetShaderResources(2, 1, &rampSRV);
		}

		g_pToon2Model->Draw();

		ID3D11ShaderResourceView* nullSRV = nullptr;
		GetDeviceContext()->PSSetShaderResources(2, 1, &nullSRV);
	}

	if (g_pFloor)
	{
		g_pFloor->Draw();
	}

	// 2D: Ramp スプライトは 3D の後
	SetDepthEnable(false);
	if (g_pRampSprite)
	{
		g_pRampSprite->Draw();
	}

	if (Direct3D_IsTakingScreenshot())
	{
		return;
	}

	ImGui::Begin("Toon Control");
	ImGui::Text("Toon1 (Program Steps)");
	ImGui::SliderFloat("Level-1", &g_Level1, 0.0f, 1.0f);
	ImGui::SliderFloat("Level-2", &g_Level2, 0.0f, 1.0f);
	if (g_Level1 > g_Level2)
	{
		g_Level1 = g_Level2;
	}
	ImGui::SliderFloat("Edge Threshold", &g_EdgeThreshold, -1.0f, 0.0f);
	ImGui::Separator();
	ImGui::Text("Toon2 (Ramp Texture V)");
	ImGui::SliderFloat("Texture V", &g_TexV, 0.0f, 1.0f);
	ImGui::Separator();
	ImGui::Text("Point Light");
	float pos[3] = { g_pointLightPos.x, g_pointLightPos.y, g_pointLightPos.z };
	if (ImGui::DragFloat3("Position##ToonLight", pos, 0.1f))
	{
		g_pointLightPos = XMFLOAT4(pos[0], pos[1], pos[2], 1.0f);
	}
	float col[3] = { g_pointLightDiffuse.x, g_pointLightDiffuse.y, g_pointLightDiffuse.z };
	if (ImGui::ColorEdit3("Color##ToonLight", col))
	{
		g_pointLightDiffuse = XMFLOAT4(col[0], col[1], col[2], 1.0f);
	}
	ImGui::SliderFloat("Range##ToonLight", &g_pointLightRange, 0.1f, 50.0f);
	ImGui::End();
}

void DebugToonScene_Finalize(void)
{
	delete g_pToon1Model;
	g_pToon1Model = nullptr;

	delete g_pToon2Model;
	g_pToon2Model = nullptr;

	delete g_pFloor;
	g_pFloor = nullptr;

	delete g_pRampSprite;
	g_pRampSprite = nullptr;

	DebugCamera_Finalize();
}
