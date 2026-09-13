#pragma once
/*==============================================================================
   統合デバッグシーン [debugscene.h]
   複数のデバッグシーン (MODEL / LIGHTING / TOON) を Tabキーで切り替える
==============================================================================*/

void DebugScene_Initialize(void);
void DebugScene_Update(void);
void DebugScene_Draw(void);
void DebugScene_Finalize(void);