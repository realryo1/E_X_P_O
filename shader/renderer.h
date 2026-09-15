/*==============================================================================

   レンダリング管理[renderer.h]
														 Author :
														 Date   :
--------------------------------------------------------------------------------

==============================================================================*/
#pragma once

#include "main.h"

//*********************************************************
// 構造体
//*********************************************************

struct Vertex3D
{
	XMFLOAT3 position;
	XMFLOAT3 normal;
	XMFLOAT4 color;
	XMFLOAT2 texCoord;
};

struct VertexSkinned
{
	XMFLOAT3 position;
	XMFLOAT3 normal;
	XMFLOAT4 color;
	XMFLOAT2 texCoord;
	UINT boneIndex[4];
	float boneWeight[4];
};

#define SAFE_RELEASE(p) do { if (p) { (p)->Release(); (p) = nullptr; } } while (0)

// 頂点構造体
struct VERTEX_3D
{
	XMFLOAT3 Position;	//頂点座標　XMFLOAT3 ＝　float x,y,z
	XMFLOAT3 Normal;	//法線ベクトル 
	XMFLOAT4 Diffuse;	//色  XMFLOAT4 = float x,y,z,w
	XMFLOAT2 TexCoord;	//テクスチャ座標 XMFLOAT2 = float x,y
};

// マテリアル構造体
struct MATERIAL
{
	XMFLOAT4	Ambient;	//アンビエント
	XMFLOAT4	Diffuse;	//デフューズ
	XMFLOAT4	Specular;	//スペキュラ
	XMFLOAT4	Emission;	//エミッシブ
	float		Shininess;	//スペキュラパラメータ
	float		Dummy[3];	//16byte境界調整用パディング
};

struct LIGHT
{
	BOOL		Enable;
	BOOL		Dummy[3];//16byte境界用
	XMFLOAT4	Direction;
	XMFLOAT4	Diffuse;
	XMFLOAT4	Ambient;

	XMFLOAT4	Position;
	XMFLOAT4	PointLightParam;
	XMFLOAT4	Angle; // スポットライト: x=コーン半角(rad)
	XMFLOAT4	SkyColor; // 天球色
	XMFLOAT4	GroundColor; // 地面色
	XMFLOAT4	GroundNormal; // 地面法線
};

// 距離・高度フォグの定数。
// Color.w はフォグ密度、Param は start/end/heightMin/heightRange。
struct FOG_CONSTANT
{
	XMFLOAT4 Color;
	XMFLOAT4 Param;
};

struct SSAO_CONSTANT
{
	XMFLOAT4X4 InvProjection;
	XMFLOAT4 Params;
	XMFLOAT4 Settings;
};

// 近距離ほど高密度にサンプリングするカスケード数。
#define NUM_SHADOW_CASCADES 3

// シャドウマップを読むためにシェーダーへ渡す定数。
// LightViewProjection は「ライトから見た世界」に変換する行列。
struct SHADOW_CONSTANT
{
	XMFLOAT4X4 LightViewProjection[NUM_SHADOW_CASCADES];
	XMFLOAT4 Param;	// x:深度のずれ防止 y:影部分の明るさ
	XMFLOAT4 CascadeSplits; // x,y,z:各カスケードの終端距離
	XMFLOAT4 CascadeTexelSize; // x,y,z:各カスケードの1テクセルのUV寸法
};

// トンネルの4面それぞれ用のShadowMap定数。
// 面ごとに別の光方向で影を焼き、受け手は面ごとに対応するスライスを読む。
// さらに Player用/Enemy用でスライスを分け、濃さを個別に設定できるようにする。
#define NUM_SHADOW_FACES 4
#define NUM_SHADOW_SLICES 8  // 4面 × 2種(0-3:Player, 4-7:Enemy)
struct FACE_SHADOW_CONSTANT
{
	XMFLOAT4X4 LightViewProjection[NUM_SHADOW_FACES];
	XMFLOAT4 Param;	 // x:バイアス y:Playerの影の濃さ z,w:1テクセルのUVサイズ
	XMFLOAT4 Param2; // x:Enemyの影の濃さ
};

enum	BLENDSTATE
{
	BLENDSTATE_NONE = 0,	//ブレンドしない
	BLENDSTATE_ALFA,		//普通のαブレンド
	BLENDSTATE_ADD,			//加算合成 
	BLENDSTATE_SUB,			//減算合成

	BLENDSTATE_MAX
};

enum CULLSTATE
{
	CULLSTATE_NONE = 0,
	CULLSTATE_FRONT,
	CULLSTATE_BACK,

	CULLSTATE_MAX
};

void SetBlendState(BLENDSTATE blend);
void SetCullState(CULLSTATE cull);

//*****************************************************************************
// プロトタイプ宣言
//*****************************************************************************
HRESULT InitRenderer(HINSTANCE hInstance, HWND hWnd, BOOL bWindow);
void FinalizeRenderer(void);

void Clear(void);
void Present(void);

ID3D11Device *GetDevice( void );
ID3D11DeviceContext *GetDeviceContext( void );
void SetDefaultSampler(void);

void SetDepthEnable( bool Enable );
void SetDepthWriteEnable( bool Enable );

void SetWorldViewProjection2D(void);
void ResetWorldViewProjection3D(void);


void SetWorldMatrix(XMMATRIX WorldMatrix );
void SetViewMatrix(XMMATRIX ViewMatrix );
void SetProjectionMatrix(XMMATRIX ProjectionMatrix );

void SetCameraPosition(XMFLOAT3 CameraPosition);
void SetFog(FOG_CONSTANT Fog);

void SetParameter(XMFLOAT4 Parameter);
XMFLOAT4 GetParameter(void);
void SetParameterW(float w);

// ShadowMap用の行列と調整値をGPUへ送る。
void SetShadowMatrix(XMMATRIX LightViewProjection, XMFLOAT4 Param);
void SetShadowCascadeMatrices(
	const XMMATRIX LightViewProjection[NUM_SHADOW_CASCADES],
	XMFLOAT4 Param,
	XMFLOAT4 CascadeSplits,
	XMFLOAT4 CascadeTexelSize);

// ここから先の描画を、画面ではなくShadowMapへ書き込む。
void BeginShadowMap(void);
void BeginShadowMapSlice(int slice);

// ShadowMapへの描画を終えて、通常の画面描画へ戻す。
void EndShadowMap(void);

// --- 4面ShadowMap（トンネルの各面用）---
// 4面分のライト行列(既にView*Projection済み)と、Player/Enemyそれぞれの影の濃さをGPUへ送る。
// bias:深度ずれ防止  playerBrightness/enemyBrightness:影の濃さ(0=真っ黒〜1=影なし)
void SetFaceShadowMatrices(const XMMATRIX faceViewProjection[NUM_SHADOW_FACES], float bias, float playerBrightness, float enemyBrightness);
// 指定したスライス(0-3:Player各面, 4-7:Enemy各面)へ描き込み開始。
void BeginFaceShadowMap(int slice);
// 4面ShadowMapへの描画を終えて、通常描画へ戻し、配列を受け手へ読ませる。
void EndFaceShadowMap(void);


void SetMaterial( MATERIAL Material );

void CreateVertexShader(ID3D11VertexShader** VertexShader, ID3D11InputLayout** VertexLayout, const char* FileName);
void CreatePixelShader(ID3D11PixelShader** PixelShader, const char* FileName);

void SetLight(LIGHT Light);

// 3点照明(PBR専用)。キー/フィル/リムの3灯をまとめてb7へ送る。
#define NUM_PLAYER_LIGHTS 3
void SetPlayerLights(const LIGHT lights[NUM_PLAYER_LIGHTS]);

void Direct3D_ResizeWindow(unsigned int clientW, unsigned int clientH);
float Direct3D_GetClientWidth(void);
float Direct3D_GetClientHeight(void);
void Direct3D_Resize(unsigned int width, unsigned int height);
void TakeScreenshot(void);
bool Direct3D_IsTakingScreenshot(void);
void Direct3D_BeginScene(void);
void Direct3D_ApplySsao(void);
void Direct3D_SetSsaoParameters(bool enabled, float intensity, float radius, float bias, float power);
float Direct3D_GetLastGpuFrameMs(void);
bool Direct3D_GetMemoryInfo(unsigned long long* localBudgetMb,
	unsigned long long* localUsageMb,
	unsigned long long* nonLocalBudgetMb,
	unsigned long long* nonLocalUsageMb);

#if defined(_DEBUG)
enum Direct3D_DebugStage
{
	DIRECT3D_DEBUG_STAGE_SHADOW = 0,
	DIRECT3D_DEBUG_STAGE_FIELD,
	DIRECT3D_DEBUG_STAGE_OBJECTS,
	DIRECT3D_DEBUG_STAGE_UI,
	DIRECT3D_DEBUG_STAGE_PUMP,
	DIRECT3D_DEBUG_STAGE_COUNT
};

void Direct3D_DebugResetFrameCounters(void);
void Direct3D_DebugStageBegin(Direct3D_DebugStage stage);
void Direct3D_DebugStageEnd(Direct3D_DebugStage stage);
double Direct3D_DebugGetStageMs(Direct3D_DebugStage stage);
unsigned long long Direct3D_DebugGetMapCount(void);
double Direct3D_DebugGetMapMs(void);
#endif

