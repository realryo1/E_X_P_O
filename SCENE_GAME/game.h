#pragma once

void Game_Initialize(void);
void Game_Finalize(void);
void Game_Update(void);
void Game_PumpAfterPresent(double lastDrawMs, float lastGpuMs);
void Game_Draw(void);
