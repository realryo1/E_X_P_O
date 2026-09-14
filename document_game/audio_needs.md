# SCENE_GAME 音声（BGM / SE）

再生は `SCENE_GAME/gameaudio.cpp` が行い、決定入力の SE だけはフレームワーク側でも鳴る。
`LoadMP3` が失敗したパスは無音のまま進む。
権利は [copylight.md](../document_framework/copylight.md)、再生 API は [framework_usage.md](../document_framework/framework_usage.md) の Sound 節。
パスに `bgm` を含むファイルは BGM 扱いになる。

## 役割分担

`INPUT_ACTION_DECIDE` 成功時は `framework` が `asset/sound/se/kettei.mp3` を再生する。メニュー決定と、ゴール後の `Enter` / A でも鳴る。ゲーム側では同じ決定に重ねて鳴らさない。
メニュー決定と機体上昇はどちらも `Space` を使うが、メニュー中は `Player_SetControlEnabled(false)` のため上昇入力は止まる。

元音源なしのスロット（`menu.mp3`、`warp.mp3`、`race_abort.mp3`、`land.mp3`、`spawn.mp3`）はコードから再生するが、ファイルが無いので無音である。

## BGM

モード切替の本体は `SCENE_GAME/course.cpp` の `CourseMode`（`FreeFlight` / `CourseCreate` / `RaceCountdown` / `RaceRunning` / `RaceGoal`）である。
`sunlight.cpp` / `field.cpp` / `ui.cpp` は描画・ロード表示専用で、BGM の切替点にはならない。

会場散策／コース作成 — `asset/sound/bgm/explore.mp3`。元: `軽井沢の野鳥たち2.mp3`。`GameAudio_Initialize` で開始し、レース開始やシーン終了で止める。ループ。コース作成へ入っても同じ曲を継続する。
レース本編 — `asset/sound/bgm/race.mp3`。元: `Thrilling_Race.mp3`。`RaceCountdown` から `RaceRunning` へ変わったとき（3秒経過）に開始し、ゴール・中断・フリー飛行復帰で止める。
ゴール／リザルト — `asset/sound/bgm/goal.mp3`。元: `リザルト.mp3`。`EnterGoal` で開始し、`ReturnToFreeFlight` または `ABORT RACE` で止める。
ゲーム内メニュー — `asset/sound/bgm/menu.mp3`。元音源なし。`SetMenuOpen(true)` で開始し、閉じるとモードに応じた曲へ戻す。

## SE（操作・UI）

メニュー開閉 — `asset/sound/se/menu_open.mp3`。元: `決定ボタンを押す33.mp3`。`SetMenuOpen(true)` / `SetMenuOpen(false)`。
カーソル移動 — `asset/sound/se/cursor.mp3`。元: `決定ボタンを押す38.mp3`。`MoveMenuCursor`。クリック決定は `kettei.mp3`。
決定（メニュー項目実行） — `asset/sound/se/kettei.mp3`。元: 場面展開05。既存ファイル。`InputManager` の `INPUT_ACTION_DECIDE`（Enter / パッドA）と、メニュー行のクリック決定（`Input_PlayDecideSe`）。ゲーム側では重ねない。
無効操作／点の取消 — `asset/sound/se/invalid.mp3`。元: `キャンセル9.mp3`。レース開始なのに点数 2 未満のとき、作成中 `KK_U`、およびメニューの点の取消。
コース点の追加 — `asset/sound/se/point_add.mp3`。元: `カーソル移動3.mp3`。`AddCoursePoint`（作成中 `KK_P`）。
コース保存成功 — `asset/sound/se/save_ok.mp3`。元: `メニューを開く1.mp3`。`SaveWorkingCourse` 成功後。
コース保存失敗 — `asset/sound/se/save_ng.mp3`。元: `キャンセル3.mp3`。`SaveWorkingCourse` 失敗時。

## SE（レース）

レース開始ワープ — `asset/sound/se/warp.mp3`。元音源なし。`StartRace` の `Player_WarpTo`。
カウントダウン（3 / 2 / 1 / GO） — `asset/sound/se/countdown.mp3`。元: `カウントダウン電子音.mp3`。4カウントが1ファイルに入っている。`StartRace` で一度だけ再生する。レース中断時は止める。
リング通過（ブースト） — `asset/sound/se/boost.mp3`。元: `決定ボタンを押す20.mp3`。`CrossedGate` 成功後の `Player_ActivateDash`。
ゴール — `asset/sound/se/goal.mp3`。元: `ラッパのファンファーレ.mp3`（元seではゴール確定）。`EnterGoal`。
ゴール確定（フリー飛行へ） — `asset/sound/se/kettei.mp3`。元: 場面展開05。既存決定 SE。`RaceGoal` 中の `INPUT_ACTION_DECIDE`。
レース中断 — `asset/sound/se/race_abort.mp3`。元音源なし。`ABORT RACE` から `ReturnToFreeFlight`。

## SE（機体・衝突・環境）

プロペラ／ホバー（ループ） — `asset/sound/se/hover.mp3`。元: `ヘリコプター飛行中（機内）.mp3`。操作が有効なあいだループ。速度で音量・ピッチを変える。加速入力と上昇／下降もこのループに含める。
壁・床ヒット — `asset/sound/se/hit.mp3`。元: `机をドンと叩く.mp3`。`Collision_MoveAABB` 後に位置が入力どおり進まなかったとき。連続再生を抑える。
着地 — `asset/sound/se/land.mp3`。元音源なし。`grounded` が false から true。
機体出現 — `asset/sound/se/spawn.mp3`。元音源なし。`Player_Initialize`。ロード完了後に一度。

## 実装時の置き場所

散策／レース／ゴール／メニュー BGM の切替は `course.cpp` と `gameaudio.cpp`。コース作成は散策と同じ `explore.mp3` を継続する。
メニュー開閉・カーソルは `SetMenuOpen`、`MoveMenuCursor`、コース一覧ページ。
作成の P / U は `Course_Update` の `CourseCreate` 分岐と `AddCoursePoint`。
エンジンループと衝突は `Player_Update`。

`game.cpp` は `GameAudio_Initialize` / `Finalize` の呼び出しだけである。
`ui.cpp` のロード／メモリ表示、`sunlight.cpp` のライティング調整はゲーム音の対象外とする。

## 重複に注意する入力

`INPUT_ACTION_DECIDE` はメニュー決定とゴール後の進行の両方で使う。`asset/sound/se/kettei.mp3` が既に鳴る。
`INPUT_ACTION_PAUSE` はメニュー開閉のトグル。開く音と閉じる音を `SetMenuOpen` で分岐する。
`Space` はメニュー外では上昇（`INPUT_ACTION_JUMP`）。上昇はホバーループ側で扱う。
