#pragma once

// 水面と水面エディタ。
// 床のオルソ写真の上に、動く法線・空の映り込み・太陽のきらめきを持つ透明な多角形を重ねる。
// 水域は F8 の水面エディタで床をクリックして輪郭の頂点を置き、asset\water\areas.txt に保存する。
// 水域ごとに水平な高さを1つ持つ。見た目の調整は Debug の ImGui「Expo Water」。
void Water_Initialize(void);
void Water_Finalize(void);
void Water_Update(void);
void Water_Draw(void);
void Water_DrawOverlay(void);
void Water_DrawDebug(void);
bool Water_IsEditing(void);
