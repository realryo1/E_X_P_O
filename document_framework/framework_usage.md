# フレームワーク使い方ガイド

プロジェクト名: **expogame**（DirectX 11 フレームワーク）

API の呼び方、起動順、検証シーン。
万博ゲームのできることと操作は [game_specification.md](../document_game/game_specification.md)。
次の作業は [task_list.md](../document_game/task_list.md)。
当たり判定は [collision.md](collision.md)。
実装と食い違う記述があればソースを優先する。

## 目次

1. [かんたんな使い方](#かんたんな使い方)
1. [input_manager](#input_manager)
1. [Sprite2D（2Dスプライト）](#sprite2d2dスプライト)
1. [Sprite3D（3Dモデル）](#sprite3d3dモデル)
1. [AnimSprite3D（スケルタルアニメーション）](#animsprite3dスケルタルアニメーション)
1. [Billboard（3D板ポリゴン）](#billboard3d板ポリゴン)
1. [Movie（動画）](#movie動画)
1. [Sound（音声）](#sound音声)
1. [DrawFont（テキスト描画）](#DrawFontテキスト描画)
1. [Fade（フェード遷移）](#fadeフェード遷移)
1. [Renderer（描画エンジン）](#renderer描画エンジン)
1. [Camera（カメラ）](#cameraカメラ)
1. [Texture / Light / Transform](#texture--light--transform)
1. [SCENE_DEBUGについて](#SCENE_DEBUGについて)
1. [デバッグ用ユーティリティ](#デバッグ用ユーティリティ)
1. [tool（開発用スクリプト）](#tool開発用スクリプト)
1. [注意点など](#注意点など)

---

## かんたんな使い方

参考用に**title.cpp**にいろいろおいてある。

staticなグローバル変数を宣言
```cpp
static Sprite2D* g_pNaiyo = nullptr;

```
Initializeして
```cpp
g_pNaiyo = new Sprite2D(
	{ 140.0f, 140.0f },
	{ 200.0f, 200.0f },
	0.0f,
	{ 1.0f, 1.0f, 1.0f, 1.0f },
	BLENDSTATE_NONE,
	L"asset\\texture\\notfound_thumbnail.png"
);


```
**updateが一番触ることになると思う（例は決定ボタンを押してる間だけ回転）**

入力方式が授業のとはかなり違うので気を付ける（次の章）
```cpp
	if (Input_IsActionDown(INPUT_ACTION_DECIDE))
	{
		g_pNaiyo->AddRot(-360.0f * (1.0f / FPS / 4));
	}

```
draw、finalizeでSAFE_DELETE。
```cpp

Draw
if (g_pNaiyo) g_pNaiyo->Draw();

Finalize
SAFE_DELETE(g_pNaiyo);

```

~~人間が読むのはここまで。あとはAIに食わせてね~~

---

## プロジェクト概要

- ソリューション: [`expogame.sln`](../expogame.sln)
- 描画API: Direct3D 11
- 推奨構成: `x64` の `Debug` または `Release`
- UI論理座標: `SCREEN_X` × `SCREEN_Y` = `1280` × `720`
- 描画基準アスペクト: `DRAW_SCREEN_X` × `DRAW_SCREEN_Y` = `3840` × `2160`
- バックバッファ: クライアント領域サイズ、Flipモデル2枚（`DXGI_SWAP_EFFECT_FLIP_DISCARD`）
- 目標固定更新: `FPS` = `60`
- 実行時パス基準: カレントディレクトリ
- 現在の初期シーン: `SCENE_GAME`（`app/scene.cpp`）。`SCENE_TITLE`はプレースホルダーとして残っている

| ディレクトリ | 内容 |
| :--- | :--- |
| `app/` | 共通定数とシーン管理 |
| `framework/` | 入力、カメラ、モデル、テクスチャ、音声、メインループ |
| `shader/` | Direct3D 11 レンダラー、シェーダー |
| `SCENE_TITLE/` `SCENE_GAME/` `SCENE_RESULT/` | 通常シーン。`SCENE_GAME` は `game.cpp` が呼び出し列、場は `field.cpp`、ホバー移動は `player.cpp`、三人称は `playercamera.cpp`、HUD は `ui.cpp` |
| `SCENE_DEBUG/` | Debugビルド専用の検証シーン |
| `asset/` | 実行時アセット |
| `tool/` | 開発用ツール |

万博アセット用の `prepare_expo_*.py` は [plateau.md](../document_game/plateau.md)。

入口: メインループ [`framework/main.cpp`](../framework/main.cpp)、シーン [`app/scene.cpp`](../app/scene.cpp)、描画 [`shader/renderer.h`](../shader/renderer.h)、モデル [`framework/sprite3d.h`](../framework/sprite3d.h)。

---

## 起動とメインループ

`WinMain`（[`framework/main.cpp`](../framework/main.cpp)）のおおよその初期化順:

1. DPI認識（`DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`）
2. COM
3. `InputMonitorConsole_Initialize`（Debugではコンソールを先に出す）
4. Win32ウィンドウ
5. `InitRenderer`
6. ImGui
7. `Keyboard_Initialize`、`Mouse_Initialize`、`InitSound`、`Input_Initialize`
8. `InitShader`、`Font_InitializeGlobalData`、`Sprite_Initialize`、`Fade_Initialize`、`Gamepad_Initialize`
9. [`app/scene.cpp`](../app/scene.cpp) の `Init`

`Input_Initialize` 内でも `Gamepad_Initialize` が呼ばれる。入力を足すときは所有者を増やさない。

メッセージがないとき、`steady_clock` の経過をアキュムレータへ足し、論理更新は `1.0 / FPS` の固定ステップ（1ループ最大5ステップ）。

1論理ステップの順:

1. `Gamepad_Update`
2. `Input_Update`
3. `InputMonitorConsole_Update`
4. F11などのウィンドウ操作
5. `Fade_Update`
6. `UpdateTextureCache`
7. `UpdateSoundCache`
8. 現在シーンの `Update`
9. `keycopy`（キーボード前フレーム）

描画は論理更新が1回以上あり、かつ `NeedsPresent` が真のときだけ。要求されるのは起動ウォームアップ、`RequestRedraw`、フェード中、Debug の `SCENE_DEBUG`。静止した通常シーンは `Clear` / `Present` を間引く。

描画フレームの順: ImGui開始 → `Clear` → `SetWorldViewProjection2D` → シーン `Draw` → `SetDepthEnable(false)` とフェード → ImGui → `Present(1, 0)` → シーンの `PumpAfterPresent`（`SCENE_GAME` のみ。初期ロード完了後の GLB GPU 化）。
スワップチェーンは Flip モデルのため、`Present()` 成功後に `IDXGISwapChain3::GetCurrentBackBufferIndex()` で次のバックバッファを取得し、
対応する RTV を `OMSetRenderTargets()` へ再設定する。次フレームの `Clear` と描画は、その RTV に対して行われる。

`Present` は垂直同期待ち（第1引数 `1`、フラグ `0`）。`ALLOW_TEARING` 付きの即時 Present は FPS 表示だけ上がり画面が更新されないことがあるため使わない。

`SetFPS` は目標値を書き換えるだけ。固定ステップは `FPS` マクロなので、呼んでも論理更新速度は変わらない。

終了時は ImGui、現在シーン、テクスチャ、サウンド、ゲームパッド、入力、音声、フェード、フォント、スプライト、シェーダー、レンダラーの順。COM は `SAFE_RELEASE`、`new` は `SAFE_DELETE`。

---

## input_manager

シーンからは `input_manager.h` の **`Input_IsActionDown`** / **`Input_IsActionTrigger`** を使う。

 `keyboard.h` / `gamepad.h` **の直接読み取は禁止**。 コントローラー・キーボード双方の入力実装をやりやすくするため。 
（SCENE_DEBUG内ならどうせ除外されるので何やってもいいが）

詳細・アクション定数・マッピングは [input.md](input.md) を参照。  
マウス／カメラ設計は [mouse_camera_implementation_flow.md](mouse_camera_implementation_flow.md)。

```cpp
#include "input_manager.h"

// 押し続け / 押した瞬間
if (Input_IsActionDown(INPUT_ACTION_DECIDE)) { /* ... */ }
if (Input_IsActionTrigger(INPUT_ACTION_DECIDE)) { /* ... */ }

Input_Vector2 move = Input_GetMoveVector();  // WASD / DPad / LStick
Input_Vector2 look = Input_GetLookVector();  // RStick
float zoom = Input_GetZoomDelta();           // RT - LT
```

| 定数                                     | 主な用途       |
| -------------------------------------- | ---------- |
| `INPUT_ACTION_DECIDE`                  | 決定 / 進行    |
| `INPUT_ACTION_CANCEL`                  | キャンセル / 戻る |
| `INPUT_ACTION_MENU_UP/DOWN/LEFT/RIGHT` | メニュー移動     |
| `INPUT_ACTION_PAUSE`                   | ポーズ        |
| `INPUT_ACTION_JUMP`                    | 上昇          |
| `INPUT_ACTION_DESCEND`                 | 下降          |

### 例外（キーボード直叩き）

デバッグシーンの特殊キー（Tab 切替、Esc でマウスアンロックなど）、`SCENE_GAME` の `Esc` ポインタ解除、メインループの F2 / F11 のように、抽象アクションに載せない操作のみ `keyboard.h` を直接使ってよい。

```cpp
// 例外: デバッグ等
if (Keyboard_IsKeyDown(KK_W)) { }
if (Keyboard_IsKeyDownTrigger(KK_TAB)) { }
```

主要キー: `KK_A〜KK_Z`, `KK_LEFT/RIGHT/UP/DOWN`, `KK_SPACE`, `KK_ENTER`, `KK_ESCAPE`, `KK_LEFTSHIFT`, `KK_LEFTCONTROL`, `KK_TAB`, `KK_F2`, `KK_F11`

### マウス

```cpp
Mouse_State ms;
Mouse_GetState(&ms);
Mouse_SetMode(MOUSE_POSITION_MODE_RELATIVE);
LockMouse();
UnLockMouse();
```

相対モードでは `Mouse_GetState` がフレームあたり1回だけ `dx`/`dy` を返す。2回目以降は 0 になる。`SCENE_GAME` ではロック中の `GetState` は `playercamera.cpp` のみ。解除判定は `Mouse_IsVisible()` で行い、ロック中は `game.cpp` が `GetState` しない。

### Gamepad

ヘッダ: `framework/gamepad.h`

`main` で `Gamepad_Initialize` / `Finalize`。通常プレイは `input_manager` 経由。振動やレイアウト切替など低レベル操作時のみ直接使う。

```cpp
Gamepad_SetLayout(GAMEPAD_LAYOUT_XBOX);  // または GAMEPAD_LAYOUT_SWITCH_ABXY
Gamepad_SetVibration(0, 0.5f, 0.5f);
bool connected = Gamepad_IsConnected(0);
```

---

## Sprite2D（2Dスプライト）

ヘッダ: `framework/sprite2d.h`

座標は `SCREEN_X(1280) × SCREEN_Y(720)` の論理座標。

```cpp
Sprite2D sprite(
    XMFLOAT2(640.0f, 360.0f),
    XMFLOAT2(100.0f, 100.0f),
    0.0f,
    XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
    BLENDSTATE_ALFA,
    L"asset\\texture\\image.png"
);
sprite.Draw();
sprite.SetColor(XMFLOAT4(1.0f, 0.5f, 0.5f, 0.8f));
sprite.SetFlipType(FLIPTYPE2D::FLIPTYPE2D_HORIZONTAL);
```

### 分割テクスチャ

```cpp
SplitSprite split(
    XMFLOAT2(100.0f, 100.0f),
    XMFLOAT2(64.0f, 64.0f),
    0.0f,
    XMFLOAT4(1, 1, 1, 1),
    BLENDSTATE_ALFA,
    L"asset\\texture\\spritesheet.png",
    4, 2
);
split.SetTextureNumber(3);
split.Draw();
```

### クリック判定付き

```cpp
ClickSprite2D button(/* Sprite2D と同じ引数 */);
if (button.IsClick()) { /* ... */ }
```


| 値                                   | 説明   |
| ----------------------------------- | ---- |
| `FLIPTYPE2D::FLIPTYPE2D_NONE`       | 反転なし |
| `FLIPTYPE2D::FLIPTYPE2D_HORIZONTAL` | 左右   |
| `FLIPTYPE2D::FLIPTYPE2D_VERTICAL`   | 上下   |
| `FLIPTYPE2D::FLIPTYPE2D_BOTH`       | 両方   |


※ `FLIPTYPE2D` は `enum class`。

---

## Sprite3D（3Dモデル）

ヘッダ: `framework/sprite3d.h`

`.fbx` は Assimp、`.glb` は `GlbModel` に分岐。アニメーションは `AnimSprite3D` を使う。

```cpp
Sprite3D model(
    XMFLOAT3(0.0f, 0.0f, 0.0f),
    XMFLOAT3(1.0f, 1.0f, 1.0f),
    XMFLOAT3(0.0f, 0.0f, 0.0f),
    "asset/model/cube.fbx",
    S_PHONG
);

model.Draw();
model.SetColor(1.0f, 0.5f, 0.5f, 1.0f);
model.SetColorAlpha(0.5f);
model.ResetColor();
model.DrawShadowMap(lightView, lightProj);  // 静的影

XMFLOAT3 originalSize = model.GetModelSize();
XMFLOAT3 displaySize  = model.GetDisplaySize();
```

`Transform3D` 由来: `pos` / `scale` / `rot`（度）。

内部ローダー:

- `.fbx` → `framework/model.h`（`MODEL` / Assimp、スキニング対応）
- `.glb` → `framework/glb_model.h`（`GlbModel`）

通常は `Sprite3D` / `AnimSprite3D` 経由で使い、ローダーを直接触る必要はない。

`ModelLoad` / `GlbModel::Load` では左手座標系変換、三角形化、法線生成などを行う。
GLB/GLTFは Assimp の GlobalScale を 100 倍にしている。メートル単位データを接続するときは、
表示サイズとカメラの Near/Far を確認する。

`GlbImportOptions` で法線再生成と頂点結合を止められる。`genNormals=false` の場合は
Assimpの法線生成フラグも付けず、GLBに法線が無ければ既定の上向き法線を使う。
埋め込みテクスチャの WIC デコードは
ワーカーの `DecodeEmbeddedTextures`、`PumpGpu` は SRV と頂点／インデックスバッファの生成・逐次転送だけにする。
`DecodeEmbeddedTextures` はDirectXTexのCPUデコード後、長辺2048pxを上限に縮小する。
ただし `expo_floor.glb` は例外として縮小せず、生成時の最大8192pxを維持する。
8192×8192のRGBAテクスチャはGPU上で約256MBを使用するため、床以外のGLBにはこの例外を適用しない。
ゲーム側のストリーミングでは、初期ロード中のGPU化とリソース破棄を1フレーム6ms
（フェード中は最大40ms）で進める。初期完了後は `Present` のあと最大3ms。
直前フレームの CPU 描画が 8ms 超、または GPU が 8ms 超ならそのフレームはポンプしない。
GPU タイムスタンプ Query は Release でも有効である。ローカル VRAM 使用量が予算の 85% を超えたら
Present 後の追加アップロードを止める。パビリオン失敗は最大 3 回、5 秒単位の間隔で再試行する。
同時インポートとテクスチャデコードは各 1 スレッド。
描画と同じフレームの先頭で `UpdateSubresource` すると、NVIDIA では続く `DrawIndexed` が待ちやすい。
`SCENE_GAME`の万博GLBは `GlbModel::ImportPreparedFile` がGLB 2.0のJSON/BIN、
`bufferView.byteStride`、Accessorの`byteOffset`、u8/u16/u32インデックスを直接検証する。
ノード変換をCPUで頂点へ適用し、参照された頂点だけからモデル境界を求める。
直接デコード経路のUVはglTFの値をそのまま使用する。glTFとDirectXのテクスチャ座標は
上端原点のため、ここでVを反転しない。旧Assimp経路の`aiProcess_ConvertToLeftHanded`
に含まれるUV変換と混在させないこと。頂点は衝突 bin の実行時変換と同じく
メートル座標を 100 倍し Z を反転する。インデックスの巻き順もそれに合わせる。
直接経路のGPU転送は頂点・インデックス・テクスチャをチャンク化し、全転送完了まで
`IsLoaded()`を真にしない。
`AttachPreparedData` のあと、同一マテリアルの prepared メッシュを
`MergePreparedMeshesByMaterial` で結合できる。結合後も `expo_batch_id` は
`GlbBatchRange` として残り、hidden batch の省略と隣接範囲の `DrawIndexed` 結合に使う。
`TryBuildSmallCombinedShadow` は結合データが 8MB 以下のときだけ影用バッファを作る。
`GlbModel::Load`（プレイヤー機体など Assimp 経路）はアニメーション 0 フレームを頂点へ焼いたあと、
同じ結合経路へ渡す。会場の巨大結合シャドウは作らない。
PLATEAU の遠景LOD2は1ファイルでもプリミティブ分割が多く、NVIDIA の D3D11 ユーザモード
ドライバでは1発行の固定費が AMD APU より高い。結合は見た目を変えずに発行回数を減らす。
GLB のテクスチャが無い、または SRV を作れないマテリアルには `GlbModel` が内部生成する
1×1 の白テクスチャを割り当てる。描画時に null の SRV を通常テクスチャとして渡さない。

`Sprite3D::DrawShadowMap` は `SetCastShadow(true)` のモデルだけ深度を書く。GLB は `GlbModel::DrawShadowMap` へ渡し、影用 XZ セルがある場合は注視点周辺のセルだけを描く。`SetReceiveShadow(true)` の GLB は `S_PBR` の `Parameter.w` で ShadowMap を読む。

万博GLBの直接デコードに失敗した場合は、壊れた頂点バッファを作らずロード失敗として扱う。
`SCENE_GAME`のロード表示には、CPU実行中の`IN`、CPU準備済みの`READY`、
GPU転送中の`GPU`を分けて表示する。カタールと中国は、stride付き属性と32bitインデックスの
回帰確認用モデルである。

`Sprite3D::Draw()` の先頭では、モデル空間の AABB 中心・サイズをワールド変換して保守的な境界球を計算し、
視錐台の外側にあるモデルを描画しない。FBX と GLB の両方に共通で適用され、画面外・カメラの
Near / Far 外にある大きなモデルの描画コストを抑える。`AnimSprite3D::Draw()` にも同じ判定を適用する。
これはモデル単位の簡易カリングであり、
3D Tiles の `boundingVolume` を使ったタイル単位のストリーミングや LOD 選択ではない。

### 大量のGLBを扱う場合のメモリ注意

視錐台カリングは `Draw()` を呼ばないだけであり、モデル、頂点／インデックスバッファ、
埋め込みテクスチャ、白テクスチャなどのGPUリソースを解放しない。したがって、画面外を
向いて軽くなっても、ロード済みのモデルが多ければRAMとVRAMの使用量は減らない。
`SCENE_GAME`では別途カメラ距離によるストリーミングを行い、範囲外の`Sprite3D`を破棄する。
これにより初回ロードと常駐メモリが改善する。新規GLBのCPUデコードはワーカーで行い、
GPU化と破棄はゲーム更新中（初期ロード）または Present 後（ストリーミング継続）の
時間予算に分割するため、移動先の建物がロードされても更新と描画を長時間占有しない。

LOD3パビリオンの距離判定はカメラ、視線先予測点、移動先予測点のXZ距離に、既知のモデルXZ半径を足す。
開始はロード半径 48、破棄は半径 72（ワールド単位）。視線方向（半頂角60°）は半径をさらに32足して
先行開始する。初期優先ロードは `expo_pavilion_null2.glb`、`expo_pavilion_dynamic_equilibrium.glb`、
`expo_pavilion_expo_related_31.glb`、`expo_pavilion_expo_related_25.glb`、`expo_pavilion_angola.glb`、
`expo_pavilion_czech.glb` をこの順で床・LOD2・リングと
同じ初期完了条件に含め、明転前に最優先でGPU化する。CPUインポートはロード半径内だけ始め、
GPU化は破棄半径内まで進める。
ヒステリシス帯に残った READY はインポート枠を塞がず、視線先と近い棟の新規開始を優先する。
範囲外で無効化した未完了ジョブは、再び範囲内へ戻ったら同じジョブを再開する。
ファイル無し以外の失敗も、再接近でやり直す。

特に多数の高ポリゴンGLBを `Sprite3D` として常駐させる構成では、次を分けて考える。

- **描画カリング**: 既存の `Sprite3D::IsVisibleFromCamera()`。描画負荷を下げる。
- **タイル単位カリング**: 3D Tiles の `boundingVolume` で描画対象をまとめて選ぶ。
- **実行時LOD**: 距離や `geometricError` に応じて表示モデルを切り替える。
- **ストリーミング／キャッシュ**: 範囲外モデルのCPUデータ、GPUバッファ、テクスチャを解放する。距離選択、視線方向と移動方向の先読み、GPUアップロードと破棄のフレーム予算を実装している。

RAMやVRAMが逼迫する場合は、モデル単位カリングを追加するだけでは不十分である。
ロード済み件数、メッシュ数、頂点数、インデックス数、テクスチャ総容量を記録し、
上限を超えないロードキューと、参照がなくなったモデルを解放する所有権設計が必要になる。
さらに、GPU化中は`cube.fbx`のサイズ調整済みプレースホルダーを表示し、
本モデルのGPU化完了時に置き換える。`SCENE_GAME`の未パンチLOD2遠景は、
LOD3と共有するバッチIDをGPU化状態に応じて非表示にするため、遠景タイルを
建物単位のGLBへ分割せずに排他表示できる。

---

## AnimSprite3D（スケルタルアニメーション）

ヘッダ: `framework/anim_sprite3d.h`

詳細は [anim_sprite3d_usage.md](anim_sprite3d_usage.md) を参照。

```cpp
AnimSprite3D* chara = new AnimSprite3D(
    XMFLOAT3(0, 0, 0), XMFLOAT3(1, 1, 1), XMFLOAT3(0, 0, 0),
    "asset/model/character.fbx", S_PHONG);

chara->SetAnimationBlendDuration(0.2);
chara->PlayAnimationByName("Walk", true);
chara->UpdateAnimation(1.0f / FPS);  // 内部で UpdateBoneMatrices() も実行
chara->Draw();
delete chara;
```

---

## Billboard（3D板ポリゴン）

ヘッダ: `framework/billboard.h`

3D 空間に置く四角形。既定はカメラ追従ビルボード。床など固定板にも切り替え可能。

```cpp
Billboard* bb = new Billboard(
    XMFLOAT3(0, 1, 0), XMFLOAT2(2, 2), XMFLOAT3(0, 0, 0),
    "asset/texture/orb.png", false  // isDoubleSided
);
bb->Update();
bb->Draw();              // または Draw(S_PHONG) でシェーダー指定
bb->SetBillboardMode(false);       // 固定板
bb->SetReceiveShadow(true);        // 影受け（床など）
bb->SetNormalMap("asset/texture/Normal.png");
bb->SetUVAnimation(4, 0.1f);       // 横コマ数, 1コマ秒数
bb->DrawShadowMap(lightView, lightProj);
delete bb;
```

### SplitBilBoard（分割テクスチャ）

ヘッダ: `framework/split_bilboard.h`（`Billboard` 継承）

```cpp
SplitBilBoard* anim = new SplitBilBoard(
    4, 2,  // cols, rows
    XMFLOAT3(0, 1, 0), XMFLOAT2(1, 1), XMFLOAT3(0, 0, 0),
    "asset/texture/sheet.png"
);
anim->SetFPS(12.0f);
anim->SetLoop(true);
anim->Update();
anim->Draw();
anim->SetTextureIndex(3);  // 手動コマ指定も可
delete anim;
```

検証例: `SCENE_DEBUG/debug_lighting_scene.cpp`。

---

## Movie（動画）

ヘッダ: `framework/movie.h`（Media Foundation、`Transform2D` 継承）

MP4 等を 2D テクスチャとして描画。音声は同ファイルから自動再生。

```cpp
Movie* movie = new Movie(
    XMFLOAT2(640.0f, 360.0f),
    640.0f,                 // 幅（高さはアスペクトから算出）
    0.0f,
    XMFLOAT4(1, 1, 1, 1),
    BLENDSTATE_ALFA,
    L"asset/movie/intro.mp4",
    false,  // useChromaKey（緑背景透過 → S_CHROMAKEY）
    true,   // loop
    true    // autoPlay
);
movie->Update();
movie->Draw();   // SetDepthEnable(false) 後
movie->Play();   // 再再生
delete movie;
```

---

## Sound（音声）

ヘッダ: `framework/sound.h`（XAudio2 + Media Foundation、MP3）

```cpp
InitSound();    //main.cppで呼び出し
UninitSound();  //main.cppで呼び出し

SoundData* bgm = LoadMP3(L"asset/sound/bgm.mp3");
SoundData* se  = LoadMP3(L"asset/sound/se/kettei.mp3");

PlaySound(bgm, true);
PlaySound(se, false);
StopSound(bgm);
UnloadSound(bgm);

SetMasterVolume(0.5f);
double sec = GetPlaybackPositionSec(bgm);
```

推奨ボリューム（`define.h`）:

- BGM: `SOUND_BGM_VOLUME = 0.3f`
- SE: `SOUND_SE_VOLUME = 0.4f`

`INPUT_ACTION_DECIDE` のトリガー時、`Input_Initialize` で読み込んだ `kettei.mp3` が自動再生される。
ゲーム側の BGM / SE は `SCENE_GAME/gameaudio.cpp`。一覧は [audio_needs.md](../document_game/audio_needs.md)。
パスに `bgm`、`music`、`score` を含むファイルは BGM として扱い、それ以外は効果音として扱う。
音声キャッシュも通常シーンとグローバルBGMを分けて管理する。

Windows の `mmsystem.h` が `PlaySound` を `PlaySoundA` / `PlaySoundW` に置き換える。
`sound.h` は宣言の前にこのマクロを `#undef` する。`sound.cpp` は先に `define.h`（`WIN32_LEAN_AND_MEAN`）を読む。
呼び出し側も `define.h` を `sound.h` より先にインクルードする。

---

## DrawFont（テキスト描画）

ヘッダ: `framework/font.h`

詳細は [font_renderer_usage.md](font_renderer_usage.md) など。

```cpp
Font_InitializeGlobalData();
Font_FinalizeGlobalData();

DrawFont* label = new DrawFont(
    XMFLOAT2(100.0f, 50.0f),
    32.0f,
    0.0f,
    XMFLOAT4(1, 1, 1, 1),
    "Score: 0",
    TA_MIDDLE              // 省略可（既定 TA_MIDDLE）
);
label->Draw();             // SetDepthEnable(false) の後で呼ぶ
label->SetText("Score: 100");
label->PreCacheGlyphs();
delete label;
```

フォント: `asset/font/ZenKakuGothicNew-Medium.ttf`。アトラス 2048×2048、LRU キャッシュ。
フォントアトラスと動的フォント頂点バッファは `D3D11_USAGE_DYNAMIC` で作成し、更新時に
`Map(D3D11_MAP_WRITE_DISCARD)` で初期データを転送する。`D3D11_BUFFER_DESC` の初期データ指定に
依存しないため、D3D11 ドライバー間の初期化差を避けられる。

### 派生クラス

| クラス | ヘッダ | 用途 | 詳細 |
| ------ | ------ | ---- | ---- |
| `ClickFont` | `ClickFont.h` | ホバー／クリック付き 1 行 | [click_font_usage.md](click_font_usage.md) |
| `MultiLineDrawFont` | `MultiLineDrawFont.h` | 複数行テキスト | [multiline_font_renderer_usage.md](multiline_font_renderer_usage.md) |
| `MultiLineClickFont` | `MultiLineClickFont.h` | 複数行 + クリック | [multiline_click_font_usage.md](multiline_click_font_usage.md) |

---

## Fade（フェード遷移）

ヘッダ: `framework/fade.h`

```cpp
Fade_Initialize();
Fade_Finalize();
Fade_Update();   // 毎フレーム
Fade_Draw();     // 2D の最後（main が Present 直前に呼ぶ）

SetSceneFade(SCENE_GAME);   // フェードアウト → シーン切替 → ウォームアップ → フェードイン
Fade_StartIn();             // SCENE_NONE 暗転後のフェードイン
Fade_HoldUntilReady();      // シーン側の初期ロード完了まで明転を保留
Fade_NotifyReady();         // 初期ロード完了を通知
Fade_SetLoadProgress(0.5f); // フェード前面のロード進捗を更新
FADESTAT state = GetFadeState();
```

| 値                | 状態                      |
| ---------------- | ----------------------- |
| `FADE_NONE`      | 非フェード                   |
| `FADE_OUT`       | 暗転中                     |
| `FADE_WAIT_LOAD` | 暗転後、ロード前の 1 フレーム待機      |
| `FADE_WARMUP`    | シーン初期化スパイク逃がし           |
| `FADE_IN`        | 明転中                     |
| `FADE_MAX`       | 完全暗転で待機（`SCENE_NONE` 時） |


速度: α ±0.05f/フレーム（約 20 フレーム ≈ 1/3 秒）。
シーン初期化後は 6 論理フレームのウォームアップ（`FADE_WARMUP`）。フェード中は Present 間引きが無効。

シーン初期化中に初期ロードを行う場合は `Fade_HoldUntilReady()` を呼ぶ。
`FADE_WARMUP` はロード完了通知の `Fade_NotifyReady()` まで暗転を維持し、
通知後に通常のウォームアップを経て `FADE_IN` へ進む。
保留中に `Fade_SetLoadProgress()` を更新すると、`Fade_Draw()` が全画面フェードの
前面へプログレスバーと残り割合を描画する。ロード完了後は表示を終了する。

シーン列挙は [`app/scene.h`](../app/scene.h):

```text
SCENE_TITLE = 0
SCENE_GAME
SCENE_RESULT
SCENE_MAX
SCENE_NONE
SCENE_DEBUG
```

`SCENE_DEBUG` は `SCENE_MAX` より後ろなので、通常シーン用のキャッシュ配列に含まれない。
公開遷移は `SetSceneFade`。完了後に `ApplySceneInternal` が旧シーン `Finalize`、ID更新、新シーン `Init`、`RequestRedraw` を実行する。
ゲーム内容は [game_specification.md](../document_game/game_specification.md)。入力は [input.md](input.md)。

---

## Renderer（描画エンジン）

ヘッダ: `shader/renderer.h`

`InitRenderer()` は `IDXGIFactory6::EnumAdapterByGpuPreference(HIGH_PERFORMANCE)` で
高性能 GPU を選び、それが使えない OS では専用 VRAM 最大のハードウェアアダプターを選ぶ。
`EnumOutputs()` の有無では選ばない（ハイブリッドノートで内蔵 GPU が優先されるのを避ける）。
`--gpu=high` は同じ高性能選択を明示する。D3D11 デバイスと
`CreateSwapChainForHwnd()` による Flip モデルのスワップチェーンを作成する。スワップチェーンは
2 枚のバックバッファ（`DXGI_SWAP_EFFECT_FLIP_DISCARD`）を持ち、`configureBackBuffer()` が
各バックバッファの RTV、バックバッファと同じ解像度のシーン色中間RT、シーンの 1/4 解像度 AO RT、および
シーン解像度の `R24G8_TYPELESS` 深度バッファを生成する。深度バッファは
`D24_UNORM_S8_UINT` のDSVと `R24_UNORM_X8_TYPELESS` のSRVを同じリソースから作る。
Present 後はバックバッファだけをバインドする。

ウィンドウサイズ変更時は `Direct3D_ResizeWindow()` でクライアントサイズを記録した後、
`Direct3D_Resize()` が `ResizeBuffers()` とバックバッファ／深度バッファの再生成を行う。
`DRAW_SCREEN_X/Y` は固定バックバッファ解像度ではなく、2D/3D の基準アスペクトに使う。

現在の `SCENE_GAME` は、多数のLOD3近景を距離ストリーミングする検証構成である。
カリングで描画が軽くなっても、モデル単位カリングだけではリソースは解放されないため、
距離ストリーミングが範囲外のGPUバッファとテクスチャを破棄する。
CPU準備済みモデルはGPU待ちキューとして別に数え、ワーカー数をGPU待ちで塞がない。
GPU待ちは破棄半径内だけを枠に数え、ロード半径外の READY が近景の開始を止めない。
初期完了後の GPU アップロードは `Present` の後へ送る。

LOD2本体と未パンチLOD2遠景では、`boundingVolume.region` による描画時タイルカリングを使う。
`geometricError` に基づく実行時LOD切り替えは未実装である。
距離ストリーミングとロード待ち上限で現在の常駐量を抑えている。

- 可視範囲外のタイルをGPU化しないタイル単位カリング
- 遠距離でLOD3を使わない実行時LOD
- 範囲外モデルのGPUバッファ／テクスチャを破棄するストリーミングキャッシュ
- RAM／VRAMの使用量とロード待ち件数の計測

### 毎フレームの描画フロー

```cpp
Clear();

// --- 3D描画 ---
Direct3D_BeginScene();   // SCENE_GAMEのみ。シーン色RTへ切り替え
SetDepthEnable(true);   // 内部で 3D ビューポートも設定
// モデルの Draw()
Direct3D_ApplySsao();    // 深度からAOを作り、バックバッファへ合成

// --- 2D描画 ---
SetDepthEnable(false);  // 内部で 2D ビューポートも設定
// スプライト / Font の Draw()（各 Draw が行列をセットアップ）

Present();
PumpAfterPresent(lastDrawMs, lastGpuMs);
```

`main.cpp` ではシーン `Draw()` の後に `SetDepthEnable(false)` → `Fade_Draw()` → ImGui → `Present()` を行う。
`SCENE_GAME` は続けて `PumpAfterPresent` で LOD3 の GPU 化を進める。
静止した通常シーンは `NeedsPresent` が偽なら `Clear` / `Present` を間引く。動いたフレームは `RequestRedraw`。
処理順の詳細は [起動とメインループ](#起動とメインループ)。

`Direct3D_ApplySsao()` は3D描画色へだけ画面空間AOを合成する。AOは既定オフで、オン時はシーンの 1/4 解像度。オフ時は生成を省略し内部RTをバックバッファへコピーするだけである。
`SsaoPS` の近傍遮蔽、`SsaoBlurPS` の深度依存ぼかし、`SsaoCompositePS` の
色合成を順に行い、2D HUD / Font / ImGuiはAOの影響を受けない。
調整値は `Direct3D_SetSsaoParameters()` で設定し、ゲーム側では
`Sunlight_DrawDebug()` の `Expo Sunlight` から変更する。

`F2` は `app/scene.cpp` から `TakeScreenshot` を呼び、`screenshot/` に `1920×1080` の PNG を保存する。
撮影は `Update` 中にオフスクリーンへ `Draw` し直すため ImGui フレーム外であり、
`Direct3D_IsTakingScreenshot` が真のときは ImGui ウィジェットを描かない。

通常の ShadowMap は `BeginShadowMap` / `EndShadowMap` の間で描画する。
4方向 × Player/Enemy 用の配列 ShadowMap API もあるが、通常の Title / Game / Result では使っていない。

### 行列・ライト設定

```cpp
SetWorldMatrix(worldMat);
SetViewMatrix(viewMat);
SetProjectionMatrix(projMat);
SetCameraPosition(XMFLOAT3(x, y, z));
SetLight(light);
SetPlayerLights(lights);   // PBR 用 3 点照明
SetFog(FOG_CONSTANT{        // S_PBR 用の距離・高度フォグ
    XMFLOAT4(r, g, b, density),
    XMFLOAT4(start, end, heightMin, heightRange)
});
SetParameter(XMFLOAT4(...)); // Toon1 閾値、S_PBR の roughness/metallic（z=0 でマップ無し）
```

`SetFog` は `b11` の定数バッファへフォグ設定を送る。`S_PBR` の最終色にだけ適用され、
距離が `start` から `end` へ近づくほど、また `heightMin` 付近ほど `Color.rgb` へ混合する。
`Color.a` が密度であり、スカイドーム (`S_SKYBOX`) や `S_UNLIT` には適用されない。

### ブレンドステート

```cpp
SetBlendState(BLENDSTATE_NONE);   // 合成なし
SetBlendState(BLENDSTATE_ALFA);   // αブレンド
SetBlendState(BLENDSTATE_ADD);    // 加算
SetBlendState(BLENDSTATE_SUB);    // 減算
```

### シェーダータイプ（SHADERTYPE）

`shader/shadermanager.h` より。


| 値                             | 説明                      |
| ----------------------------- | ----------------------- |
| `S_UNLIT`                     | ライティングなし                |
| `S_LAMBERT`                   | 頂点ランバート                 |
| `S_PHONG`                     | ピクセルフォン（汎用の既定寄り）        |
| `S_PBR`                       | GGX PBR。`SetLight` の `Position.w==0` で平行光、それ以外は点光源。3点は `SetPlayerLights`。`SetFog` の距離・高度フォグを適用 |
| `S_RIM_LIGHT`                 | リムライト                   |
| `S_OUTLINE`                   | アウトライン                  |
| `S_SHADOW_MAP`                | ShadowMap 深度描画          |
| `S_BILLBOARD_SHADOW_MAP`      | 透過ビルボード用 ShadowMap      |
| `S_SHADOW_RECEIVE`            | 影受け                     |
| `S_NORMAL_MAP_SHADOW_RECEIVE` | 法線マップ + 影受け             |
| `S_PHONG_SHADOW`              | 点光ランバート + 影受け           |
| `S_CHROMAKEY`                 | クロマキー（動画透過）             |
| `S_COOK_TORRANCE`             | Cook-Torrance           |
| `S_DISNEY_PBR`                | Disney PBR              |
| `S_HEMISPHERE`                | 半球ライティング                |
| `S_NORMAL_MAP`                | 法線マップ単体                 |
| `S_POINT_LIGHT`               | ポイントライト                 |
| `S_SPOT_LIGHT`                | スポットライト                 |
| `S_TOON1`                     | 段階トゥーン + 簡易エッジ          |
| `S_TOON2`                     | ランプテクスチャ（`Toon2.png`）   |
| `S_SKYBOX`                    | ワールド方向から正距円筒UVを計算するスカイドーム |


検証例: `SCENE_DEBUG/debug_lighting_scene.cpp`。
`GrayscaleVS.hlsl` / `GrayscalePS.hlsl` はプロジェクトに含まれるが、`SHADERTYPE` とシェーダーマネージャーには未登録。

---

## Camera（カメラ）

ヘッダ: `framework/camera.h`

プレイヤー追従のオービットカメラ。マウス相対移動で yaw/pitch を更新する。

```cpp
Camera_Initialize();
Camera_Finalize();
Camera_Update();

Camera_SetTargetPos(playerPos);
Camera_SetLookTarget(XMFLOAT3(x, y, z));  // Yオフセットなし
Camera_SetOrbit(yaw, pitch, distance);
Camera_LookAtPoint(XMFLOAT3(x, y, z));  // 約 0.25 秒で向きを合わせる
GetCamera()->SkipNextInput(2);

Camera_SetSensitivity(1.0f);
Camera_SetDistance(6.0f);

Camera* cam = GetCamera();
XMFLOAT3 pos = cam->GetPos();
float yaw = Camera_GetYaw();
```

初期化時の既定: 視野角 45 度、Near 1.0、Far 500.0、距離 `CAMERA_DISTANCE`、注視点 Y オフセット `CAMERA_OFFSET_Y`。
`Camera_LookAtPoint` は向きを約 0.25 秒で補間する。
`Camera::Update` はマウス相対移動に加え、`Input_GetLookVector`、`Input_GetZoomDelta`、ホイールを
同じヨー・ピッチ制限と距離補間へ集約する。

ピッチ制限（`define.h`）:

- 上: `PITCH_LIMIT_LOOK_UP = 25.0f`
- 下: `PITCH_LIMIT_LOOK_DOWN = -60.0f`

デバッグ用フリーカメラは `SCENE_DEBUG/debugcamera.h`（WASD + マウス）。
`SCENE_GAME` の三人称追従は `SCENE_GAME/playercamera.cpp`。`PlayerCamera_Initialize` が `Camera_Initialize` と Far 2000 を設定し、`PlayerCamera_Update` / `PlayerCamera_Draw` が `SetCameraPosition` する。フレームワーク既定の `Camera_Update` オービットは使わない。Debugビルドでは ImGui 窓 `Expo Debug Camera` でフリーカメラに切り替えられる。操作は [input.md](input.md)。

---

## Texture / Light / Transform

### Texture

ヘッダ: `framework/texture.h`

スプライト等が内部で呼ぶテクスチャキャッシュ。終了時は `main` が `ReleaseAllTextures()` する。

- `LoadTexture`: sRGB色テクスチャ
- `LoadTextureLinear`: 法線など、sRGBを無視
- 通常シーンは `SCENE_MAX` サイズのシーン別キャッシュ
- `fade.png` は共通キャッシュとしてシーン切替後も保持
- シーン切替時に前シーンのキャッシュを `UpdateTextureCache` が解放
- `SCENE_DEBUG` は `SCENE_MAX` の範囲外なのでシーン別キャッシュの対象外

モデル内テクスチャの相対パスは、モデルファイルのディレクトリを基準に解決する。GLBの埋め込みテクスチャは `GlbModel` 側。

```cpp
ID3D11ShaderResourceView* srv = LoadTexture(L"asset\\texture\\image.png");
ID3D11ShaderResourceView* nrm = LoadTextureLinear(L"asset\\texture\\normal.png");  // 法線用（リニア）
ReleaseTexturesForScene(SCENE_GAME);  // シーン単位解放
UpdateTextureCache();                 // キャッシュ更新（必要時）
```

### Light

ヘッダ: `framework/light.h`

`SetLight` 用のヘルパー。`AmbientLight` + `PointLight::Apply` で一括設定できる。

```cpp
AmbientLight ambient(XMFLOAT4(0.15f, 0.15f, 0.15f, 1.0f));
PointLight point(
    TRUE,
    XMFLOAT4(0, 5, 0, 1),
    XMFLOAT4(1, 1, 1, 1),
    20.0f,   // range
    1.0f     // intensity
);
point.Apply(ambient);  // 内部で SetLight(ToLIGHT(...))
```

`S_PBR` の単一ライトは `SetLight`。`LIGHT.Position.w == 0` なら平行光で、`Direction` は光が進む向き、強度は `PointLightParam.y`、環境光は `Ambient`。点光源は `Position.w != 0`。マップ無しのときは `SetParameter(XMFLOAT4(roughness, metallic, 0, 0))`。3 点照明は従来どおり `SetPlayerLights`。本編の太陽は `SCENE_GAME/sunlight.cpp`（場のモデルとタクシーが `S_PBR`、スカイドームはワールド方向からHDRの正距円筒UVを計算する `S_SKYBOX`）。検証例: `SCENE_DEBUG/debug_lighting_scene.cpp`。

### Transform（component.h）

ヘッダ: `framework/component.h`

- `Transform3D` … `pos` / `rot`（度）/ `scale`。`Sprite3D`・`AnimSprite3D` の基底。
- `Transform2D` … `pos` / `rot`（度）/ `scale`。`Movie` などの基底。

---

## SCENE_DEBUGについて

デバッグへは Title 右上の `DEBUG` をクリック。実装フォルダは `SCENE_DEBUG/`。
`SCENE_DEBUG` 内は **Tab** でサブシーン循環（MODEL → LIGHTING）、右クリックで視点操作、移動はマイクラのクリエと同じ。
**Esc** で Title に戻る。

- MODEL: `asset/model` と `asset/expomodel` 直下の `.fbx` と `.glb` を候補として列挙し、選択中の1体だけを表示する。`←` / `→` でモデル切替、`U` マウスロック、`B` 原点キューブ、`R` 再読み込み。
- LIGHTING: 各種ライトを ImGui でパラメータ確認。

Releaseビルドには `SCENE_DEBUG` が含まれない。

## デバッグ用ユーティリティ

| ヘッダ | 用途 |
| ------ | ---- |
| `framework/debug_ostream.h` | `hal::dout`。既定では `OutputDebugString` しない。Debug かつプリプロセッサ `EXPO_VERBOSE_DEBUG_LOG` のときだけ UTF-8 をワイドへ変換して出す |
| `framework/input_monitor_console.h` | 別コンソールに入力状態を表示（`main` が自動初期化） |
| `framework/main.h` | Win32 / D3D / DirectXTex 共通 include、`SAFE_DELETE`、`SetFPS` |
| `shader/renderer.h` | 描画エンジン API、`SAFE_RELEASE`。Debug では `Direct3D_DebugStageBegin` と Map 回数 |

Debug ビルドの `SCENE_GAME` では、ウィンドウキャプションに Draw/Logic FPS、`Upd` / `Drw` / `Prs` / `GPU` / `Map` / `Idx` / `Shd` / `Fld` / `Obj` / `UI` / `Pump` を出す。同じ値をプロジェクトルートの `debug-frame-perf.log` へ CSV で残す（起動のたびに上書き）。`gpuMs` が低く `fldMs` と `idx` が高いときは CPU 側の Draw 発行、両方が高いときは GPU 待ちである。Debug の `F5` キーは局所影パスのオン／オフ。Cursor の F5（CodeLLDB の `Debug` / `Release`）はデバッグイベントで実速度を落とすことがある。実速度は `Debug Clean (No Debugger)` または `Release Clean (No Debugger)` で測る。

サードパーティ（直接触らない）: `assimp/`・`freetype/`・`imgui/`・`nlohmann/`・`DirectXTex.h`・`stb_truetype.h`。

### SAFE_DELETE

定義: `framework/main.h`

`new` で確保したオブジェクトを解放するマクロ。`nullptr` なら何もしない。解放後はポインタを `nullptr` に戻す。
シーンの `Finalize` などで使う。

```cpp
void Title_Finalize(void)
{
	SAFE_DELETE(g_pTitleText);
	SAFE_DELETE(g_pHintText);
}
```

| 項目 | 内容 |
| ---- | ---- |
| 対象 | `new` / `delete` した単一オブジェクト（`DrawFont*` など） |
| 効果 | `delete` + ポインタを `nullptr` にクリア |
| 再呼び出し | 安全（2 回目は何もしない） |
| 配列 | `new[]` には使わない（`delete[]` が必要） |
| COM | `ID3D11*` 等は `SAFE_RELEASE`（`shader/renderer.h`）を使う |

`main.h` を include していれば利用可能。シーン雛形（`template/template.cpp`）もこの書き方になっている。

### SAFE_RELEASE

定義: `shader/renderer.h`

COM オブジェクト（Direct3D のバッファ・ビュー・シェーダーなど）を解放するマクロ。有効なポインタなら `Release()` を呼び、その後 `nullptr` に戻す。

```cpp
#define SAFE_RELEASE(p) do { if (p) { (p)->Release(); (p) = nullptr; } } while (0)
```

`Finalize` やリソース破棄時に使う。

```cpp
SAFE_RELEASE(m_VertexBuffer);
SAFE_RELEASE(g_RenderTargetView);
SAFE_RELEASE(g_DepthStencilView);
```

| 項目 | 内容 |
| ---- | ---- |
| 対象 | COM（`ID3D11Buffer*`、`ID3D11ShaderResourceView*` など） |
| 効果 | `Release()` + ポインタを `nullptr` にクリア |
| 再呼び出し | 安全（2 回目は何もしない） |
| C++ オブジェクト | `new` したものには使わない（`SAFE_DELETE` を使う） |

`renderer.h` を include していれば利用可能。

---

# tool（開発用スクリプト）

ディレクトリ: `tool/`

ソース編集後のエンコーディング整備や、シーン追加・プロジェクト名変更など、開発用の補助スクリプトを置く。
基本的にrealryo1用なので触れるべきではない（安全のための説明）。

---

## encoding_converter.py

`.editorconfig` のルールに合わせて、対象ディレクトリ直下のファイルを変換する（**サブディレクトリは再帰しない**）。

| 対象                               | 変換後                      |
| -------------------------------- | ------------------------ |
| `.h` / `.hpp` / `.c` / `.cpp` など | UTF-8 **BOM 付き**、改行 CRLF |
| `.hlsl` / `.hlsli`               | UTF-8 **BOM なし**、改行 CRLF |

```powershell
# setupdirectory.txt に列挙したディレクトリを変換
python tool/encoding_converter.py

# 単一ディレクトリのみ（例: /framework）
python tool/encoding_converter.py /framework
```

- `.editorconfig` が無い場合は既定内容で作成する。

---

## prepare_collision.py

描画GLBから衝突バイナリ `asset/collision/<stem>.bin` を書く。引数なしでは床・リング、存在するLOD2タイル／遠景タイル、東西ゲートも対象にする。詳細は [collision.md](collision.md)。

```powershell
python tool/prepare_collision.py
python tool/prepare_collision.py asset/model/foo.glb
```
- `setupdirectory.txt` が無い場合も既定で作成する。
- 実装・編集の完了後に実行。

`setupdirectory.txt` の書き方:

```
# / = プロジェクトルート直下のみ（サブフォルダは含まない）
/
/framework
/shader
/app
/SCENE_TITLE
```

---

## manage_scene.py

`template/` を雛形に、シーンの追加・削除を対話で行う。

```powershell
python tool/manage_scene.py
```

追加時に更新されるもの:

- `SCENE_XXX/`（`template.h` / `template.cpp` から生成）
- `app/scene.h` の `enum SCENE`
- `app/scene.cpp` の `#include` と `switch` の `case`
- `tool/setupdirectory.txt`
- ルート `*.vcxproj` / `*.vcxproj.filters`

`SCENE_DEBUG` / `SCENE_MAX` / `SCENE_NONE` は対象外。追加・削除後は内部で `encoding_converter` 相当の処理も走る。

万博モデルの抽出・床・リング・パビリオン準備はゲーム側ツールであり、
[plateau.md](../document_game/plateau.md) を参照する。

---
## rename_project.py

`.sln` / `.vcxproj` / `.rc` などのファイル名と、リポジトリ内テキスト中の旧名称をまとめて置換する。CI（`.github/workflows/*.yml`）も対象。

```powershell
python tool/rename_project.py NewName
python tool/rename_project.py NewName --old expogame
python tool/rename_project.py NewName --dry-run   # 書き込みなしで確認
python tool/rename_project.py                    # 対話モード
```

実行前に Visual Studio を閉じ、完了後は新しい `.sln` を開き直す。古い名前が残る場合は `.vs/` を削除する。

---
## tool/build.ps1（VSCode系IDE用ビルドツール）

**通常のVisualStudioを使用する場合は関係ない。**
想定される使い方は VS Code 系 IDE（Cursor 含む）のタスク、およびブレークポイント用の「実行とデバッグ」。`vswhere` または既定パスから MSBuild を探し、`expogame.sln` を x64 でビルドする。実体は `tool/build.ps1` である。


| IDE 操作                           | 呼び出し先                                | 実体                                      |
| -------------------------------- | ------------------------------------ | --------------------------------------- |
| 実行とデバッグ → `Debug` / `Release`   | CodeLLDB（`type: lldb`）          | デバッガー接続。実速度計測には使わない |
| 実行とデバッグ → `Debug Clean (No Debugger)` / `Release Clean (No Debugger)` | `node-terminal`（LLDB なし） | Clean → 各 Configuration のビルド → `expogame.exe` をターミナルから直接起動 |
| タスク: Run Release (No Debugger)   | `.vscode/tasks.json`                 | Build Release のあと `x64/Release/expogame.exe` を直接起動 |
| タスク: Run Release Binary          | 同上                                   | ビルドせず同じ exe を直接起動 |
| タスク: Rebuild and Run Release (No Debugger) | 同上                          | Clean → Build → 直接起動 |
| タスク: Build Debug（既定ビルド）          | 同上                                   | `tool/build.ps1 -Configuration Debug`        |
| タスク: Build Release               | 同上                                   | `tool/build.ps1 -Configuration Release`      |
| タスク: Clean Debug / Clean Release / Rebuild Release | 同上                    | `tool/build.ps1` の Clean と Rebuild |


設定ファイル:

- `.vscode/launch.json` … `Debug` / `Release` は CodeLLDB。`Debug Clean (No Debugger)` / `Release Clean (No Debugger)` は LLDB を付けず Clean ビルドして起動する。Cursor では `cppvsdbg` は使えない
- `.vscode/tasks.json` … ビルドとデバッガーなし起動

ZIP 作成（`create_release_zip.py`）は毎回 Release を Clean してからビルドする。開発側 F5 が重いのに ZIP や No Debugger が軽いときは、描画コードより CodeLLDB 接続、増分ビルド／共有 CSO、または `asset/expomodel` の差を疑う。

---

## create_release_zip.py

リリース用ファイルの収集補助。CI（[`.github/workflows/build.yml`](../.github/workflows/build.yml)）は手動起動の Release x64 ビルドと配布 ZIP 作成に使う。
シェーダー CSO、実行時 DLL、`asset` は配布 ZIP へ収集される。

Visual Studio プロジェクトは Win32 と x64、Debug と Release を持つが、依存 DLL と Debug シーンを考慮して x64 を基本とする。

---

## 注意点など

- **座標は必ず** `SCREEN_X/HEIGHT` **基準**。`DRAW_SCREEN_`* を位置計算に使わない。
- **2D / Font は** `Draw()` **単体で完結**。2D 前に `SetDepthEnable(false)`。
- **3D は** `SetDepthEnable(true)`**、2D 前に** `false`。ビューポートもこれに連動。
- `Fade_Draw()` **は他 UI より前面**（`main` が Present 直前に呼ぶ）。
- **ウィンドウリサイズ仕様**は [direct3d_viewport_resize_spec.md](direct3d_viewport_resize_spec.md)（実装は `shader/renderer.cpp`）。
- `SetFPS` は固定ステップへ未接続（[起動とメインループ](#起動とメインループ)）。
- `SCENE_DEBUG` は `SCENE_MAX` の外側なので、通常のテクスチャ・音声シーンキャッシュと一致しない。
- サウンド初期化とゲームパッド初期化が複数の入口から呼ばれる。新規機能では所有者を増やさない。
- 一部サンプルは現行シーンの実装例ではなく、API利用例として読む。
- GLB は既定で 100 倍スケール。`Sprite3D::SetCastShadow(true)` なら ShadowMap 対象になる。受信は `SetReceiveShadow(true)` と `S_PBR`。
- `.h/.cpp` **は UTF-8 BOM 付き**、`.hlsl` **は UTF-8 BOM なし**。編集後は `python tool/encoding_converter.py` を実行する。
- `new` **したポインタの解放は** `SAFE_DELETE`**（**`delete` **直書きや二重解放を避ける）。**
- **COM（**`ID3D11*` **等）の解放は** `SAFE_RELEASE`**（**`Release()` **直書きや二重解放を避ける）。**

