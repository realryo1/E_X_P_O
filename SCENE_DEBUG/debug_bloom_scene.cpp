#include "debug_bloom_scene.h"
#include "define.h"
#include "renderer.h"
#include "shadermanager.h"
#include "sprite3d.h"
#include "camera.h"
#include "debugcamera.h"
#include "mouse.h"
#include "imgui/imgui.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

using namespace DirectX;

// 発光物体のリング配置
static const int GLOW_COUNT = 8;
static const float RING_RADIUS = 4.5f;
static const XMFLOAT3 RING_CENTER = { 0.0f, 0.0f, 5.0f };

// 発光キューブの位置に置く点光源の届く距離と強さ
static const float GLOW_LIGHT_RANGE = 7.0f;
static const float GLOW_LIGHT_POWER = 2.0f;

// 発光色。1 を超える値は HDR の輝度として残り、ブルームの種になる
static const XMFLOAT3 GLOW_COLORS[GLOW_COUNT] = {
	{ 4.0f, 1.2f, 0.4f },
	{ 0.6f, 2.6f, 4.0f },
	{ 3.2f, 0.6f, 3.6f },
	{ 4.0f, 3.0f, 0.8f },
	{ 0.4f, 3.0f, 1.2f },
	{ 1.0f, 1.0f, 4.0f },
	{ 4.0f, 0.6f, 0.6f },
	{ 2.5f, 2.5f, 2.5f }
};

// 各パスへ渡す定数。意味は各 PS の BloomParams / BloomWeights を参照
struct BLOOM_CONSTANT
{
	XMFLOAT4 Params;
	XMFLOAT4 Weights;
};

// HDR（R16G16B16A16_FLOAT）の描画先。RTV と SRV を持つ
struct BLOOM_TARGET
{
	ID3D11Texture2D* Texture = nullptr;
	ID3D11RenderTargetView* RTV = nullptr;
	ID3D11ShaderResourceView* SRV = nullptr;
	UINT Width = 0;
	UINT Height = 0;
};

static Sprite3D* g_pFloor = nullptr;
static Sprite3D* g_pPillar = nullptr;
static Sprite3D* g_pGlowCubes[GLOW_COUNT] = {};

// RT0: 通常シーン。RT1: 輝度マップ（半分の解像度、ミップマップ付き）
static BLOOM_TARGET g_SceneTarget;
static BLOOM_TARGET g_GlowTarget;
static ID3D11Texture2D* g_pDepthTexture = nullptr;
static ID3D11DepthStencilView* g_pDepthView = nullptr;
static UINT g_TargetWidth = 0;
static UINT g_TargetHeight = 0;

static ID3D11Buffer* g_pQuadVertexBuffer = nullptr;
static ID3D11Buffer* g_pBloomConstantBuffer = nullptr;
static ID3D11SamplerState* g_pLinearClampSampler = nullptr;

// GUI 調整値（初期値は参考値）
static bool g_BloomEnabled = true;
static float g_BloomLuminance = 0.5504f;
static float g_BloomBlend[3] = { 1.2231f, 1.2090f, 2.0000f };
// 0:合成結果 1:シーンのみ 2:輝度マップのみ（確認用）
static int g_BloomView = 0;

static float g_Time = 0.0f;
static float g_RingAngle = 0.0f;

// 診断用（GUI の Diagnostics に表示）
static HRESULT g_GlowCreateHr = S_OK;
static bool g_ExtractShaderOk = false;
static bool g_CompositeShaderOk = false;
static int g_ExtractDrawCount = 0;
static int g_CompositeDrawCount = 0;

static void ReleaseBloomTarget(BLOOM_TARGET& target)
{
	SAFE_RELEASE(target.SRV);
	SAFE_RELEASE(target.RTV);
	SAFE_RELEASE(target.Texture);
}

static void ReleaseBloomTargets(void)
{
	ReleaseBloomTarget(g_SceneTarget);
	ReleaseBloomTarget(g_GlowTarget);
	SAFE_RELEASE(g_pDepthView);
	SAFE_RELEASE(g_pDepthTexture);
	g_TargetWidth = 0;
	g_TargetHeight = 0;
}

// generateMips が真なら全ミップレベルを作り、GenerateMips で縮小段を埋められるようにする
static HRESULT CreateBloomTarget(BLOOM_TARGET& target, UINT width, UINT height, bool generateMips)
{
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = generateMips ? 0 : 1; // 0 は全ミップレベルを作る
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	desc.MiscFlags = generateMips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;

	HRESULT hr = GetDevice()->CreateTexture2D(&desc, nullptr, &target.Texture);
	if (SUCCEEDED(hr))
	{
		hr = GetDevice()->CreateRenderTargetView(target.Texture, nullptr, &target.RTV); // mip 0 を描く
	}
	if (SUCCEEDED(hr))
	{
		hr = GetDevice()->CreateShaderResourceView(target.Texture, nullptr, &target.SRV); // 全ミップを読む
	}
	target.Width = width;
	target.Height = height;
	return hr;
}

// 描画サイズが変わったときだけ、RT0・RT1・深度を作り直す
static void EnsureBloomTargets(UINT width, UINT height)
{
	if (g_SceneTarget.Texture && width == g_TargetWidth && height == g_TargetHeight)
	{
		return;
	}

	ReleaseBloomTargets();
	g_TargetWidth = width;
	g_TargetHeight = height;

	CreateBloomTarget(g_SceneTarget, width, height, false);
	g_GlowCreateHr = CreateBloomTarget(g_GlowTarget, (std::max)(1u, width / 2), (std::max)(1u, height / 2), true);

	D3D11_TEXTURE2D_DESC depthDesc = {};
	depthDesc.Width = width;
	depthDesc.Height = height;
	depthDesc.MipLevels = 1;
	depthDesc.ArraySize = 1;
	depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthDesc.SampleDesc.Count = 1;
	depthDesc.Usage = D3D11_USAGE_DEFAULT;
	depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	GetDevice()->CreateTexture2D(&depthDesc, nullptr, &g_pDepthTexture);
	GetDevice()->CreateDepthStencilView(g_pDepthTexture, nullptr, &g_pDepthView);
}

static void CreateQuadVertexBuffer(void)
{
	const VERTEX_3D vertices[4] = {
		{{-1.0f, -1.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
		{{ 1.0f, -1.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
		{{-1.0f,  1.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
		{{ 1.0f,  1.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}}
	};
	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = sizeof(vertices);
	desc.Usage = D3D11_USAGE_IMMUTABLE;
	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA data = {};
	data.pSysMem = vertices;
	GetDevice()->CreateBuffer(&desc, &data, &g_pQuadVertexBuffer);
}

// 画面全体を覆う1パス。inputs を読み、target へ書く（深度は使わない）
static void DrawFullscreenPass(
	SHADERTYPE shaderType,
	ID3D11ShaderResourceView* const* inputs,
	UINT inputCount,
	ID3D11RenderTargetView* target,
	UINT width,
	UINT height,
	const BLOOM_CONSTANT& constant)
{
	ShaderManager* shader = GetShader(shaderType);
	const bool shaderOk = shader && shader->GetVertexShader() && shader->GetPixelShader() && shader->GetVertexLayout();
	if (shaderType == S_BLOOM_EXTRACT)
	{
		g_ExtractShaderOk = shaderOk;
	}
	if (shaderType == S_BLOOM_COMPOSITE)
	{
		g_CompositeShaderOk = shaderOk;
	}
	if (!shaderOk)
	{
		return;
	}

	ID3D11DeviceContext* context = GetDeviceContext();
	ID3D11ShaderResourceView* nullSRVs[4] = {};
	// 先に全ての書き込み先を外す。書き込み先に割り当て中のテクスチャを SRV に設定すると、
	// D3D11 がその SRV を黙って無効化するため（抽出がシーンを黒く読んでしまう）
	context->OMSetRenderTargets(0, nullptr, nullptr);

	// 読み取り元を外してから書き込み先を設定する（同じテクスチャの同時利用を避ける）
	context->PSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);
	context->UpdateSubresource(g_pBloomConstantBuffer, 0, nullptr, &constant, 0, 0);
	context->PSSetConstantBuffers(10, 1, &g_pBloomConstantBuffer);
	context->PSSetShaderResources(0, inputCount, inputs);
	context->PSSetSamplers(0, 1, &g_pLinearClampSampler);
	context->OMSetRenderTargets(1, &target, nullptr);

	D3D11_VIEWPORT viewport = {
		0.0f, 0.0f,
		static_cast<float>(width), static_cast<float>(height),
		0.0f, 1.0f
	};
	context->RSSetViewports(1, &viewport);

	UINT stride = sizeof(VERTEX_3D);
	UINT offset = 0;
	context->IASetVertexBuffers(0, 1, &g_pQuadVertexBuffer, &stride, &offset);
	context->IASetInputLayout(shader->GetVertexLayout());
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	context->VSSetShader(shader->GetVertexShader(), nullptr, 0);
	context->PSSetShader(shader->GetPixelShader(), nullptr, 0);
	SetBlendState(BLENDSTATE_NONE);
	SetCullState(CULLSTATE_NONE);
	context->Draw(4, 0);
	if (shaderType == S_BLOOM_EXTRACT)
	{
		++g_ExtractDrawCount;
	}
	if (shaderType == S_BLOOM_COMPOSITE)
	{
		++g_CompositeDrawCount;
	}

	// 次のパスで書き込み先に使えるよう、読み取り側を外しておく
	context->PSSetShaderResources(0, inputCount, nullSRVs);
}

// 発光キューブ i の現在位置（リング上で回り、上下に揺れる）
static XMFLOAT3 GetGlowPosition(int index)
{
	const float angle = XMConvertToRadians(g_RingAngle + index * (360.0f / GLOW_COUNT));
	const float bob = std::sin(g_Time * 1.5f + index * 0.8f) * 0.4f;
	return XMFLOAT3(
		RING_CENTER.x + std::cos(angle) * RING_RADIUS,
		0.6f + bob,
		RING_CENTER.z + std::sin(angle) * RING_RADIUS
	);
}

// 発光キューブの位置に色付きの点光源を3つ置き、床と柱を照らす
static void SetGlowLights(void)
{
	static const int LIGHT_SOURCES[NUM_PLAYER_LIGHTS] = { 0, 3, 6 };
	LIGHT lights[NUM_PLAYER_LIGHTS] = {};
	for (int i = 0; i < NUM_PLAYER_LIGHTS; ++i)
	{
		const int source = LIGHT_SOURCES[i];
		const XMFLOAT3 pos = GetGlowPosition(source);
		const XMFLOAT3& color = GLOW_COLORS[source];
		// 最大成分が 1 になるよう正規化する（明るさは PointLightParam.y で決める）
		const float peak = (std::max)({ color.x, color.y, color.z, 1.0f });
		lights[i].Enable = TRUE;
		lights[i].Position = XMFLOAT4(pos.x, pos.y, pos.z, 1.0f);
		lights[i].Diffuse = XMFLOAT4(color.x / peak, color.y / peak, color.z / peak, 1.0f);
		lights[i].PointLightParam = XMFLOAT4(GLOW_LIGHT_RANGE, GLOW_LIGHT_POWER, 0.0f, 0.0f);
	}
	lights[0].Ambient = XMFLOAT4(0.03f, 0.03f, 0.04f, 1.0f);
	SetPlayerLights(lights);
}

void DebugBloomScene_Initialize(void)
{
	g_pFloor = new Sprite3D(
		{ 0.0f, -1.0f, RING_CENTER.z },
		{ 30.0f, 0.2f, 30.0f },
		{ 0.0f, 0.0f, 0.0f },
		"asset\\model\\cube.fbx",
		S_PBR
	);
	g_pFloor->SetColor(0.50f, 0.47f, 0.44f, 1.0f);

	g_pPillar = new Sprite3D(
		{ 0.0f, 0.5f, RING_CENTER.z },
		{ 1.0f, 3.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f },
		"asset\\model\\cube.fbx",
		S_PBR
	);
	g_pPillar->SetColor(0.85f, 0.85f, 0.90f, 1.0f);

	for (int i = 0; i < GLOW_COUNT; ++i)
	{
		g_pGlowCubes[i] = new Sprite3D(
			{ RING_CENTER.x, 0.6f, RING_CENTER.z },
			{ 0.6f, 0.6f, 0.6f },
			{ 0.0f, 0.0f, 0.0f },
			"asset\\model\\cube.fbx",
			S_UNLIT
		);
		const XMFLOAT3& color = GLOW_COLORS[i];
		g_pGlowCubes[i]->SetColor(color.x, color.y, color.z, 1.0f);
	}

	CreateQuadVertexBuffer();

	D3D11_BUFFER_DESC constantDesc = {};
	constantDesc.ByteWidth = sizeof(BLOOM_CONSTANT);
	constantDesc.Usage = D3D11_USAGE_DEFAULT;
	constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	GetDevice()->CreateBuffer(&constantDesc, nullptr, &g_pBloomConstantBuffer);

	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	GetDevice()->CreateSamplerState(&samplerDesc, &g_pLinearClampSampler);

	DebugCamera_Initialize({ 0.0f, 2.5f, -7.0f }, 0.0f, 8.0f);
	if (GetCamera())
	{
		GetCamera()->SetTargetPos({ 0.0f, 0.0f, 5.0f });
		SetCameraPosition(GetCamera()->GetPos());
	}

	UnLockMouse();
}

void DebugBloomScene_Update(void)
{
	DebugCamera_Update();

	if (GetCamera())
	{
		SetCameraPosition(GetCamera()->GetPos());
	}

	g_Time += 1.0f / FPS;
	g_RingAngle += 20.0f / FPS;
	if (g_RingAngle >= 360.0f)
	{
		g_RingAngle -= 360.0f;
	}

	// 発光物体をリング上でゆっくり回し、上下に揺らす
	for (int i = 0; i < GLOW_COUNT; ++i)
	{
		g_pGlowCubes[i]->SetPos(GetGlowPosition(i));
		g_pGlowCubes[i]->SetRot({ 0.0f, g_RingAngle + i * 45.0f, 0.0f });
	}
}

void DebugBloomScene_Draw(void)
{
	ID3D11DeviceContext* context = GetDeviceContext();

	// 現在の出力先（画面、またはスクリーンショット用のRT）を退避し、最後に戻す
	ID3D11RenderTargetView* outputRTV = nullptr;
	ID3D11DepthStencilView* outputDSV = nullptr;
	context->OMGetRenderTargets(1, &outputRTV, &outputDSV);
	if (!outputRTV)
	{
		SAFE_RELEASE(outputDSV);
		return;
	}

	// 3D 用のビューポートから描画サイズを得る
	SetDepthEnable(true);
	D3D11_VIEWPORT viewport = {};
	UINT viewportCount = 1;
	context->RSGetViewports(&viewportCount, &viewport);
	const UINT width = static_cast<UINT>(viewport.Width);
	const UINT height = static_cast<UINT>(viewport.Height);
	if (width == 0 || height == 0)
	{
		SAFE_RELEASE(outputRTV);
		SAFE_RELEASE(outputDSV);
		return;
	}
	EnsureBloomTargets(width, height);

	if (GetCamera())
	{
		SetCameraPosition(GetCamera()->GetPos());
	}

	// パス1: 通常シーンを RT0 へ描く
	ID3D11ShaderResourceView* nullSRVs[4] = {};
	context->PSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);
	ID3D11RenderTargetView* sceneRTV = g_SceneTarget.RTV;
	context->OMSetRenderTargets(1, &sceneRTV, g_pDepthView);
	const float clearColor[4] = { 0.004f, 0.005f, 0.01f, 1.0f };
	context->ClearRenderTargetView(sceneRTV, clearColor);
	context->ClearDepthStencilView(g_pDepthView, D3D11_CLEAR_DEPTH, 1.0f, 0);
	SetBlendState(BLENDSTATE_NONE);
	SetCullState(CULLSTATE_BACK);
	SetDefaultSampler();

	SetGlowLights();
	SetParameter(XMFLOAT4(0.5f, 0.0f, 0.0f, 0.0f));  // 床: 中程度の粗さ、非金属
	g_pFloor->Draw();
	SetParameter(XMFLOAT4(0.2f, 0.9f, 0.0f, 0.0f));  // 柱: 滑らかな金属
	g_pPillar->Draw();
	for (int i = 0; i < GLOW_COUNT; ++i)
	{
		g_pGlowCubes[i]->Draw();
	}

	// パス2: RT0 から明るい部分だけを RT1（ミップマップ付き）へ抜き出す
	ID3D11ShaderResourceView* sceneSRV = g_SceneTarget.SRV;
	ID3D11ShaderResourceView* compositeInputs[2] = { sceneSRV, sceneSRV };
	BLOOM_CONSTANT composite = {};

	if (g_BloomEnabled)
	{
		BLOOM_CONSTANT extract = {};
		extract.Params = XMFLOAT4(g_BloomLuminance, 0.0f, 0.0f, 0.0f);
		DrawFullscreenPass(S_BLOOM_EXTRACT, &sceneSRV, 1, g_GlowTarget.RTV, g_GlowTarget.Width, g_GlowTarget.Height, extract);

		// RT1 を書き込み先から外してから、ミップマップを生成する（書き込み中のテクスチャは使えない）
		context->OMSetRenderTargets(0, nullptr, nullptr);
		context->GenerateMips(g_GlowTarget.SRV);

		compositeInputs[1] = g_GlowTarget.SRV;
		composite.Weights = XMFLOAT4(g_BloomBlend[0], g_BloomBlend[1], g_BloomBlend[2], 0.0f);
	}

	// 確認用の表示: 加算量を 0 にして、選んだ画像だけを出す
	if (g_BloomView != 0)
	{
		composite.Weights = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		if (g_BloomView == 2 && g_BloomEnabled)
		{
			compositeInputs[0] = g_GlowTarget.SRV;
		}
	}

	// パス3: 通常シーン（RT0）に、ミップから読んだぼけた輝度を加算して元の出力先へ書く
	DrawFullscreenPass(S_BLOOM_COMPOSITE, compositeInputs, 2, outputRTV, width, height, composite);

	// 出力先を元に戻す
	SetDefaultSampler();
	context->OMSetRenderTargets(1, &outputRTV, outputDSV);
	SetDepthEnable(false);
	SetCullState(CULLSTATE_BACK);
	SAFE_RELEASE(outputRTV);
	SAFE_RELEASE(outputDSV);

	if (Direct3D_IsTakingScreenshot())
	{
		return;
	}

	ImGui::Begin("Bloom Control Panel");
	ImGui::Checkbox("Bloom", &g_BloomEnabled);
	static const char* const VIEW_NAMES[] = { "Final", "Scene", "Luminance" };
	ImGui::Combo("View", &g_BloomView, VIEW_NAMES, IM_ARRAYSIZE(VIEW_NAMES));
	ImGui::Separator();
	ImGui::SliderFloat("Luminance", &g_BloomLuminance, 0.0f, 1.0f, "%.4f");
	ImGui::SliderFloat("Blend 1", &g_BloomBlend[0], 0.0f, 2.0f, "%.4f");
	ImGui::SliderFloat("Blend 2", &g_BloomBlend[1], 0.0f, 2.0f, "%.4f");
	ImGui::SliderFloat("Blend 3", &g_BloomBlend[2], 0.0f, 2.0f, "%.4f");
	ImGui::Separator();
	if (ImGui::CollapsingHeader("Diagnostics", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("Scene: %u x %u", width, height);
		ImGui::Text("Glow: %u x %u", g_GlowTarget.Width, g_GlowTarget.Height);
		ImGui::Text("Glow create HRESULT: 0x%08X", static_cast<unsigned int>(g_GlowCreateHr));
		ImGui::Text("Extract shader: %s", g_ExtractShaderOk ? "ok" : "NULL");
		ImGui::Text("Composite shader: %s", g_CompositeShaderOk ? "ok" : "NULL");
		ImGui::Text("Extract draws: %d", g_ExtractDrawCount);
		ImGui::Text("Composite draws: %d", g_CompositeDrawCount);
	}
	ImGui::End();
}

void DebugBloomScene_Finalize(void)
{
	delete g_pFloor;
	g_pFloor = nullptr;
	delete g_pPillar;
	g_pPillar = nullptr;
	for (int i = 0; i < GLOW_COUNT; ++i)
	{
		delete g_pGlowCubes[i];
		g_pGlowCubes[i] = nullptr;
	}

	ReleaseBloomTargets();
	SAFE_RELEASE(g_pLinearClampSampler);
	SAFE_RELEASE(g_pBloomConstantBuffer);
	SAFE_RELEASE(g_pQuadVertexBuffer);

	DebugCamera_Finalize();
}
