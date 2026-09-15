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

	std::atomic<SoundData*> g_BgmExplore{ nullptr };
	std::atomic<SoundData*> g_BgmRace{ nullptr };
	std::atomic<SoundData*> g_BgmGoal{ nullptr };
	std::atomic<SoundData*> g_BgmMenu{ nullptr };
	SoundData* g_SeMenu = nullptr;
	SoundData* g_SeCursor = nullptr;
	SoundData* g_SeInvalid = nullptr;
	SoundData* g_SePointAdd = nullptr;
	SoundData* g_SeSaveOk = nullptr;
	SoundData* g_SeSaveNg = nullptr;
	SoundData* g_SeWarp = nullptr;
	SoundData* g_SeCountdown = nullptr;
	SoundData* g_SeBoost = nullptr;
	SoundData* g_SeGoal = nullptr;
	SoundData* g_SeRaceAbort = nullptr;
	SoundData* g_SeHover = nullptr;
	SoundData* g_SeHit = nullptr;
	SoundData* g_SeLand = nullptr;
	SoundData* g_SeSpawn = nullptr;

	BgmKind g_CurrentBgm = BgmKind::None;
	bool g_BgmPlaying = false;
	bool g_HoverPlaying = false;
	int g_HitCooldown = 0;
	std::thread g_BgmThread;
	std::atomic<bool> g_BgmShutdown{ false };
	LONGLONG g_BgmLoadStart = 0;

	SoundData* CurrentBgmData(void)
	{
		switch (g_CurrentBgm)
		{
		case BgmKind::Explore: return g_BgmExplore.load();
		case BgmKind::Race: return g_BgmRace.load();
		case BgmKind::Goal: return g_BgmGoal.load();
		case BgmKind::Menu: return g_BgmMenu.load();
		default: return nullptr;
		}
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

		StopSound(g_BgmExplore.load());
		StopSound(g_BgmRace.load());
		StopSound(g_BgmGoal.load());
		StopSound(g_BgmMenu.load());
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

	g_SeMenu = LoadMP3("asset/sound/se/menu_open.mp3");
	g_SeCursor = LoadMP3("asset/sound/se/cursor.mp3");
	g_SeInvalid = LoadMP3("asset/sound/se/invalid.mp3");
	g_SePointAdd = LoadMP3("asset/sound/se/point_add.mp3");
	g_SeSaveOk = LoadMP3("asset/sound/se/save_ok.mp3");
	g_SeSaveNg = LoadMP3("asset/sound/se/save_ng.mp3");
	g_SeWarp = LoadMP3("asset/sound/se/warp.mp3");
	g_SeCountdown = LoadMP3("asset/sound/se/countdown.mp3");
	g_SeBoost = LoadMP3("asset/sound/se/boost.mp3");
	g_SeGoal = LoadMP3("asset/sound/se/goal.mp3");
	g_SeRaceAbort = LoadMP3("asset/sound/se/race_abort.mp3");
	g_SeHover = LoadMP3("asset/sound/se/hover.mp3");
	g_SeHit = LoadMP3("asset/sound/se/hit.mp3");
	g_SeLand = LoadMP3("asset/sound/se/land.mp3");
	g_SeSpawn = LoadMP3("asset/sound/se/spawn.mp3");

	LARGE_INTEGER start = {};
	QueryPerformanceCounter(&start);
	g_BgmLoadStart = start.QuadPart;
	g_BgmThread = std::thread([]()
	{
		const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const bool shouldUninit = SUCCEEDED(coHr);
		if (!g_BgmShutdown.load())
		{
			g_BgmExplore.store(LoadMP3("asset/sound/bgm/explore.mp3"));
		}
		if (!g_BgmShutdown.load())
		{
			g_BgmRace.store(LoadMP3("asset/sound/bgm/race.mp3"));
		}
		if (!g_BgmShutdown.load())
		{
			g_BgmGoal.store(LoadMP3("asset/sound/bgm/goal.mp3"));
		}
		if (!g_BgmShutdown.load())
		{
			g_BgmMenu.store(LoadMP3("asset/sound/bgm/menu.mp3"));
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
	UnloadSound(g_BgmExplore.exchange(nullptr));
	UnloadSound(g_BgmRace.exchange(nullptr));
	UnloadSound(g_BgmGoal.exchange(nullptr));
	UnloadSound(g_BgmMenu.exchange(nullptr));
	UnloadSound(g_SeMenu);
	UnloadSound(g_SeCursor);
	UnloadSound(g_SeInvalid);
	UnloadSound(g_SePointAdd);
	UnloadSound(g_SeSaveOk);
	UnloadSound(g_SeSaveNg);
	UnloadSound(g_SeWarp);
	UnloadSound(g_SeCountdown);
	UnloadSound(g_SeBoost);
	UnloadSound(g_SeGoal);
	UnloadSound(g_SeRaceAbort);
	UnloadSound(g_SeHover);
	UnloadSound(g_SeHit);
	UnloadSound(g_SeLand);
	UnloadSound(g_SeSpawn);
	g_SeMenu = nullptr;
	g_SeCursor = nullptr;
	g_SeInvalid = nullptr;
	g_SePointAdd = nullptr;
	g_SeSaveOk = nullptr;
	g_SeSaveNg = nullptr;
	g_SeWarp = nullptr;
	g_SeCountdown = nullptr;
	g_SeBoost = nullptr;
	g_SeGoal = nullptr;
	g_SeRaceAbort = nullptr;
	g_SeHover = nullptr;
	g_SeHit = nullptr;
	g_SeLand = nullptr;
	g_SeSpawn = nullptr;
}

void GameAudio_SetBgmExplore(void)
{
	SetBgm(BgmKind::Explore, g_BgmExplore.load());
}

void GameAudio_SetBgmRace(void)
{
	SetBgm(BgmKind::Race, g_BgmRace.load());
}

void GameAudio_SetBgmGoal(void)
{
	SetBgm(BgmKind::Goal, g_BgmGoal.load());
}

void GameAudio_SetBgmMenu(void)
{
	SetBgm(BgmKind::Menu, g_BgmMenu.load());
}

void GameAudio_SetBgmCourseCreate(void)
{
	GameAudio_SetBgmExplore();
}

void GameAudio_PlayMenuOpen(void) { PlaySe(g_SeMenu); }
void GameAudio_PlayMenuClose(void) { PlaySe(g_SeMenu); }
void GameAudio_PlayCursor(void) { PlaySe(g_SeCursor); }
void GameAudio_PlayCourseSwitch(void) { PlaySe(g_SeCursor); }
void GameAudio_PlayInvalid(void) { PlaySe(g_SeInvalid); }
void GameAudio_PlayPointAdd(void) { PlaySe(g_SePointAdd); }
void GameAudio_PlayPointUndo(void) { PlaySe(g_SeInvalid); }
void GameAudio_PlaySaveOk(void) { PlaySe(g_SeSaveOk); }
void GameAudio_PlaySaveNg(void) { PlaySe(g_SeSaveNg); }
void GameAudio_PlayWarp(void) { PlaySe(g_SeWarp); }
void GameAudio_PlayCountdown(void) { PlaySe(g_SeCountdown); }
void GameAudio_StopCountdown(void) { StopSound(g_SeCountdown); }
void GameAudio_PlayBoost(void) { PlaySe(g_SeBoost); }
void GameAudio_PlayGoal(void) { PlaySe(g_SeGoal); }
void GameAudio_PlayRaceAbort(void) { PlaySe(g_SeRaceAbort); }

void GameAudio_PlayHit(void)
{
	if (g_HitCooldown > 0)
	{
		return;
	}
	PlaySe(g_SeHit);
	g_HitCooldown = static_cast<int>(FPS * 0.2f);
}

void GameAudio_PlayLand(void) { PlaySe(g_SeLand); }
void GameAudio_PlaySpawn(void) { PlaySe(g_SeSpawn); }

void GameAudio_UpdateHover(bool active, float speedRatio)
{
	if (g_HitCooldown > 0)
	{
		--g_HitCooldown;
	}

	if (!active || !g_SeHover)
	{
		if (g_HoverPlaying)
		{
			StopSound(g_SeHover);
			g_HoverPlaying = false;
		}
		return;
	}

	if (speedRatio < 0.0f) speedRatio = 0.0f;
	if (speedRatio > 1.0f) speedRatio = 1.0f;
	const float volumeScale = 0.35f + 0.65f * speedRatio;
	if (!g_HoverPlaying)
	{
		PlaySound(g_SeHover, true, volumeScale);
		g_HoverPlaying = true;
	}
	else if (g_SeHover->pSourceVoice)
	{
		const float volume = SOUND_SE_VOLUME * volumeScale;
		g_SeHover->pSourceVoice->SetVolume(volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume));
		g_SeHover->pSourceVoice->SetFrequencyRatio(0.85f + 0.35f * speedRatio);
	}
}
