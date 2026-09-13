#include "split_bilboard.h"
#include "renderer.h"
#include <vector>

SplitBilBoard::SplitBilBoard()
	: Billboard(),
	  m_Cols(1), m_Rows(1), m_TextureIndex(0),
	  m_AnimEnabled(false), m_Loop(true), m_FPS(0.0f), m_AnimTimer(0.0f)
{
}

SplitBilBoard::SplitBilBoard(int cols, int rows)
	: Billboard(),
	  m_Cols(cols > 0 ? cols : 1), m_Rows(rows > 0 ? rows : 1), m_TextureIndex(0),
	  m_AnimEnabled(false), m_Loop(true), m_FPS(0.0f), m_AnimTimer(0.0f)
{
	UpdateUV();
}

SplitBilBoard::SplitBilBoard(int cols, int rows, XMFLOAT3 pos, XMFLOAT2 size, XMFLOAT3 rot, const char* texturePath, bool isDoubleSided)
	: Billboard(pos, size, rot, texturePath, isDoubleSided),
	  m_Cols(cols > 0 ? cols : 1), m_Rows(rows > 0 ? rows : 1), m_TextureIndex(0),
	  m_AnimEnabled(false), m_Loop(true), m_FPS(0.0f), m_AnimTimer(0.0f)
{
	UpdateUV();
}

SplitBilBoard::~SplitBilBoard()
{
}

void SplitBilBoard::SetFPS(float fps)
{
	m_FPS = (fps > 0.0f) ? fps : 0.0f;
}

void SplitBilBoard::Update(void)
{
	if (m_AnimEnabled && m_FPS > 0.0f)
	{
		m_AnimTimer += 1.0f / 60.0f;
		float interval = 1.0f / m_FPS;
		if (m_AnimTimer >= interval)
		{
			m_AnimTimer -= interval;
			int totalFrames = m_Cols * m_Rows;
			if (totalFrames > 0)
			{
				int nextIndex = m_TextureIndex + 1;
				if (nextIndex >= totalFrames)
				{
					if (m_Loop)
					{
						m_TextureIndex = 0;
						UpdateUV();
					}
					else
					{
						m_AnimEnabled = false; // ループしない場合は自動的にアニメーションを終了
					}
				}
				else
				{
					m_TextureIndex = nextIndex;
					UpdateUV();
				}
			}
		}
	}
}

void SplitBilBoard::SetTextureIndex(int index)
{
	m_TextureIndex = index;
	UpdateUV();
}

void SplitBilBoard::UpdateUV(void)
{
	if (m_Cols <= 0 || m_Rows <= 0) return;

	int totalFrames = m_Cols * m_Rows;
	if (m_TextureIndex < 0) m_TextureIndex = 0;
	if (m_TextureIndex >= totalFrames) m_TextureIndex = totalFrames - 1;

	int c = m_TextureIndex % m_Cols;
	int r = m_TextureIndex / m_Cols;

	float uMin = (float)c / m_Cols;
	float uMax = (float)(c + 1) / m_Cols;
	float vMin = (float)r / m_Rows;
	float vMax = (float)(r + 1) / m_Rows;

	CreateBufferWithSplitUV(uMin, uMax, vMin, vMax);
}

void SplitBilBoard::CreateBufferWithSplitUV(float uMin, float uMax, float vMin, float vMax)
{
	if (m_VertexBuffer)
	{
		m_VertexBuffer->Release();
		m_VertexBuffer = nullptr;
	}

	float w = 0.5f;
	float h = 0.5f;
	std::vector<BILLBOARD_VERTEX> vList;

	// 頂点カラーにメンバ変数 m_Color を設定（アルファブレンドを効かせるため）
	XMFLOAT4 vertexColor = m_Color;

	// 表面
	vList.push_back({ { -w,  h, 0.0f }, { 0.0f, 0.0f, -1.0f }, vertexColor, { uMin, vMin } });
	vList.push_back({ {  w,  h, 0.0f }, { 0.0f, 0.0f, -1.0f }, vertexColor, { uMax, vMin } });
	vList.push_back({ { -w, -h, 0.0f }, { 0.0f, 0.0f, -1.0f }, vertexColor, { uMin, vMax } });
	vList.push_back({ { -w, -h, 0.0f }, { 0.0f, 0.0f, -1.0f }, vertexColor, { uMin, vMax } });
	vList.push_back({ {  w,  h, 0.0f }, { 0.0f, 0.0f, -1.0f }, vertexColor, { uMax, vMin } });
	vList.push_back({ {  w, -h, 0.0f }, { 0.0f, 0.0f, -1.0f }, vertexColor, { uMax, vMax } });

	// 裏面
	if (m_IsDoubleSided)
	{
		vList.push_back({ {  w,  h, 0.0f }, { 0.0f, 0.0f, 1.0f }, vertexColor, { uMax, vMin } });
		vList.push_back({ { -w,  h, 0.0f }, { 0.0f, 0.0f, 1.0f }, vertexColor, { uMin, vMin } });
		vList.push_back({ {  w, -h, 0.0f }, { 0.0f, 0.0f, 1.0f }, vertexColor, { uMax, vMax } });
		vList.push_back({ {  w, -h, 0.0f }, { 0.0f, 0.0f, 1.0f }, vertexColor, { uMax, vMax } });
		vList.push_back({ { -w,  h, 0.0f }, { 0.0f, 0.0f, 1.0f }, vertexColor, { uMin, vMin } });
		vList.push_back({ { -w, -h, 0.0f }, { 0.0f, 0.0f, 1.0f }, vertexColor, { uMin, vMax } });
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
