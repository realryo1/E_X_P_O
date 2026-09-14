#pragma once

void GameAudio_Initialize(void);
void GameAudio_Finalize(void);
void GameAudio_SetBgmExplore(void);
void GameAudio_SetBgmRace(void);
void GameAudio_SetBgmGoal(void);
void GameAudio_SetBgmMenu(void);
void GameAudio_SetBgmCourseCreate(void);
void GameAudio_PlayMenuOpen(void);
void GameAudio_PlayMenuClose(void);
void GameAudio_PlayCursor(void);
void GameAudio_PlayCourseSwitch(void);
void GameAudio_PlayInvalid(void);
void GameAudio_PlayPointAdd(void);
void GameAudio_PlayPointUndo(void);
void GameAudio_PlaySaveOk(void);
void GameAudio_PlaySaveNg(void);
void GameAudio_PlayWarp(void);
void GameAudio_PlayCountdown(void);
void GameAudio_StopCountdown(void);
void GameAudio_PlayBoost(void);
void GameAudio_PlayGoal(void);
void GameAudio_PlayRaceAbort(void);
void GameAudio_PlayHit(void);
void GameAudio_PlayLand(void);
void GameAudio_PlaySpawn(void);
void GameAudio_UpdateHover(bool active, float speedRatio);
