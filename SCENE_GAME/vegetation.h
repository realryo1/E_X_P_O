#pragma once

// 木や草をビルボードで配置するマップエディタ。
// 画像は asset\vegetation\ に置いた png / jpg / tga が自動で配置の種類になる。
// 配置結果は asset\vegetation\placement.txt に保存する。
void Vegetation_Initialize(void);
void Vegetation_Finalize(void);
void Vegetation_Update(void);
void Vegetation_Draw(void);
void Vegetation_DrawOverlay(void);
bool Vegetation_IsEditing(void);
