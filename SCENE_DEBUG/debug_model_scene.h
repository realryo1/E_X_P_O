#pragma once
/*==============================================================================
   デバッグモデルビューアシーン [debug_model_scene.h]
   asset\model と asset\expomodel フォルダ内の .fbx / .glb を列挙し、1体ずつ表示する
==============================================================================*/

#include <d3d11.h>

void DebugModelScene_Initialize(void);
void DebugModelScene_Update(void);
void DebugModelScene_Draw(void);
void DebugModelScene_Finalize(void);
