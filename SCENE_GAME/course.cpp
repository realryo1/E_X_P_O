#include "course.h"
#include "gameaudio.h"
#include "player.h"
#include "playercamera.h"
#include "billboard.h"
#include "sprite2d.h"
#include "font.h"
#include "camera.h"
#include "MultiLineClickFont.h"
#include "MultiLineDrawFont.h"
#include "define.h"
#include "keyboard.h"
#include "input_manager.h"
#include "main.h"
#include "mouse.h"
#include "renderer.h"

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
	Billboard* g_StartMarker = nullptr;

	CourseMode g_Mode = CourseMode::FreeFlight;
	int g_SelectedCourse = -1;
	int g_NextGate = 0;
	XMFLOAT3 g_LastPlayerPos = {};
	std::chrono::steady_clock::time_point g_CountdownStarted;
	std::chrono::steady_clock::time_point g_RaceStarted;
	double g_RaceElapsed = 0.0;
	enum class MenuPage
	{
		Root,
		RaceSelect,
		EditSelect,
	};

	bool g_MenuOpen = false;
	bool g_MenuWaitRelease = false;
	int g_MenuCursor = 0;
	MenuPage g_MenuPage = MenuPage::Root;
	int g_LastCountdownNumber = 0;

	DrawFont* g_pTimerText = nullptr;
	DrawFont* g_pCountdownText = nullptr;
	DrawFont* g_pGoalText = nullptr;
	DrawFont* g_pGoalUpdateText = nullptr;
	MultiLineDrawFont* g_pGoalRankingText = nullptr;
	DrawFont* g_pGoalHintText = nullptr;
	Sprite2D* g_pGoalBackground = nullptr;
	bool g_GoalRecordUpdated = false;
	DrawFont* g_pCourseHintText = nullptr;
	DrawFont* g_pMenuTitleText = nullptr;
	DrawFont* g_pMenuHintText = nullptr;
	MultiLineClickFont* g_pMenuText = nullptr;
	Sprite2D* g_pMenuBackground = nullptr;

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

	Billboard* CreateCourseMarker(
		const XMFLOAT3& pos,
		float size,
		const XMFLOAT4& color)
	{
		Billboard* marker = new Billboard(
			pos,
			{ size, size },
			{ 0.0f, 0.0f, 0.0f },
			COURSE_TEXTURE,
			true);
		marker->SetBillboardMode(true);
		marker->SetWallFadeEnabled(false);
		marker->SetIgnoreLighting(true);
		marker->SetColor(color);
		return marker;
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
		if (points.empty())
		{
			ClearRings();
			SAFE_DELETE(g_StartMarker);
			return;
		}

		const XMFLOAT4 startColor = { 0.45f, 0.85f, 1.0f, 1.0f };
		const XMFLOAT4 ringColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		if (!g_StartMarker)
		{
			g_StartMarker = CreateCourseMarker(points[0], 0.8f, startColor);
		}
		else
		{
			g_StartMarker->SetPos(points[0]);
		}

		const size_t ringCount = points.size() - 1;
		while (g_Rings.size() > ringCount)
		{
			delete g_Rings.back();
			g_Rings.pop_back();
		}
		for (size_t i = 1; i < points.size(); ++i)
		{
			const size_t ringIndex = i - 1;
			if (ringIndex < g_Rings.size())
			{
				g_Rings[ringIndex]->SetPos(points[i]);
				continue;
			}
			g_Rings.push_back(
				CreateCourseMarker(points[i], COURSE_RING_SIZE, ringColor));
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

	void StartCourseCreate(int courseIndex)
	{
		g_WorkingCourse = {};
		if (courseIndex >= 0 &&
			courseIndex < static_cast<int>(g_Courses.size()))
		{
			g_WorkingCourse = g_Courses[courseIndex];
		}
		else
		{
			g_WorkingCourse.name.clear();
		}
		g_Mode = CourseMode::CourseCreate;
		RebuildRings(g_WorkingCourse.points);
		GameAudio_SetBgmCourseCreate();
	}

	void ReturnToFreeFlight(void)
	{
		Player_SetControlEnabled(true);
		g_Mode = CourseMode::FreeFlight;
		g_NextGate = 0;
		g_RaceElapsed = 0.0;
		ClearRings();
		SAFE_DELETE(g_StartMarker);
		GameAudio_StopCountdown();
		GameAudio_SetBgmExplore();
	}

	std::string CreateAutomaticCourseName(void)
	{
		const std::time_t currentTime = std::time(nullptr);
		std::tm localTime = {};
		localtime_s(&localTime, &currentTime);

		char text[64] = {};
		std::snprintf(
			text,
			sizeof(text),
			"course_%04d%02d%02d_%02d%02d%02d",
			localTime.tm_year + 1900,
			localTime.tm_mon + 1,
			localTime.tm_mday,
			localTime.tm_hour,
			localTime.tm_min,
			localTime.tm_sec);
		return text;
	}

	bool SaveWorkingCourse(void)
	{
		if (g_WorkingCourse.name.empty())
		{
			g_WorkingCourse.name = CreateAutomaticCourseName();
		}

		if (g_WorkingCourse.filePath.empty())
		{
			g_WorkingCourse.filePath =
				std::string(COURSE_DIRECTORY) + "\\" +
				SanitizeFileStem(g_WorkingCourse.name) + ".yml";
		}

		if (!SaveCourseFile(g_WorkingCourse))
		{
			GameAudio_PlaySaveNg();
			return false;
		}

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
		GameAudio_PlaySaveOk();
		return true;
	}

	void AddCoursePoint(void)
	{
		if (!Player_IsReady())
		{
			return;
		}
		g_WorkingCourse.points.push_back(Player_GetPos());
		RebuildRings(g_WorkingCourse.points);
		GameAudio_PlayPointAdd();
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

	bool ParseLogCentiseconds(const std::string& log, int& outCentiseconds)
	{
		const size_t space = log.rfind(' ');
		if (space == std::string::npos || space + 1 >= log.size())
		{
			return false;
		}

		int minutes = 0;
		int seconds = 0;
		int centiseconds = 0;
		if (sscanf_s(
			log.c_str() + space + 1,
			"%d:%d:%d",
			&minutes,
			&seconds,
			&centiseconds) != 3)
		{
			return false;
		}
		if (minutes < 0 || seconds < 0 || centiseconds < 0)
		{
			return false;
		}

		outCentiseconds = minutes * 6000 + seconds * 100 + centiseconds;
		return true;
	}

	void RefreshGoalRanking(void)
	{
		g_GoalRecordUpdated = false;
		if (!g_pGoalRankingText)
		{
			return;
		}

		struct RankEntry
		{
			int centiseconds;
			size_t logIndex;
		};

		std::vector<RankEntry> entries;
		entries.reserve(g_RaceCourse.logs.size());
		for (size_t i = 0; i < g_RaceCourse.logs.size(); ++i)
		{
			int centiseconds = 0;
			if (!ParseLogCentiseconds(g_RaceCourse.logs[i], centiseconds))
			{
				continue;
			}
			entries.push_back({ centiseconds, i });
		}

		int previousBest = -1;
		const size_t currentIndex =
			g_RaceCourse.logs.empty() ? 0 : g_RaceCourse.logs.size() - 1;
		int currentCentiseconds = 0;
		const bool hasCurrent = ParseLogCentiseconds(
			g_RaceCourse.logs.empty() ? std::string() : g_RaceCourse.logs.back(),
			currentCentiseconds);
		for (const RankEntry& entry : entries)
		{
			if (entry.logIndex == currentIndex)
			{
				continue;
			}
			if (previousBest < 0 || entry.centiseconds < previousBest)
			{
				previousBest = entry.centiseconds;
			}
		}
		g_GoalRecordUpdated =
			hasCurrent &&
			(previousBest < 0 || currentCentiseconds < previousBest);

		std::sort(
			entries.begin(),
			entries.end(),
			[](const RankEntry& a, const RankEntry& b)
			{
				if (a.centiseconds != b.centiseconds)
				{
					return a.centiseconds < b.centiseconds;
				}
				return a.logIndex < b.logIndex;
			});

		std::ostringstream ranking;
		ranking << "ランキング TOP5";
		const int showCount = (std::min)(5, static_cast<int>(entries.size()));
		for (int i = 0; i < showCount; ++i)
		{
			ranking << "\n" << (i + 1) << "位  "
				<< FormatRaceTime(entries[i].centiseconds / 100.0);
			if (hasCurrent && entries[i].logIndex == currentIndex)
			{
				ranking << "  ★";
			}
		}
		if (showCount == 0)
		{
			ranking << "\n記録なし";
		}
		g_pGoalRankingText->SetText(ranking.str());
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

		RefreshGoalRanking();
		Player_SetControlEnabled(true);
		g_Mode = CourseMode::RaceGoal;
		GameAudio_PlayGoal();
		GameAudio_SetBgmGoal();
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
		const XMFLOAT3 playerPos = Player_GetPos();
		const float warpDx = playerPos.x - startPosition.x;
		const float warpDy = playerPos.y - startPosition.y;
		const float warpDz = playerPos.z - startPosition.z;
		const bool needWarp =
			warpDx * warpDx + warpDy * warpDy + warpDz * warpDz > 0.25f;
		if (needWarp)
		{
			Player_WarpTo(startPosition);
			GameAudio_PlayWarp();
		}

		const XMFLOAT3 firstRing = g_RaceCourse.points[1];
		const float faceDx = firstRing.x - startPosition.x;
		const float faceDz = firstRing.z - startPosition.z;
		if (faceDx * faceDx + faceDz * faceDz > 0.0001f)
		{
			const float faceYaw = XMConvertToDegrees(atan2f(faceDx, faceDz));
			PlayerCamera_SetLookAngles(faceYaw, 20.0f);
			Player_SetFacingYaw(faceYaw);
		}

		Player_SetControlEnabled(false);
		g_LastPlayerPos = startPosition;
		g_CountdownStarted = std::chrono::steady_clock::now();
		g_Mode = CourseMode::RaceCountdown;
		g_LastCountdownNumber = 0;
		GameAudio_SetBgmExplore();
		GameAudio_PlayCountdown();
	}

	std::string MakeCourseLabel(int index)
	{
		return "コース" + std::to_string(index + 1);
	}

	void AppendCourseLabels(std::vector<std::string>& lines)
	{
		for (int i = 0; i < static_cast<int>(g_Courses.size()); ++i)
		{
			lines.push_back(MakeCourseLabel(i));
		}
	}

	int GetMenuItemCount(void)
	{
		if (g_Mode == CourseMode::RaceCountdown ||
			g_Mode == CourseMode::RaceRunning ||
			g_Mode == CourseMode::RaceGoal)
		{
			return 1;
		}
		if (g_Mode == CourseMode::CourseCreate)
		{
			return 4;
		}
		if (g_MenuPage == MenuPage::RaceSelect)
		{
			return static_cast<int>(g_Courses.size()) + 1;
		}
		if (g_MenuPage == MenuPage::EditSelect)
		{
			return static_cast<int>(g_Courses.size()) + 2;
		}
		return 3;
	}

	bool MenuHasBackGap(void)
	{
		return g_Mode == CourseMode::FreeFlight &&
			(g_MenuPage == MenuPage::Root ||
				g_MenuPage == MenuPage::RaceSelect ||
				g_MenuPage == MenuPage::EditSelect);
	}

	int MenuItemToLine(int item)
	{
		if (MenuHasBackGap() && item == GetMenuItemCount() - 1)
		{
			return item + 1;
		}
		return item;
	}

	int MenuLineToItem(int line)
	{
		if (!MenuHasBackGap())
		{
			return line;
		}

		const int backItem = GetMenuItemCount() - 1;
		if (line == backItem)
		{
			return -1;
		}
		if (line == backItem + 1)
		{
			return backItem;
		}
		if (line >= 0 && line < backItem)
		{
			return line;
		}
		return -1;
	}

	std::vector<std::string> BuildMenuLines(void)
	{
		std::vector<std::string> lines;
		if (g_Mode == CourseMode::CourseCreate)
		{
			lines = {
				"保存して終了",
				"直前の点を取り消す",
				"破棄して終了",
				"編集に戻る",
			};
		}
		else if (g_Mode == CourseMode::RaceCountdown ||
			g_Mode == CourseMode::RaceRunning ||
			g_Mode == CourseMode::RaceGoal)
		{
			lines = { "レースを中止" };
		}
		else if (g_MenuPage == MenuPage::RaceSelect)
		{
			AppendCourseLabels(lines);
			lines.push_back("");
			lines.push_back("戻る");
		}
		else if (g_MenuPage == MenuPage::EditSelect)
		{
			lines.push_back("コース追加");
			AppendCourseLabels(lines);
			lines.push_back("");
			lines.push_back("戻る");
		}
		else
		{
			lines = {
				"レース開始",
				"コース編集・追加",
				"",
				"戻る",
			};
		}

		const int cursorLine = MenuItemToLine(g_MenuCursor);
		for (int i = 0; i < static_cast<int>(lines.size()); ++i)
		{
			if (i == cursorLine && !lines[i].empty())
			{
				lines[i] = "> " + lines[i];
			}
		}
		return lines;
	}

	void RefreshMenuChrome(void)
	{
		if (g_pMenuTitleText)
		{
			const char* title = "ゲームメニュー";
			if (g_Mode == CourseMode::CourseCreate)
			{
				title = "コース作成";
			}
			else if (g_Mode == CourseMode::RaceCountdown ||
				g_Mode == CourseMode::RaceRunning ||
				g_Mode == CourseMode::RaceGoal)
			{
				title = "レース";
			}
			else if (g_MenuPage == MenuPage::RaceSelect)
			{
				title = "レース開始";
			}
			else if (g_MenuPage == MenuPage::EditSelect)
			{
				title = "コース編集・追加";
			}
			g_pMenuTitleText->SetText(title);
		}

		if (g_pMenuHintText)
		{
			const char* hint =
				"クリック / 上下: 選択   Enter / A: 決定   ESC / START: 閉じる";
			if (g_Mode == CourseMode::FreeFlight &&
				(g_MenuPage == MenuPage::RaceSelect ||
					g_MenuPage == MenuPage::EditSelect))
			{
				hint =
					"クリック / 上下: 選択   Enter / A: 決定   ESC: 戻る";
			}
			g_pMenuHintText->SetText(hint);
		}
	}

	void RefreshMenuText(void)
	{
		if (!g_pMenuText)
		{
			return;
		}

		const std::vector<std::string> lines = BuildMenuLines();
		std::string text;
		for (const std::string& line : lines)
		{
			if (!text.empty())
			{
				text += "\n";
			}
			text += line;
		}
		g_pMenuText->SetText(text);
		RefreshMenuChrome();
	}

	void SetMenuPage(MenuPage page)
	{
		g_MenuPage = page;
		g_MenuCursor = 0;
		g_MenuWaitRelease = true;
		if (g_pMenuText)
		{
			g_pMenuText->ClearClick();
		}
		RefreshMenuText();
	}

	void ApplyModeBgm(void)
	{
		if (g_Mode == CourseMode::CourseCreate)
		{
			GameAudio_SetBgmCourseCreate();
		}
		else if (g_Mode == CourseMode::RaceRunning)
		{
			GameAudio_SetBgmRace();
		}
		else if (g_Mode == CourseMode::RaceGoal)
		{
			GameAudio_SetBgmGoal();
		}
		else
		{
			GameAudio_SetBgmExplore();
		}
	}

	void SetMenuOpen(bool open)
	{
		if (g_MenuOpen == open)
		{
			return;
		}

		g_MenuOpen = open;
		if (g_MenuOpen)
		{
			g_MenuCursor = 0;
			g_MenuPage = MenuPage::Root;
			g_MenuWaitRelease = true;
			UnLockMouse();
			if (g_pMenuText)
			{
				g_pMenuText->ClearClick();
			}
			Player_SetControlEnabled(false);
			RefreshMenuText();
			GameAudio_PlayMenuOpen();
			GameAudio_SetBgmMenu();
		}
		else
		{
			g_MenuWaitRelease = false;
			LockMouse();
			Player_SetControlEnabled(g_Mode != CourseMode::RaceCountdown);
			GameAudio_PlayMenuClose();
			ApplyModeBgm();
		}
	}

	void MoveMenuCursor(int direction)
	{
		const int itemCount = GetMenuItemCount();
		if (itemCount <= 0)
		{
			return;
		}
		g_MenuCursor = (g_MenuCursor + direction + itemCount) % itemCount;
		RefreshMenuText();
		GameAudio_PlayCursor();
	}

	void ExecuteMenuItem(int item)
	{
		if (g_Mode == CourseMode::CourseCreate)
		{
			switch (item)
			{
			case 0:
				if (SaveWorkingCourse())
				{
					SetMenuOpen(false);
				}
				break;
			case 1:
				if (!g_WorkingCourse.points.empty())
				{
					g_WorkingCourse.points.pop_back();
					RebuildRings(g_WorkingCourse.points);
					GameAudio_PlayPointUndo();
				}
				break;
			case 2:
				ReturnToFreeFlight();
				SetMenuOpen(false);
				break;
			case 3:
				SetMenuOpen(false);
				break;
			default:
				break;
			}
			return;
		}

		if (g_Mode == CourseMode::RaceCountdown ||
			g_Mode == CourseMode::RaceRunning ||
			g_Mode == CourseMode::RaceGoal)
		{
			if (item == 0)
			{
				GameAudio_PlayRaceAbort();
				ReturnToFreeFlight();
				SetMenuOpen(false);
			}
			return;
		}

		if (g_MenuPage == MenuPage::RaceSelect)
		{
			const int backIndex = static_cast<int>(g_Courses.size());
			if (item == backIndex)
			{
				SetMenuPage(MenuPage::Root);
				return;
			}
			if (item >= 0 && item < static_cast<int>(g_Courses.size()) &&
				g_Courses[item].points.size() >= 2)
			{
				g_SelectedCourse = item;
				SetMenuOpen(false);
				StartRace();
			}
			else
			{
				GameAudio_PlayInvalid();
			}
			return;
		}

		if (g_MenuPage == MenuPage::EditSelect)
		{
			const int backIndex = static_cast<int>(g_Courses.size()) + 1;
			if (item == 0)
			{
				SetMenuOpen(false);
				StartCourseCreate(-1);
				return;
			}
			if (item == backIndex)
			{
				SetMenuPage(MenuPage::Root);
				return;
			}
			const int courseIndex = item - 1;
			if (courseIndex >= 0 &&
				courseIndex < static_cast<int>(g_Courses.size()))
			{
				g_SelectedCourse = courseIndex;
				SetMenuOpen(false);
				StartCourseCreate(courseIndex);
			}
			else
			{
				GameAudio_PlayInvalid();
			}
			return;
		}

		switch (item)
		{
		case 0:
			SetMenuPage(MenuPage::RaceSelect);
			break;
		case 1:
			SetMenuPage(MenuPage::EditSelect);
			break;
		case 2:
			SetMenuOpen(false);
			break;
		default:
			break;
		}
	}

	int GetMenuPadPlayer(void)
	{
		return Gamepad_FindConnectedPlayer();
	}

	bool IsMenuHoldBlocking(void)
	{
		Mouse_State mouseState = {};
		Mouse_GetState(&mouseState);
		const int player = GetMenuPadPlayer();
		return mouseState.leftButton ||
			Keyboard_IsKeyDown(KK_ENTER) ||
			Keyboard_IsKeyDown(KK_SPACE) ||
			Keyboard_IsKeyDown(KK_UP) ||
			Keyboard_IsKeyDown(KK_DOWN) ||
			Keyboard_IsKeyDown(KK_LEFT) ||
			Keyboard_IsKeyDown(KK_RIGHT) ||
			Keyboard_IsKeyDown(KK_W) ||
			Keyboard_IsKeyDown(KK_S) ||
			Keyboard_IsKeyDown(KK_A) ||
			Keyboard_IsKeyDown(KK_D) ||
			Gamepad_IsButtonDown(player, GPB_A) ||
			Gamepad_IsButtonDown(player, GPB_DPAD_UP) ||
			Gamepad_IsButtonDown(player, GPB_DPAD_DOWN) ||
			Gamepad_IsButtonDown(player, GPB_DPAD_LEFT) ||
			Gamepad_IsButtonDown(player, GPB_DPAD_RIGHT) ||
			std::fabs(Input_GetMoveVector().y) > 0.5f ||
			std::fabs(Input_GetMoveVector().x) > 0.5f;
	}

	void UpdateMenu(void)
	{
		if (!g_pMenuText)
		{
			return;
		}

		g_pMenuText->Update();
		if (g_MenuWaitRelease)
		{
			if (IsMenuHoldBlocking())
			{
				g_pMenuText->ClearClick();
				return;
			}
			g_MenuWaitRelease = false;
		}

		if (g_pMenuText->IsClick())
		{
			const int clickedLine = g_pMenuText->GetClickedLineIndex();
			const int clickedItem = MenuLineToItem(clickedLine);
			if (clickedItem >= 0 && clickedItem < GetMenuItemCount())
			{
				g_MenuCursor = clickedItem;
				RefreshMenuText();
				Input_PlayDecideSe();
				ExecuteMenuItem(clickedItem);
				return;
			}
		}

		const int player = GetMenuPadPlayer();
		if (Keyboard_IsKeyDownTrigger(KK_UP) ||
			Gamepad_IsButtonTrigger(player, GPB_DPAD_UP))
		{
			MoveMenuCursor(-1);
		}
		else if (Keyboard_IsKeyDownTrigger(KK_DOWN) ||
			Gamepad_IsButtonTrigger(player, GPB_DPAD_DOWN))
		{
			MoveMenuCursor(1);
		}
		else if (Input_IsActionTrigger(INPUT_ACTION_CANCEL))
		{
			if (g_Mode == CourseMode::FreeFlight &&
				g_MenuPage != MenuPage::Root)
			{
				SetMenuPage(MenuPage::Root);
			}
			else
			{
				SetMenuOpen(false);
			}
		}
		else if (Input_IsActionTrigger(INPUT_ACTION_DECIDE))
		{
			ExecuteMenuItem(g_MenuCursor);
		}
	}
}

void Course_Initialize(void)
{
	g_Mode = CourseMode::FreeFlight;
	g_SelectedCourse = -1;
	g_MenuOpen = false;
	g_MenuWaitRelease = false;
	g_MenuCursor = 0;
	g_MenuPage = MenuPage::Root;
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
	g_pGoalBackground = new Sprite2D(
		{ SCREEN_X * 0.5f, 370.0f },
		{ 520.0f, 490.0f },
		0.0f,
		{ 0.0f, 0.0f, 0.0f, 0.55f },
		BLENDSTATE_ALFA,
		L"asset\\texture\\fade.png");
	g_pGoalText = new DrawFont(
		{ SCREEN_X * 0.5f, 200.0f },
		64.0f,
		0.0f,
		{ 1.0f, 0.9f, 0.3f, 1.0f },
		"ゴール",
		TA_MIDDLE);
	g_pGoalUpdateText = new DrawFont(
		{ SCREEN_X * 0.5f, 248.0f },
		40.0f,
		0.0f,
		{ 1.0f, 0.35f, 0.2f, 1.0f },
		"更新！",
		TA_MIDDLE);
	g_pGoalRankingText = new MultiLineDrawFont(
		{ SCREEN_X * 0.5f, 295.0f },
		28.0f,
		0.0f,
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		"",
		1.35f,
		TA_MIDDLE);
	g_pGoalHintText = new DrawFont(
		{ SCREEN_X * 0.5f, 575.0f },
		28.0f,
		0.0f,
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		"決定で散策に戻る",
		TA_MIDDLE);
	g_GoalRecordUpdated = false;
	g_pCourseHintText = new DrawFont(
		{ SCREEN_X * 0.5f, 25.0f },
		22.0f,
		0.0f,
		{ 0.1f, 0.1f, 0.1f, 1.0f },
		"P: 配置   U: 取り消し   ESC / START: メニュー",
		TA_MIDDLE);
	g_pMenuTitleText = new DrawFont(
		{ SCREEN_X * 0.5f, 120.0f },
		42.0f,
		0.0f,
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		"ゲームメニュー",
		TA_MIDDLE);
	g_pMenuHintText = new DrawFont(
		{ SCREEN_X * 0.5f, SCREEN_Y - 100.0f },
		22.0f,
		0.0f,
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		"クリック / 上下: 選択   Enter / A: 決定   ESC / START: 閉じる",
		TA_MIDDLE);
	g_pMenuText = new MultiLineClickFont(
		{ SCREEN_X * 0.5f, SCREEN_Y * 0.5f },
		34.0f,
		0.0f,
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		{ 1.0f, 0.85f, 0.25f, 1.0f },
		"レース開始\nコース編集・追加\n\n戻る",
		1.5f,
		TA_MIDDLE);
	g_pMenuBackground = new Sprite2D(
		{ SCREEN_X * 0.5f, SCREEN_Y * 0.5f },
		{ SCREEN_X * 0.78f, SCREEN_Y * 0.82f },
		0.0f,
		{ 0.0f, 0.0f, 0.0f, 0.55f },
		BLENDSTATE_ALFA,
		L"asset\\texture\\fade.png");
}

void Course_Finalize(void)
{
	SetMenuOpen(false);
	Player_SetControlEnabled(true);
	ClearRings();
	SAFE_DELETE(g_StartMarker);
	SAFE_DELETE(g_pTimerText);
	SAFE_DELETE(g_pCountdownText);
	SAFE_DELETE(g_pGoalBackground);
	SAFE_DELETE(g_pGoalText);
	SAFE_DELETE(g_pGoalUpdateText);
	SAFE_DELETE(g_pGoalRankingText);
	SAFE_DELETE(g_pGoalHintText);
	g_GoalRecordUpdated = false;
	SAFE_DELETE(g_pCourseHintText);
	SAFE_DELETE(g_pMenuTitleText);
	SAFE_DELETE(g_pMenuHintText);
	SAFE_DELETE(g_pMenuText);
	SAFE_DELETE(g_pMenuBackground);
	g_Courses.clear();
	g_WorkingCourse = {};
	g_RaceCourse = {};
}

void Course_Update(void)
{
	if (Input_IsActionTrigger(INPUT_ACTION_PAUSE) &&
		!(g_Mode == CourseMode::RaceGoal && !g_MenuOpen))
	{
		if (g_MenuOpen &&
			g_Mode == CourseMode::FreeFlight &&
			g_MenuPage != MenuPage::Root)
		{
			SetMenuPage(MenuPage::Root);
		}
		else
		{
			SetMenuOpen(!g_MenuOpen);
		}
	}

	if (g_MenuOpen)
	{
		UpdateMenu();
		return;
	}

	if (g_Mode == CourseMode::CourseCreate)
	{
		if (Keyboard_IsKeyDownTrigger(KK_P))
		{
			AddCoursePoint();
		}
		if (Keyboard_IsKeyDownTrigger(KK_U) &&
			!g_WorkingCourse.points.empty())
		{
			g_WorkingCourse.points.pop_back();
			RebuildRings(g_WorkingCourse.points);
			GameAudio_PlayPointUndo();
		}
		return;
	}

	if (g_Mode == CourseMode::RaceCountdown)
	{
		const double elapsed = std::chrono::duration<double>(
			std::chrono::steady_clock::now() - g_CountdownStarted).count();
		const int number = 3 - static_cast<int>(elapsed);
		if (number > 0 && number != g_LastCountdownNumber)
		{
			g_LastCountdownNumber = number;
		}
		if (elapsed >= 3.0)
		{
			g_Mode = CourseMode::RaceRunning;
			g_RaceStarted = std::chrono::steady_clock::now();
			g_LastPlayerPos = Player_GetPos();
			Player_SetControlEnabled(true);
			GameAudio_SetBgmRace();
		}
		return;
	}

	if (g_Mode == CourseMode::RaceGoal &&
		Input_IsActionTrigger(INPUT_ACTION_DECIDE))
	{
		ReturnToFreeFlight();
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
				GameAudio_PlayBoost();
			}
		}
	}
	g_LastPlayerPos = currentPlayerPos;
}

bool Course_IsMenuOpen(void)
{
	return g_MenuOpen;
}

void Course_Draw(void)
{
	if (g_Mode == CourseMode::FreeFlight)
	{
		return;
	}
	const bool racing =
		g_Mode == CourseMode::RaceCountdown ||
		g_Mode == CourseMode::RaceRunning ||
		g_Mode == CourseMode::RaceGoal;
	XMFLOAT3 cameraPos = {};
	Camera* camera = GetCamera();
	if (camera)
	{
		cameraPos = camera->GetPos();
	}
	const float markerCullSq = 48.0f * 48.0f;
	auto isNearCamera = [&](const XMFLOAT3& pos) -> bool
	{
		if (!camera)
		{
			return true;
		}
		const float dx = pos.x - cameraPos.x;
		const float dy = pos.y - cameraPos.y;
		const float dz = pos.z - cameraPos.z;
		return dx * dx + dy * dy + dz * dz <= markerCullSq;
	};
	if (g_StartMarker && (!racing || isNearCamera(g_StartMarker->GetPos())))
	{
		g_StartMarker->Draw();
	}
	for (size_t i = 0; i < g_Rings.size(); ++i)
	{
		const int gateIndex = static_cast<int>(i) + 1;
		if (racing && gateIndex < g_NextGate)
		{
			continue;
		}

		Billboard* ring = g_Rings[i];
		if (!ring)
		{
			continue;
		}
		if (racing &&
			gateIndex > g_NextGate + 1 &&
			!isNearCamera(ring->GetPos()))
		{
			continue;
		}
		ring->Draw();
	}
}

void Course_DrawHud(void)
{
	if (Direct3D_IsTakingScreenshot())
	{
		return;
	}

	if (g_Mode == CourseMode::CourseCreate && g_pCourseHintText)
	{
		g_pCourseHintText->Draw();
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
		if (g_Mode == CourseMode::RaceGoal && g_pGoalBackground)
		{
			g_pGoalBackground->Draw();
		}
		if (g_Mode == CourseMode::RaceGoal && g_pGoalText)
		{
			g_pGoalText->Draw();
		}
		if (g_Mode == CourseMode::RaceGoal &&
			g_GoalRecordUpdated &&
			g_pGoalUpdateText)
		{
			g_pGoalUpdateText->Draw();
		}
		if (g_Mode == CourseMode::RaceGoal && g_pGoalRankingText)
		{
			g_pGoalRankingText->Draw();
		}
		if (g_Mode == CourseMode::RaceGoal && g_pGoalHintText)
		{
			g_pGoalHintText->Draw();
		}
	}
}

void Course_DrawMenu(void)
{
	if (Direct3D_IsTakingScreenshot() || !g_MenuOpen)
	{
		return;
	}

	if (g_pMenuBackground) g_pMenuBackground->Draw();
	if (g_pMenuTitleText) g_pMenuTitleText->Draw();
	if (g_pMenuText) g_pMenuText->Draw();
	if (g_pMenuHintText) g_pMenuHintText->Draw();
}
