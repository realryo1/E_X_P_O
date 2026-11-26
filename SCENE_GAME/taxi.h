#pragma once

// 空飛ぶタクシーの「お急ぎ送迎ミッション」。
// パビリオンの公式名と位置を使い、迎え→送りを連続で繰り返す1分半のスコアアタック。
void Taxi_Initialize(void);
void Taxi_Finalize(void);
// 開始できなければ false（パビリオンが3件未満など）
bool Taxi_Start(void);
void Taxi_Stop(void);
void Taxi_Update(void);
void Taxi_Draw(void);
void Taxi_DrawHud(void);
// 制限時間が終わって結果表示中か
bool Taxi_IsFinished(void);
