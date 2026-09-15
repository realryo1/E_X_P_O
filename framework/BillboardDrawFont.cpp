#include "BillboardDrawFont.h"
#include "camera.h"
#include "renderer.h"

const float BillboardDrawFont::kPixelSize = 48.0f;

BillboardDrawFont::BillboardDrawFont(
	XMFLOAT3 worldPos,
	float worldHeight,
	XMFLOAT4 color,
	const std::string& text)
	: DrawFont({ 0.0f, 0.0f }, kPixelSize, 0.0f, color, text, TA_MIDDLE, true)
	, m_WorldPos(worldPos)
	, m_WorldHeight(worldHeight > 0.01f ? worldHeight : 1.0f)
{
	SetLineSpacing(3.0f);
}

void BillboardDrawFont::SetWorldPos(XMFLOAT3 pos)
{
	m_WorldPos = pos;
	RequestRedraw();
}

void BillboardDrawFont::SetWorldHeight(float height)
{
	if (height < 0.01f)
	{
		height = 0.01f;
	}
	m_WorldHeight = height;
	RequestRedraw();
}

void BillboardDrawFont::Draw()
{
	if (!EnsureDrawMesh())
	{
		return;
	}

	Camera* camera = GetCamera();
	if (!camera)
	{
		return;
	}

	const XMMATRIX view = camera->GetView();
	const XMMATRIX proj = camera->GetProjection();
	XMVECTOR det;
	XMMATRIX invView = XMMatrixInverse(&det, view);
	invView.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

	const float worldScale = m_WorldHeight / kPixelSize;
	const XMMATRIX matScale = XMMatrixScaling(worldScale, -worldScale, worldScale);
	const XMMATRIX matTrans = XMMatrixTranslation(
		m_WorldPos.x,
		m_WorldPos.y,
		m_WorldPos.z);
	const XMMATRIX world = matScale * invView * matTrans;

	SetWorldMatrix(world);
	SetViewMatrix(view);
	SetProjectionMatrix(proj);
	SetDepthEnable(true);
	SetDepthWriteEnable(false);
	DrawUnlitMesh();
	SetDepthWriteEnable(true);
	SetDepthEnable(true);
}
