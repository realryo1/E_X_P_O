#pragma once
/*==============================================================================
   凹凸地面＋プレイヤー検証シーン [debug_meshfield.h]
   起伏のあるメッシュフィールド上を、GPUスキニングのキャラクターが歩く・走る・跳ぶ・攻撃する。
   アセットは asset\debug\ に置く（git管理外）。
==============================================================================*/

void DebugMeshFieldScene_Initialize(void);
void DebugMeshFieldScene_Update(void);
void DebugMeshFieldScene_Draw(void);
void DebugMeshFieldScene_Finalize(void);
