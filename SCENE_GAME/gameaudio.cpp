#include "gameaudio.h"
#include "define.h"
#include "sound.h"
#include <atomic>
#include <thread>
#include <cstdio>
#include <windows.h>

namespace
{
	enum class BgmKind
	{
		None,
		Explore,
		Race,
		Goal,
		Menu,
	};

	// BGM は BgmKind（None を除く）の順。読込スレッドもこの順にロードする
	constexpr int BGM_COUNT = 4;
	const char* const BGM_PATHS[BGM_COUNT] = {
		"asset/sound/bgm/explore.mp3",
		"asset/sound/bgm/race.mp3",
		"asset/sound/bgm/goal.mp3",
		"asset/sound/bgm/menu.mp3",
	};
	std::atomic<SoundData*> g_Bgm[BGM_COUNT] = {};

	enum SeId
	{
		SE_MENU,
		SE_CURSOR,
		SE_INVALID,
		SE_POINT_ADD,
		SE_SAVE_OK,
		SE_SAVE_NG,
		SE_WARP,
		SE_COUNTDOWN,
		SE_BOOST,
		SE_GOAL,
		SE_RACE_ABORT,
		SE_HOVER,
		SE_HIT,
		SE_LAND,
		SE_SPAWN,
		SE_COUNT
	};
	const char* const SE_PATHS[SE_COUNT] = {
		"asset/sound/se/menu_open.mp3",
		"asset/sound/se/cursor.mp3",
		"asset/sound/se/invalid.mp3",
		"asset/sound/se/point_add.mp3",
		"asset/sound/se/save_ok.mp3",
		"asset/sound/se/save_ng.mp3",
		"asset/sound/se/warp.mp3",
		"asset/sound/se/countdown.mp3",
		"asset/sound/se/boost.mp3",
		"asset/sound/se/goal.mp3",
		"asset/sound/se/race_abort.mp3",
		"asset/sound/se/hover.mp3",
		"asset/sound/se/hit.mp3",
		"asset/sound/se/land.mp3",
		"asset/sound/se/spawn.mp3",
	};
	SoundData* g_Se[SE_COUNT] = {};

	BgmKind g_CurrentBgm = BgmKind::None;
	bool g_BgmPlaying = false;
	bool g_HoverPlaying = false;
	int g_HitCooldown = 0;
	std::thread g_BgmThread;
	std::atomic<bool> g_BgmShutdown{ false };
	LONGLONG g_BgmLoadStart = 0;

	SoundData* BgmData(BgmKind kind)
	{
		if (kind == BgmKind::None)
		{
			return nullptr;
		}
		return g_Bgm[static_cast<int>(kind) - 1].load();
	}

	SoundData* CurrentBgmData(void)
	{
		return BgmData(g_CurrentBgm);
	}

	void PlaySe(SoundData* data)
	{
		PlaySound(data, false);
	}

	void SetBgm(BgmKind kind, SoundData* data)
	{
		if (g_CurrentBgm == kind)
		{
			return;
		}

		for (std::atomic<SoundData*>& bgm : g_Bgm)
		{
			StopSound(bgm.load());
		}
		g_CurrentBgm = kind;
		g_BgmPlaying = false;
		if (kind != BgmKind::None && data)
		{
			PlaySound(data, true);
			g_BgmPlaying = true;
		}
	}

	void LogBgmLoadDone(void)
	{
		static LONGLONG frequency = 0;
		if (frequency == 0)
		{
			LARGE_INTEGER value = {};
			QueryPerformanceFrequency(&value);
			frequency = value.QuadPart;
		}
		double elapsed = 0.0;
		if (frequency > 0 && g_BgmLoadStart != 0)
		{
			LARGE_INTEGER now = {};
			QueryPerformanceCounter(&now);
			elapsed = static_cast<double>(now.QuadPart - g_BgmLoadStart) * 1000.0 /
				static_cast<double>(frequency);
		}
		char line[128] = {};
		sprintf_s(line, "[ExpoLoad] BGMロード完了: %.1f ms\n", elapsed);
		OutputDebugStringA(line);
	}

	void JoinBgmThread(void)
	{
		if (g_BgmThread.joinable())
		{
			g_BgmThread.join();
		}
	}
}

void GameAudio_Initialize(void)
{
	g_CurrentBgm = BgmKind::None;
	g_BgmPlaying = false;
	g_HoverPlaying = false;
	g_HitCooldown = 0;
	g_BgmShutdown = false;

	for (int i = 0; i < SE_COUNT; ++i)
	{
		g_Se[i] = LoadMP3(SE_PATHS[i]);
	}

	LARGE_INTEGER start = {};
	QueryPerformanceCounter(&start);
	g_BgmLoadStart = start.QuadPart;
	g_BgmThread = std::thread([]()
	{
		const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const bool shouldUninit = SUCCEEDED(coHr);
		for (int i = 0; i < BGM_COUNT; ++i)
		{
			if (!g_BgmShutdown.load())
			{
				g_Bgm[i].store(LoadMP3(BGM_PATHS[i]));
			}
		}
		LogBgmLoadDone();
		if (shouldUninit)
		{
			CoUninitialize();
		}
	});

	GameAudio_SetBgmExplore();
}

void GameAudio_Pump(void)
{
	if (g_CurrentBgm == BgmKind::None || g_BgmPlaying)
	{
		return;
	}
	SoundData* data = CurrentBgmData();
	if (!data)
	{
		return;
	}
	PlaySound(data, true);
	g_BgmPlaying = true;
}

void GameAudio_Finalize(void)
{
	g_BgmShutdown = true;
	JoinBgmThread();
	GameAudio_UpdateHover(false, 0.0f);
	SetBgm(BgmKind::None, nullptr);
	for (std::atomic<SoundData*>& bgm : g_Bgm)
	{
		UnloadSound(bgm.exchange(nullptr));
	}
	for (SoundData*& se : g_Se)
	{
		UnloadSound(se);
		se = nullptr;
	}
}

void GameAudio_SetBgmExplore(void) { SetBgm(BgmKind::Explore, BgmData(BgmKind::Explore)); }
void GameAudio_SetBgmRace(void) { SetBgm(BgmKind::Race, BgmData(BgmKind::Race)); }
void GameAudio_SetBgmGoal(void) { SetBgm(BgmKind::Goal, BgmData(BgmKind::Goal)); }
void GameAudio_SetBgmMenu(void) { SetBgm(BgmKind::Menu, BgmData(BgmKind::Menu)); }

void GameAudio_SetBgmCourseCreate(void)
{
	GameAudio_SetBgmExplore();
}

void GameAudio_PlayMenuOpen(void) { PlaySe(g_Se[SE_MENU]); }
void GameAudio_PlayMenuClose(void) { PlaySe(g_Se[SE_MENU]); }
void GameAudio_PlayCursor(void) { PlaySe(g_Se[SE_CURSOR]); }
void GameAudio_PlayCourseSwitch(void) { PlaySe(g_Se[SE_CURSOR]); }
void GameAudio_PlayInvalid(void) { PlaySe(g_Se[SE_INVALID]); }
void GameAudio_PlayPointAdd(void) { PlaySe(g_Se[SE_POINT_ADD]); }
void GameAudio_PlayPointUndo(void) { PlaySe(g_Se[SE_INVALID]); }
void GameAudio_PlaySaveOk(void) { PlaySe(g_Se[SE_SAVE_OK]); }
void GameAudio_PlaySaveNg(void) { PlaySe(g_Se[SE_SAVE_NG]); }
void GameAudio_PlayWarp(void) { PlaySe(g_Se[SE_WARP]); }
void GameAudio_PlayCountdown(void) { PlaySe(g_Se[SE_COUNTDOWN]); }
void GameAudio_StopCountdown(void) { StopSound(g_Se[SE_COUNTDOWN]); }
void GameAudio_PlayBoost(void) { PlaySe(g_Se[SE_BOOST]); }
void GameAudio_PlayGoal(void) { PlaySe(g_Se[SE_GOAL]); }
void GameAudio_PlayRaceAbort(void) { PlaySe(g_Se[SE_RACE_ABORT]); }

void GameAudio_PlayHit(void)
{
	if (g_HitCooldown > 0)
	{
		return;
	}
	PlaySe(g_Se[SE_HIT]);
	g_HitCooldown = static_cast<int>(FPS * 0.2f);
}

void GameAudio_PlayLand(void) { PlaySe(g_Se[SE_LAND]); }
void GameAudio_PlaySpawn(void) { PlaySe(g_Se[SE_SPAWN]); }

void GameAudio_UpdateHover(bool active, float speedRatio)
{
	if (g_HitCooldown > 0)
	{
		--g_HitCooldown;
	}

	SoundData* hover = g_Se[SE_HOVER];
	if (!active || !hover)
	{
		if (g_HoverPlaying)
		{
			StopSound(hover);
			g_HoverPlaying = false;
		}
		return;
	}

	if (speedRatio < 0.0f) speedRatio = 0.0f;
	if (speedRatio > 1.0f) speedRatio = 1.0f;
	const float volumeScale = 0.35f + 0.65f * speedRatio;
	if (!g_HoverPlaying)
	{
		PlaySound(hover, true, volumeScale);
		g_HoverPlaying = true;
	}
	else if (hover->pSourceVoice)
	{
		const float volume = SOUND_SE_VOLUME * volumeScale;
		hover->pSourceVoice->SetVolume(volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume));
		hover->pSourceVoice->SetFrequencyRatio(0.85f + 0.35f * speedRatio);
	}
}
