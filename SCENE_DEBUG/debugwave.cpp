#include "debugwave.h"
#include "renderer.h"
#include "shadermanager.h"
#include "camera.h"
#include "debugcamera.h"
#include "texture.h"
#include "light.h"
#include <cmath>
#include <vector>

using namespace DirectX;

namespace
{
	constexpr uint32_t kGridCountX = 96;
	constexpr uint32_t kGridCountZ = 96;
	constexpr float kGridSpacing = 0.25f;
	constexpr float kAmplitude = 0.45f;
	constexpr float kWaveLength = 4.0f;
	constexpr float kPeriod = 1.6f;
	constexpr float kSourceX = 0.0f;
	constexpr float kSourceZ = 0.0f;
	constexpr float kAttenuation = 0.08f;
	constexpr float kFixedStep = 1.0f / 60.0f;

	struct WaveVertex
	{
		XMFLOAT3 position;
		XMFLOAT3 normal;
		XMFLOAT4 color;
		XMFLOAT2 texCoord;
	};

	std::vector<WaveVertex> g_vertices;
	std::vector<uint32_t> g_indices;
	ID3D11Buffer* g_vertexBuffer = nullptr;
	ID3D11Buffer* g_indexBuffer = nullptr;
	ID3D11ShaderResourceView* g_texture = nullptr;
	float g_time = 0.0f;

	float TriangleWave(float cycles)
	{
		float t = cycles - std::floor(cycles);
		if (t < 0.5f)
		{
			return t * 4.0f - 1.0f;
		}
		return 3.0f - t * 4.0f;
	}

	float CalculateTriangleWaveHeight(float x, float z, float time)
	{
		const float dx = x - kSourceX;
		const float dz = z - kSourceZ;
		const float distance = std::sqrt(dx * dx + dz * dz);
		const float cycles = (distance / kWaveLength) - (time / kPeriod);
		const float attenuation = 1.0f / (1.0f + kAttenuation * distance);
		return kAmplitude * attenuation * TriangleWave(cycles);
	}

	void UpdateNormals()
	{
		const uint32_t lastX = kGridCountX - 1;
		const uint32_t lastZ = kGridCountZ - 1;

		for (uint32_t z = 1; z < lastZ; ++z)
		{
			for (uint32_t x = 1; x < lastX; ++x)
			{
				const uint32_t index = z * kGridCountX + x;
				const WaveVertex& left = g_vertices[index - 1];
				const WaveVertex& right = g_vertices[index + 1];
				const WaveVertex& down = g_vertices[index - kGridCountX];
				const WaveVertex& up = g_vertices[index + kGridCountX];

				const XMVECTOR tangentX = XMLoadFloat3(&right.position) - XMLoadFloat3(&left.position);
				const XMVECTOR tangentZ = XMLoadFloat3(&up.position) - XMLoadFloat3(&down.position);
				const XMVECTOR normal = XMVector3Normalize(XMVector3Cross(tangentZ, tangentX));
				XMStoreFloat3(&g_vertices[index].normal, normal);
			}
		}

		for (uint32_t x = 0; x < kGridCountX; ++x)
		{
			g_vertices[x].normal = g_vertices[kGridCountX + x].normal;
			g_vertices[lastZ * kGridCountX + x].normal = g_vertices[(lastZ - 1) * kGridCountX + x].normal;
		}
		for (uint32_t z = 0; z < kGridCountZ; ++z)
		{
			g_vertices[z * kGridCountX].normal = g_vertices[z * kGridCountX + 1].normal;
			g_vertices[z * kGridCountX + lastX].normal = g_vertices[z * kGridCountX + lastX - 1].normal;
		}
	}

	void UploadVertexBuffer()
	{
		D3D11_MAPPED_SUBRESOURCE mapped{};
		ID3D11DeviceContext* context = GetDeviceContext();
		const HRESULT hr = context->Map(g_vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(hr))
		{
			return;
		}

		std::memcpy(mapped.pData, g_vertices.data(), sizeof(WaveVertex) * g_vertices.size());
		context->Unmap(g_vertexBuffer, 0);
	}
}

void DebugWaveScene_Initialize(void)
{
	g_time = 0.0f;
	g_vertices.resize(kGridCountX * kGridCountZ);
	g_indices.clear();
	g_indices.reserve((kGridCountX - 1) * (kGridCountZ - 1) * 6);

	for (uint32_t z = 0; z < kGridCountZ; ++z)
	{
		for (uint32_t x = 0; x < kGridCountX; ++x)
		{
			const uint32_t index = z * kGridCountX + x;
			const float px = (static_cast<float>(x) - (kGridCountX - 1) * 0.5f) * kGridSpacing;
			const float pz = (static_cast<float>(z) - (kGridCountZ - 1) * 0.5f) * kGridSpacing;

			g_vertices[index].position = { px, 0.0f, pz };
			g_vertices[index].normal = { 0.0f, 1.0f, 0.0f };
			g_vertices[index].color = { 1.0f, 1.0f, 1.0f, 1.0f };
			g_vertices[index].texCoord = { px * 0.25f, pz * 0.25f };
		}
	}

	for (uint32_t z = 0; z < kGridCountZ - 1; ++z)
	{
		for (uint32_t x = 0; x < kGridCountX - 1; ++x)
		{
			const uint32_t i0 = z * kGridCountX + x;
			const uint32_t i1 = i0 + 1;
			const uint32_t i2 = i0 + kGridCountX;
			const uint32_t i3 = i2 + 1;
			g_indices.push_back(i0);
			g_indices.push_back(i2);
			g_indices.push_back(i1);
			g_indices.push_back(i1);
			g_indices.push_back(i2);
			g_indices.push_back(i3);
		}
	}

	ID3D11Device* device = GetDevice();

	D3D11_BUFFER_DESC vertexDesc{};
	vertexDesc.ByteWidth = static_cast<UINT>(sizeof(WaveVertex) * g_vertices.size());
	vertexDesc.Usage = D3D11_USAGE_DYNAMIC;
	vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertexDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	device->CreateBuffer(&vertexDesc, nullptr, &g_vertexBuffer);

	D3D11_BUFFER_DESC indexDesc{};
	indexDesc.ByteWidth = static_cast<UINT>(sizeof(uint32_t) * g_indices.size());
	indexDesc.Usage = D3D11_USAGE_DEFAULT;
	indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	D3D11_SUBRESOURCE_DATA indexData{};
	indexData.pSysMem = g_indices.data();
	device->CreateBuffer(&indexDesc, &indexData, &g_indexBuffer);

	for (WaveVertex& vertex : g_vertices)
	{
		vertex.position.y = CalculateTriangleWaveHeight(vertex.position.x, vertex.position.z, g_time);
	}
	UpdateNormals();
	UploadVertexBuffer();

	g_texture = LoadTexture(L"assetdebug\\wave.png");

	DebugCamera_Initialize({ 0.0f, 8.0f, -18.0f }, 0.0f, 20.0f);
	if (GetCamera())
	{
		GetCamera()->SetTargetPos({ 0.0f, 0.0f, 0.0f });
		SetCameraPosition(GetCamera()->GetPos());
	}
}

void DebugWaveScene_Update(void)
{
	DebugCamera_Update();

	if (GetCamera())
	{
		SetCameraPosition(GetCamera()->GetPos());
	}

	g_time += kFixedStep;

	for (WaveVertex& vertex : g_vertices)
	{
		vertex.position.y = CalculateTriangleWaveHeight(vertex.position.x, vertex.position.z, g_time);
	}
	UpdateNormals();
	UploadVertexBuffer();
}

void DebugWaveScene_Draw(void)
{
	if (!g_vertexBuffer || !g_indexBuffer || !GetCamera())
	{
		return;
	}

	SetDepthEnable(true);
	SetCullState(CULLSTATE_NONE);
	SetCameraPosition(GetCamera()->GetPos());

	LIGHT light = {};
	light.Enable = TRUE;
	light.Position = XMFLOAT4(0.0f, 20.0f, 0.0f, 1.0f);
	light.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	light.Ambient = XMFLOAT4(0.85f, 0.90f, 0.95f, 1.0f);
	light.PointLightParam = XMFLOAT4(80.0f, 0.8f, 0.0f, 0.0f);
	SetLight(light);

	MATERIAL material = {};
	material.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	material.Ambient = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	material.Specular = XMFLOAT4(0.4f, 0.4f, 0.4f, 1.0f);
	material.Emission = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
	material.Shininess = 16.0f;
	SetMaterial(material);

	ID3D11DeviceContext* context = GetDeviceContext();
	ShaderManager* shader = GetShader(S_LAMBERT);
	context->IASetInputLayout(shader->GetVertexLayout());
	context->VSSetShader(shader->GetVertexShader(), nullptr, 0);
	context->PSSetShader(shader->GetPixelShader(), nullptr, 0);
	context->PSSetShaderResources(0, 1, &g_texture);
	SetDefaultSampler();

	SetWorldMatrix(XMMatrixIdentity());
	SetViewMatrix(GetCamera()->GetView());
	SetProjectionMatrix(GetCamera()->GetProjection());

	UINT stride = sizeof(WaveVertex);
	UINT offset = 0;
	context->IASetVertexBuffers(0, 1, &g_vertexBuffer, &stride, &offset);
	context->IASetIndexBuffer(g_indexBuffer, DXGI_FORMAT_R32_UINT, 0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->DrawIndexed(static_cast<UINT>(g_indices.size()), 0, 0);

	SetCullState(CULLSTATE_BACK);
}

void DebugWaveScene_Finalize(void)
{
	SAFE_RELEASE(g_vertexBuffer);
	SAFE_RELEASE(g_indexBuffer);
	g_texture = nullptr;
	g_vertices.clear();
	g_indices.clear();
	g_time = 0.0f;
}
