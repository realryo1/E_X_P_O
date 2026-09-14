# expogame 入力仕様および実装ドキュメント

本ドキュメントでは、expogame における入力抽象化の仕様、現行シーンでの使い方、および `Input_Action` 定数について解説します。

---

## 1. 入力抽象化の概要

接続デバイスに関わらず、ゲーム側の入力は `InputManager`（`framework/input_manager.h` / `input_manager.cpp`）を介して抽象化する。

* **任天堂配列 (ABXY) 強制**: `Input_Initialize` 時に `GAMEPAD_LAYOUT_SWITCH_ABXY` を設定。右ボタンが A、下ボタンが B。
* **シーンからの直接インクルード排除**: シーン実装は `keyboard.h` / `gamepad.h` ではなく `input_manager.h` を使う（デバッグシーンの特殊キー操作は例外あり）。
* **決定 SE**: `INPUT_ACTION_DECIDE` のトリガー成功時に `asset/sound/se/kettei.mp3` を再生する。ゲーム内のその他の BGM / SE は `SCENE_GAME/gameaudio.cpp` が再生し、決定音は重ねない。

追加 API:

```cpp
Input_Vector2 Input_GetMoveVector(void);  // WASD / DPad / LStick
Input_Vector2 Input_GetLookVector(void);  // 矢印 / RStick
float Input_GetZoomDelta(void);           // RT - LT（正で接近）
void Input_SetRumble(float leftMotor, float rightMotor);
void Input_SetGamepadLayout(Gamepad_Layout layout);
```

---

## 2. アクション定数（Input_Action）とマッピング

現行の `Input_Action` は以下である。

| 定数名 | 主な用途 | キーボード | ゲームパッド |
| :--- | :--- | :--- | :--- |
| `INPUT_ACTION_DECIDE` | 決定 / 進行 | `Space`, `Enter` | `A` |
| `INPUT_ACTION_CANCEL` | キャンセル / 戻る | `BackSpace` | `B` |
| `INPUT_ACTION_MENU_UP` | メニュー上 | `W`, `↑` | `DPad-UP` / `LStick-UP` (※1) |
| `INPUT_ACTION_MENU_DOWN` | メニュー下 | `S`, `↓` | `DPad-DOWN` / `LStick-DOWN` (※1) |
| `INPUT_ACTION_MENU_LEFT` | メニュー左 | `A`, `←` | `DPad-LEFT` / `LStick-LEFT` (※1) |
| `INPUT_ACTION_MENU_RIGHT` | メニュー右 | `D`, `→` | `DPad-RIGHT` / `LStick-RIGHT` (※1) |
| `INPUT_ACTION_PAUSE` | ポーズ | `Escape` | `START` |
| `INPUT_ACTION_BACK` | 一つ戻る | `Escape`, `X` | `X` |
| `INPUT_ACTION_JUMP` | 上昇 | `Space` | `B` |
| `INPUT_ACTION_DESCEND` | 下降 | `Left Shift`, `Right Shift` | - |

> **(※1) スティックのトリガー検出**  
> `Input_Update` 内で LStick の前フレーム比較を行い、閾値 `0.5f` を超えた最初の 1 フレームだけを `Input_IsActionTrigger` で返す。長押しによるメニュー誤作動を防ぐ。

判定 API:

```cpp
bool Input_IsActionDown(Input_Action action);     // 押し続け
bool Input_IsActionTrigger(Input_Action action);  // 押した瞬間
```

---

`Input_Initialize` 内でも `Gamepad_Initialize` が呼ばれ、`main` からも再度呼ぶ。
新しい入力処理を足すときは所有者を増やさない。詳細は [framework_usage.md](framework_usage.md#起動とメインループ)。

---

## 3. 各シーンにおける入力処理

ここはアクション API の利用例。操作の仕様は [game_specification.md](../document_game/game_specification.md)。次の作業は [task_list.md](../document_game/task_list.md)。

### SCENE_TITLE（`title.cpp`）
* Decide で `SetSceneFade(SCENE_GAME)`
* 使用アクション: `INPUT_ACTION_DECIDE`
* プレースホルダ

### SCENE_GAME（ホバー移動は `player.cpp`、視点と旋回入力は `playercamera.cpp`）
* `game.cpp` が `Esc`（`Keyboard_IsKeyDownTrigger(KK_ESCAPE)`）で `UnLockMouse`、ImGui 以外のウィンドウ左クリックで `LockMouse`
* ロック中は `game.cpp` が `Mouse_GetState` しない。相対 `dx`/`dy` は `playercamera.cpp` が1回だけ読む
* 解除中はマウス相対移動を視点に使わない。右スティックは従来どおり
* `Input_GetMoveVector` の前方向（W / DPad-UP / 左スティック上）でカメラヨー基準に前進する。A/Dの横移動とSの後退は使わない
* マウスの左右入力でカメラを旋回し、機体がカメラヨーへゆっくり追従する
* `INPUT_ACTION_JUMP`（`Space` / パッドB）で上昇し、`INPUT_ACTION_DESCEND`（`Shift`）で下降する。上下速度は徐々に変化する
* 前進中の上下移動は前進ベクトルと合成し、前進していない場合は垂直移動になる
* 旋回中は機体をロール方向へゆっくり傾け、カメラヨーへ正面が戻ると水平へ戻す
* マウスロックと `Input_GetLookVector`（右スティック）で視点回転
* `Expo Course` のコース作成モードでは `P` キーのトリガーで現在位置をコースへ追加する。ImGuiのテキスト入力中は配置しない
* シーン遷移はしない
* 使用アクション: `INPUT_ACTION_JUMP`, `INPUT_ACTION_DESCEND`

### SCENE_RESULT（`result.cpp`）
* Decide で `SetSceneFade(SCENE_TITLE)`
* 使用アクション: `INPUT_ACTION_DECIDE`
* プレースホルダ

### SCENE_DEBUG（`SCENE_DEBUG/debugscene.cpp`）
* **Tab**: サブシーン切替（MODEL → LIGHTING → TOON）
* **Esc**: `UnLockMouse()`
* カメラ操作はデバッグカメラ側（WASD + マウス相対移動）
* 上記は `keyboard.h` / `mouse.h` を直接使用

### その他（メインループ）
* **F2**: スクリーンショット（`scene.cpp`）
* **F11**: ボーダレスウィンドウ切替（`main.cpp`）

---
