/*==============================================================================

   レンダリング管理 [renderer.cpp]
														 Author :
														 Date   :
--------------------------------------------------------------------------------

==============================================================================*/
#include <io.h>
#include "renderer.h"
#include "define.h"
#include <dxgi1_4.h>
#include "../framework/debug_ostream.h"
#include <cstring>
#include <cwchar>
#include <chrono>
#include <string>

#pragma comment(lib, "dxgi.lib")

// スクリーンショットの解像度設定
#define SCREENSHOT_WIDTH  (1920)
#define SCREENSHOT_HEIGHT (1080)

//*********************************************************
// 構造体
//*********************************************************


//*****************************************************************************
// グローバル変数:
//*****************************************************************************
D3D_FEATURE_LEVEL       g_FeatureLevel = D3D_FEATURE_LEVEL_11_0;

ID3D11Device*           g_D3DDevice = NULL;
ID3D11DeviceContext*    g_ImmediateContext = NULL;
IDXGISwapChain*         g_SwapChain = NULL;
static IDXGISwapChain3* g_SwapChain3 = nullptr;
ID3D11RenderTargetView* g_RenderTargetView = NULL;
static ID3D11RenderTargetView* g_RenderTargetViews[2] = {};
static UINT g_CurrentBackBufferIndex = 0;
ID3D11DepthStencilView* g_DepthStencilView = NULL;



ID3D11VertexShader*     g_VertexShader = NULL;
ID3D11PixelShader*      g_PixelShader = NULL;
ID3D11InputLayout*      g_VertexLayout = NULL;

ID3D11Buffer*			g_WorldViewProjection = NULL;

ID3D11Buffer*			g_WorldBuffer = NULL;
ID3D11Buffer*			g_ViewBuffer = NULL;
ID3D11Buffer*			g_ProjectionBuffer = NULL;
ID3D11Buffer*			g_MaterialBuffer = NULL;
ID3D11Buffer*			g_LightBuffer = NULL;
ID3D11Buffer*			g_PlayerLightBuffer = NULL;	// 3点照明(PBR専用)
ID3D11Buffer*			g_CameraBuffer = NULL;
ID3D11Buffer*			g_ParameterBuffer = NULL;
static XMFLOAT4			g_Parameter = XMFLOAT4(1.0f, 0.0f, 0.0f, 0.0f);
ID3D11Buffer*			g_ShadowBuffer = NULL;




XMMATRIX				g_WorldMatrix;
XMMATRIX				g_ViewMatrix;
XMMATRIX				g_ProjectionMatrix;

ID3D11DepthStencilState* g_DepthStateEnable;
ID3D11DepthStencilState* g_DepthStateDisable;
ID3D11DepthStencilState* g_DepthStateEnableNoWrite = nullptr;

static float	bFactor[4] = { 0.0f,0.0f,0.0f,0.0f };
static ID3D11BlendState* bState[BLENDSTATE_MAX];
static ID3D11RasterizerState* rState[CULLSTATE_MAX];
static ID3D11SamplerState* g_DefaultSampler = nullptr;

static XMMATRIX g_LastWorldMatrix = {};
static XMMATRIX g_LastViewMatrix = {};
static XMMATRIX g_LastProjectionMatrix = {};
static MATERIAL g_LastMaterial = {};
static LIGHT g_LastLight = {};
static XMFLOAT4 g_LastCameraPosition = {};
static XMFLOAT4 g_LastParameter = {};
static FOG_CONSTANT g_Fog = {};
static bool g_HasLastWorldMatrix = false;
static bool g_HasLastViewMatrix = false;
static bool g_HasLastProjectionMatrix = false;
static bool g_HasLastMaterial = false;
static bool g_HasLastLight = false;
static bool g_HasLastCameraPosition = false;
static bool g_HasLastParameter = false;
static bool g_HasLastFog = false;

// ShadowMapは、ライトから見た「深度だけの画像」。
// Texture本体、深度書き込み用View、シェーダーで読む用View、読み取り用Samplerを分けて持つ。
// NVIDIAを含むdGPUでのカスケード影の深度描画負荷を抑える。
// カスケード数と影判定は維持し、解像度だけを下げて互換性を保つ。
static const UINT SHADOW_MAP_SIZE = 2048;
static ID3D11Texture2D* g_ShadowMapTexture = NULL;
static ID3D11DepthStencilView* g_ShadowMapDepthViews[NUM_SHADOW_CASCADES] = {};
static ID3D11ShaderResourceView* g_ShadowMapShaderView = NULL;
static ID3D11SamplerState* g_ShadowMapSampler = NULL;

// 面ごとの影(4面: FLOOR/LEFT_WALL/CEILING/RIGHT_WALL)用のShadowMap配列。
// トンネルの各面へ、それぞれの光方向で影を落とすために使う。
static ID3D11Texture2D* g_FaceShadowTexture = NULL;
static ID3D11DepthStencilView* g_FaceShadowDSV[NUM_SHADOW_SLICES] = {};
static ID3D11ShaderResourceView* g_FaceShadowSRV = NULL;
static ID3D11Buffer* g_FaceShadowBuffer = NULL; // b9: 4面分の行列＋濃さ
static ID3D11Buffer* g_FogBuffer = NULL; // b11: 距離・高度フォグ

// ウィンドウクライアントサイズ（ビューポート計算用）
static float g_ClientWidth  = DRAW_SCREEN_X;
static float g_ClientHeight = DRAW_SCREEN_Y;

// バックバッファ情報（リサイズ時に参照）
static D3D11_TEXTURE2D_DESC g_BackBufferDesc;

// 深度ステンシルバッファ（解放用に保持）
static ID3D11Texture2D* g_pDepthStencilBuffer = NULL;

// スクリーンショット撮影用フラグとターゲット
static bool g_IsTakingScreenshot = false;
static ID3D11RenderTargetView* g_SSTargetView = nullptr;
static ID3D11DepthStencilView* g_SSDepthView = nullptr;

#if defined(_DEBUG)
static constexpr int GPU_TIMING_QUERY_COUNT = 8;

struct GpuTimingQuery
{
	ID3D11Query* disjoint = nullptr;
	ID3D11Query* begin = nullptr;
	ID3D11Query* end = nullptr;
	bool submitted = false;
};

static GpuTimingQuery g_GpuTimingQueries[GPU_TIMING_QUERY_COUNT] = {};
static int g_ActiveGpuTimingQuery = -1;
static bool g_GpuTimingAvailable = false;
#endif
static float g_LastGpuFrameMs = -1.0f;

#if defined(_DEBUG)
static unsigned long long g_DebugMapCount = 0;
static double g_DebugMapMs = 0.0;
static std::chrono::steady_clock::time_point
	g_DebugStageStarts[DIRECT3D_DEBUG_STAGE_COUNT] = {};
static bool g_DebugStageActive[DIRECT3D_DEBUG_STAGE_COUNT] = {};
static double g_DebugStageMs[DIRECT3D_DEBUG_STAGE_COUNT] = {};
#endif

namespace
{
	enum class GpuSelectionMode
	{
		Auto,
		HighPerformance
	};

	static bool HasCommandLineOption(const wchar_t* option)
	{
		const wchar_t* commandLine = GetCommandLineW();
		return commandLine && option && wcsstr(commandLine, option) != nullptr;
	}

	static bool AdapterHasDisplayOutput(IDXGIAdapter1* adapter)
	{
		if (!adapter)
		{
			return false;
		}

		for (UINT outputIndex = 0; ; ++outputIndex)
		{
			IDXGIOutput* output = nullptr;
			const HRESULT hr = adapter->EnumOutputs(outputIndex, &output);
			if (hr == DXGI_ERROR_NOT_FOUND)
			{
				break;
			}
			if (SUCCEEDED(hr) && output)
			{
				output->Release();
				return true;
			}
			SAFE_RELEASE(output);
			if (FAILED(hr))
			{
				break;
			}
		}
		return false;
	}

	static std::string AdapterNameForLog(const DXGI_ADAPTER_DESC1& desc)
	{
		std::string name;
		for (int i = 0; i < ARRAYSIZE(desc.Description) && desc.Description[i] != L'\0'; ++i)
		{
			const wchar_t c = desc.Description[i];
			name.push_back(c >= 0 && c <= 0x7f ? static_cast<char>(c) : '?');
		}
		return name;
	}

	static bool UpdateDynamicConstantBuffer(
		ID3D11Buffer* buffer,
		const void* data,
		size_t dataSize)
	{
		if (!g_ImmediateContext || !buffer || !data || dataSize == 0)
		{
			return false;
		}

		D3D11_MAPPED_SUBRESOURCE mapped = {};
#if defined(_DEBUG)
		const auto mapStart = std::chrono::steady_clock::now();
		++g_DebugMapCount;
#endif
		const HRESULT mapResult = g_ImmediateContext->Map(
			buffer,
			0,
			D3D11_MAP_WRITE_DISCARD,
			0,
			&mapped);
		if (FAILED(mapResult))
		{
#if defined(_DEBUG)
			g_DebugMapMs += std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - mapStart).count();
#endif
			return false;
		}

		memcpy(mapped.pData, data, dataSize);
		g_ImmediateContext->Unmap(buffer, 0);
#if defined(_DEBUG)
		g_DebugMapMs += std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - mapStart).count();
#endif
		return true;
	}

#if defined(_DEBUG)
	static void ReleaseGpuTimingQueries(void)
	{
		for (GpuTimingQuery& query : g_GpuTimingQueries)
		{
			SAFE_RELEASE(query.disjoint);
			SAFE_RELEASE(query.begin);
			SAFE_RELEASE(query.end);
			query.submitted = false;
		}
		g_ActiveGpuTimingQuery = -1;
		g_GpuTimingAvailable = false;
	}

	static void InitializeGpuTimingQueries(void)
	{
		ReleaseGpuTimingQueries();
		if (!g_D3DDevice)
		{
			return;
		}

		D3D11_QUERY_DESC disjointDesc = {};
		disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
		D3D11_QUERY_DESC timestampDesc = {};
		timestampDesc.Query = D3D11_QUERY_TIMESTAMP;

		for (GpuTimingQuery& query : g_GpuTimingQueries)
		{
			if (FAILED(g_D3DDevice->CreateQuery(&disjointDesc, &query.disjoint)) ||
				FAILED(g_D3DDevice->CreateQuery(&timestampDesc, &query.begin)) ||
				FAILED(g_D3DDevice->CreateQuery(&timestampDesc, &query.end)))
			{
				ReleaseGpuTimingQueries();
				return;
			}
		}
		g_GpuTimingAvailable = true;
	}

	static void PollGpuTimingQueries(void)
	{
		if (!g_GpuTimingAvailable || !g_ImmediateContext)
		{
			return;
		}

		GpuTimingQuery* query = nullptr;
		for (GpuTimingQuery& candidate : g_GpuTimingQueries)
		{
			if (candidate.submitted)
			{
				query = &candidate;
				break;
			}
		}
		if (!query)
		{
			return;
		}

		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
		const HRESULT disjointResult = g_ImmediateContext->GetData(
			query->disjoint,
			&disjoint,
			sizeof(disjoint),
			D3D11_ASYNC_GETDATA_DONOTFLUSH);
		if (disjointResult == S_FALSE)
		{
			return;
		}
		if (FAILED(disjointResult))
		{
			query->submitted = false;
			return;
		}

		UINT64 beginTimestamp = 0;
		UINT64 endTimestamp = 0;
		const HRESULT beginResult = g_ImmediateContext->GetData(
			query->begin,
			&beginTimestamp,
			sizeof(beginTimestamp),
			D3D11_ASYNC_GETDATA_DONOTFLUSH);
		const HRESULT endResult = g_ImmediateContext->GetData(
			query->end,
			&endTimestamp,
			sizeof(endTimestamp),
			D3D11_ASYNC_GETDATA_DONOTFLUSH);
		if (beginResult == S_FALSE || endResult == S_FALSE)
		{
			return;
		}

		if (SUCCEEDED(beginResult) &&
			SUCCEEDED(endResult) &&
			!disjoint.Disjoint &&
			disjoint.Frequency > 0 &&
			endTimestamp >= beginTimestamp)
		{
			const double elapsedSeconds =
				static_cast<double>(endTimestamp - beginTimestamp) /
				static_cast<double>(disjoint.Frequency);
			g_LastGpuFrameMs = static_cast<float>(elapsedSeconds * 1000.0);
		}
		query->submitted = false;
	}

	static void BeginGpuFrameTiming(void)
	{
		if (!g_GpuTimingAvailable || !g_ImmediateContext)
		{
			return;
		}

		PollGpuTimingQueries();
		g_ActiveGpuTimingQuery = -1;
		for (int i = 0; i < GPU_TIMING_QUERY_COUNT; ++i)
		{
			GpuTimingQuery& query = g_GpuTimingQueries[i];
			if (!query.submitted)
			{
				g_ImmediateContext->Begin(query.disjoint);
				g_ImmediateContext->End(query.begin);
				g_ActiveGpuTimingQuery = i;
				return;
			}
		}
	}

	static void EndGpuFrameTiming(void)
	{
		if (g_ActiveGpuTimingQuery < 0 ||
			g_ActiveGpuTimingQuery >= GPU_TIMING_QUERY_COUNT ||
			!g_ImmediateContext)
		{
			return;
		}

		GpuTimingQuery& query = g_GpuTimingQueries[g_ActiveGpuTimingQuery];
		g_ImmediateContext->End(query.end);
		g_ImmediateContext->End(query.disjoint);
		query.submitted = true;
		g_ActiveGpuTimingQuery = -1;
	}
#endif
}

static void ResetRendererStateCache(void)
{
	g_HasLastWorldMatrix = false;
	g_HasLastViewMatrix = false;
	g_HasLastProjectionMatrix = false;
	g_HasLastMaterial = false;
	g_HasLastLight = false;
	g_HasLastCameraPosition = false;
	g_HasLastParameter = false;
}


// =====================================================
// 内部ヘルパー：バックバッファ/深度バッファ解放
// =====================================================
static void releaseBackBuffer(void)
{
	if (g_ImmediateContext)
		g_ImmediateContext->OMSetRenderTargets(0, NULL, NULL);
	for (ID3D11RenderTargetView*& renderTargetView : g_RenderTargetViews)
	{
		SAFE_RELEASE(renderTargetView);
	}
	g_RenderTargetView = nullptr;
	SAFE_RELEASE(g_pDepthStencilBuffer);
	SAFE_RELEASE(g_DepthStencilView);
}

// =====================================================
// 内部ヘルパー：バックバッファ/深度バッファ生成
// =====================================================
static void configureBackBuffer(void)
{
	// フリップモデルの全バックバッファにRenderTargetViewを作る。
	// Present後に現在のバッファへ切り替えないと、次フレームが表示されない。
	for (UINT i = 0; i < ARRAYSIZE(g_RenderTargetViews); ++i)
	{
		ID3D11Texture2D* pBackBuffer = nullptr;
		if (FAILED(g_SwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer))))
		{
			continue;
		}
		g_D3DDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_RenderTargetViews[i]);
		if (i == 0)
		{
			pBackBuffer->GetDesc(&g_BackBufferDesc);
		}
		pBackBuffer->Release();
	}

	g_CurrentBackBufferIndex = g_SwapChain3
		? g_SwapChain3->GetCurrentBackBufferIndex()
		: 0;
	g_RenderTargetView = g_RenderTargetViews[g_CurrentBackBufferIndex];

	// 深度ステンシルバッファ生成
	D3D11_TEXTURE2D_DESC td;
	ZeroMemory(&td, sizeof(td));
	td.Width              = g_BackBufferDesc.Width;
	td.Height             = g_BackBufferDesc.Height;
	td.MipLevels          = 1;
	td.ArraySize          = 1;
	td.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
	td.SampleDesc.Count   = 1;
	td.SampleDesc.Quality = 0;
	td.Usage              = D3D11_USAGE_DEFAULT;
	td.BindFlags          = D3D11_BIND_DEPTH_STENCIL;
	td.CPUAccessFlags     = 0;
	td.MiscFlags          = 0;
	g_D3DDevice->CreateTexture2D(&td, NULL, &g_pDepthStencilBuffer);

	// 深度ステンシルビュー生成
	D3D11_DEPTH_STENCIL_VIEW_DESC dsvd;
	ZeroMemory(&dsvd, sizeof(dsvd));
	dsvd.Format        = td.Format;
	dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	dsvd.Flags         = 0;
	g_D3DDevice->CreateDepthStencilView(g_pDepthStencilBuffer, &dsvd, &g_DepthStencilView);

	// DirectX へセット
	g_ImmediateContext->OMSetRenderTargets(1, &g_RenderTargetView, g_DepthStencilView);

	// ビューポートをバックバッファ全体に設定
	D3D11_VIEWPORT vp;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	vp.Width    = (FLOAT)g_BackBufferDesc.Width;
	vp.Height   = (FLOAT)g_BackBufferDesc.Height;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	g_ImmediateContext->RSSetViewports(1, &vp);
}

// =====================================================
// 内部ヘルパー：2D ビューポート設定（spec §2.1）
// アスペクト比を保ちレターボックス/ピラーボックスを生成
// =====================================================
static void Direct3D_SetViewport2D(void)
{
	const float targetAspect = DRAW_SCREEN_X / DRAW_SCREEN_Y;
	const float windowAspect = g_ClientWidth / g_ClientHeight;

	float vpW, vpH, vpX = 0.0f, vpY = 0.0f;

	if (windowAspect > targetAspect)
	{
		// 横長: 縦を基準、左右に余白
		vpH = g_ClientHeight;
		vpW = g_ClientHeight * targetAspect;
		vpX = (g_ClientWidth - vpW) * 0.5f;
	}
	else
	{
		// 縦長または同等: 横を基準、上下に余白
		vpW = g_ClientWidth;
		vpH = g_ClientWidth / targetAspect;
		vpY = (g_ClientHeight - vpH) * 0.5f;
	}

	// クライアントサイズとバックバッファサイズの比率差を補正
	const float scaleX = (float)g_BackBufferDesc.Width  / g_ClientWidth;
	const float scaleY = (float)g_BackBufferDesc.Height / g_ClientHeight;

	D3D11_VIEWPORT vp;
	vp.TopLeftX = vpX * scaleX;
	vp.TopLeftY = vpY * scaleY;
	vp.Width    = vpW * scaleX;
	vp.Height   = vpH * scaleY;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	g_ImmediateContext->RSSetViewports(1, &vp);
}

// =====================================================
// 内部ヘルパー：3D ビューポート設定
// 3D は射影行列でアスペクトを調整するため、描画先全体を使用する。
// =====================================================
static void Direct3D_SetViewport3D(void)
{
	D3D11_VIEWPORT vp;
	vp.TopLeftX = 0.0f;
	vp.TopLeftY = 0.0f;
	vp.Width    = static_cast<float>(g_BackBufferDesc.Width);
	vp.Height   = static_cast<float>(g_BackBufferDesc.Height);
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	g_ImmediateContext->RSSetViewports(1, &vp);
}


ID3D11Device* GetDevice( void )
{
	return g_D3DDevice;
}


ID3D11DeviceContext* GetDeviceContext( void )
{
	return g_ImmediateContext;
}

void SetDefaultSampler(void)
{
	if (g_ImmediateContext && g_DefaultSampler)
	{
		g_ImmediateContext->PSSetSamplers(0, 1, &g_DefaultSampler);
	}
}


void SetDepthEnable( bool Enable )
{
	if( Enable )
	{
		g_ImmediateContext->OMSetDepthStencilState( g_DepthStateEnable, NULL );
		Direct3D_SetViewport3D();
	}
	else
	{
		g_ImmediateContext->OMSetDepthStencilState( g_DepthStateDisable, NULL );
		Direct3D_SetViewport2D();
	}
}

void SetDepthWriteEnable( bool Enable )
{
	if( Enable )
	{
		g_ImmediateContext->OMSetDepthStencilState( g_DepthStateEnable, NULL );
	}
	else
	{
		g_ImmediateContext->OMSetDepthStencilState( g_DepthStateEnableNoWrite, NULL );
	}
}

void ResetWorldViewProjection3D(void)
{
	//行列を単位行列にして初期化
	g_ProjectionMatrix = XMMatrixIdentity();
	g_ViewMatrix = XMMatrixIdentity();
	g_WorldMatrix = XMMatrixIdentity();
}

void SetWorldViewProjection2D( void )
{
	//2D用正射影行列をセット
	g_ProjectionMatrix = XMMatrixOrthographicOffCenterLH(0.0f, SCREEN_X, SCREEN_Y, 0.0f, 0.0f, 1.0f);
	SetProjectionMatrix(g_ProjectionMatrix);
	//行列を単位行列にして初期化
	g_ViewMatrix = XMMatrixIdentity();
	SetViewMatrix(g_ViewMatrix);
	g_WorldMatrix = XMMatrixIdentity();
	SetWorldMatrix(g_WorldMatrix);

}


void SetWorldMatrix( XMMATRIX WorldMatrix )
{
	if (g_HasLastWorldMatrix &&
		std::memcmp(&g_LastWorldMatrix, &WorldMatrix, sizeof(XMMATRIX)) == 0)
	{
		return;
	}
	XMMATRIX world;
	world = XMMatrixTranspose(WorldMatrix);
	XMFLOAT4X4 matrix;
	XMStoreFloat4x4(&matrix, world);
	UpdateDynamicConstantBuffer(g_WorldBuffer, &matrix, sizeof(matrix));
	g_LastWorldMatrix = WorldMatrix;
	g_HasLastWorldMatrix = true;
}

void SetViewMatrix( XMMATRIX ViewMatrix )
{
	if (g_HasLastViewMatrix &&
		std::memcmp(&g_LastViewMatrix, &ViewMatrix, sizeof(XMMATRIX)) == 0)
	{
		return;
	}
	XMMATRIX view;
	view = XMMatrixTranspose(ViewMatrix);
	XMFLOAT4X4 matrix;
	XMStoreFloat4x4(&matrix, view);
	UpdateDynamicConstantBuffer(g_ViewBuffer, &matrix, sizeof(matrix));
	g_LastViewMatrix = ViewMatrix;
	g_HasLastViewMatrix = true;
}

void SetProjectionMatrix( XMMATRIX ProjectionMatrix )
{
	if (g_HasLastProjectionMatrix &&
		std::memcmp(&g_LastProjectionMatrix, &ProjectionMatrix, sizeof(XMMATRIX)) == 0)
	{
		return;
	}
	XMMATRIX projection;
	projection = XMMatrixTranspose(ProjectionMatrix);
	XMFLOAT4X4 matrix;
	XMStoreFloat4x4(&matrix, projection);
	UpdateDynamicConstantBuffer(g_ProjectionBuffer, &matrix, sizeof(matrix));
	g_LastProjectionMatrix = ProjectionMatrix;
	g_HasLastProjectionMatrix = true;
}



void SetMaterial( MATERIAL Material )
{
	if (g_HasLastMaterial &&
		std::memcmp(&g_LastMaterial, &Material, sizeof(Material)) == 0)
	{
		return;
	}
	UpdateDynamicConstantBuffer(g_MaterialBuffer, &Material, sizeof(Material));
	g_LastMaterial = Material;
	g_HasLastMaterial = true;
}

void SetCameraPosition(XMFLOAT3 CameraPosition)
{
	XMFLOAT4	temp = XMFLOAT4(CameraPosition.x, CameraPosition.y, CameraPosition.z, 0.0f);
	if (g_HasLastCameraPosition &&
		std::memcmp(&g_LastCameraPosition, &temp, sizeof(temp)) == 0)
	{
		return;
	}
	UpdateDynamicConstantBuffer(g_CameraBuffer, &temp, sizeof(temp));
	g_LastCameraPosition = temp;
	g_HasLastCameraPosition = true;
}

void SetFog(FOG_CONSTANT Fog)
{
	if (g_HasLastFog &&
		std::memcmp(&g_Fog, &Fog, sizeof(Fog)) == 0)
	{
		return;
	}
	g_Fog = Fog;
	UpdateDynamicConstantBuffer(g_FogBuffer, &g_Fog, sizeof(g_Fog));
	g_HasLastFog = true;
}

void SetParameter(XMFLOAT4 Parameter)
{
	g_Parameter = Parameter;
	if (g_HasLastParameter &&
		std::memcmp(&g_LastParameter, &Parameter, sizeof(Parameter)) == 0)
	{
		return;
	}
	UpdateDynamicConstantBuffer(g_ParameterBuffer, &g_Parameter, sizeof(g_Parameter));
	g_LastParameter = Parameter;
	g_HasLastParameter = true;
}

XMFLOAT4 GetParameter(void)
{
	return g_Parameter;
}

void SetParameterW(float w)
{
	g_Parameter.w = w;
	if (g_HasLastParameter &&
		g_LastParameter.w == g_Parameter.w &&
		g_LastParameter.x == g_Parameter.x &&
		g_LastParameter.y == g_Parameter.y &&
		g_LastParameter.z == g_Parameter.z)
	{
		return;
	}
	if (g_ParameterBuffer)
	{
		UpdateDynamicConstantBuffer(g_ParameterBuffer, &g_Parameter, sizeof(g_Parameter));
	}
	g_LastParameter = g_Parameter;
	g_HasLastParameter = true;
}

void SetShadowMatrix(XMMATRIX LightViewProjection, XMFLOAT4 Param)
{
	if (!g_ShadowBuffer) return;

	SHADOW_CONSTANT shadow = {};
	const XMMATRIX transposed = XMMatrixTranspose(LightViewProjection);
	for (int i = 0; i < NUM_SHADOW_CASCADES; ++i)
	{
		// HLSL側でmulしやすいように転置してからGPUへ送る。
		XMStoreFloat4x4(&shadow.LightViewProjection[i], transposed);
	}
	shadow.Param = Param;
	shadow.CascadeSplits = XMFLOAT4(1.0e30f, 1.0e30f, 1.0e30f, 0.0f);
	shadow.CascadeTexelSize = XMFLOAT4(
		1.0f / static_cast<float>(SHADOW_MAP_SIZE),
		1.0f / static_cast<float>(SHADOW_MAP_SIZE),
		1.0f / static_cast<float>(SHADOW_MAP_SIZE),
		0.0f);

	UpdateDynamicConstantBuffer(g_ShadowBuffer, &shadow, sizeof(shadow));
}

void SetShadowCascadeMatrices(
	const XMMATRIX LightViewProjection[NUM_SHADOW_CASCADES],
	XMFLOAT4 Param,
	XMFLOAT4 CascadeSplits,
	XMFLOAT4 CascadeTexelSize)
{
	if (!g_ShadowBuffer || !LightViewProjection)
	{
		return;
	}

	SHADOW_CONSTANT shadow = {};
	for (int i = 0; i < NUM_SHADOW_CASCADES; ++i)
	{
		XMStoreFloat4x4(
			&shadow.LightViewProjection[i],
			XMMatrixTranspose(LightViewProjection[i]));
	}
	shadow.Param = Param;
	shadow.CascadeSplits = CascadeSplits;
	shadow.CascadeTexelSize = CascadeTexelSize;
	UpdateDynamicConstantBuffer(g_ShadowBuffer, &shadow, sizeof(shadow));
}

void BeginShadowMap(void)
{
	BeginShadowMapSlice(0);
}

void BeginShadowMapSlice(int slice)
{
	if (slice < 0 || slice >= NUM_SHADOW_CASCADES ||
		!g_ShadowMapDepthViews[slice])
	{
		return;
	}

	// 同じShadowMapを「読みながら書く」とDirectXで不正になるので、先に読み取りを外す。
	ID3D11ShaderResourceView* nullSRV = NULL;
	g_ImmediateContext->PSSetShaderResources(1, 1, &nullSRV);

	// RenderTargetViewを外し、深度だけを書くShadowMap用DepthViewへ切り替える。
	SetDepthEnable(true);
	g_ImmediateContext->OMSetRenderTargets(0, NULL, g_ShadowMapDepthViews[slice]);
	g_ImmediateContext->ClearDepthStencilView(
		g_ShadowMapDepthViews[slice],
		D3D11_CLEAR_DEPTH,
		1.0f,
		0);

	// ShadowMapは画面サイズではなく、専用の固定サイズで描く。
	D3D11_VIEWPORT vp;
	vp.TopLeftX = 0.0f;
	vp.TopLeftY = 0.0f;
	vp.Width = (FLOAT)SHADOW_MAP_SIZE;
	vp.Height = (FLOAT)SHADOW_MAP_SIZE;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	g_ImmediateContext->RSSetViewports(1, &vp);
}

void EndShadowMap(void)
{
	// ShadowMapへの描画を終えたので、通常の画面描画用RenderTargetへ戻す。
	if (g_IsTakingScreenshot)
	{
		g_ImmediateContext->OMSetRenderTargets(1, &g_SSTargetView, g_SSDepthView);
	}
	else
	{
		g_ImmediateContext->OMSetRenderTargets(1, &g_RenderTargetView, g_DepthStencilView);
	}
	SetDepthEnable(true);

	// 以降のピクセルシェーダーがShadowMapを読めるように、t1/s1へセットする。
	g_ImmediateContext->PSSetShaderResources(1, 1, &g_ShadowMapShaderView);
	g_ImmediateContext->PSSetSamplers(1, 1, &g_ShadowMapSampler);
}

// --- 4面ShadowMap ---
void SetFaceShadowMatrices(const XMMATRIX faceViewProjection[NUM_SHADOW_FACES], float bias, float playerBrightness, float enemyBrightness)
{
	if (!g_FaceShadowBuffer) return;

	FACE_SHADOW_CONSTANT c = {};
	for (int i = 0; i < NUM_SHADOW_FACES; i++)
	{
		// HLSL側でmulしやすいよう転置して送る。
		XMStoreFloat4x4(&c.LightViewProjection[i], XMMatrixTranspose(faceViewProjection[i]));
	}
	c.Param = XMFLOAT4(
		bias,
		playerBrightness,
		1.0f / static_cast<float>(SHADOW_MAP_SIZE),
		1.0f / static_cast<float>(SHADOW_MAP_SIZE));
	c.Param2 = XMFLOAT4(enemyBrightness, 0.0f, 0.0f, 0.0f);

	UpdateDynamicConstantBuffer(g_FaceShadowBuffer, &c, sizeof(c));
}

void BeginFaceShadowMap(int slice)
{
	if (slice < 0 || slice >= NUM_SHADOW_SLICES) return;

	// 読みながら書くのを避けるため、配列SRV(t6)を一旦外す。
	ID3D11ShaderResourceView* nullSRV = NULL;
	g_ImmediateContext->PSSetShaderResources(6, 1, &nullSRV);

	SetDepthEnable(true);
	g_ImmediateContext->OMSetRenderTargets(0, NULL, g_FaceShadowDSV[slice]);
	g_ImmediateContext->ClearDepthStencilView(g_FaceShadowDSV[slice], D3D11_CLEAR_DEPTH, 1.0f, 0);

	D3D11_VIEWPORT vp;
	vp.TopLeftX = 0.0f;
	vp.TopLeftY = 0.0f;
	vp.Width = (FLOAT)SHADOW_MAP_SIZE;
	vp.Height = (FLOAT)SHADOW_MAP_SIZE;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	g_ImmediateContext->RSSetViewports(1, &vp);
}

void EndFaceShadowMap(void)
{
	// 通常の画面描画用RenderTargetへ戻す（SetDepthEnableでビューポートも3Dへ復帰）。
	if (g_IsTakingScreenshot)
	{
		g_ImmediateContext->OMSetRenderTargets(1, &g_SSTargetView, g_SSDepthView);
	}
	else
	{
		g_ImmediateContext->OMSetRenderTargets(1, &g_RenderTargetView, g_DepthStencilView);
	}
	SetDepthEnable(true);

	// 受け手が4面ShadowMap配列を読めるように、t6へセット（サンプラーはs1を流用）。
	g_ImmediateContext->PSSetShaderResources(6, 1, &g_FaceShadowSRV);
	g_ImmediateContext->PSSetSamplers(1, 1, &g_ShadowMapSampler);
}


void SetBlendState(BLENDSTATE blend)
{

	g_ImmediateContext->OMSetBlendState(bState[blend], bFactor, 0xffffffff);

}

void SetCullState(CULLSTATE cull)
{
	if (cull < 0 || cull >= CULLSTATE_MAX) return;
	g_ImmediateContext->RSSetState(rState[cull]);
}

//=============================================================================
// 初期化処理
//=============================================================================
HRESULT InitRenderer(HINSTANCE hInstance, HWND hWnd, BOOL bWindow)
{
	HRESULT hr = S_OK;
	ResetRendererStateCache();

	// デバイス、フリップモデルのスワップチェーン、コンテキスト生成
	// スワップチェーンをウィンドウの実際のクライアント領域サイズで生成（ネイティブ解像度描画）
	RECT clientRect;
	GetClientRect(hWnd, &clientRect);
	UINT initClientW = (UINT)(clientRect.right  - clientRect.left);
	UINT initClientH = (UINT)(clientRect.bottom - clientRect.top);
	if (initClientW == 0) initClientW = (UINT)SCREEN_X;
	if (initClientH == 0) initClientH = (UINT)SCREEN_Y;
	IDXGIFactory2* dxgiFactory = nullptr;
	hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory));
	if (FAILED(hr))
	{
		return hr;
	}

	const GpuSelectionMode gpuSelectionMode =
		HasCommandLineOption(L"--gpu=high")
		? GpuSelectionMode::HighPerformance
		: GpuSelectionMode::Auto;

	IDXGIAdapter1* selectedAdapter = nullptr;
	SIZE_T selectedMemory = 0;
	bool selectedHasDisplayOutput = false;
	DXGI_ADAPTER_DESC1 selectedAdapterDesc = {};
	for (UINT index = 0; ; ++index)
	{
		IDXGIAdapter1* adapter = nullptr;
		if (dxgiFactory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND)
		{
			break;
		}
		if (!adapter)
		{
			continue;
		}

		DXGI_ADAPTER_DESC1 desc = {};
		adapter->GetDesc1(&desc);
		if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
		{
			adapter->Release();
			continue;
		}

		const bool hasDisplayOutput = AdapterHasDisplayOutput(adapter);
		bool shouldSelect = selectedAdapter == nullptr;
		if (!shouldSelect)
		{
			// autoでは表示出力を持つGPUを優先し、同条件なら専用VRAM量で選ぶ。
			// highでは従来どおり専用VRAM量だけで選ぶ。
			if (gpuSelectionMode == GpuSelectionMode::Auto &&
				hasDisplayOutput != selectedHasDisplayOutput)
			{
				shouldSelect = hasDisplayOutput;
			}
			else
			{
				shouldSelect = desc.DedicatedVideoMemory >= selectedMemory;
			}
		}

		if (shouldSelect)
		{
			SAFE_RELEASE(selectedAdapter);
			selectedAdapter = adapter;
			selectedMemory = desc.DedicatedVideoMemory;
			selectedHasDisplayOutput = hasDisplayOutput;
			selectedAdapterDesc = desc;
		}
		else
		{
			adapter->Release();
		}
	}

	if (selectedAdapter)
	{
		hal::dout
			<< "[Renderer] GPU mode: "
			<< (gpuSelectionMode == GpuSelectionMode::HighPerformance ? "high" : "auto")
			<< " | Adapter: " << AdapterNameForLog(selectedAdapterDesc)
			<< " | Display output: " << (selectedHasDisplayOutput ? "yes" : "no")
			<< " | Dedicated VRAM: "
			<< static_cast<unsigned long long>(
				selectedMemory / (1024 * 1024))
			<< " MB" << std::endl;
	}

	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0
	};
	hr = D3D11CreateDevice(
		selectedAdapter,
		selectedAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		0,
		featureLevels,
		ARRAYSIZE(featureLevels),
		D3D11_SDK_VERSION,
		&g_D3DDevice,
		&g_FeatureLevel,
		&g_ImmediateContext);
	if (SUCCEEDED(hr))
	{
		DXGI_SWAP_CHAIN_DESC1 swapDesc = {};
		swapDesc.Width = initClientW;
		swapDesc.Height = initClientH;
		swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapDesc.SampleDesc.Count = 1;
		swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapDesc.BufferCount = 2;
		swapDesc.Scaling = DXGI_SCALING_NONE;
		swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

		IDXGISwapChain1* swapChain1 = nullptr;
		hr = dxgiFactory->CreateSwapChainForHwnd(
			g_D3DDevice,
			hWnd,
			&swapDesc,
			nullptr,
			nullptr,
			&swapChain1);
		if (FAILED(hr) && swapDesc.Scaling == DXGI_SCALING_NONE)
		{
			swapDesc.Scaling = DXGI_SCALING_STRETCH;
			hr = dxgiFactory->CreateSwapChainForHwnd(
				g_D3DDevice,
				hWnd,
				&swapDesc,
				nullptr,
				nullptr,
				&swapChain1);
		}
		if (SUCCEEDED(hr))
		{
			hr = swapChain1->QueryInterface(IID_PPV_ARGS(&g_SwapChain));
			if (SUCCEEDED(hr))
			{
				g_SwapChain->QueryInterface(IID_PPV_ARGS(&g_SwapChain3));
			}
			swapChain1->Release();
		}
	}

	SAFE_RELEASE(selectedAdapter);
	dxgiFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);
	dxgiFactory->Release();

	if( FAILED( hr ) )
	{
		SAFE_RELEASE(g_ImmediateContext);
		SAFE_RELEASE(g_D3DDevice);
		SAFE_RELEASE(g_SwapChain);
		return hr;
	}

	configureBackBuffer();
#if defined(_DEBUG)
	InitializeGpuTimingQueries();
#endif

	// ラスタライザステート設定
	D3D11_RASTERIZER_DESC rd;
	ZeroMemory(&rd, sizeof(rd));
	rd.FillMode = D3D11_FILL_SOLID;
	rd.DepthClipEnable = TRUE;
	rd.MultisampleEnable = FALSE;

	D3D11_CULL_MODE cullMode[CULLSTATE_MAX] = {
		D3D11_CULL_NONE,
		D3D11_CULL_FRONT,
		D3D11_CULL_BACK
	};
	for (int i = 0; i < CULLSTATE_MAX; i++)
	{
		rd.CullMode = cullMode[i];
		g_D3DDevice->CreateRasterizerState(&rd, &rState[i]);
	}
	SetCullState(CULLSTATE_NONE);


	// ブレンドステート設定
	D3D11_BLEND_DESC blendDesc;
	ZeroMemory( &blendDesc, sizeof( blendDesc ) );
	blendDesc.AlphaToCoverageEnable = FALSE;
	blendDesc.IndependentBlendEnable = FALSE;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	// BLENDSTATE_NONE: ブレンドなし
	blendDesc.RenderTarget[0].BlendEnable = FALSE;
	g_D3DDevice->CreateBlendState( &blendDesc, &bState[BLENDSTATE_NONE] );

	// BLENDSTATE_ALFA: 通常αブレンド
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend       = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp        = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
	g_D3DDevice->CreateBlendState( &blendDesc, &bState[BLENDSTATE_ALFA] );

	// BLENDSTATE_ADD: 加算合成
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOp   = D3D11_BLEND_OP_ADD;
	g_D3DDevice->CreateBlendState( &blendDesc, &bState[BLENDSTATE_ADD] );

	// BLENDSTATE_SUB: 減算合成
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_REV_SUBTRACT;
	g_D3DDevice->CreateBlendState( &blendDesc, &bState[BLENDSTATE_SUB] );

	// 初期ブレンドステートはαブレンド
	g_ImmediateContext->OMSetBlendState( bState[BLENDSTATE_ALFA], bFactor, 0xffffffff );


	// 深度ステンシルステート設定
	D3D11_DEPTH_STENCIL_DESC depthStencilDesc;
	ZeroMemory( &depthStencilDesc, sizeof( depthStencilDesc ) );
	depthStencilDesc.DepthEnable = TRUE;
	depthStencilDesc.DepthWriteMask	= D3D11_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;
	depthStencilDesc.StencilEnable = FALSE;
	g_D3DDevice->CreateDepthStencilState( &depthStencilDesc, &g_DepthStateEnable );//深度有効ステート

	depthStencilDesc.DepthEnable = FALSE;
	depthStencilDesc.DepthWriteMask	= D3D11_DEPTH_WRITE_MASK_ZERO;
	g_D3DDevice->CreateDepthStencilState( &depthStencilDesc, &g_DepthStateDisable );//深度無効ステート

	depthStencilDesc.DepthEnable = TRUE;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	g_D3DDevice->CreateDepthStencilState( &depthStencilDesc, &g_DepthStateEnableNoWrite );//深度有効・書き込み無効ステート

	//深度テスト有効にしておく
	g_ImmediateContext->OMSetDepthStencilState( g_DepthStateEnable, NULL );

	// サンプラーステート設定
	D3D11_SAMPLER_DESC samplerDesc;
	ZeroMemory( &samplerDesc, sizeof( samplerDesc ) );
	samplerDesc.Filter = D3D11_FILTER_ANISOTROPIC;//ちょっといいフィルターにする
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;//横の座標範囲外は画像繰り返し
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;//縦の座標範囲外は画像繰り返し
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;//未使用
	samplerDesc.MipLODBias = 0;
	samplerDesc.MaxAnisotropy = 16;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	hr = g_D3DDevice->CreateSamplerState(&samplerDesc, &g_DefaultSampler);
	if (FAILED(hr))
	{
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.MaxAnisotropy = 1;
		hr = g_D3DDevice->CreateSamplerState(&samplerDesc, &g_DefaultSampler);
	}
	if (FAILED(hr) || !g_DefaultSampler)
	{
		return FAILED(hr) ? hr : E_FAIL;
	}
	//サンプラーをシェーダーへセット
	SetDefaultSampler();

	// ShadowMap本体。深度として書き込み、あとでテクスチャとして読むためTYPELESSで作る。
	D3D11_TEXTURE2D_DESC shadowTextureDesc;
	ZeroMemory(&shadowTextureDesc, sizeof(shadowTextureDesc));
	shadowTextureDesc.Width = SHADOW_MAP_SIZE;
	shadowTextureDesc.Height = SHADOW_MAP_SIZE;
	shadowTextureDesc.MipLevels = 1;
	shadowTextureDesc.ArraySize = NUM_SHADOW_CASCADES;
	shadowTextureDesc.Format = DXGI_FORMAT_R16_TYPELESS;
	shadowTextureDesc.SampleDesc.Count = 1;
	shadowTextureDesc.SampleDesc.Quality = 0;
	shadowTextureDesc.Usage = D3D11_USAGE_DEFAULT;
	shadowTextureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	g_D3DDevice->CreateTexture2D(&shadowTextureDesc, NULL, &g_ShadowMapTexture);

	// ShadowMapへ深度を書き込むためのView。
	D3D11_DEPTH_STENCIL_VIEW_DESC shadowDepthDesc;
	ZeroMemory(&shadowDepthDesc, sizeof(shadowDepthDesc));
	shadowDepthDesc.Format = DXGI_FORMAT_D16_UNORM;
	shadowDepthDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
	shadowDepthDesc.Texture2DArray.MipSlice = 0;
	shadowDepthDesc.Texture2DArray.ArraySize = 1;
	for (UINT i = 0; i < NUM_SHADOW_CASCADES; ++i)
	{
		shadowDepthDesc.Texture2DArray.FirstArraySlice = i;
		g_D3DDevice->CreateDepthStencilView(
			g_ShadowMapTexture,
			&shadowDepthDesc,
			&g_ShadowMapDepthViews[i]);
	}

	// ShadowMapをピクセルシェーダーで読むためのView。
	D3D11_SHADER_RESOURCE_VIEW_DESC shadowResourceDesc;
	ZeroMemory(&shadowResourceDesc, sizeof(shadowResourceDesc));
	shadowResourceDesc.Format = DXGI_FORMAT_R16_UNORM;
	shadowResourceDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	shadowResourceDesc.Texture2DArray.MostDetailedMip = 0;
	shadowResourceDesc.Texture2DArray.MipLevels = 1;
	shadowResourceDesc.Texture2DArray.FirstArraySlice = 0;
	shadowResourceDesc.Texture2DArray.ArraySize = NUM_SHADOW_CASCADES;
	g_D3DDevice->CreateShaderResourceView(g_ShadowMapTexture, &shadowResourceDesc, &g_ShadowMapShaderView);

	// ShadowMapを読むときのサンプラー。範囲外は影なし扱いにしやすいようBorderを白にする。
	D3D11_SAMPLER_DESC shadowSamplerDesc;
	ZeroMemory(&shadowSamplerDesc, sizeof(shadowSamplerDesc));
	shadowSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	shadowSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
	shadowSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
	shadowSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
	shadowSamplerDesc.BorderColor[0] = 1.0f;
	shadowSamplerDesc.BorderColor[1] = 1.0f;
	shadowSamplerDesc.BorderColor[2] = 1.0f;
	shadowSamplerDesc.BorderColor[3] = 1.0f;
	shadowSamplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	shadowSamplerDesc.MinLOD = 0.0f;
	shadowSamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	g_D3DDevice->CreateSamplerState(&shadowSamplerDesc, &g_ShadowMapSampler);
	g_ImmediateContext->PSSetSamplers(1, 1, &g_ShadowMapSampler);

	// --- 4面ShadowMap（Texture2DArray 4スライス）---
	{
		D3D11_TEXTURE2D_DESC td;
		ZeroMemory(&td, sizeof(td));
		td.Width = SHADOW_MAP_SIZE;
		td.Height = SHADOW_MAP_SIZE;
		td.MipLevels = 1;
		td.ArraySize = NUM_SHADOW_SLICES;
		td.Format = DXGI_FORMAT_R16_TYPELESS;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		g_D3DDevice->CreateTexture2D(&td, NULL, &g_FaceShadowTexture);

		// スライスごとの深度書き込みView
		for (int i = 0; i < NUM_SHADOW_SLICES; i++)
		{
			D3D11_DEPTH_STENCIL_VIEW_DESC dsv;
			ZeroMemory(&dsv, sizeof(dsv));
			dsv.Format = DXGI_FORMAT_D16_UNORM;
			dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
			dsv.Texture2DArray.FirstArraySlice = i;
			dsv.Texture2DArray.ArraySize = 1;
			g_D3DDevice->CreateDepthStencilView(g_FaceShadowTexture, &dsv, &g_FaceShadowDSV[i]);
		}

		// 配列全体を読むためのSRV
		D3D11_SHADER_RESOURCE_VIEW_DESC srv;
		ZeroMemory(&srv, sizeof(srv));
		srv.Format = DXGI_FORMAT_R16_UNORM;
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		srv.Texture2DArray.MostDetailedMip = 0;
		srv.Texture2DArray.MipLevels = 1;
		srv.Texture2DArray.FirstArraySlice = 0;
		srv.Texture2DArray.ArraySize = NUM_SHADOW_SLICES;
		g_D3DDevice->CreateShaderResourceView(g_FaceShadowTexture, &srv, &g_FaceShadowSRV);

		// 4面分の行列を送る定数バッファ b9
		D3D11_BUFFER_DESC bd;
		ZeroMemory(&bd, sizeof(bd));
		bd.ByteWidth = sizeof(FACE_SHADOW_CONSTANT);
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		g_D3DDevice->CreateBuffer(&bd, NULL, &g_FaceShadowBuffer);
		g_ImmediateContext->VSSetConstantBuffers(9, 1, &g_FaceShadowBuffer);
		g_ImmediateContext->PSSetConstantBuffers(9, 1, &g_FaceShadowBuffer);
	}

	//定数バッファ生成

	//================================================
	// WorldViewProjection行列用定数バッファ生成
	D3D11_BUFFER_DESC hBufferDesc = {};
	hBufferDesc.ByteWidth = sizeof(XMMATRIX);
	hBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	hBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	hBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	hBufferDesc.MiscFlags = 0;
	hBufferDesc.StructureByteStride = 0;
	//行列オブジェクトをシェーダーへ接続　b0をつかう
	g_D3DDevice->CreateBuffer(&hBufferDesc, NULL, &g_WorldBuffer);
	g_ImmediateContext->VSSetConstantBuffers(0, 1, &g_WorldBuffer);

	g_D3DDevice->CreateBuffer(&hBufferDesc, NULL, &g_ViewBuffer);
	g_ImmediateContext->VSSetConstantBuffers(1, 1, &g_ViewBuffer);
	g_ImmediateContext->PSSetConstantBuffers(1, 1, &g_ViewBuffer);

	g_D3DDevice->CreateBuffer(&hBufferDesc, NULL, &g_ProjectionBuffer);
	g_ImmediateContext->VSSetConstantBuffers(2, 1, &g_ProjectionBuffer);

	//マテリアル用定数バッファ生成
	hBufferDesc.ByteWidth = sizeof(MATERIAL);
	//マテリアルオブジェクトをシェーダーへ接続　b1を使う
	g_D3DDevice->CreateBuffer( &hBufferDesc, NULL, &g_MaterialBuffer );
	g_ImmediateContext->VSSetConstantBuffers( 3, 1, &g_MaterialBuffer );
	g_ImmediateContext->PSSetConstantBuffers( 3, 1, &g_MaterialBuffer );

	hBufferDesc.ByteWidth = sizeof(LIGHT);

	g_D3DDevice->CreateBuffer(&hBufferDesc, NULL, &g_LightBuffer);
	g_ImmediateContext->VSSetConstantBuffers(4, 1, &g_LightBuffer);
	g_ImmediateContext->PSSetConstantBuffers(4, 1, &g_LightBuffer);

	// 3点照明(PBR専用)用定数バッファ生成 b7を使う
	hBufferDesc.ByteWidth = sizeof(LIGHT) * NUM_PLAYER_LIGHTS;
	g_D3DDevice->CreateBuffer(&hBufferDesc, NULL, &g_PlayerLightBuffer);
	g_ImmediateContext->PSSetConstantBuffers(7, 1, &g_PlayerLightBuffer);
	// 既定は全て無効（PBRは単一ライトへフォールバックする）
	{
		LIGHT initLights[NUM_PLAYER_LIGHTS] = {};
		SetPlayerLights(initLights);
	}

	hBufferDesc.ByteWidth = sizeof(XMFLOAT4);
	g_D3DDevice->CreateBuffer(&hBufferDesc, NULL, &g_CameraBuffer);
	g_ImmediateContext->PSSetConstantBuffers(5, 1, &g_CameraBuffer);

	g_D3DDevice->CreateBuffer(&hBufferDesc, NULL, &g_ParameterBuffer);
	g_ImmediateContext->VSSetConstantBuffers(6, 1, &g_ParameterBuffer);
	g_ImmediateContext->PSSetConstantBuffers(6, 1, &g_ParameterBuffer);
	g_Parameter = XMFLOAT4(1.0f, 0.0f, 0.0f, 0.0f);
	SetParameter(g_Parameter);

	// LightViewProjectionなど、ShadowMap判定に必要な値を入れる定数バッファ。
	hBufferDesc.ByteWidth = sizeof(SHADOW_CONSTANT);
	g_D3DDevice->CreateBuffer(&hBufferDesc, NULL, &g_ShadowBuffer);
	g_ImmediateContext->VSSetConstantBuffers(8, 1, &g_ShadowBuffer);
	g_ImmediateContext->PSSetConstantBuffers(8, 1, &g_ShadowBuffer);
	SetShadowMatrix(XMMatrixIdentity(), XMFLOAT4(0.003f, 0.55f, 0.0f, 0.0f));

	// 距離・高度フォグ用定数バッファ b11
	hBufferDesc.ByteWidth = sizeof(FOG_CONSTANT);
	g_D3DDevice->CreateBuffer(&hBufferDesc, NULL, &g_FogBuffer);
	g_ImmediateContext->VSSetConstantBuffers(11, 1, &g_FogBuffer);
	g_ImmediateContext->PSSetConstantBuffers(11, 1, &g_FogBuffer);
	SetFog(FOG_CONSTANT{
		XMFLOAT4(0.70f, 0.78f, 0.90f, 1.0f),
		XMFLOAT4(80.0f, 500.0f, 0.0f, 60.0f)
	});

	MATERIAL material;
	ZeroMemory(&material, sizeof(material));
	material.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	material.Ambient = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	SetMaterial(material);


	return S_OK;
}


//=============================================================================
// 終了処理
//=============================================================================
void FinalizeRenderer(void)
{
	// オブジェクト解放
	if(g_WorldViewProjection)	g_WorldViewProjection->Release();
	if( g_MaterialBuffer )		g_MaterialBuffer->Release();
	if( g_VertexLayout )		g_VertexLayout->Release();
	if( g_VertexShader )		g_VertexShader->Release();
	if( g_PixelShader )			g_PixelShader->Release();
	SAFE_RELEASE(g_ShadowBuffer);
	SAFE_RELEASE(g_PlayerLightBuffer);
	SAFE_RELEASE(g_FaceShadowBuffer);
	SAFE_RELEASE(g_FogBuffer);
	SAFE_RELEASE(g_FaceShadowSRV);
	for (int i = 0; i < NUM_SHADOW_SLICES; i++) SAFE_RELEASE(g_FaceShadowDSV[i]);
	SAFE_RELEASE(g_FaceShadowTexture);
	SAFE_RELEASE(g_DepthStateEnableNoWrite);
	SAFE_RELEASE(g_ShadowMapSampler);
	SAFE_RELEASE(g_DefaultSampler);
	SAFE_RELEASE(g_ShadowMapShaderView);
	for (int i = 0; i < NUM_SHADOW_CASCADES; ++i)
	{
		SAFE_RELEASE(g_ShadowMapDepthViews[i]);
	}
	SAFE_RELEASE(g_ShadowMapTexture);
	for (int i = 0; i < CULLSTATE_MAX; i++)
	{
		SAFE_RELEASE(rState[i]);
	}

#if defined(_DEBUG)
	ReleaseGpuTimingQueries();
#endif
	if( g_ImmediateContext )	g_ImmediateContext->ClearState();
	releaseBackBuffer();
	SAFE_RELEASE(g_SwapChain3);
	if( g_SwapChain )			g_SwapChain->Release();
	if( g_ImmediateContext )	g_ImmediateContext->Release();
	if( g_D3DDevice )			g_D3DDevice->Release();
}


//=============================================================================
// バックバッファクリア
//=============================================================================
void Clear(void)
{
#if defined(_DEBUG)
	BeginGpuFrameTiming();
#endif
	// バックバッファクリア色
	float ClearColor[4] = { 181.0f / 255.0f, 200.0f / 255.0f, 211.0f / 255.0f, 1.0f }; // #B5C8D3
	//バックバッファをクリア
	g_ImmediateContext->ClearRenderTargetView( g_RenderTargetView, ClearColor );
	//デプスステンシルバッファをクリア
	g_ImmediateContext->ClearDepthStencilView( g_DepthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);

	// 前フレームのImGuiや特殊描画で変更された共通ステートを、3D描画の既定値へ戻す。
	SetCullState(CULLSTATE_NONE);
	SetBlendState(BLENDSTATE_ALFA);

	// 前フレームのImGui描画などで上書きされた共通バインドを復元する。
	g_ImmediateContext->VSSetConstantBuffers(0, 1, &g_WorldBuffer);
	g_ImmediateContext->VSSetConstantBuffers(1, 1, &g_ViewBuffer);
	g_ImmediateContext->VSSetConstantBuffers(2, 1, &g_ProjectionBuffer);
	g_ImmediateContext->PSSetConstantBuffers(1, 1, &g_ViewBuffer);
	g_ImmediateContext->VSSetConstantBuffers(3, 1, &g_MaterialBuffer);
	g_ImmediateContext->PSSetConstantBuffers(3, 1, &g_MaterialBuffer);
	g_ImmediateContext->VSSetConstantBuffers(4, 1, &g_LightBuffer);
	g_ImmediateContext->PSSetConstantBuffers(4, 1, &g_LightBuffer);
	g_ImmediateContext->PSSetConstantBuffers(5, 1, &g_CameraBuffer);
	g_ImmediateContext->VSSetConstantBuffers(6, 1, &g_ParameterBuffer);
	g_ImmediateContext->PSSetConstantBuffers(6, 1, &g_ParameterBuffer);
	g_ImmediateContext->PSSetConstantBuffers(7, 1, &g_PlayerLightBuffer);
	g_ImmediateContext->VSSetConstantBuffers(8, 1, &g_ShadowBuffer);
	g_ImmediateContext->PSSetConstantBuffers(8, 1, &g_ShadowBuffer);
	g_ImmediateContext->VSSetConstantBuffers(9, 1, &g_FaceShadowBuffer);
	g_ImmediateContext->PSSetConstantBuffers(9, 1, &g_FaceShadowBuffer);
	g_ImmediateContext->VSSetConstantBuffers(11, 1, &g_FogBuffer);
	g_ImmediateContext->PSSetConstantBuffers(11, 1, &g_FogBuffer);
	SetDefaultSampler();
}


//=============================================================================
// プレゼント
//=============================================================================
void Present(void)
{
	if (!g_SwapChain)
	{
		return;
	}

#if defined(_DEBUG)
	EndGpuFrameTiming();
#endif
	const HRESULT presentResult = g_SwapChain->Present(1, 0);
	if (SUCCEEDED(presentResult))
	{
		if (g_SwapChain3)
		{
			g_CurrentBackBufferIndex = g_SwapChain3->GetCurrentBackBufferIndex();
		}
		else
		{
			g_CurrentBackBufferIndex = (g_CurrentBackBufferIndex + 1) % ARRAYSIZE(g_RenderTargetViews);
		}
		g_RenderTargetView = g_RenderTargetViews[g_CurrentBackBufferIndex];
		g_ImmediateContext->OMSetRenderTargets(
			1,
			&g_RenderTargetView,
			g_DepthStencilView);
	}
}


// 頂点シェーダ生成
void CreateVertexShader(ID3D11VertexShader** VertexShader, ID3D11InputLayout** VertexLayout, const char* FileName)
{

	FILE* file;
	long int fsize;

	fopen_s(&file, FileName, "rb");
	if (file == NULL)
	{
		MessageBoxA(NULL, FileName, "Shader File Not Found (VS)", MB_OK);
		return;
	}
	fsize = _filelength(_fileno(file));
	unsigned char* buffer = new unsigned char[fsize];
	fread(buffer, fsize, 1, file);
	fclose(file);

	g_D3DDevice->CreateVertexShader(buffer, fsize, NULL, VertexShader);

	// 入力レイアウト生成
	D3D11_INPUT_ELEMENT_DESC layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 4 * 3, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 4 * 6, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 4 * 10, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};
	UINT numElements = ARRAYSIZE(layout);

	g_D3DDevice->CreateInputLayout(layout,
		numElements,
		buffer,
		fsize,
		VertexLayout);

	delete[] buffer;
}



// ピクセルシェーダ生成
void CreatePixelShader(ID3D11PixelShader** PixelShader, const char* FileName)
{
	FILE* file;
	long int fsize;

	fopen_s(&file, FileName, "rb");
	if (file == NULL)
	{
		MessageBoxA(NULL, FileName, "Shader File Not Found (PS)", MB_OK);
		return;
	}
	fsize = _filelength(_fileno(file));
	unsigned char* buffer = new unsigned char[fsize];
	fread(buffer, fsize, 1, file);
	fclose(file);

	g_D3DDevice->CreatePixelShader(buffer, fsize, NULL, PixelShader);

	delete[] buffer;
}

void SetLight(LIGHT Light)
{
	if (g_HasLastLight &&
		std::memcmp(&g_LastLight, &Light, sizeof(Light)) == 0)
	{
		return;
	}
	UpdateDynamicConstantBuffer(g_LightBuffer, &Light, sizeof(Light));
	g_LastLight = Light;
	g_HasLastLight = true;
}

// 3点照明(キー/フィル/リム)をまとめてPBRシェーダー(b7)へ送る。
void SetPlayerLights(const LIGHT lights[NUM_PLAYER_LIGHTS])
{
	UpdateDynamicConstantBuffer(
		g_PlayerLightBuffer,
		lights,
		sizeof(LIGHT) * NUM_PLAYER_LIGHTS);
}

//=============================================================================
// ウィンドウクライアントサイズ通知（D3D リソースは変更しない）
//=============================================================================
void Direct3D_ResizeWindow(unsigned int clientW, unsigned int clientH)
{
	g_ClientWidth  = (clientW  > 0) ? (float)clientW  : 1.0f;
	g_ClientHeight = (clientH > 0) ? (float)clientH : 1.0f;
}

float Direct3D_GetClientWidth(void)
{
	return g_ClientWidth;
}

float Direct3D_GetClientHeight(void)
{
	return g_ClientHeight;
}

//=============================================================================
// バックバッファ/深度バッファの再構築（ウィンドウリサイズ時に明示的に呼ぶ）
//=============================================================================
void Direct3D_Resize(unsigned int width, unsigned int height)
{
	if (g_SwapChain == NULL || g_D3DDevice == NULL) return;
	if (width == 0 || height == 0) return;

	releaseBackBuffer();
	g_SwapChain->ResizeBuffers(2, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
	configureBackBuffer();
}

#include <direct.h>
#include <time.h>
#include <wincodec.h>
#include "DirectXTex.h"

void TakeScreenshot(void)
{
	// screenshotフォルダを作成（存在しない場合のみ作成される）
	_mkdir("screenshot");

	// 現在の時刻を取得してファイル名を決定
	time_t t = time(nullptr);
	struct tm tm_local;
	localtime_s(&tm_local, &t);
	wchar_t fileName[256];
	swprintf_s(fileName, L"screenshot/screenshot_%04d%02d%02d_%02d%02d%02d.png",
		tm_local.tm_year + 1900, tm_local.tm_mon + 1, tm_local.tm_mday,
		tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec);

	// SCREENSHOT_WIDTH x SCREENSHOT_HEIGHT のテクスチャ（レンダーターゲット用）を作成
	D3D11_TEXTURE2D_DESC td;
	ZeroMemory(&td, sizeof(td));
	td.Width = SCREENSHOT_WIDTH;
	td.Height = SCREENSHOT_HEIGHT;
	td.MipLevels = 1;
	td.ArraySize = 1;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1;
	td.SampleDesc.Quality = 0;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	td.CPUAccessFlags = 0;
	td.MiscFlags = 0;

	ID3D11Texture2D* pSSTexture = nullptr;
	HRESULT hr = g_D3DDevice->CreateTexture2D(&td, nullptr, &pSSTexture);
	if (FAILED(hr)) return;

	ID3D11RenderTargetView* pSSRTView = nullptr;
	hr = g_D3DDevice->CreateRenderTargetView(pSSTexture, nullptr, &pSSRTView);
	if (FAILED(hr))
	{
		SAFE_RELEASE(pSSTexture);
		return;
	}

	// SCREENSHOT_WIDTH x SCREENSHOT_HEIGHT の深度ステンシルバッファを作成
	D3D11_TEXTURE2D_DESC depthDesc;
	ZeroMemory(&depthDesc, sizeof(depthDesc));
	depthDesc.Width = SCREENSHOT_WIDTH;
	depthDesc.Height = SCREENSHOT_HEIGHT;
	depthDesc.MipLevels = 1;
	depthDesc.ArraySize = 1;
	depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthDesc.SampleDesc.Count = 1;
	depthDesc.SampleDesc.Quality = 0;
	depthDesc.Usage = D3D11_USAGE_DEFAULT;
	depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	depthDesc.CPUAccessFlags = 0;
	depthDesc.MiscFlags = 0;

	ID3D11Texture2D* pSSDepthBuffer = nullptr;
	hr = g_D3DDevice->CreateTexture2D(&depthDesc, nullptr, &pSSDepthBuffer);
	if (FAILED(hr))
	{
		SAFE_RELEASE(pSSRTView);
		SAFE_RELEASE(pSSTexture);
		return;
	}

	ID3D11DepthStencilView* pSSDSView = nullptr;
	hr = g_D3DDevice->CreateDepthStencilView(pSSDepthBuffer, nullptr, &pSSDSView);
	if (FAILED(hr))
	{
		SAFE_RELEASE(pSSDepthBuffer);
		SAFE_RELEASE(pSSRTView);
		SAFE_RELEASE(pSSTexture);
		return;
	}

	// 現在のレンダーターゲット、深度バッファ、ビューポート、クライアントサイズを保存
	ID3D11RenderTargetView* pPrevRTView = nullptr;
	ID3D11DepthStencilView* pPrevDSView = nullptr;
	g_ImmediateContext->OMGetRenderTargets(1, &pPrevRTView, &pPrevDSView);

	UINT numViewports = 1;
	D3D11_VIEWPORT prevViewport;
	g_ImmediateContext->RSGetViewports(&numViewports, &prevViewport);

	float prevClientWidth = g_ClientWidth;
	float prevClientHeight = g_ClientHeight;
	D3D11_TEXTURE2D_DESC prevBackBufferDesc = g_BackBufferDesc;

	// SCREENSHOT_WIDTH x SCREENSHOT_HEIGHT に解像度を変更し、バックバッファ記述も書き換える
	g_ClientWidth = static_cast<float>(SCREENSHOT_WIDTH);
	g_ClientHeight = static_cast<float>(SCREENSHOT_HEIGHT);
	g_BackBufferDesc.Width = SCREENSHOT_WIDTH;
	g_BackBufferDesc.Height = SCREENSHOT_HEIGHT;

	// スクリーンショット撮影中フラグとターゲットの設定
	g_IsTakingScreenshot = true;
	g_SSTargetView = pSSRTView;
	g_SSDepthView = pSSDSView;

	g_ImmediateContext->OMSetRenderTargets(1, &pSSRTView, pSSDSView);

	D3D11_VIEWPORT vp;
	vp.TopLeftX = 0.0f;
	vp.TopLeftY = 0.0f;
	vp.Width = static_cast<float>(SCREENSHOT_WIDTH);
	vp.Height = static_cast<float>(SCREENSHOT_HEIGHT);
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	g_ImmediateContext->RSSetViewports(1, &vp);

	// レンダーターゲットと深度バッファをクリア (背景色をゲーム本来の色に統一)
	float clearColor[4] = { 181.0f / 255.0f, 200.0f / 255.0f, 211.0f / 255.0f, 1.0f }; // #B5C8D3
	g_ImmediateContext->ClearRenderTargetView(pSSRTView, clearColor);
	g_ImmediateContext->ClearDepthStencilView(pSSDSView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	// 2D投影行列を 1920x1080 に適応させる
	SetWorldViewProjection2D();

	// Update() 中の再描画なので ImGui::NewFrame() の外。Draw() 側は
	// Direct3D_IsTakingScreenshot() で ImGui ウィジェットを省略する。
	extern void Draw(void);
	Draw();

	SetDepthEnable(false);
	extern void Fade_Draw(void);
	Fade_Draw();

	// GPUの描画コマンド完了を保証するためFlushを呼ぶ
	g_ImmediateContext->Flush();

	// DirectXTex を使用して PNG としてキャプチャ＆保存
	DirectX::ScratchImage scratchImage;
	hr = DirectX::CaptureTexture(g_D3DDevice, g_ImmediateContext, pSSTexture, scratchImage);
	if (SUCCEEDED(hr))
	{
		const DirectX::Image* img = scratchImage.GetImage(0, 0, 0);
		if (img && img->pixels)
		{
			// ゲームはsRGB空間で描画しており、WICのフォーマット変換（RGBA→BGR24bpp）が
			// 内部でリニア→sRGBのガンマ補正を誤適用して白っぽくなってしまう。
			// 変換なし（RGBA32bppのまま）で保存することでガンマ変換をスキップし、
			// アルファのみ直接0xFFに書き換えて透過を防ぐ。
			uint8_t* pPixels = img->pixels;
			const size_t rowPitch = img->rowPitch;
			for (size_t y = 0; y < img->height; ++y)
			{
				uint8_t* pRow = pPixels + y * rowPitch;
				for (size_t x = 0; x < img->width; ++x)
				{
					pRow[x * 4 + 3] = 0xFF; // アルファを不透明に強制
				}
			}
			DirectX::SaveToWICFile(
				*img,
				DirectX::WIC_FLAGS_FORCE_SRGB,  // sRGBメタデータを埋め込みビューワーの誤ガンマ補正を防ぐ
				GUID_ContainerFormatPng,
				fileName
				// ターゲットフォーマット指定なし → RGBA32bppのままでガンマ変換が起きない
			);
		}
	}

	// 状態を復元する
	g_IsTakingScreenshot = false;
	g_SSTargetView = nullptr;
	g_SSDepthView = nullptr;

	g_ClientWidth = prevClientWidth;
	g_ClientHeight = prevClientHeight;
	g_BackBufferDesc = prevBackBufferDesc;

	g_ImmediateContext->OMSetRenderTargets(1, &pPrevRTView, pPrevDSView);
	g_ImmediateContext->RSSetViewports(1, &prevViewport);

	// リソースを解放
	SAFE_RELEASE(pPrevRTView);
	SAFE_RELEASE(pPrevDSView);
	SAFE_RELEASE(pSSDSView);
	SAFE_RELEASE(pSSDepthBuffer);
	SAFE_RELEASE(pSSRTView);
	SAFE_RELEASE(pSSTexture);
}

bool Direct3D_IsTakingScreenshot(void)
{
	return g_IsTakingScreenshot;
}

float Direct3D_GetLastGpuFrameMs(void)
{
	return g_LastGpuFrameMs;
}

#if defined(_DEBUG)
void Direct3D_DebugResetFrameCounters(void)
{
	g_DebugMapCount = 0;
	g_DebugMapMs = 0.0;
	for (int i = 0; i < DIRECT3D_DEBUG_STAGE_COUNT; ++i)
	{
		g_DebugStageActive[i] = false;
		g_DebugStageMs[i] = 0.0;
	}
}

void Direct3D_DebugStageBegin(Direct3D_DebugStage stage)
{
	if (stage < 0 || stage >= DIRECT3D_DEBUG_STAGE_COUNT)
	{
		return;
	}
	g_DebugStageStarts[stage] = std::chrono::steady_clock::now();
	g_DebugStageActive[stage] = true;
}

void Direct3D_DebugStageEnd(Direct3D_DebugStage stage)
{
	if (stage < 0 ||
		stage >= DIRECT3D_DEBUG_STAGE_COUNT ||
		!g_DebugStageActive[stage])
	{
		return;
	}
	g_DebugStageMs[stage] += std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - g_DebugStageStarts[stage]).count();
	g_DebugStageActive[stage] = false;
}

double Direct3D_DebugGetStageMs(Direct3D_DebugStage stage)
{
	if (stage < 0 || stage >= DIRECT3D_DEBUG_STAGE_COUNT)
	{
		return 0.0;
	}
	return g_DebugStageMs[stage];
}

unsigned long long Direct3D_DebugGetMapCount(void)
{
	return g_DebugMapCount;
}

double Direct3D_DebugGetMapMs(void)
{
	return g_DebugMapMs;
}
#endif

bool Direct3D_GetMemoryInfo(
	unsigned long long* localBudgetMb,
	unsigned long long* localUsageMb,
	unsigned long long* nonLocalBudgetMb,
	unsigned long long* nonLocalUsageMb)
{
	if (localBudgetMb) *localBudgetMb = 0;
	if (localUsageMb) *localUsageMb = 0;
	if (nonLocalBudgetMb) *nonLocalBudgetMb = 0;
	if (nonLocalUsageMb) *nonLocalUsageMb = 0;
	if (!g_D3DDevice)
	{
		return false;
	}
	IDXGIDevice* dxgiDevice = nullptr;
	IDXGIAdapter* adapter = nullptr;
	IDXGIAdapter3* adapter3 = nullptr;
	bool ok = false;
	if (SUCCEEDED(g_D3DDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) &&
		SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
		SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&adapter3))))
	{
		DXGI_QUERY_VIDEO_MEMORY_INFO local = {};
		DXGI_QUERY_VIDEO_MEMORY_INFO nonLocal = {};
		if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local)) &&
			SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocal)))
		{
			if (localBudgetMb) *localBudgetMb = local.Budget / (1024 * 1024);
			if (localUsageMb) *localUsageMb = local.CurrentUsage / (1024 * 1024);
			if (nonLocalBudgetMb) *nonLocalBudgetMb = nonLocal.Budget / (1024 * 1024);
			if (nonLocalUsageMb) *nonLocalUsageMb = nonLocal.CurrentUsage / (1024 * 1024);
			ok = true;
		}
	}
	SAFE_RELEASE(adapter3);
	SAFE_RELEASE(adapter);
	SAFE_RELEASE(dxgiDevice);
	return ok;
}


