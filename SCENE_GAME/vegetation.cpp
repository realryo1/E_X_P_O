#include "vegetation.h"

#include "billboard.h"
#include "camera.h"
#include "course.h"
#include "field.h"
#include "imgui/imgui.h"
#include "input_manager.h"
#include "keyboard.h"
#include "main.h"
#include "mouse.h"
#include "photomode.h"
#include "player.h"
#include "renderer.h"
#include "texture.h"
#include "playercamera.h"

#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace DirectX;

namespace
{
	const char* VEGETATION_DIRECTORY = "asset\\vegetation";
	const char* VEGETATION_PLACEMENT_FILE = "asset\\vegetation\\placement.txt";
	const float DEFAULT_TYPE_HEIGHT = 2.0f;
	const size_t MAX_INSTANCES = 30000;
	const size_t MAX_UNDO = 20;
	const float STROKE_INTERVAL = 0.05f;
	const float PICK_MAX_DISTANCE = 400.0f;
	const float PICK_STEP = 1.0f;

	struct PlantType
	{
		std::string file;
		std::string path;
		ID3D11ShaderResourceView* texture = nullptr;
		Billboard* billboard = nullptr;
		float aspect = 1.0f;
		float height = DEFAULT_TYPE_HEIGHT;
		bool enabled = true;
	};

	struct Instance
	{
		std::string file;
		int type = -1;
		XMFLOAT3 pos = {};
		float scale = 1.0f;
		bool flip = false;
	};

	std::vector<PlantType> g_Types;
	std::vector<Instance> g_Instances;
	std::vector<std::vector<Instance>> g_Undo;
	std::mt19937 g_Random(12345);

	bool g_Editing = false;
	bool g_Dirty = false;
	bool g_StrokeActive = false;
	float g_StrokeTimer = 0.0f;
	std::string g_Status;

	bool g_Visible = true;
	float g_DrawDistance = 150.0f;
	float g_BrushRadius = 4.0f;
	int g_BrushDensity = 4;
	float g_ScaleMin = 0.8f;
	float g_ScaleMax = 1.3f;
	bool g_EraseAllTypes = false;

	bool g_HasBrush = false;
	XMFLOAT3 g_BrushCenter = {};

	float RandomRange(float minValue, float maxValue)
	{
		std::uniform_real_distribution<float> dist(minValue, maxValue);
		return dist(g_Random);
	}

	float DistanceSquaredXZ(const XMFLOAT3& a, const XMFLOAT3& b)
	{
		const float dx = a.x - b.x;
		const float dz = a.z - b.z;
		return dx * dx + dz * dz;
	}

	int FindTypeIndex(const std::string& file)
	{
		for (size_t i = 0; i < g_Types.size(); ++i)
		{
			if (g_Types[i].file == file)
			{
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	void RelinkInstances(void)
	{
		for (Instance& instance : g_Instances)
		{
			instance.type = FindTypeIndex(instance.file);
		}
	}

	bool HasImageExtension(const std::string& name)
	{
		const size_t dot = name.find_last_of('.');
		if (dot == std::string::npos)
		{
			return false;
		}
		std::string extension = name.substr(dot);
		std::transform(
			extension.begin(),
			extension.end(),
			extension.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return extension == ".png" || extension == ".jpg" ||
			extension == ".jpeg" || extension == ".tga";
	}

	void ClearTypes(void)
	{
		for (PlantType& type : g_Types)
		{
			SAFE_DELETE(type.billboard);
		}
		g_Types.clear();
	}

	// asset\vegetation の画像を列挙して種類を作る。高さ・有効状態は同名の既存設定を引き継ぐ。
	void ScanTypes(void)
	{
		std::vector<PlantType> previous;
		previous.swap(g_Types);
		for (PlantType& type : previous)
		{
			SAFE_DELETE(type.billboard);
		}

		CreateDirectoryA("asset", nullptr);
		CreateDirectoryA(VEGETATION_DIRECTORY, nullptr);

		const std::string pattern = std::string(VEGETATION_DIRECTORY) + "\\*.*";
		WIN32_FIND_DATAA findData = {};
		HANDLE findHandle = FindFirstFileA(pattern.c_str(), &findData);
		if (findHandle != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					continue;
				}
				const std::string name = findData.cFileName;
				if (!HasImageExtension(name))
				{
					continue;
				}

				PlantType type;
				type.file = name;
				type.path = std::string(VEGETATION_DIRECTORY) + "\\" + name;
				for (const PlantType& old : previous)
				{
					if (old.file == name)
					{
						type.height = old.height;
						type.enabled = old.enabled;
						break;
					}
				}
				g_Types.push_back(std::move(type));
			} while (FindNextFileA(findHandle, &findData));
			FindClose(findHandle);
		}

		std::sort(
			g_Types.begin(),
			g_Types.end(),
			[](const PlantType& a, const PlantType& b) { return a.file < b.file; });

		for (PlantType& type : g_Types)
		{
			type.billboard = new Billboard(
				{ 0.0f, 0.0f, 0.0f },
				{ 1.0f, 1.0f },
				{ 0.0f, 0.0f, 0.0f },
				type.path.c_str(),
				true);
			// 向きは Draw 側で Y 軸だけカメラへ向ける（木が傾かないよう固定板で描く）。
			type.billboard->SetBillboardMode(false);
			type.billboard->SetWallFadeEnabled(false);
			type.billboard->SetIgnoreLighting(true);

			const std::wstring widePath(type.path.begin(), type.path.end());
			type.texture = LoadTexture(widePath.c_str());
			if (type.texture)
			{
				ID3D11Resource* resource = nullptr;
				type.texture->GetResource(&resource);
				ID3D11Texture2D* texture2d = nullptr;
				if (resource &&
					SUCCEEDED(resource->QueryInterface(
						__uuidof(ID3D11Texture2D),
						reinterpret_cast<void**>(&texture2d))))
				{
					D3D11_TEXTURE2D_DESC desc = {};
					texture2d->GetDesc(&desc);
					if (desc.Height > 0)
					{
						type.aspect = static_cast<float>(desc.Width) /
							static_cast<float>(desc.Height);
					}
					texture2d->Release();
				}
				SAFE_RELEASE(resource);
			}
		}
		RelinkInstances();
	}

	void LoadPlacement(void)
	{
		g_Instances.clear();
		std::ifstream file(VEGETATION_PLACEMENT_FILE);
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
			if (line.compare(0, 2, "h ") == 0)
			{
				// h <file> <height>
				const size_t split = line.find_last_of(' ');
				if (split == std::string::npos || split <= 2)
				{
					continue;
				}
				const int index = FindTypeIndex(line.substr(2, split - 2));
				if (index >= 0)
				{
					g_Types[index].height = std::strtof(line.c_str() + split + 1, nullptr);
				}
			}
			else if (line.compare(0, 2, "p ") == 0)
			{
				// p <x> <y> <z> <scale> <flip> <file>
				Instance instance;
				int flip = 0;
				int consumed = 0;
				if (sscanf_s(
					line.c_str() + 2,
					"%f %f %f %f %d %n",
					&instance.pos.x,
					&instance.pos.y,
					&instance.pos.z,
					&instance.scale,
					&flip,
					&consumed) < 5)
				{
					continue;
				}
				instance.flip = flip != 0;
				instance.file = line.substr(2 + consumed);
				instance.type = FindTypeIndex(instance.file);
				if (!instance.file.empty() && g_Instances.size() < MAX_INSTANCES)
				{
					g_Instances.push_back(std::move(instance));
				}
			}
		}
	}

	bool SavePlacement(void)
	{
		CreateDirectoryA("asset", nullptr);
		CreateDirectoryA(VEGETATION_DIRECTORY, nullptr);

		std::ofstream file(VEGETATION_PLACEMENT_FILE, std::ios::trunc);
		if (!file)
		{
			return false;
		}

		char text[512] = {};
		file << "# vegetation placement\n";
		file << "# h <file> <height>\n";
		file << "# p <x> <y> <z> <scale> <flip> <file>\n";
		for (const PlantType& type : g_Types)
		{
			std::snprintf(text, sizeof(text), "h %s %.3f\n", type.file.c_str(), type.height);
			file << text;
		}
		for (const Instance& instance : g_Instances)
		{
			std::snprintf(
				text,
				sizeof(text),
				"p %.3f %.3f %.3f %.3f %d %s\n",
				instance.pos.x,
				instance.pos.y,
				instance.pos.z,
				instance.scale,
				instance.flip ? 1 : 0,
				instance.file.c_str());
			file << text;
		}
		return file.good();
	}

	void PushUndo(void)
	{
		g_Undo.push_back(g_Instances);
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
		g_Instances = std::move(g_Undo.back());
		g_Undo.pop_back();
		g_Dirty = true;
	}

	// 画面座標からカメラのレイを作る。
	bool BuildRay(float screenX, float screenY, XMVECTOR* origin, XMVECTOR* direction)
	{
		Camera* camera = GetCamera();
		if (!camera)
		{
			return false;
		}
		const ImVec2 size = ImGui::GetIO().DisplaySize;
		if (size.x <= 1.0f || size.y <= 1.0f)
		{
			return false;
		}
		const XMMATRIX view = camera->GetView();
		const XMMATRIX proj = camera->GetProjection();
		const XMVECTOR nearPoint = XMVector3Unproject(
			XMVectorSet(screenX, screenY, 0.0f, 1.0f),
			0.0f, 0.0f, size.x, size.y, 0.0f, 1.0f,
			proj, view, XMMatrixIdentity());
		const XMVECTOR farPoint = XMVector3Unproject(
			XMVectorSet(screenX, screenY, 1.0f, 1.0f),
			0.0f, 0.0f, size.x, size.y, 0.0f, 1.0f,
			proj, view, XMMatrixIdentity());
		*origin = nearPoint;
		*direction = XMVector3Normalize(XMVectorSubtract(farPoint, nearPoint));
		return true;
	}

	// レイを床へ進めて最初に床より下へ入る点を求める。
	bool PickGround(float screenX, float screenY, XMFLOAT3* outPoint)
	{
		XMVECTOR origin;
		XMVECTOR direction;
		if (!BuildRay(screenX, screenY, &origin, &direction))
		{
			return false;
		}

		float previousT = 0.0f;
		bool previousAbove = false;
		for (float t = 0.0f; t <= PICK_MAX_DISTANCE; t += PICK_STEP)
		{
			XMFLOAT3 point;
			XMStoreFloat3(&point, XMVectorAdd(origin, XMVectorScale(direction, t)));
			float groundY = 0.0f;
			if (!Field_SampleGroundY(point.x, point.z, &groundY))
			{
				previousAbove = false;
				continue;
			}

			const bool above = point.y > groundY;
			if (!above && previousAbove)
			{
				// previousT..t の間で床をまたいだので二分探索で詰める。
				float low = previousT;
				float high = t;
				for (int i = 0; i < 10; ++i)
				{
					const float mid = (low + high) * 0.5f;
					XMFLOAT3 midPoint;
					XMStoreFloat3(&midPoint, XMVectorAdd(origin, XMVectorScale(direction, mid)));
					float midGround = 0.0f;
					if (Field_SampleGroundY(midPoint.x, midPoint.z, &midGround) &&
						midPoint.y <= midGround)
					{
						high = mid;
					}
					else
					{
						low = mid;
					}
				}
				XMStoreFloat3(outPoint, XMVectorAdd(origin, XMVectorScale(direction, high)));
				Field_SampleGroundY(outPoint->x, outPoint->z, &outPoint->y);
				return true;
			}
			previousAbove = above;
			previousT = t;
		}
		return false;
	}

	int PickRandomEnabledType(void)
	{
		std::vector<int> candidates;
		for (size_t i = 0; i < g_Types.size(); ++i)
		{
			if (g_Types[i].enabled)
			{
				candidates.push_back(static_cast<int>(i));
			}
		}
		if (candidates.empty())
		{
			return -1;
		}
		std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
		return candidates[dist(g_Random)];
	}

	void PlantAtBrush(void)
	{
		for (int i = 0; i < g_BrushDensity; ++i)
		{
			if (g_Instances.size() >= MAX_INSTANCES)
			{
				g_Status = "配置数の上限に達しました";
				return;
			}
			const int typeIndex = PickRandomEnabledType();
			if (typeIndex < 0)
			{
				return;
			}

			const float angle = RandomRange(0.0f, XM_2PI);
			const float distance = g_BrushRadius * std::sqrt(RandomRange(0.0f, 1.0f));
			Instance instance;
			instance.file = g_Types[typeIndex].file;
			instance.type = typeIndex;
			instance.pos.x = g_BrushCenter.x + std::cos(angle) * distance;
			instance.pos.z = g_BrushCenter.z + std::sin(angle) * distance;
			if (!Field_SampleGroundY(instance.pos.x, instance.pos.z, &instance.pos.y))
			{
				continue;
			}
			instance.scale = RandomRange(
				(std::min)(g_ScaleMin, g_ScaleMax),
				(std::max)(g_ScaleMin, g_ScaleMax));
			instance.flip = RandomRange(0.0f, 1.0f) < 0.5f;
			g_Instances.push_back(std::move(instance));
			g_Dirty = true;
		}
	}

	void EraseAtBrush(void)
	{
		const float radiusSquared = g_BrushRadius * g_BrushRadius;
		const size_t before = g_Instances.size();
		g_Instances.erase(
			std::remove_if(
				g_Instances.begin(),
				g_Instances.end(),
				[&](const Instance& instance)
				{
					if (DistanceSquaredXZ(instance.pos, g_BrushCenter) > radiusSquared)
					{
						return false;
					}
					if (g_EraseAllTypes)
					{
						return true;
					}
					return instance.type >= 0 &&
						instance.type < static_cast<int>(g_Types.size()) &&
						g_Types[instance.type].enabled;
				}),
			g_Instances.end());
		if (g_Instances.size() != before)
		{
			g_Dirty = true;
		}
	}

	void SetEditing(bool editing)
	{
		if (g_Editing == editing)
		{
			return;
		}
		g_Editing = editing;
		g_StrokeActive = false;
		g_HasBrush = false;
		if (editing)
		{
			ScanTypes();
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

	void DrawBrushRing(void)
	{
		Camera* camera = GetCamera();
		if (!camera || !g_HasBrush)
		{
			return;
		}
		const ImVec2 size = ImGui::GetIO().DisplaySize;
		const XMMATRIX view = camera->GetView();
		const XMMATRIX proj = camera->GetProjection();

		const int segments = 48;
		std::vector<ImVec2> points;
		points.reserve(segments);
		for (int i = 0; i < segments; ++i)
		{
			const float angle = XM_2PI * static_cast<float>(i) / static_cast<float>(segments);
			XMFLOAT3 world = {
				g_BrushCenter.x + std::cos(angle) * g_BrushRadius,
				g_BrushCenter.y,
				g_BrushCenter.z + std::sin(angle) * g_BrushRadius,
			};
			Field_SampleGroundY(world.x, world.z, &world.y);
			world.y += 0.05f;
			const XMVECTOR screen = XMVector3Project(
				XMLoadFloat3(&world),
				0.0f, 0.0f, size.x, size.y, 0.0f, 1.0f,
				proj, view, XMMatrixIdentity());
			const float depth = XMVectorGetZ(screen);
			if (depth < 0.0f || depth > 1.0f)
			{
				return;
			}
			points.push_back(ImVec2(XMVectorGetX(screen), XMVectorGetY(screen)));
		}
		const ImU32 color = Keyboard_IsKeyDown(KK_X) ||
			(Keyboard_IsKeyDown(KK_LEFTCONTROL) && ImGui::GetIO().MouseDown[0])
			? IM_COL32(255, 80, 80, 255)
			: IM_COL32(80, 255, 120, 255);
		ImGui::GetForegroundDrawList()->AddPolyline(
			points.data(),
			static_cast<int>(points.size()),
			color,
			ImDrawFlags_Closed,
			2.0f);
	}
}

void Vegetation_Initialize(void)
{
	g_Editing = false;
	g_Dirty = false;
	g_StrokeActive = false;
	g_HasBrush = false;
	g_Undo.clear();
	g_Status.clear();
	ScanTypes();
	LoadPlacement();
}

void Vegetation_Finalize(void)
{
	SetEditing(false);
	ClearTypes();
	g_Instances.clear();
	g_Undo.clear();
}

bool Vegetation_IsEditing(void)
{
	return g_Editing;
}

void Vegetation_Update(void)
{
	if (!g_Editing)
	{
		// フォトモードやメニュー中は入らない。
		if (Keyboard_IsKeyDownTrigger(KK_F9) &&
			!PhotoMode_IsActive() &&
			!Course_IsMenuOpen() &&
			Player_IsReady())
		{
			SetEditing(true);
		}
		return;
	}

	if (Keyboard_IsKeyDownTrigger(KK_F9) ||
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

	// 右クリックで視点操作中（マウス固定）は画面中央、それ以外はカーソル位置で床を狙う。
	const bool looking = !Mouse_IsVisible();
	const ImVec2 size = io.DisplaySize;
	const ImVec2 aim = looking ? ImVec2(size.x * 0.5f, size.y * 0.5f) : io.MousePos;
	g_HasBrush = (looking || !io.WantCaptureMouse) && PickGround(aim.x, aim.y, &g_BrushCenter);

	const bool ctrl = Keyboard_IsKeyDown(KK_LEFTCONTROL);
	const bool mouseBrush = !looking && !io.WantCaptureMouse && io.MouseDown[0];
	const bool erase = Keyboard_IsKeyDown(KK_X) || (mouseBrush && ctrl);
	const bool plant = !erase && (Keyboard_IsKeyDown(KK_F) || mouseBrush);
	const bool stroke = g_HasBrush && (erase || plant);

	if (!stroke)
	{
		g_StrokeActive = false;
		g_StrokeTimer = 0.0f;
		return;
	}
	if (!g_StrokeActive)
	{
		g_StrokeActive = true;
		g_StrokeTimer = 0.0f;
		PushUndo();
	}

	g_StrokeTimer -= io.DeltaTime;
	if (g_StrokeTimer > 0.0f)
	{
		return;
	}
	g_StrokeTimer = STROKE_INTERVAL;
	if (erase)
	{
		EraseAtBrush();
	}
	else
	{
		PlantAtBrush();
	}
}

void Vegetation_Draw(void)
{
	if (!g_Visible || g_Instances.empty())
	{
		return;
	}
	Camera* camera = GetCamera();
	if (!camera)
	{
		return;
	}

	const XMFLOAT3 cameraPos = camera->GetPos();
	const XMFLOAT3 lookAt = camera->GetAtPos();
	XMFLOAT3 forward = {
		lookAt.x - cameraPos.x,
		lookAt.y - cameraPos.y,
		lookAt.z - cameraPos.z,
	};
	const float forwardLength = std::sqrt(
		forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
	if (forwardLength > 0.0001f)
	{
		forward.x /= forwardLength;
		forward.y /= forwardLength;
		forward.z /= forwardLength;
	}

	struct Visible
	{
		const Instance* instance;
		float distanceSquared;
	};
	static std::vector<Visible> visible;
	visible.clear();
	const float limitSquared = g_DrawDistance * g_DrawDistance;
	for (const Instance& instance : g_Instances)
	{
		if (instance.type < 0 || instance.type >= static_cast<int>(g_Types.size()))
		{
			continue;
		}
		const float dx = instance.pos.x - cameraPos.x;
		const float dy = instance.pos.y - cameraPos.y;
		const float dz = instance.pos.z - cameraPos.z;
		const float distanceSquared = dx * dx + dy * dy + dz * dz;
		if (distanceSquared > limitSquared)
		{
			continue;
		}
		// 背後は描かない（大きい木の縁が見切れないよう少し余裕を持たせる）。
		if (dx * forward.x + dy * forward.y + dz * forward.z < -4.0f)
		{
			continue;
		}
		visible.push_back({ &instance, distanceSquared });
	}

	// 半透明は深度を書かないので、奥から手前へ並べて描く。
	std::sort(
		visible.begin(),
		visible.end(),
		[](const Visible& a, const Visible& b)
		{
			return a.distanceSquared > b.distanceSquared;
		});

	for (const Visible& entry : visible)
	{
		const Instance& instance = *entry.instance;
		PlantType& type = g_Types[instance.type];
		if (!type.billboard)
		{
			continue;
		}

		const float height = type.height * instance.scale;
		const float width = height * type.aspect;
		const float yaw = XMConvertToDegrees(std::atan2(
			cameraPos.x - instance.pos.x,
			cameraPos.z - instance.pos.z));
		type.billboard->SetDrawSize({ instance.flip ? -width : width, height });
		type.billboard->SetPos({
			instance.pos.x,
			instance.pos.y + height * 0.5f,
			instance.pos.z });
		type.billboard->SetRotation({ 0.0f, yaw, 0.0f });
		type.billboard->Draw();
	}
}

void Vegetation_DrawOverlay(void)
{
	if (!g_Editing || Direct3D_IsTakingScreenshot())
	{
		return;
	}

	DrawBrushRing();

	bool open = true;
	ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Vegetation Editor", &open))
	{
		ImGui::TextUnformatted("WASD / Space / Shift: move   RMB: look");
		ImGui::TextUnformatted("LMB / F (hold): plant   Ctrl+LMB / X (hold): erase");
		ImGui::TextUnformatted("Ctrl+Z: undo   F9 / Esc: exit");
		ImGui::Separator();

		ImGui::SliderFloat("Brush Radius", &g_BrushRadius, 0.5f, 20.0f, "%.1f m");
		ImGui::SliderInt("Density / tick", &g_BrushDensity, 1, 20);
		ImGui::SliderFloat("Scale Min", &g_ScaleMin, 0.2f, 3.0f, "%.2f");
		ImGui::SliderFloat("Scale Max", &g_ScaleMax, 0.2f, 3.0f, "%.2f");
		ImGui::Checkbox("Erase all types", &g_EraseAllTypes);
		ImGui::Checkbox("Show vegetation", &g_Visible);
		ImGui::SliderFloat("Draw Distance", &g_DrawDistance, 20.0f, 500.0f, "%.0f m");

		ImGui::Separator();
		ImGui::Text("Types (%d)  - %s", static_cast<int>(g_Types.size()), VEGETATION_DIRECTORY);
		if (ImGui::Button("Rescan Images"))
		{
			ScanTypes();
		}
		if (g_Types.empty())
		{
			ImGui::TextUnformatted("No images. Put png/jpg/tga into asset\\vegetation.");
		}
		for (size_t i = 0; i < g_Types.size(); ++i)
		{
			PlantType& type = g_Types[i];
			ImGui::PushID(static_cast<int>(i));
			ImGui::Checkbox("##enabled", &type.enabled);
			ImGui::SameLine();
			if (type.texture)
			{
				const float thumbHeight = 40.0f;
				ImGui::Image(
					(ImTextureID)(uintptr_t)type.texture,
					ImVec2(thumbHeight * type.aspect, thumbHeight));
				ImGui::SameLine();
			}
			int count = 0;
			for (const Instance& instance : g_Instances)
			{
				if (instance.type == static_cast<int>(i))
				{
					++count;
				}
			}
			ImGui::BeginGroup();
			ImGui::Text("%s (%d)", type.file.c_str(), count);
			ImGui::SetNextItemWidth(140.0f);
			if (ImGui::DragFloat("Height", &type.height, 0.05f, 0.1f, 50.0f, "%.2f m"))
			{
				g_Dirty = true;
			}
			ImGui::EndGroup();
			ImGui::PopID();
		}

		ImGui::Separator();
		ImGui::Text("Placed: %d / %d%s",
			static_cast<int>(g_Instances.size()),
			static_cast<int>(MAX_INSTANCES),
			g_Dirty ? "  *unsaved*" : "");
		if (ImGui::Button("Save"))
		{
			if (SavePlacement())
			{
				g_Dirty = false;
				g_Status = "Saved: " + std::string(VEGETATION_PLACEMENT_FILE);
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
			LoadPlacement();
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
