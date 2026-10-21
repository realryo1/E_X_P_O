#pragma once
/*==============================================================================
   ブルーム検証シーン [debug_bloom_scene.h]
   HDR のオフスクリーンへ発光物体を描き、輝度抽出 → 多段ブラー → 合成で光の世界を作る
==============================================================================*/

void DebugBloomScene_Initialize(void);
void DebugBloomScene_Update(void);
void DebugBloomScene_Draw(void);
void DebugBloomScene_Finalize(void);
