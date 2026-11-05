#include "water.h"

#include "course.h"
#include "field.h"
#include "groundpick.h"
#include "imgui/imgui.h"
#include "input_manager.h"
#include "keyboard.h"
#include "main.h"
#include "mouse.h"
#include "photomode.h"
#include "player.h"
#include "playercamera.h"
#include "renderer.h"
#include "shadermanager.h"

#include <Windows.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace DirectX;

namespace
{
	const char* WATER_DIRECTORY = "asset\\water";
	const char* WATER_AREA_FILE = "asset\\water\\areas.txt";
	// ワールド1単位 = 5m（モデルスケール 0.002 × GLB全体スケール 100 の逆数）
	const float METERS_PER_UNIT = 5.0f;
	// b0〜b13 は埋まっているため、4面ShadowMap用の b9 を借りる（SCENE_GAME では未使用）。
	const UINT WATER_BUFFER_SLOT = 9;
	const UINT WATER_EDGE_SLOT = 2;
	// 新しい水域の水面は、最初にクリックした床から少しだけ上げる。
	const float NEW_AREA_SURFACE_OFFSET = 0.02f;
	const size_t MAX_UNDO = 30;
	const float PICK_MAX_DISTANCE = 400.0f;
	const float PICK_STEP = 1.0f;
	const float HANDLE_PIXELS = 10.0f;

	struct WaterConstant
	{
		XMFLOAT4 shape;		// x:輪郭の先頭の辺 y:辺の数 w:縁のぼかし幅
		XMFLOAT4 wave;		// x:波の強さ y:波長倍率 z:速さ w:1ワールド単位あたりのメートル
		XMFLOAT4 shallow;	// rgb:浅瀬色 a:浅瀬の不透明度
		XMFLOAT4 deep;		// rgb:深場色 a:深場の不透明度
		XMFLOAT4 misc;		// x:太陽の反射 y:コースティクス z:泡 w:空のYaw(度)
		XMFLOAT4 misc2;		// x:浅瀬から深場までの距離 y:空の映り込み
	};

	struct WaterSettings
	{
		bool enabled;
		float edgeSoftness;
		float depthRamp;
		float waveStrength;
		float waveScale;
		float waveSpeed;
		float shallowColor[4];
		float deepColor[4];
		float sunGlint;
		float caustics;
		float foam;
		float skyReflection;
	};

	// 1つの水域。輪郭はXZの閉じた多角形で、水面は height の水平面。
	struct WaterArea
	{
		std::vector<XMFLOAT2> points;
		float height = 0.0f;
	};

	// 水域ごとの描画範囲。三角形分割できなかった水域は triangulated = false。
	struct AreaDrawRange
	{
		UINT indexStart = 0;
		UINT indexCount = 0;
		UINT edgeStart = 0;
		UINT edgeCount = 0;
		bool triangulated = false;
	};

	WaterSettings g_Settings = {};
	std::vector<WaterArea> g_Areas;
	std::vector<AreaDrawRange> g_DrawRanges;
	std::vector<std::vector<WaterArea>> g_Undo;

	ID3D11Buffer* g_VertexBuffer = nullptr;
	ID3D11Buffer* g_IndexBuffer = nullptr;
	ID3D11Buffer* g_EdgeBuffer = nullptr;
	ID3D11ShaderResourceView* g_EdgeView = nullptr;
	ID3D11Buffer* g_ConstantBuffer = nullptr;
	bool g_MeshDirty = true;

	bool g_Editing = false;
	bool g_Dirty = false;
	std::string g_Status;
	int g_SelectedArea = -1;
	int g_SelectedVertex = -1;
	bool g_Dragging = false;
	bool g_HasCursor = false;
	XMFLOAT3 g_Cursor = {};
	int g_HoverArea = -1;
	int g_HoverVertex = -1;
	int g_HoverEdge = -1;

	WaterSettings MakeDefaultSettings(void)
	{
		WaterSettings s = {};
		s.enabled = true;
		s.edgeSoftness = 0.6f;
		s.depthRamp = 8.0f;
		s.waveStrength = 1.0f;
		s.waveScale = 1.0f;
		s.waveSpeed = 1.0f;
		s.shallowColor[0] = 0.16f;
		s.shallowColor[1] = 0.52f;
		s.shallowColor[2] = 0.55f;
		s.shallowColor[3] = 0.28f;
		s.deepColor[0] = 0.03f;
		s.deepColor[1] = 0.18f;
		s.deepColor[2] = 0.30f;
		s.deepColor[3] = 0.55f;
		s.sunGlint = 1.0f;
		s.caustics = 0.6f;
		s.foam = 0.8f;
		s.skyReflection = 1.0f;
		return s;
	}

	float Cross(const XMFLOAT2& a, const XMFLOAT2& b, const XMFLOAT2& c)
	{
		return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
	}

	float SignedArea(const std::vector<XMFLOAT2>& points)
	{
		float area = 0.0f;
		for (size_t i = 0; i < points.size(); ++i)
		{
			const XMFLOAT2& a = points[i];
			const XMFLOAT2& b = points[(i + 1) % points.size()];
			area += a.x * b.y - b.x * a.y;
		}
		return area * 0.5f;
	}

	bool IsInsideTriangle(const XMFLOAT2& p, const XMFLOAT2& a, const XMFLOAT2& b, const XMFLOAT2& c)
	{
		return Cross(a, b, p) >= 0.0f && Cross(b, c, p) >= 0.0f && Cross(c, a, p) >= 0.0f;
	}

	// 耳刈り取り法。輪郭が自己交差していると最後まで切れず false を返す（切れた分は出力する）。
	bool Triangulate(const std::vector<XMFLOAT2>& points, UINT baseVertex, std::vector<UINT>* outIndices)
	{
		const int count = static_cast<int>(points.size());
		if (count < 3)
		{
			return false;
		}
		std::vector<int> remaining(count);
		for (int i = 0; i < count; ++i)
		{
			remaining[i] = i;
		}
		// 反時計回りにそろえて、凸な頂点を Cross > 0 で判定できるようにする。
		if (SignedArea(points) < 0.0f)
		{
			std::reverse(remaining.begin(), remaining.end());
		}

		while (remaining.size() > 3)
		{
			const int size = static_cast<int>(remaining.size());
			bool clipped = false;
			for (int i = 0; i < size; ++i)
			{
				const int prev = remaining[(i + size - 1) % size];
				const int cur = remaining[i];
				const int next = remaining[(i + 1) % size];
				const XMFLOAT2& a = points[prev];
				const XMFLOAT2& b = points[cur];
				const XMFLOAT2& c = points[next];
				if (Cross(a, b, c) <= 0.000001f)
				{
					continue;
				}
				bool containsOther = false;
				for (int other : remaining)
				{
					if (other != prev && other != cur && other != next &&
						IsInsideTriangle(points[other], a, b, c))
					{
						containsOther = true;
						break;
					}
				}
				if (containsOther)
				{
					continue;
				}
				outIndices->push_back(baseVertex + prev);
				outIndices->push_back(baseVertex + cur);
				outIndices->push_back(baseVertex + next);
				remaining.erase(remaining.begin() + i);
				clipped = true;
				break;
			}
			if (!clipped)
			{
				return false;
			}
		}
		outIndices->push_back(baseVertex + remaining[0]);
		outIndices->push_back(baseVertex + remaining[1]);
		outIndices->push_back(baseVertex + remaining[2]);
		return true;
	}

	void ReleaseMeshBuffers(void)
	{
		SAFE_RELEASE(g_VertexBuffer);
		SAFE_RELEASE(g_IndexBuffer);
		SAFE_RELEASE(g_EdgeView);
		SAFE_RELEASE(g_EdgeBuffer);
	}

	ID3D11Buffer* CreateImmutableBuffer(UINT bindFlags, const void* data, UINT byteWidth, UINT miscFlags = 0, UINT stride = 0)
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = byteWidth;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = bindFlags;
		desc.MiscFlags = miscFlags;
		desc.StructureByteStride = stride;
		D3D11_SUBRESOURCE_DATA initial = {};
		initial.pSysMem = data;
		ID3D11Buffer* buffer = nullptr;
		GetDevice()->CreateBuffer(&desc, &initial, &buffer);
		return buffer;
	}

	// 全水域の頂点・インデックス・輪郭の辺を1本ずつのバッファへまとめる。
	void RebuildMesh(void)
	{
		g_MeshDirty = false;
		ReleaseMeshBuffers();
		g_DrawRanges.assign(g_Areas.size(), AreaDrawRange());

		std::vector<VERTEX_3D> vertices;
		std::vector<UINT> indices;
		std::vector<XMFLOAT4> edges;
		for (size_t i = 0; i < g_Areas.size(); ++i)
		{
			const WaterArea& area = g_Areas[i];
			AreaDrawRange& range = g_DrawRanges[i];
			if (area.points.size() < 3)
			{
				continue;
			}
			const UINT baseVertex = static_cast<UINT>(vertices.size());
			for (const XMFLOAT2& point : area.points)
			{
				VERTEX_3D vertex = {};
				vertex.Position = XMFLOAT3(point.x, area.height, point.y);
				vertex.Normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
				vertex.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
				vertex.TexCoord = point;
				vertices.push_back(vertex);
			}
			range.indexStart = static_cast<UINT>(indices.size());
			range.triangulated = Triangulate(area.points, baseVertex, &indices);
			range.indexCount = static_cast<UINT>(indices.size()) - range.indexStart;

			range.edgeStart = static_cast<UINT>(edges.size());
			for (size_t p = 0; p < area.points.size(); ++p)
			{
				const XMFLOAT2& a = area.points[p];
				const XMFLOAT2& b = area.points[(p + 1) % area.points.size()];
				edges.push_back(XMFLOAT4(a.x, a.y, b.x, b.y));
			}
			range.edgeCount = static_cast<UINT>(edges.size()) - range.edgeStart;
		}
		if (indices.empty() || !GetDevice())
		{
			return;
		}

		g_VertexBuffer = CreateImmutableBuffer(
			D3D11_BIND_VERTEX_BUFFER,
			vertices.data(),
			static_cast<UINT>(sizeof(VERTEX_3D) * vertices.size()));
		g_IndexBuffer = CreateImmutableBuffer(
			D3D11_BIND_INDEX_BUFFER,
			indices.data(),
			static_cast<UINT>(sizeof(UINT) * indices.size()));
		g_EdgeBuffer = CreateImmutableBuffer(
			D3D11_BIND_SHADER_RESOURCE,
			edges.data(),
			static_cast<UINT>(sizeof(XMFLOAT4) * edges.size()),
			D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
			sizeof(XMFLOAT4));
		if (g_EdgeBuffer)
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
			viewDesc.Format = DXGI_FORMAT_UNKNOWN;
			viewDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			viewDesc.Buffer.FirstElement = 0;
			viewDesc.Buffer.NumElements = static_cast<UINT>(edges.size());
			GetDevice()->CreateShaderResourceView(g_EdgeBuffer, &viewDesc, &g_EdgeView);
		}
		if (!g_VertexBuffer || !g_IndexBuffer || !g_EdgeView)
		{
			ReleaseMeshBuffers();
		}
	}

	bool UploadConstants(const AreaDrawRange& range)
	{
		ID3D11DeviceContext* context = GetDeviceContext();
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context->Map(g_ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			return false;
		}
		const WaterSettings& s = g_Settings;
		WaterConstant constant = {};
		constant.shape = XMFLOAT4(
			static_cast<float>(range.edgeStart),
			static_cast<float>(range.edgeCount),
			0.0f,
			s.edgeSoftness);
		constant.wave = XMFLOAT4(s.waveStrength, s.waveScale, s.waveSpeed, METERS_PER_UNIT);
		constant.shallow = XMFLOAT4(s.shallowColor[0], s.shallowColor[1], s.shallowColor[2], s.shallowColor[3]);
		constant.deep = XMFLOAT4(s.deepColor[0], s.deepColor[1], s.deepColor[2], s.deepColor[3]);
		constant.misc = XMFLOAT4(s.sunGlint, s.caustics, s.foam, Field_GetSkyboxYaw());
		constant.misc2 = XMFLOAT4(s.depthRamp, s.skyReflection, 0.0f, 0.0f);
		*static_cast<WaterConstant*>(mapped.pData) = constant;
		context->Unmap(g_ConstantBuffer, 0);
		return true;
	}

	void LoadAreas(void)
	{
		g_Areas.clear();
		g_MeshDirty = true;
		std::ifstream file(WATER_AREA_FILE);
		if (!file)
		{
			return;
		}
		std::string line;
		while (std::getline(file, line))
		{
			if (line.empty() || line[0] == '#')
			{
				continue;
			}
			if (line.compare(0, 2, "a ") == 0)
			{
				// a <height>
				WaterArea area;
				area.height = std::strtof(line.c_str() + 2, nullptr);
				g_Areas.push_back(std::move(area));
			}
			else if (line.compare(0, 2, "v ") == 0 && !g_Areas.empty())
			{
				// v <x> <z>
				XMFLOAT2 point = {};
				if (sscanf_s(line.c_str() + 2, "%f %f", &point.x, &point.y) == 2)
				{
					g_Areas.back().points.push_back(point);
				}
			}
		}
	}

	bool SaveAreas(void)
	{
		CreateDirectoryA("asset", nullptr);
		CreateDirectoryA(WATER_DIRECTORY, nullptr);

		std::ofstream file(WATER_AREA_FILE, std::ios::trunc);
		if (!file)
		{
			return false;
		}
		char text[128] = {};
		file << "# water areas\n";
		file << "# a <height>   水域の開始と水面の高さ\n";
		file << "# v <x> <z>    輪郭の頂点（順番どおりに閉じる）\n";
		for (const WaterArea& area : g_Areas)
		{
			if (area.points.empty())
			{
				continue;
			}
			std::snprintf(text, sizeof(text), "a %.3f\n", area.height);
			file << text;
			for (const XMFLOAT2& point : area.points)
			{
				std::snprintf(text, sizeof(text), "v %.3f %.3f\n", point.x, point.y);
				file << text;
			}
		}
		return file.good();
	}

	void MarkChanged(void)
	{
		g_Dirty = true;
		g_MeshDirty = true;
	}

	void ClampSelection(void)
	{
		if (g_SelectedArea < 0 || g_SelectedArea >= static_cast<int>(g_Areas.size()))
		{
			g_SelectedArea = -1;
			g_SelectedVertex = -1;
			return;
		}
		const int count = static_cast<int>(g_Areas[g_SelectedArea].points.size());
		if (g_SelectedVertex >= count)
		{
			g_SelectedVertex = count - 1;
		}
	}

	void PushUndo(void)
	{
		g_Undo.push_back(g_Areas);
		if (g_Undo.size() > MAX_UNDO)
		{
			g_Undo.erase(g_Undo.begin());
		}
	}

	void Undo(void)
	{
		if (g_Undo.empty())
		{
			return;
		}
		g_Areas = std::move(g_Undo.back());
		g_Undo.pop_back();
		g_Dragging = false;
		ClampSelection();
		MarkChanged();
	}

	XMFLOAT3 AreaPoint(const WaterArea& area, size_t index)
	{
		return XMFLOAT3(area.points[index].x, area.height, area.points[index].y);
	}

	float DistanceSquared(const ImVec2& a, const XMFLOAT2& b)
	{
		const float dx = a.x - b.x;
		const float dy = a.y - b.y;
		return dx * dx + dy * dy;
	}

	// 画面上でカーソルに一番近い頂点を探す。選択中の水域を優先する。
	void FindHoverVertex(const ImVec2& aim)
	{
		g_HoverArea = -1;
		g_HoverVertex = -1;
		float best = HANDLE_PIXELS * HANDLE_PIXELS;
		for (int pass = 0; pass < 2 && g_HoverVertex < 0; ++pass)
		{
			for (int a = 0; a < static_cast<int>(g_Areas.size()); ++a)
			{
				if ((pass == 0) != (a == g_SelectedArea))
				{
					continue;
				}
				const WaterArea& area = g_Areas[a];
				for (int v = 0; v < static_cast<int>(area.points.size()); ++v)
				{
					XMFLOAT2 screen;
					if (!GroundPick_Project(AreaPoint(area, v), &screen))
					{
						continue;
					}
					const float d = DistanceSquared(aim, screen);
					if (d < best)
					{
						best = d;
						g_HoverArea = a;
						g_HoverVertex = v;
					}
				}
			}
		}
	}

	// 選択中の水域で、画面上でカーソルに近い辺を探す。
	void FindHoverEdge(const ImVec2& aim)
	{
		g_HoverEdge = -1;
		if (g_SelectedArea < 0)
		{
			return;
		}
		const WaterArea& area = g_Areas[g_SelectedArea];
		const int count = static_cast<int>(area.points.size());
		if (count < 2)
		{
			return;
		}
		float best = HANDLE_PIXELS * HANDLE_PIXELS;
		for (int i = 0; i < count; ++i)
		{
			XMFLOAT2 a;
			XMFLOAT2 b;
			if (!GroundPick_Project(AreaPoint(area, i), &a) ||
				!GroundPick_Project(AreaPoint(area, (i + 1) % count), &b))
			{
				continue;
			}
			const float abx = b.x - a.x;
			const float aby = b.y - a.y;
			const float lengthSquared = abx * abx + aby * aby;
			float t = lengthSquared > 0.0f
				? ((aim.x - a.x) * abx + (aim.y - a.y) * aby) / lengthSquared
				: 0.0f;
			t = (std::max)(0.0f, (std::min)(1.0f, t));
			const XMFLOAT2 nearest = { a.x + abx * t, a.y + aby * t };
			const float d = DistanceSquared(aim, nearest);
			if (d < best)
			{
				best = d;
				g_HoverEdge = i;
			}
		}
	}

	void DeleteSelectedVertex(void)
	{
		if (g_SelectedArea < 0 || g_SelectedVertex < 0)
		{
			return;
		}
		PushUndo();
		WaterArea& area = g_Areas[g_SelectedArea];
		area.points.erase(area.points.begin() + g_SelectedVertex);
		if (area.points.empty())
		{
			g_Areas.erase(g_Areas.begin() + g_SelectedArea);
			g_SelectedArea = -1;
			g_SelectedVertex = -1;
		}
		else
		{
			g_SelectedVertex = (g_SelectedVertex + static_cast<int>(area.points.size()) - 1) %
				static_cast<int>(area.points.size());
		}
		MarkChanged();
	}

	void DeleteSelectedArea(void)
	{
		if (g_SelectedArea < 0)
		{
			return;
		}
		PushUndo();
		g_Areas.erase(g_Areas.begin() + g_SelectedArea);
		g_SelectedArea = -1;
		g_SelectedVertex = -1;
		MarkChanged();
	}

	// 選択中の頂点の次に追加する。水域が未選択なら新しい水域を作る。
	void AddVertexAtCursor(void)
	{
		PushUndo();
		if (g_SelectedArea < 0)
		{
			WaterArea area;
			area.height = g_Cursor.y + NEW_AREA_SURFACE_OFFSET;
			g_Areas.push_back(std::move(area));
			g_SelectedArea = static_cast<int>(g_Areas.size()) - 1;
			g_SelectedVertex = -1;
		}
		WaterArea& area = g_Areas[g_SelectedArea];
		const int insertAt = g_SelectedVertex < 0
			? static_cast<int>(area.points.size())
			: g_SelectedVertex + 1;
		area.points.insert(area.points.begin() + insertAt, XMFLOAT2(g_Cursor.x, g_Cursor.z));
		g_SelectedVertex = insertAt;
		MarkChanged();
	}

	// 水面の高さを、輪郭の頂点の床の高さの平均に合わせる。
	void FitSelectedHeightToGround(void)
	{
		if (g_SelectedArea < 0)
		{
			return;
		}
		WaterArea& area = g_Areas[g_SelectedArea];
		float sum = 0.0f;
		int count = 0;
		for (const XMFLOAT2& point : area.points)
		{
			float y = 0.0f;
			if (Field_SampleGroundY(point.x, point.y, &y))
			{
				sum += y;
				++count;
			}
		}
		if (count == 0)
		{
			g_Status = "Floor collision not ready";
			return;
		}
		PushUndo();
		area.height = sum / static_cast<float>(count) + NEW_AREA_SURFACE_OFFSET;
		MarkChanged();
	}

	void SetEditing(bool editing)
	{
		if (g_Editing == editing)
		{
			return;
		}
		g_Editing = editing;
		g_Dragging = false;
		g_HasCursor = false;
		g_HoverArea = -1;
		g_HoverVertex = -1;
		g_HoverEdge = -1;
		if (editing)
		{
			g_Undo.clear();
			g_Status.clear();
			Player_SetControlEnabled(false);
			PlayerCamera_SetFreeCameraActive(true);
			UnLockMouse();
		}
		else
		{
			PlayerCamera_SetFreeCameraActive(false);
			Player_SetControlEnabled(!Course_IsRaceCountdown());
			LockMouse();
		}
		RequestRedraw();
	}

	void DrawAreaOutlines(void)
	{
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		for (int a = 0; a < static_cast<int>(g_Areas.size()); ++a)
		{
			const WaterArea& area = g_Areas[a];
			const bool selected = a == g_SelectedArea;
			const bool broken = a < static_cast<int>(g_DrawRanges.size()) &&
				area.points.size() >= 3 && !g_DrawRanges[a].triangulated;
			const ImU32 lineColor = broken
				? IM_COL32(255, 80, 80, 255)
				: (selected ? IM_COL32(80, 230, 255, 255) : IM_COL32(60, 140, 255, 180));
			const int count = static_cast<int>(area.points.size());
			for (int i = 0; i < count; ++i)
			{
				XMFLOAT2 p0;
				XMFLOAT2 p1;
				if (count >= 2 &&
					GroundPick_Project(AreaPoint(area, i), &p0) &&
					GroundPick_Project(AreaPoint(area, (i + 1) % count), &p1))
				{
					const bool hoverEdge = selected && i == g_HoverEdge && ImGui::GetIO().KeyShift;
					drawList->AddLine(
						ImVec2(p0.x, p0.y),
						ImVec2(p1.x, p1.y),
						hoverEdge ? IM_COL32(255, 255, 255, 255) : lineColor,
						selected ? 2.5f : 1.5f);
				}
			}
			for (int i = 0; i < count; ++i)
			{
				XMFLOAT2 p;
				if (!GroundPick_Project(AreaPoint(area, i), &p))
				{
					continue;
				}
				const bool selectedVertex = selected && i == g_SelectedVertex;
				const bool hoverVertex = a == g_HoverArea && i == g_HoverVertex;
				drawList->AddCircleFilled(
					ImVec2(p.x, p.y),
					selectedVertex ? 6.0f : 4.0f,
					selectedVertex ? IM_COL32(255, 220, 60, 255) : lineColor);
				if (hoverVertex)
				{
					drawList->AddCircle(ImVec2(p.x, p.y), 9.0f, IM_COL32(255, 255, 255, 255), 0, 2.0f);
				}
			}
		}

		// 次のクリックで頂点が入る位置を、前後の頂点とつないで見せる。
		if (!g_HasCursor || g_Dragging || g_HoverVertex >= 0)
		{
			return;
		}
		XMFLOAT3 cursor = g_Cursor;
		if (g_SelectedArea >= 0)
		{
			cursor.y = g_Areas[g_SelectedArea].height;
		}
		XMFLOAT2 c;
		if (!GroundPick_Project(cursor, &c))
		{
			return;
		}
		const ImU32 previewColor = IM_COL32(255, 255, 255, 140);
		if (g_SelectedArea >= 0 && !ImGui::GetIO().KeyShift)
		{
			const WaterArea& area = g_Areas[g_SelectedArea];
			const int count = static_cast<int>(area.points.size());
			if (count > 0)
			{
				const int prev = g_SelectedVertex < 0 ? count - 1 : g_SelectedVertex;
				const int next = (prev + 1) % count;
				XMFLOAT2 p;
				if (GroundPick_Project(AreaPoint(area, prev), &p))
				{
					drawList->AddLine(ImVec2(p.x, p.y), ImVec2(c.x, c.y), previewColor, 1.5f);
				}
				if (count >= 2 && GroundPick_Project(AreaPoint(area, next), &p))
				{
					drawList->AddLine(ImVec2(p.x, p.y), ImVec2(c.x, c.y), previewColor, 1.5f);
				}
			}
		}
		drawList->AddCircle(ImVec2(c.x, c.y), 5.0f, previewColor, 0, 1.5f);
	}
}

void Water_Initialize(void)
{
	Water_Finalize();
	g_Settings = MakeDefaultSettings();
	g_Editing = false;
	g_Dirty = false;
	g_Undo.clear();
	g_Status.clear();
	g_SelectedArea = -1;
	g_SelectedVertex = -1;
	LoadAreas();

	D3D11_BUFFER_DESC constantDesc = {};
	constantDesc.ByteWidth = sizeof(WaterConstant);
	constantDesc.Usage = D3D11_USAGE_DYNAMIC;
	constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	constantDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	if (ID3D11Device* device = GetDevice())
	{
		device->CreateBuffer(&constantDesc, nullptr, &g_ConstantBuffer);
	}
}

void Water_Finalize(void)
{
	SetEditing(false);
	ReleaseMeshBuffers();
	SAFE_RELEASE(g_ConstantBuffer);
	g_Areas.clear();
	g_DrawRanges.clear();
	g_Undo.clear();
	g_MeshDirty = true;
}

bool Water_IsEditing(void)
{
	return g_Editing;
}

void Water_Update(void)
{
	if (!g_Editing)
	{
		// フォトモードやメニュー中は入らない。
		if (Keyboard_IsKeyDownTrigger(KK_F8) &&
			!PhotoMode_IsActive() &&
			!Course_IsMenuOpen() &&
			Player_IsReady())
		{
			SetEditing(true);
		}
		return;
	}

	if (Keyboard_IsKeyDownTrigger(KK_F8) ||
		Input_IsActionTrigger(INPUT_ACTION_PAUSE))
	{
		SetEditing(false);
		return;
	}

	const ImGuiIO& io = ImGui::GetIO();
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
	{
		Undo();
	}
	if (!io.WantCaptureKeyboard &&
		(Keyboard_IsKeyDownTrigger(KK_DELETE) ||
			Keyboard_IsKeyDownTrigger(KK_BACK) ||
			Keyboard_IsKeyDownTrigger(KK_X)))
	{
		g_Dragging = false;
		DeleteSelectedVertex();
	}

	// 右クリックで視点操作中（マウス固定）は画面中央、それ以外はカーソル位置で床を狙う。
	const bool looking = !Mouse_IsVisible();
	const ImVec2 size = io.DisplaySize;
	const ImVec2 aim = looking ? ImVec2(size.x * 0.5f, size.y * 0.5f) : io.MousePos;
	const bool canAim = looking || !io.WantCaptureMouse;
	g_HasCursor = canAim && GroundPick_Pick(aim.x, aim.y, PICK_MAX_DISTANCE, PICK_STEP, &g_Cursor);

	const bool mouseClick = !looking && !io.WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	const bool click = mouseClick || (!io.WantCaptureKeyboard && Keyboard_IsKeyDownTrigger(KK_F));
	const bool hold = (!looking && io.MouseDown[0]) || Keyboard_IsKeyDown(KK_F);

	if (g_Dragging)
	{
		if (!hold)
		{
			g_Dragging = false;
		}
		else if (g_HasCursor && g_SelectedArea >= 0 && g_SelectedVertex >= 0)
		{
			XMFLOAT2& point = g_Areas[g_SelectedArea].points[g_SelectedVertex];
			if (point.x != g_Cursor.x || point.y != g_Cursor.z)
			{
				point = XMFLOAT2(g_Cursor.x, g_Cursor.z);
				MarkChanged();
			}
		}
		return;
	}

	if (canAim)
	{
		FindHoverVertex(aim);
		FindHoverEdge(aim);
	}
	else
	{
		g_HoverArea = -1;
		g_HoverVertex = -1;
		g_HoverEdge = -1;
	}
	if (!click)
	{
		return;
	}

	// 頂点の上なら選んでドラッグ、Shift+辺なら辺の途中へ挿入、それ以外は選択中の頂点の次へ追加。
	if (g_HoverVertex >= 0)
	{
		PushUndo();
		g_SelectedArea = g_HoverArea;
		g_SelectedVertex = g_HoverVertex;
		g_Dragging = true;
		return;
	}
	if (io.KeyShift && g_HoverEdge >= 0)
	{
		g_SelectedVertex = g_HoverEdge;
	}
	if (g_HasCursor)
	{
		AddVertexAtCursor();
		g_Dragging = true;
	}
}

void Water_Draw(void)
{
	if (!g_Settings.enabled || !g_ConstantBuffer || !Field_IsLoadComplete())
	{
		return;
	}
	if (g_MeshDirty)
	{
		RebuildMesh();
	}
	ID3D11ShaderResourceView* sky = Field_GetSkyboxTexture();
	ShaderManager* shader = GetShader(S_WATER);
	if (!g_VertexBuffer || !sky || !shader)
	{
		return;
	}

	ID3D11DeviceContext* context = GetDeviceContext();
	SetWorldMatrix(XMMatrixIdentity());
	context->IASetInputLayout(shader->GetVertexLayout());
	context->VSSetShader(shader->GetVertexShader(), nullptr, 0);
	context->PSSetShader(shader->GetPixelShader(), nullptr, 0);
	const UINT stride = sizeof(VERTEX_3D);
	const UINT offset = 0;
	context->IASetVertexBuffers(0, 1, &g_VertexBuffer, &stride, &offset);
	context->IASetIndexBuffer(g_IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetConstantBuffers(WATER_BUFFER_SLOT, 1, &g_ConstantBuffer);
	context->PSSetConstantBuffers(WATER_BUFFER_SLOT, 1, &g_ConstantBuffer);
	context->PSSetShaderResources(0, 1, &sky);
	// t2 は PBR の法線マップ枠なので、描き終えたら元に戻す。
	ID3D11ShaderResourceView* previousEdgeSlot = nullptr;
	context->PSGetShaderResources(WATER_EDGE_SLOT, 1, &previousEdgeSlot);
	context->PSSetShaderResources(WATER_EDGE_SLOT, 1, &g_EdgeView);
	SetDefaultSampler();

	// 透明なので深度は読むだけ。床の起伏が水面より高い所は床が手前に出る。
	SetBlendState(BLENDSTATE_ALFA);
	SetCullState(CULLSTATE_NONE);
	SetDepthWriteEnable(false);
	for (const AreaDrawRange& range : g_DrawRanges)
	{
		if (range.indexCount == 0 || !UploadConstants(range))
		{
			continue;
		}
		DrawIndexed(range.indexCount, range.indexStart, 0);
	}
	SetDepthWriteEnable(true);

	context->PSSetShaderResources(WATER_EDGE_SLOT, 1, &previousEdgeSlot);
	SAFE_RELEASE(previousEdgeSlot);
}

void Water_DrawOverlay(void)
{
	if (!g_Editing || Direct3D_IsTakingScreenshot())
	{
		return;
	}

	DrawAreaOutlines();

	bool open = true;
	ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Water Editor", &open))
	{
		ImGui::TextUnformatted("WASD / Space / Shift: move   RMB: look");
		ImGui::TextUnformatted("LMB / F: add vertex after selected (drag to move)");
		ImGui::TextUnformatted("LMB on vertex: select & drag   Shift+LMB on edge: insert");
		ImGui::TextUnformatted("Del / X: delete vertex   Ctrl+Z: undo   F8 / Esc: exit");
		ImGui::Separator();

		ImGui::Checkbox("Show water", &g_Settings.enabled);
		ImGui::Text("Areas (%d)", static_cast<int>(g_Areas.size()));
		if (ImGui::BeginListBox("##areas", ImVec2(-FLT_MIN, 6.0f * ImGui::GetTextLineHeightWithSpacing())))
		{
			for (int i = 0; i < static_cast<int>(g_Areas.size()); ++i)
			{
				const WaterArea& area = g_Areas[i];
				const bool broken = i < static_cast<int>(g_DrawRanges.size()) &&
					area.points.size() >= 3 && !g_DrawRanges[i].triangulated;
				char label[96];
				std::snprintf(
					label,
					sizeof(label),
					"Area %d  (%d verts, Y %.2f)%s",
					i,
					static_cast<int>(area.points.size()),
					area.height,
					broken ? "  !self-intersect" : "");
				if (ImGui::Selectable(label, i == g_SelectedArea))
				{
					g_SelectedArea = i;
					g_SelectedVertex = static_cast<int>(area.points.size()) - 1;
				}
			}
			ImGui::EndListBox();
		}
		if (ImGui::Button("New Area"))
		{
			// 選択を外すと、次のクリックで新しい水域が始まる。
			g_SelectedArea = -1;
			g_SelectedVertex = -1;
			g_Status = "Click the floor to start a new area";
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(g_SelectedArea < 0);
		if (ImGui::Button("Delete Area"))
		{
			DeleteSelectedArea();
		}
		ImGui::EndDisabled();

		if (g_SelectedArea >= 0)
		{
			WaterArea& area = g_Areas[g_SelectedArea];
			ImGui::SeparatorText("Selected Area");
			ImGui::Text("Vertices: %d   Selected: %d",
				static_cast<int>(area.points.size()),
				g_SelectedVertex);
			float height = area.height;
			if (ImGui::DragFloat("Height", &height, 0.005f, -100.0f, 100.0f, "%.3f"))
			{
				area.height = height;
				MarkChanged();
			}
			if (ImGui::IsItemActivated())
			{
				PushUndo();
			}
			if (ImGui::Button("Fit Height to Ground"))
			{
				FitSelectedHeightToGround();
			}
			if (area.points.size() < 3)
			{
				ImGui::TextDisabled("Needs 3 or more vertices to draw");
			}
			else if (g_SelectedArea < static_cast<int>(g_DrawRanges.size()) &&
				!g_DrawRanges[g_SelectedArea].triangulated)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Outline crosses itself");
			}
		}

		ImGui::Separator();
		ImGui::Text("%s%s", WATER_AREA_FILE, g_Dirty ? "  *unsaved*" : "");
		if (ImGui::Button("Save"))
		{
			if (SaveAreas())
			{
				g_Dirty = false;
				g_Status = "Saved: " + std::string(WATER_AREA_FILE);
			}
			else
			{
				g_Status = "Save failed";
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Reload"))
		{
			PushUndo();
			LoadAreas();
			ClampSelection();
			g_Dirty = false;
			g_Status = "Reloaded";
		}
		ImGui::SameLine();
		if (ImGui::Button("Undo"))
		{
			Undo();
		}
		ImGui::SameLine();
		if (ImGui::Button("Exit"))
		{
			open = false;
		}
		if (!g_Status.empty())
		{
			ImGui::TextUnformatted(g_Status.c_str());
		}
	}
	ImGui::End();

	if (!open)
	{
		SetEditing(false);
	}
}

void Water_DrawDebug(void)
{
#if defined(_DEBUG)
	if (Direct3D_IsTakingScreenshot())
	{
		return;
	}
	ImGui::Begin("Expo Water");
	ImGui::Checkbox("Enabled", &g_Settings.enabled);
	ImGui::TextUnformatted("Edit areas with F8 (Water Editor)");
	ImGui::SeparatorText("Shape");
	ImGui::SliderFloat("Edge Softness", &g_Settings.edgeSoftness, 0.01f, 4.0f, "%.2f");
	ImGui::SliderFloat("Depth Ramp", &g_Settings.depthRamp, 0.1f, 40.0f, "%.2f");
	ImGui::SeparatorText("Wave");
	ImGui::SliderFloat("Strength", &g_Settings.waveStrength, 0.0f, 4.0f, "%.2f");
	ImGui::SliderFloat("Wavelength", &g_Settings.waveScale, 0.3f, 4.0f, "%.2f");
	ImGui::SliderFloat("Speed", &g_Settings.waveSpeed, 0.0f, 4.0f, "%.2f");
	ImGui::SeparatorText("Look");
	ImGui::ColorEdit4("Shallow", g_Settings.shallowColor);
	ImGui::ColorEdit4("Deep", g_Settings.deepColor);
	ImGui::SliderFloat("Sky Reflection", &g_Settings.skyReflection, 0.0f, 2.0f, "%.2f");
	ImGui::SliderFloat("Sun Glint", &g_Settings.sunGlint, 0.0f, 4.0f, "%.2f");
	ImGui::SliderFloat("Caustics", &g_Settings.caustics, 0.0f, 3.0f, "%.2f");
	ImGui::SliderFloat("Foam", &g_Settings.foam, 0.0f, 1.0f, "%.2f");
	ImGui::Separator();
	if (ImGui::Button("Reset"))
	{
		g_Settings = MakeDefaultSettings();
	}
	ImGui::End();
#endif
}
