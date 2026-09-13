#include "course.h"
#include "player.h"
#include "billboard.h"
#include "sprite3d.h"
#include "font.h"
#include "define.h"
#include "keyboard.h"
#include "main.h"
#include "renderer.h"
#include "imgui/imgui.h"

#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace DirectX;

namespace
{
	const char* COURSE_DIRECTORY = "asset\\course";
	const char* COURSE_TEXTURE = "asset\\texture\\makulogo.png";
	const float COURSE_RING_SIZE = 2.0f;
	const float COURSE_GATE_RADIUS = COURSE_RING_SIZE * 0.5f;

	struct CourseData
	{
		std::string filePath;
		std::string name;
		std::vector<XMFLOAT3> points;
		std::vector<std::string> logs;
	};

	enum class CourseMode
	{
		FreeFlight,
		CourseCreate,
		RaceCountdown,
		RaceRunning,
		RaceGoal,
	};

	std::vector<CourseData> g_Courses;
	CourseData g_WorkingCourse;
	CourseData g_RaceCourse;
	std::vector<Billboard*> g_Rings;
	Sprite3D* g_StartMarker = nullptr;

	CourseMode g_Mode = CourseMode::FreeFlight;
	int g_SelectedCourse = -1;
	int g_WorkingCourseIndex = -1;
	int g_NextGate = 0;
	XMFLOAT3 g_LastPlayerPos = {};
	std::chrono::steady_clock::time_point g_CountdownStarted;
	std::chrono::steady_clock::time_point g_RaceStarted;
	double g_RaceElapsed = 0.0;
	bool g_HasLastSaveResult = false;
	bool g_LastSaveSucceeded = false;
	char g_CourseName[128] = "course";

	DrawFont* g_pTimerText = nullptr;
	DrawFont* g_pCountdownText = nullptr;
	DrawFont* g_pGoalText = nullptr;

	std::string Trim(const std::string& value)
	{
		size_t begin = 0;
		while (begin < value.size() &&
			std::isspace(static_cast<unsigned char>(value[begin])))
		{
			++begin;
		}

		size_t end = value.size();
		while (end > begin &&
			std::isspace(static_cast<unsigned char>(value[end - 1])))
		{
			--end;
		}
		return value.substr(begin, end - begin);
	}

	std::string StripYamlQuotes(const std::string& value)
	{
		std::string result = Trim(value);
		if (result.size() >= 2 &&
			((result.front() == '"' && result.back() == '"') ||
				(result.front() == '\'' && result.back() == '\'')))
		{
			result = result.substr(1, result.size() - 2);
		}
		return result;
	}

	bool StartsWith(const std::string& value, const char* prefix)
	{
		const std::string prefixString(prefix);
		return value.size() >= prefixString.size() &&
			value.compare(0, prefixString.size(), prefixString) == 0;
	}

	float ReadYamlFloat(const std::string& line, const char* key)
	{
		const size_t keyPosition = line.find(key);
		if (keyPosition == std::string::npos)
		{
			return 0.0f;
		}
		return std::strtof(line.c_str() + keyPosition + std::strlen(key), nullptr);
	}

	bool LoadCourseFile(const std::string& path, CourseData* course)
	{
		if (!course)
		{
			return false;
		}

		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			return false;
		}

		CourseData loaded;
		loaded.filePath = path;
		bool readingPoints = false;
		bool readingLogs = false;
		int currentPoint = -1;
		std::string line;
		while (std::getline(file, line))
		{
			const std::string trimmed = Trim(line);
			if (trimmed.empty() || trimmed[0] == '#')
			{
				continue;
			}

			if (StartsWith(trimmed, "name:"))
			{
				loaded.name = StripYamlQuotes(trimmed.substr(5));
				readingPoints = false;
				readingLogs = false;
				continue;
			}
			if (trimmed == "points:")
			{
				readingPoints = true;
				readingLogs = false;
				continue;
			}
			if (trimmed == "logs:")
			{
				readingPoints = false;
				readingLogs = true;
				continue;
			}

			if (readingPoints && StartsWith(trimmed, "- x:"))
			{
				XMFLOAT3 point = {};
				point.x = ReadYamlFloat(trimmed, "x:");
				loaded.points.push_back(point);
				currentPoint = static_cast<int>(loaded.points.size()) - 1;
				continue;
			}
			if (readingPoints && currentPoint >= 0 &&
				currentPoint < static_cast<int>(loaded.points.size()))
			{
				if (StartsWith(trimmed, "y:"))
				{
					loaded.points[currentPoint].y = ReadYamlFloat(trimmed, "y:");
				}
				else if (StartsWith(trimmed, "z:"))
				{
					loaded.points[currentPoint].z = ReadYamlFloat(trimmed, "z:");
				}
				continue;
			}
			if (readingLogs && StartsWith(trimmed, "-"))
			{
				loaded.logs.push_back(StripYamlQuotes(trimmed.substr(1)));
			}
		}

		if (loaded.name.empty())
		{
			const size_t slash = path.find_last_of("\\/");
			const size_t dot = path.find_last_of('.');
			const size_t begin = slash == std::string::npos ? 0 : slash + 1;
			const size_t end = dot == std::string::npos ? path.size() : dot;
			loaded.name = path.substr(begin, end - begin);
		}

		*course = loaded;
		return true;
	}

	std::string SanitizeFileStem(const std::string& value)
	{
		std::string stem;
		for (const unsigned char character : value)
		{
			if (std::isalnum(character) || character == '_' || character == '-')
			{
				stem.push_back(static_cast<char>(character));
			}
			else
			{
				stem.push_back('_');
			}
		}
		if (stem.empty())
		{
			stem = "course";
		}
		return stem;
	}

	bool SaveCourseFile(const CourseData& course)
	{
		CreateDirectoryA("asset", nullptr);
		CreateDirectoryA(COURSE_DIRECTORY, nullptr);

		std::ofstream file(course.filePath, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			return false;
		}

		file << "name: " << course.name << "\n";
		file << "points:\n";
		for (const XMFLOAT3& point : course.points)
		{
			file << "  - x: " << point.x << "\n";
			file << "    y: " << point.y << "\n";
			file << "    z: " << point.z << "\n";
		}
		file << "logs:\n";
		for (const std::string& log : course.logs)
		{
			file << "  - \"" << log << "\"\n";
		}
		return file.good();
	}

	void ClearRings(void)
	{
		for (Billboard* ring : g_Rings)
		{
			delete ring;
		}
		g_Rings.clear();
	}

	XMFLOAT3 GetGateDirection(
		const std::vector<XMFLOAT3>& points,
		size_t index)
	{
		XMFLOAT3 direction = { 0.0f, 0.0f, 1.0f };
		if (points.size() >= 2)
		{
			if (index > 0)
			{
				direction.x = points[index].x - points[index - 1].x;
				direction.z = points[index].z - points[index - 1].z;
			}
			else
			{
				direction.x = points[1].x - points[0].x;
				direction.z = points[1].z - points[0].z;
			}
		}

		const float length = std::sqrt(
			direction.x * direction.x + direction.z * direction.z);
		if (length > 0.0001f)
		{
			direction.x /= length;
			direction.z /= length;
		}
		else
		{
			direction = { 0.0f, 0.0f, 1.0f };
		}
		return direction;
	}

	void RebuildRings(const std::vector<XMFLOAT3>& points)
	{
		ClearRings();
		SAFE_DELETE(g_StartMarker);
		if (!points.empty())
		{
			g_StartMarker = new Sprite3D(
				points[0],
				{ 0.4f, 0.4f, 0.4f },
				{ 0.0f, 0.0f, 0.0f },
				"asset\\model\\cube.fbx",
				S_PBR);
		}

		for (size_t i = 1; i < points.size(); ++i)
		{
			Billboard* ring = new Billboard(
				points[i],
				{ COURSE_RING_SIZE, COURSE_RING_SIZE },
				{ 0.0f, 0.0f, 0.0f },
				COURSE_TEXTURE,
				true);
			ring->SetBillboardMode(true);
			ring->SetWallFadeEnabled(false);
			ring->SetIgnoreLighting(true);
			g_Rings.push_back(ring);
		}
	}

	void LoadCourseList(void)
	{
		g_Courses.clear();
		CreateDirectoryA("asset", nullptr);
		CreateDirectoryA(COURSE_DIRECTORY, nullptr);

		const std::string pattern = std::string(COURSE_DIRECTORY) + "\\*.yml";
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

				CourseData course;
				const std::string path =
					std::string(COURSE_DIRECTORY) + "\\" + findData.cFileName;
				if (LoadCourseFile(path, &course))
				{
					g_Courses.push_back(course);
				}
			} while (FindNextFileA(findHandle, &findData));
			FindClose(findHandle);
		}

		std::sort(
			g_Courses.begin(),
			g_Courses.end(),
			[](const CourseData& lhs, const CourseData& rhs)
			{
				return lhs.name < rhs.name;
			});

		if (g_Courses.empty())
		{
			g_SelectedCourse = -1;
		}
		else if (g_SelectedCourse < 0 ||
			g_SelectedCourse >= static_cast<int>(g_Courses.size()))
		{
			g_SelectedCourse = 0;
		}
	}

	void SetCourseNameBuffer(const std::string& name)
	{
		std::snprintf(
			g_CourseName,
			sizeof(g_CourseName),
			"%s",
			name.c_str());
		g_CourseName[sizeof(g_CourseName) - 1] = '\0';
	}

	void StartCourseCreate(int courseIndex)
	{
		g_WorkingCourse = {};
		g_WorkingCourseIndex = courseIndex;
		if (courseIndex >= 0 &&
			courseIndex < static_cast<int>(g_Courses.size()))
		{
			g_WorkingCourse = g_Courses[courseIndex];
		}
		else
		{
			g_WorkingCourse.name = "course";
		}
		SetCourseNameBuffer(g_WorkingCourse.name);
		g_HasLastSaveResult = false;
		g_Mode = CourseMode::CourseCreate;
		RebuildRings(g_WorkingCourse.points);
	}

	void ReturnToFreeFlight(void)
	{
		Player_SetControlEnabled(true);
		g_Mode = CourseMode::FreeFlight;
		g_NextGate = 0;
		g_RaceElapsed = 0.0;
		g_WorkingCourseIndex = -1;
		ClearRings();
		SAFE_DELETE(g_StartMarker);
	}

	void SaveWorkingCourse(void)
	{
		g_WorkingCourse.name = Trim(g_CourseName);
		if (g_WorkingCourse.name.empty())
		{
			g_WorkingCourse.name = "course";
			SetCourseNameBuffer(g_WorkingCourse.name);
		}

		const std::string filePath =
			std::string(COURSE_DIRECTORY) + "\\" +
			SanitizeFileStem(g_WorkingCourse.name) + ".yml";
		if (!g_WorkingCourse.filePath.empty() &&
			g_WorkingCourse.filePath != filePath)
		{
			DeleteFileA(g_WorkingCourse.filePath.c_str());
		}
		g_WorkingCourse.filePath = filePath;
		g_LastSaveSucceeded = SaveCourseFile(g_WorkingCourse);
		g_HasLastSaveResult = true;

		if (g_LastSaveSucceeded)
		{
			LoadCourseList();
			for (int i = 0; i < static_cast<int>(g_Courses.size()); ++i)
			{
				if (g_Courses[i].filePath == g_WorkingCourse.filePath)
				{
					g_SelectedCourse = i;
					break;
				}
			}
			ReturnToFreeFlight();
		}
	}

	void AddCoursePoint(void)
	{
		if (!Player_IsReady())
		{
			return;
		}
		g_WorkingCourse.points.push_back(Player_GetPos());
		RebuildRings(g_WorkingCourse.points);
	}

	std::string FormatRaceTime(double seconds)
	{
		const int totalCentiseconds = static_cast<int>(seconds * 100.0);
		const int minutes = totalCentiseconds / 6000;
		const int remaining = totalCentiseconds % 6000;
		const int wholeSeconds = remaining / 100;
		const int centiseconds = remaining % 100;

		char text[32] = {};
		std::snprintf(
			text,
			sizeof(text),
			"%02d:%02d:%02d",
			minutes,
			wholeSeconds,
			centiseconds);
		return text;
	}

	std::string GetTimestamp(void)
	{
		const std::time_t currentTime = std::time(nullptr);
		std::tm localTime = {};
		localtime_s(&localTime, &currentTime);

		char text[32] = {};
		std::strftime(
			text,
			sizeof(text),
			"%Y-%m-%d %H:%M:%S",
			&localTime);
		return text;
	}

	bool CrossedGate(
		const XMFLOAT3& previous,
		const XMFLOAT3& current,
		const XMFLOAT3& center,
		const XMFLOAT3& direction)
	{
		const float previousDistance =
			(previous.x - center.x) * direction.x +
			(previous.z - center.z) * direction.z;
		const float currentDistance =
			(current.x - center.x) * direction.x +
			(current.z - center.z) * direction.z;
		if (previousDistance >= 0.0f || currentDistance < 0.0f)
		{
			return false;
		}

		const float distanceDelta = currentDistance - previousDistance;
		if (distanceDelta <= 0.0001f)
		{
			return false;
		}

		const float ratio = -previousDistance / distanceDelta;
		const XMFLOAT3 hit = {
			previous.x + (current.x - previous.x) * ratio,
			previous.y + (current.y - previous.y) * ratio,
			previous.z + (current.z - previous.z) * ratio,
		};
		const float dx = hit.x - center.x;
		const float dy = hit.y - center.y;
		const float dz = hit.z - center.z;
		const float normalDistance = dx * direction.x + dz * direction.z;
		const float tangentX = dx - normalDistance * direction.x;
		const float tangentZ = dz - normalDistance * direction.z;
		const float radiusSquared =
			tangentX * tangentX + dy * dy + tangentZ * tangentZ;
		return radiusSquared <= COURSE_GATE_RADIUS * COURSE_GATE_RADIUS;
	}

	void EnterGoal(void)
	{
		g_RaceElapsed = std::chrono::duration<double>(
			std::chrono::steady_clock::now() - g_RaceStarted).count();
		g_RaceCourse.logs.push_back(
			GetTimestamp() + " " + FormatRaceTime(g_RaceElapsed));
		SaveCourseFile(g_RaceCourse);

		if (g_SelectedCourse >= 0 &&
			g_SelectedCourse < static_cast<int>(g_Courses.size()))
		{
			g_Courses[g_SelectedCourse].logs = g_RaceCourse.logs;
		}

		Player_SetControlEnabled(true);
		g_Mode = CourseMode::RaceGoal;
	}

	void StartRace(void)
	{
		if (g_SelectedCourse < 0 ||
			g_SelectedCourse >= static_cast<int>(g_Courses.size()) ||
			g_Courses[g_SelectedCourse].points.size() < 2)
		{
			return;
		}

		g_RaceCourse = g_Courses[g_SelectedCourse];
		g_NextGate = 1;
		g_RaceElapsed = 0.0;
		RebuildRings(g_RaceCourse.points);

		const XMFLOAT3 startPosition = g_RaceCourse.points[0];
		Player_WarpTo(startPosition);
		Player_SetControlEnabled(false);
		g_LastPlayerPos = startPosition;
		g_CountdownStarted = std::chrono::steady_clock::now();
		g_Mode = CourseMode::RaceCountdown;
	}

	const char* GetModeLabel(void)
	{
		switch (g_Mode)
		{
		case CourseMode::CourseCreate: return "COURSE CREATE";
		case CourseMode::RaceCountdown: return "RACE COUNTDOWN";
		case CourseMode::RaceRunning: return "RACE";
		case CourseMode::RaceGoal: return "GOAL";
		default: return "FREE FLIGHT";
		}
	}
}

void Course_Initialize(void)
{
	g_Mode = CourseMode::FreeFlight;
	g_SelectedCourse = -1;
	g_WorkingCourseIndex = -1;
	g_HasLastSaveResult = false;
	LoadCourseList();

	g_pTimerText = new DrawFont(
		{ 40.0f, 40.0f },
		28.0f,
		0.0f,
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		"00:00:00",
		TA_START);
	g_pCountdownText = new DrawFont(
		{ SCREEN_X * 0.5f, SCREEN_Y * 0.35f },
		96.0f,
		0.0f,
		{ 1.0f, 0.9f, 0.3f, 1.0f },
		"",
		TA_MIDDLE);
	g_pGoalText = new DrawFont(
		{ SCREEN_X * 0.5f, SCREEN_Y * 0.35f },
		72.0f,
		0.0f,
		{ 1.0f, 0.9f, 0.3f, 1.0f },
		"GOAL",
		TA_MIDDLE);
}

void Course_Finalize(void)
{
	Player_SetControlEnabled(true);
	ClearRings();
	SAFE_DELETE(g_StartMarker);
	SAFE_DELETE(g_pTimerText);
	SAFE_DELETE(g_pCountdownText);
	SAFE_DELETE(g_pGoalText);
	g_Courses.clear();
	g_WorkingCourse = {};
	g_RaceCourse = {};
}

void Course_Update(void)
{
	if (g_Mode == CourseMode::CourseCreate)
	{
		const bool imguiCapturesKeyboard =
			ImGui::GetCurrentContext() != nullptr &&
			ImGui::GetIO().WantCaptureKeyboard;
		if (!imguiCapturesKeyboard &&
			Keyboard_IsKeyDownTrigger(KK_P))
		{
			AddCoursePoint();
		}
		return;
	}

	if (g_Mode == CourseMode::RaceCountdown)
	{
		const double elapsed = std::chrono::duration<double>(
			std::chrono::steady_clock::now() - g_CountdownStarted).count();
		if (elapsed >= 3.0)
		{
			g_Mode = CourseMode::RaceRunning;
			g_RaceStarted = std::chrono::steady_clock::now();
			g_LastPlayerPos = Player_GetPos();
			Player_SetControlEnabled(true);
		}
		return;
	}

	if (g_Mode != CourseMode::RaceRunning || !Player_IsReady())
	{
		return;
	}

	g_RaceElapsed = std::chrono::duration<double>(
		std::chrono::steady_clock::now() - g_RaceStarted).count();
	const XMFLOAT3 currentPlayerPos = Player_GetPos();
	if (g_NextGate < static_cast<int>(g_RaceCourse.points.size()))
	{
		const XMFLOAT3& gate = g_RaceCourse.points[g_NextGate];
		const XMFLOAT3 direction =
			GetGateDirection(g_RaceCourse.points, g_NextGate);
		if (CrossedGate(g_LastPlayerPos, currentPlayerPos, gate, direction))
		{
			++g_NextGate;
			if (g_NextGate >= static_cast<int>(g_RaceCourse.points.size()))
			{
				EnterGoal();
			}
			else
			{
				Player_ActivateDash();
			}
		}
	}
	g_LastPlayerPos = currentPlayerPos;
}

void Course_Draw(void)
{
	if (g_Mode == CourseMode::FreeFlight)
	{
		return;
	}
	if (g_StartMarker)
	{
		g_StartMarker->Draw();
	}
	for (size_t i = 0; i < g_Rings.size(); ++i)
	{
		const int gateIndex = static_cast<int>(i) + 1;
		if ((g_Mode == CourseMode::RaceCountdown ||
			g_Mode == CourseMode::RaceRunning ||
			g_Mode == CourseMode::RaceGoal) &&
			gateIndex < g_NextGate)
		{
			continue;
		}

		Billboard* ring = g_Rings[i];
		if (ring)
		{
			ring->Draw();
		}
	}
}

void Course_DrawHud(void)
{
	if (Direct3D_IsTakingScreenshot())
	{
		return;
	}

	if (g_Mode == CourseMode::RaceCountdown ||
		g_Mode == CourseMode::RaceRunning ||
		g_Mode == CourseMode::RaceGoal)
	{
		if (g_Mode == CourseMode::RaceRunning ||
			g_Mode == CourseMode::RaceGoal)
		{
			if (g_pTimerText)
			{
				g_pTimerText->SetText(FormatRaceTime(g_RaceElapsed));
				g_pTimerText->Draw();
			}
		}
		if (g_Mode == CourseMode::RaceCountdown && g_pCountdownText)
		{
			const double elapsed = std::chrono::duration<double>(
				std::chrono::steady_clock::now() - g_CountdownStarted).count();
			const int number = 3 - static_cast<int>(elapsed);
			if (number > 0)
			{
				g_pCountdownText->SetText(std::to_string(number));
				g_pCountdownText->Draw();
			}
		}
		if (g_Mode == CourseMode::RaceGoal && g_pGoalText)
		{
			g_pGoalText->Draw();
		}
	}
}

void Course_DrawDebug(void)
{
	if (Direct3D_IsTakingScreenshot())
	{
		return;
	}

	ImGui::Begin("Expo Course");
	ImGui::Text("Mode: %s", GetModeLabel());

	if (g_Mode == CourseMode::FreeFlight)
	{
		if (ImGui::Button("New Course"))
		{
			StartCourseCreate(-1);
		}
		ImGui::SameLine();
		if (ImGui::Button("Reload List"))
		{
			LoadCourseList();
		}

		ImGui::Separator();
		ImGui::Text("Course List");
		for (int i = 0; i < static_cast<int>(g_Courses.size()); ++i)
		{
			const std::string label =
				g_Courses[i].name + "##course_" + std::to_string(i);
			if (ImGui::Selectable(label.c_str(), g_SelectedCourse == i))
			{
				g_SelectedCourse = i;
			}
		}

		if (g_SelectedCourse >= 0 &&
			g_SelectedCourse < static_cast<int>(g_Courses.size()))
		{
			const CourseData& selected = g_Courses[g_SelectedCourse];
			ImGui::Text(
				"Points: %d  Logs: %d",
				static_cast<int>(selected.points.size()),
				static_cast<int>(selected.logs.size()));
			if (ImGui::Button("Edit Course"))
			{
				StartCourseCreate(g_SelectedCourse);
			}
			ImGui::SameLine();
			const bool canPlay = selected.points.size() >= 2;
			if (!canPlay)
			{
				ImGui::BeginDisabled();
			}
			if (ImGui::Button("Play"))
			{
				StartRace();
			}
			if (!canPlay)
			{
				ImGui::EndDisabled();
			}
		}
	}
	else if (g_Mode == CourseMode::CourseCreate)
	{
		ImGui::InputText("Name", g_CourseName, sizeof(g_CourseName));
		ImGui::Text("P: place point");
		ImGui::Text("Points: %d", static_cast<int>(g_WorkingCourse.points.size()));
		if (ImGui::Button("Undo Last Point") &&
			!g_WorkingCourse.points.empty())
		{
			g_WorkingCourse.points.pop_back();
			RebuildRings(g_WorkingCourse.points);
		}
		ImGui::SameLine();
		if (ImGui::Button("Save"))
		{
			SaveWorkingCourse();
		}
		if (ImGui::Button("Free Flight"))
		{
			ReturnToFreeFlight();
		}
		if (g_HasLastSaveResult)
		{
			ImGui::Text(
				"%s",
				g_LastSaveSucceeded ? "Saved." : "Save failed.");
		}
	}
	else
	{
		ImGui::Text(
			"Ring: %d / %d",
			(std::max)(0, g_NextGate - 1),
			(std::max)(0, static_cast<int>(g_RaceCourse.points.size()) - 1));
		if (g_Mode == CourseMode::RaceCountdown)
		{
			ImGui::Text("Starting...");
		}
		else if (g_Mode == CourseMode::RaceGoal)
		{
			ImGui::Text("Time: %s", FormatRaceTime(g_RaceElapsed).c_str());
		}
		if (ImGui::Button("Free Flight"))
		{
			ReturnToFreeFlight();
		}
	}

	ImGui::End();
}
