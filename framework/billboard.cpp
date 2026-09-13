#include "billboard.h"
#include "renderer.h"
#include "texture.h"
#include "shadermanager.h"

#include "Camera.h"
#include <vector>


Billboard::Billboard()
	: m_Pos(0, 0, 0), m_Size(1, 1), m_Rot(0, 0, 0), m_IsDoubleSided(false),
	m_Texture(nullptr), m_NormalTexture(nullptr), m_VertexBuffer(nullptr), m_VertexCount(0),
	m_IgnoreLighting(true),
	m_UVScale(1.0f, 1.0f),
	m_UVAnimEnabled(false), m_UVFrameCount(1), m_UVCurrentFrame(0),
	m_UVInterval(0.4f), m_UVTimer(0.0f),
	m_IsBillboardMode(true),
	m_WallFadeEnabled(true),
	m_ReceiveShadow(false),
	m_BlendMode(BLENDSTATE_ALFA)
{
	m_Color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
}

Billboard::Billboard(XMFLOAT3 pos, XMFLOAT2 size, XMFLOAT3 rot, const char* texturePath, bool isDoubleSided)
	: m_Pos(pos), m_Size(size), m_Rot(rot), m_IsDoubleSided(isDoubleSided),
	m_Texture(nullptr), m_NormalTexture(nullptr), m_VertexBuffer(nullptr), m_VertexCount(0),
	m_IgnoreLighting(true),
	m_UVScale(1.0f, 1.0f),
	m_UVAnimEnabled(false), m_UVFrameCount(1), m_UVCurrentFrame(0),
	m_UVInterval(0.4f), m_UVTimer(0.0f),
	m_IsBillboardMode(true),
	m_WallFadeEnabled(true),
	m_ReceiveShadow(false),
	m_BlendMode(BLENDSTATE_ALFA)
{
	m_Color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	CreateBuffer();
	if (texturePath)
	{
		SetTexture(texturePath);
	}
}

Billboard::~Billboard()
{
	SAFE_RELEASE(m_VertexBuffer);
	m_Texture = nullptr;
	m_NormalTexture = nullptr;
}

// 初期化
void Billboard::Initialize(XMFLOAT3 pos, XMFLOAT2 size, XMFLOAT3 rot, const char* texturePath, bool isDoubleSided)
{
	m_Pos = pos;
	m_Size = size;
	m_Rot = rot;
	m_IsDoubleSided = isDoubleSided;
	m_Color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	m_IgnoreLighting = true;
	m_UVScale = XMFLOAT2(1.0f, 1.0f);
	m_UVAnimEnabled = false;
	m_UVFrameCount = 1;
	m_UVCurrentFrame = 0;
	m_UVInterval = 0.4f;
	m_UVTimer = 0.0f;
	m_IsBillboardMode = true;
	m_WallFadeEnabled = true;
	m_ReceiveShadow = false;
	m_BlendMode = BLENDSTATE_ALFA;
	m_NormalTexture = nullptr;
	m_NormalTexturePath.clear();

	// バッファ作成 (m_Color 設定後に呼ぶ)
	CreateBuffer();
	SetTexture(texturePath);
}

void Billboard::SetTexture(const char* texturePath)
{
	if (texturePath)
	{
		std::string strPath(texturePath);
		// 同じパスが既にロード済みなら再ロードしない
		if (m_Texture && m_TexturePath == strPath) return;
		m_Texture = nullptr;
		m_TexturePath = strPath;
		std::wstring wstrPath(strPath.begin(), strPath.end());
		m_Texture = LoadTexture(wstrPath.c_str());
	}
	else
	{
		m_TexturePath.clear();
		m_Texture = nullptr;
	}
}

void Billboard::SetNormalMap(const char* texturePath)
{
	if (texturePath)
	{
		std::string strPath(texturePath);
		// 同じNormalMapを設定済みなら読み直さない。
		if (m_NormalTexture && m_NormalTexturePath == strPath) return;
		m_NormalTexture = nullptr;
		m_NormalTexturePath = strPath;
		std::wstring wstrPath(strPath.begin(), strPath.end());

		// NormalMapは色ではなく法線データなので、線形テクスチャとして読み込む。
		m_NormalTexture = LoadTextureLinear(wstrPath.c_str());
	}
	else
	{
		m_NormalTexturePath.clear();
		m_NormalTexture = nullptr;
	}
}

void Billboard::SetUVAnimation(int frameCount, float interval)
{
	m_UVAnimEnabled = true;
	m_UVFrameCount = (frameCount > 0) ? frameCount : 1;
	m_UVInterval = (interval > 0.0f) ? interval : 0.4f;
	m_UVCurrentFrame = 0;
	m_UVTimer = 0.0f;
	// 初期フレームのUVでバッファを更新
	float uMin = (float)m_UVCurrentFrame / (float)m_UVFrameCount;
	float uMax = (float)(m_UVCurrentFrame + 1) / (float)m_UVFrameCount;
	CreateBufferWithUV(uMin, uMax);
}

void Billboard::DisableUVAnimation()
{
	m_UVAnimEnabled = false;
	m_UVCurrentFrame = 0;
	m_UVTimer = 0.0f;
	CreateBuffer();
}

void Billboard::CreateBuffer(void)
{
	CreateBufferWithUV(0.0f, 1.0f, 0.0f, 1.0f);
}

void Billboard::CreateBufferWithUV(float uMin, float uMax)
{
	CreateBufferWithUV(uMin, uMax, 0.0f, 1.0f);
}

void Billboard::CreateBufferWithUV(float uMin, float uMax, float vMin, float vMax)
{
	if (m_VertexBuffer) { m_VertexBuffer->Release(); m_VertexBuffer = nullptr; }

	// サイズは Draw 時のスケール行列で適用する（単位クアッド）
	float w = 0.5f;
	float h = 0.5f;
	std::vector<BILLBOARD_VERTEX> vList;

	// 頂点カラーにメンバ変数 m_Color を設定（アルファブレンドを効かせるため）
	XMFLOAT4 vertexColor = m_Color;

	float u0 = uMin * m_UVScale.x;
	float u1 = uMax * m_UVScale.x;
	float v0 = vMin * m_UVScale.y;
	float v1 = vMax * m_UVScale.y;

	// 表面
	vList.push_back({ { -w,  h, 0.0f }, { 0.0f, 0.0f, -1.0f }, vertexColor, { u0, v0 } });
	vList.push_back({ {  w,  h, 0.0f }, { 0.0f, 0.0f, -1.0f }, vertexColor, { u1, v0 } });
	vList.push_back({ { -w, -h, 0.0f }, { 0.0f, 0.0f, -1.0f }, vertexColor, { u0, v1 } });
	vList.push_back({ { -w, -h, 0.0f }, { 0.0f, 0.0f, -1.0f }, vertexColor, { u0, v1 } });
	vList.push_back({ {  w,  h, 0.0f }, { 0.0f, 0.0f, -1.0f }, vertexColor, { u1, v0 } });
	vList.push_back({ {  w, -h, 0.0f }, { 0.0f, 0.0f, -1.0f }, vertexColor, { u1, v1 } });

	// 裏面
	if (m_IsDoubleSided)
	{
		vList.push_back({ {  w,  h, 0.0f }, { 0.0f, 0.0f, 1.0f }, vertexColor, { u1, v0 } });
		vList.push_back({ { -w,  h, 0.0f }, { 0.0f, 0.0f, 1.0f }, vertexColor, { u0, v0 } });
		vList.push_back({ {  w, -h, 0.0f }, { 0.0f, 0.0f, 1.0f }, vertexColor, { u1, v1 } });
		vList.push_back({ {  w, -h, 0.0f }, { 0.0f, 0.0f, 1.0f }, vertexColor, { u1, v1 } });
		vList.push_back({ { -w,  h, 0.0f }, { 0.0f, 0.0f, 1.0f }, vertexColor, { u0, v0 } });
		vList.push_back({ { -w, -h, 0.0f }, { 0.0f, 0.0f, 1.0f }, vertexColor, { u0, v1 } });
	}

	m_VertexCount = (int)vList.size();

	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(BILLBOARD_VERTEX) * m_VertexCount;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bd.CPUAccessFlags = 0;

	D3D11_SUBRESOURCE_DATA InitData;
	ZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = vList.data();

	GetDevice()->CreateBuffer(&bd, &InitData, &m_VertexBuffer);
}

void Billboard::Update(void)
{
	if (!m_UVAnimEnabled || m_UVFrameCount <= 1) return;

	m_UVTimer += 1.0f / 60.0f;
	if (m_UVTimer >= m_UVInterval)
	{
		m_UVTimer -= m_UVInterval;
		m_UVCurrentFrame = (m_UVCurrentFrame + 1) % m_UVFrameCount;

		float uMin = (float)m_UVCurrentFrame / (float)m_UVFrameCount;
		float uMax = (float)(m_UVCurrentFrame + 1) / (float)m_UVFrameCount;
		CreateBufferWithUV(uMin, uMax);
	}
}

void Billboard::Draw(void)
{
	if (!m_VertexBuffer || !m_Texture) return;

	SetBlendState(m_BlendMode);
	if (!m_WallFadeEnabled)
	{
		SetDepthEnable(true);
		// 不透明は Depth 書き込みで遮蔽。半透明はテストのみ（書き込み無効）。
		SetDepthWriteEnable(m_BlendMode == BLENDSTATE_NONE);
	}
	else
	{
		SetDepthEnable(false);
	}

	XMMATRIX view = GetCamera()->GetView();
	XMMATRIX proj = GetCamera()->GetProjection();

	XMMATRIX matScale = XMMatrixScaling(m_Size.x, m_Size.y, 1.0f);
	XMMATRIX matRot = XMMatrixRotationRollPitchYaw(
		XMConvertToRadians(m_Rot.x),
		XMConvertToRadians(m_Rot.y),
		XMConvertToRadians(m_Rot.z)
	);
	XMMATRIX matTrans = XMMatrixTranslation(m_Pos.x, m_Pos.y, m_Pos.z);

	XMMATRIX world;
	if (m_IsBillboardMode)
	{
		// カメラ追従ビルボード
		XMVECTOR det;
		XMMATRIX invView = XMMatrixInverse(&det, view);
		invView.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
		world = matScale * matRot * invView * matTrans;
	}
	else
	{
		// 固定板ポリゴン（m_Rot.y で向きを固定）
		world = matScale * matRot * matTrans;
	}

	SetWorldMatrix(world);
	SetViewMatrix(view);
	SetProjectionMatrix(proj);

	ID3D11DeviceContext* context = GetDeviceContext();

	// 床など影を受けるBillboardだけ、ShadowReceive系シェーダーに切り替える。
	// NormalMapがある場合は、影 + NormalMapライト計算ができる専用シェーダーを使う。
	// 通常のBillboardは今まで通りUnlitTextureで描く。
	bool useNormalMap = m_ReceiveShadow && m_NormalTexture;
	SHADERTYPE shaderType = useNormalMap ? S_NORMAL_MAP_SHADOW_RECEIVE : (m_ReceiveShadow ? S_SHADOW_RECEIVE : S_UNLIT);
	context->IASetInputLayout(GetShader(shaderType)->GetVertexLayout());
	context->VSSetShader(GetShader(shaderType)->GetVertexShader(), NULL, 0);
	context->PSSetShader(GetShader(shaderType)->GetPixelShader(), NULL, 0);

	UINT stride = sizeof(BILLBOARD_VERTEX);
	UINT offset = 0;
	context->IASetVertexBuffers(0, 1, &m_VertexBuffer, &stride, &offset);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	// t0: 通常の色テクスチャ。
	context->PSSetShaderResources(0, 1, &m_Texture);
	if (useNormalMap)
	{
		// t2: NormalMap。t1はShadowMapが使っているため空けている。
		context->PSSetShaderResources(2, 1, &m_NormalTexture);
	}

	context->Draw(m_VertexCount, 0);

	// 次に描く別のシェーダーへNormalMapが残らないよう、t2だけ外しておく。
	ID3D11ShaderResourceView* nullSRV = nullptr;
	context->PSSetShaderResources(2, 1, &nullSRV);
	SetDepthWriteEnable(true); // デプス書き込みを元に戻す
	SetDepthEnable(true); // 常に戻す
}

void Billboard::Draw(SHADERTYPE shadertype)
{
	if (!m_VertexBuffer || !m_Texture) return;
	ShaderManager* shader = GetShader(shadertype);
	if (!shader || !shader->GetVertexLayout() || !shader->GetVertexShader() || !shader->GetPixelShader()) return;

	SetBlendState(m_BlendMode);

	MATERIAL material;
	ZeroMemory(&material, sizeof(material));
	material.Diffuse = m_Color;
	material.Ambient = m_Color;
	SetMaterial(material);

	XMMATRIX view = GetCamera()->GetView();
	XMMATRIX proj = GetCamera()->GetProjection();

	XMMATRIX matScale = XMMatrixScaling(m_Size.x, m_Size.y, 1.0f);
	XMMATRIX matRot = XMMatrixRotationRollPitchYaw(
		XMConvertToRadians(m_Rot.x),
		XMConvertToRadians(m_Rot.y),
		XMConvertToRadians(m_Rot.z)
	);
	XMMATRIX matTrans = XMMatrixTranslation(m_Pos.x, m_Pos.y, m_Pos.z);

	XMMATRIX world;
	if (m_IsBillboardMode)
	{
		XMVECTOR det;
		XMMATRIX invView = XMMatrixInverse(&det, view);
		invView.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
		world = matScale * matRot * invView * matTrans;
	}
	else
	{
		world = matScale * matRot * matTrans;
	}

	SetWorldMatrix(world);
	SetViewMatrix(view);
	SetProjectionMatrix(proj);

	ID3D11DeviceContext* context = GetDeviceContext();
	context->IASetInputLayout(shader->GetVertexLayout());
	context->VSSetShader(shader->GetVertexShader(), NULL, 0);
	context->PSSetShader(shader->GetPixelShader(), NULL, 0);

	UINT stride = sizeof(BILLBOARD_VERTEX);
	UINT offset = 0;
	context->IASetVertexBuffers(0, 1, &m_VertexBuffer, &stride, &offset);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->PSSetShaderResources(0, 1, &m_Texture);
	if (m_NormalTexture && shadertype == S_NORMAL_MAP)
	{
		// NormalMapPS は t2 で法線マップを読む
		context->PSSetShaderResources(2, 1, &m_NormalTexture);
	}

	context->Draw(m_VertexCount, 0);

	if (m_NormalTexture && shadertype == S_NORMAL_MAP)
	{
		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->PSSetShaderResources(2, 1, &nullSRV);
	}
}

void Billboard::DrawShadowMap(const XMMATRIX& lightView, const XMMATRIX& lightProjection)
{
	if (!m_VertexBuffer || !m_Texture) return;

	// シャドーカメラへ正対させ、スプライトのアルファ形状だけを深度へ書き込む。
	XMVECTOR det;
	XMMATRIX invLightView = XMMatrixInverse(&det, lightView);
	invLightView.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	XMMATRIX world =
		XMMatrixScaling(m_Size.x, m_Size.y, 1.0f) *
		XMMatrixRotationZ(XMConvertToRadians(m_Rot.z)) *
		invLightView *
		XMMatrixTranslation(m_Pos.x, m_Pos.y, m_Pos.z);

	SetWorldMatrix(world);
	SetViewMatrix(lightView);
	SetProjectionMatrix(lightProjection);
	SetBlendState(BLENDSTATE_NONE);
	// BeginFaceShadowMap() が設定した影用ビューポートを維持する。
	// SetDepthEnable(true) は通常画面の3Dビューポートへ戻すため、ここでは呼ばない。
	SetDepthWriteEnable(true);
	// 面ごとのライト方向で頂点の表裏が反転しても、Orbの影を欠落させない。
	SetCullState(CULLSTATE_NONE);

	ID3D11DeviceContext* context = GetDeviceContext();
	ShaderManager* shader = GetShader(S_BILLBOARD_SHADOW_MAP);
	context->IASetInputLayout(shader->GetVertexLayout());
	context->VSSetShader(shader->GetVertexShader(), nullptr, 0);
	context->PSSetShader(shader->GetPixelShader(), nullptr, 0);

	UINT stride = sizeof(BILLBOARD_VERTEX);
	UINT offset = 0;
	context->IASetVertexBuffers(0, 1, &m_VertexBuffer, &stride, &offset);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->PSSetShaderResources(0, 1, &m_Texture);
	context->Draw(m_VertexCount, 0);
	SetCullState(CULLSTATE_BACK);
}
