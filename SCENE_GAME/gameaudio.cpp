#include "gameaudio.h"
#include "define.h"
#include "sound.h"

namespace
{
	enum class BgmKind
	{
		None,
		Explore,
		Race,
		Goal,
		Menu,
		CourseCreate,
	};

	SoundData* g_BgmExplore = nullptr;
	SoundData* g_BgmRace = nullptr;
	SoundData* g_BgmGoal = nullptr;
	SoundData* g_BgmMenu = nullptr;
	SoundData* g_BgmCourseCreate = nullptr;
	SoundData* g_SeMenuOpen = nullptr;
	SoundData* g_SeMenuClose = nullptr;
	SoundData* g_SeCursor = nullptr;
	SoundData* g_SeCourseSwitch = nullptr;
	SoundData* g_SeInvalid = nullptr;
	SoundData* g_SePointAdd = nullptr;
	SoundData* g_SePointUndo = nullptr;
	SoundData* g_SeSaveOk = nullptr;
	SoundData* g_SeSaveNg = nullptr;
	SoundData* g_SeWarp = nullptr;
	SoundData* g_SeCountdown = nullptr;
	SoundData* g_SeGo = nullptr;
	SoundData* g_SeBoost = nullptr;
	SoundData* g_SeGoal = nullptr;
	SoundData* g_SeRaceAbort = nullptr;
	SoundData* g_SeHover = nullptr;
	SoundData* g_SeHit = nullptr;
	SoundData* g_SeLand = nullptr;
	SoundData* g_SeSpawn = nullptr;

	BgmKind g_CurrentBgm = BgmKind::None;
	bool g_HoverPlaying = false;
	int g_HitCooldown = 0;

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

		StopSound(g_BgmExplore);
		StopSound(g_BgmRace);
		StopSound(g_BgmGoal);
		StopSound(g_BgmMenu);
		StopSound(g_BgmCourseCreate);
		g_CurrentBgm = kind;
		if (kind != BgmKind::None)
		{
			PlaySound(data, true);
		}
	}
}

void GameAudio_Initialize(void)
{
	g_CurrentBgm = BgmKind::None;
	g_HoverPlaying = false;
	g_HitCooldown = 0;

	g_BgmExplore = LoadMP3("asset/sound/bgm/explore.mp3");
	g_BgmRace = LoadMP3("asset/sound/bgm/race.mp3");
	g_BgmGoal = LoadMP3("asset/sound/bgm/goal.mp3");
	g_BgmMenu = LoadMP3("asset/sound/bgm/menu.mp3");
	g_BgmCourseCreate = LoadMP3("asset/sound/bgm/course_create.mp3");
	g_SeMenuOpen = LoadMP3("asset/sound/se/menu_open.mp3");
	g_SeMenuClose = LoadMP3("asset/sound/se/menu_close.mp3");
	g_SeCursor = LoadMP3("asset/sound/se/cursor.mp3");
	g_SeCourseSwitch = LoadMP3("asset/sound/se/course_switch.mp3");
	g_SeInvalid = LoadMP3("asset/sound/se/invalid.mp3");
	g_SePointAdd = LoadMP3("asset/sound/se/point_add.mp3");
	g_SePointUndo = LoadMP3("asset/sound/se/point_undo.mp3");
	g_SeSaveOk = LoadMP3("asset/sound/se/save_ok.mp3");
	g_SeSaveNg = LoadMP3("asset/sound/se/save_ng.mp3");
	g_SeWarp = LoadMP3("asset/sound/se/warp.mp3");
	g_SeCountdown = LoadMP3("asset/sound/se/countdown.mp3");
	g_SeGo = LoadMP3("asset/sound/se/go.mp3");
	g_SeBoost = LoadMP3("asset/sound/se/boost.mp3");
	g_SeGoal = LoadMP3("asset/sound/se/goal.mp3");
	g_SeRaceAbort = LoadMP3("asset/sound/se/race_abort.mp3");
	g_SeHover = LoadMP3("asset/sound/se/hover.mp3");
	g_SeHit = LoadMP3("asset/sound/se/hit.mp3");
	g_SeLand = LoadMP3("asset/sound/se/land.mp3");
	g_SeSpawn = LoadMP3("asset/sound/se/spawn.mp3");

	GameAudio_SetBgmExplore();
}

void GameAudio_Finalize(void)
{
	GameAudio_UpdateHover(false, 0.0f);
	SetBgm(BgmKind::None, nullptr);
	UnloadSound(g_BgmExplore);
	UnloadSound(g_BgmRace);
	UnloadSound(g_BgmGoal);
	UnloadSound(g_BgmMenu);
	UnloadSound(g_BgmCourseCreate);
	UnloadSound(g_SeMenuOpen);
	UnloadSound(g_SeMenuClose);
	UnloadSound(g_SeCursor);
	UnloadSound(g_SeCourseSwitch);
	UnloadSound(g_SeInvalid);
	UnloadSound(g_SePointAdd);
	UnloadSound(g_SePointUndo);
	UnloadSound(g_SeSaveOk);
	UnloadSound(g_SeSaveNg);
	UnloadSound(g_SeWarp);
	UnloadSound(g_SeCountdown);
	UnloadSound(g_SeGo);
	UnloadSound(g_SeBoost);
	UnloadSound(g_SeGoal);
	UnloadSound(g_SeRaceAbort);
	UnloadSound(g_SeHover);
	UnloadSound(g_SeHit);
	UnloadSound(g_SeLand);
	UnloadSound(g_SeSpawn);
	g_BgmExplore = nullptr;
	g_BgmRace = nullptr;
	g_BgmGoal = nullptr;
	g_BgmMenu = nullptr;
	g_BgmCourseCreate = nullptr;
	g_SeMenuOpen = nullptr;
	g_SeMenuClose = nullptr;
	g_SeCursor = nullptr;
	g_SeCourseSwitch = nullptr;
	g_SeInvalid = nullptr;
	g_SePointAdd = nullptr;
	g_SePointUndo = nullptr;
	g_SeSaveOk = nullptr;
	g_SeSaveNg = nullptr;
	g_SeWarp = nullptr;
	g_SeCountdown = nullptr;
	g_SeGo = nullptr;
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
	SetBgm(BgmKind::Explore, g_BgmExplore);
}

void GameAudio_SetBgmRace(void)
{
	SetBgm(BgmKind::Race, g_BgmRace);
}

void GameAudio_SetBgmGoal(void)
{
	SetBgm(BgmKind::Goal, g_BgmGoal);
}

void GameAudio_SetBgmMenu(void)
{
	SetBgm(BgmKind::Menu, g_BgmMenu);
}

void GameAudio_SetBgmCourseCreate(void)
{
	SetBgm(BgmKind::CourseCreate, g_BgmCourseCreate);
}

void GameAudio_PlayMenuOpen(void) { PlaySe(g_SeMenuOpen); }
void GameAudio_PlayMenuClose(void) { PlaySe(g_SeMenuClose); }
void GameAudio_PlayCursor(void) { PlaySe(g_SeCursor); }
void GameAudio_PlayCourseSwitch(void) { PlaySe(g_SeCourseSwitch); }
void GameAudio_PlayInvalid(void) { PlaySe(g_SeInvalid); }
void GameAudio_PlayPointAdd(void) { PlaySe(g_SePointAdd); }
void GameAudio_PlayPointUndo(void) { PlaySe(g_SePointUndo); }
void GameAudio_PlaySaveOk(void) { PlaySe(g_SeSaveOk); }
void GameAudio_PlaySaveNg(void) { PlaySe(g_SeSaveNg); }
void GameAudio_PlayWarp(void) { PlaySe(g_SeWarp); }
void GameAudio_PlayCountdown(void) { PlaySe(g_SeCountdown); }
void GameAudio_PlayGo(void) { PlaySe(g_SeGo); }
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
