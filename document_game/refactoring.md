# リファクタリング記録

機能・挙動を変えずに、SCENE_GAME のコードを整理・最適化する作業の記録。
対象は `SCENE_GAME/` の C++ のみ（HLSL・framework・外部ライブラリは対象外）。
方針は「毎フレームの無駄の削減」と「重複コードの整理・可読性向上」の両立。

## 進捗

| ファイル | 状態 |
| --- | --- |
| player.cpp | 完了 |
| playercamera.cpp | 完了 |
| gameaudio.cpp | 完了 |
| game.cpp | 完了 |
| photomode.cpp | 完了 |
| envprobe.cpp | 完了 |
| ui.cpp | 確認済み（デバッグ専用で変更なし） |
| collision.cpp | 完了 |
| course.cpp | 完了 |
| sunlight.cpp | 完了 |
| field.cpp | 完了（デバッグ専用コードと一部の定型処理は据え置き） |

ビルド・実行確認は行っていない（AGENTS.md の方針）。各変更は挙動が同一になるよう、式・評価順・初期化順を保っている。

## 変更内容

### player.cpp
- 加減速で目標値の大小から加速量/減速量を選んで `Approach` する処理が前後・上下で重複していたため、`ApproachAccel()` に統合。
- デバッグカメラ中と操作無効時の同一内容の早期 return を、1つの条件（`||`）にまとめた。
- ダッシュタイマーの 0 クランプを `fmaxf` で1行に。
- 移動量 `delta` を `const` の1回初期化にし、`g_ForwardSpeed + g_DashVelocity` の二重計算を `horizontalSpeed` に。
- 当たり判定半径の最小値クランプ3行を `fmaxf` に。

### playercamera.cpp
- yaw/pitch から視線方向を求める計算が追従カメラとデバッグカメラで重複していたため、`ComputeLookDir()` に共通化。
- ピッチを ±89° に丸める処理が4箇所に散在していたため、`ClampPitch()` に共通化（`ClampDebugPitch` は廃止）。
- `input_manager.h` の二重 include を削除。

### gameaudio.cpp
- BGM 4本・SE 15本ごとに個別のグローバル変数と読込/解放コードがあったため、パス表と配列（`g_Bgm[]`、`g_Se[]` + `SeId`）に置き換え、ロード・解放をループ化。読込順・解放順・終了時の `nullptr` 化は従来通り。
- `GameAudio_SetBgm*` を `BgmData()` 経由の1行関数に。
- ホバー音の更新で `g_SeHover` を何度も参照していたのをローカル変数にまとめた。

### game.cpp
- シャドウカスケード0と1以降で同じ描画を二重に書いていたのを、`i > 0` のときだけ `BeginShadowMapSlice` を呼ぶ1つのループに統合（描画順は従来通り）。

### photomode.cpp
- エフェクト値4つのリセットが2箇所で重複していたため、`ResetEffects()` に共通化。

### envprobe.cpp
- `EnvProbe_Initialize` / `EnvProbe_Finalize` の同一内容を `ResetProbeState()` に共通化。

### collision.cpp
- **高速化**: 衝突判定のたびに全メッシュで `sourceName` の文字列比較（ゲート判定・LOD2判定）をしていた。メッシュ登録時に `SetMeshSourceName()` で種別フラグ（`isHighDetailGate` / `isLod2`）を一度だけ計算して保持するようにした。判定は毎フレーム・サブステップごとに走るため効果が大きい。
- グリッドのセル走査（範囲の丸め＋セル内三角形の列挙）が衝突判定・ワイヤ描画・`Collision_SampleTopY` の3箇所で重複していたため、`ForEachGridItem()` / `ForEachGridItemInAabb()` に共通化。
- 訪問スタンプの世代更新とオーバーフロー時の初期化が4箇所で重複していたため、`NextVisitGen()` に共通化。
- `BuildFlatGrid` の「三角形が占めるセル範囲と、広すぎないかの判定」が2パスで重複していたため、`triCellRange` ラムダに共通化。

### course.cpp
- 「レース中（カウントダウン／走行中／ゴール後）」の3条件の並びが7箇所で重複していたため、`IsRaceMode()` に共通化。
- 2点間距離の二乗の計算（スタート位置へのワープ判定2箇所、マーカーのカメラ距離カリング）を `DistanceSquared()` に共通化。
- コースデータの代入・追加を `std::move` にして、ロード時の文字列・配列のコピーを削減。

### sunlight.cpp
- 初期化処理とデバッグの Reset ボタンで同じ約30項目の既定値代入が重複していたため、`ResetSunlightDefaults()` に共通化。
- 太陽方向ベクトルの計算が `ApplySunlightState`（毎フレーム）と `Sunlight_BeginLocalShadow` で重複していたため、`ComputeSunVector()` に共通化。`sinf(仰角)` の再計算は `sunVec.y` を使うことで省略（値は同一）。
- HDR 画像の輝度計算式が3箇所に直書きされていたため、`Luminance()` に共通化。

### field.cpp
- **高速化**: 非プローブ描画のたびに遠景の「非表示バッチ」集合（`unordered_set` の配列）を毎フレーム作り直していた。入力（遠景モデルのポインタと、パビリオンの遠景参照）を軽量なシグネチャとして保持し、変化がない間は再構築を省略する。モデル解放時（`ClearExpoTiles`）にシグネチャを破棄するので、再生成時の取りこぼしはない。
- パビリオンのストリーミング処理で、ワーカー状態（started/joined/workerDone/workerFailed/workerCancelled）のリセットが5箇所で重複していたため、`ResetJobWorkerState()` に共通化。
- `QueryPerformanceFrequency` の取得が2関数で重複していたため、`GetPerformanceFrequency()` に共通化。
- `RtcToWorldPosition` と `EcefToWorldPosition` が同じ計算の重複だったため、前者を後者の呼び出しに統一。
- モデル配列の一括 delete（3箇所）を `DeleteAllModels()`、所有モデル判定の3ループを `std::find` に置き換え。
