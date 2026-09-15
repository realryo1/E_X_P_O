# 万博ゲーム 現在地点とタスク

基準日: 2026年9月15日

いま何ができて、どう操作し、どのファイルを触るかは
[game_specification.md](game_specification.md) に書く。
この資料は進捗と、次に何をするかだけを持つ。
描画・ライティング仕様は [rendering_and_lighting.md](rendering_and_lighting.md)、
BGM / SE は [audio_needs.md](audio_needs.md)、
フレームワークの使い方は [framework_usage.md](../document_framework/framework_usage.md)。

## いまここ

会場都市モデル（LOD2/未パンチ遠景/LOD3ストリーミング）、描画モデル単位および3D Tiles単位の視錐台カリング、空飛ぶタクシーのホバー飛行と床・リングAABB衝突、全モデルのPBRシェーディング、局所3段CSMシャドウ、半解像度SSAO、HDR太陽光抽出・スカイドーム同期、コース作成およびレース計測、ゲーム内 BGM / SE、NVIDIA dGPU 向けの `DrawIndexed` 削減と Present 後ストリーミングまで実装完了。確定仕様は [game_specification.md](game_specification.md) と [rendering_and_lighting.md](rendering_and_lighting.md) を参照。

現在保留・未着手の主要項目は、`null2` の見た目リサーチ、衝突メッシュ間引き、機体アニメーション、IBL・昼夜サイクルである。距離＋高度フォグは実装済み。メニュー BGM と一部 SE（ワープ、中断、着地、出現）はファイル未配置のため無音。

---

## やらない／後回し

- 衝突メッシュの間引きは未着手（必要に応じて検討）。
- `null2` の全面グレー問題は原因リサーチ復帰まで触らない。
- 機体アニメーション（`flytaxi.glb` のアニメーション接続）は未着手。
- IBL、昼夜サイクル、プレイヤーへの環境マッピングは未着手。距離＋高度フォグは実装済み。
- `SCENE_TITLE` と `SCENE_RESULT` はプレースホルダーのまま。

---

## タスク一覧

- [x] `fetch-expo-data`: 公式3D Tiles ZIPを取得・展開し、利用条件と実データ構成を記録する
- [x] `prepare-first-tile`: `tileset.json`を走査し、参照切れを検出して最初の`b3dm`からGLBを抽出する
- [x] `decode-draco-glb`: Draco圧縮を解除し、Assimpで読み込めるGLBを生成する
- [x] `connect-scene-game`: `asset/expomodel/expo_tile.glb`を`SCENE_GAME`へ接続する
- [x] `verify-model-load`: `PLATEAU EXPO / LOD1 TILE`表示まで確認する
- [x] `document-and-encoding`: 出典資料を整理し、`encoding_converter.py`を実行する
- [x] `game-camera`: `SCENE_GAME`へカメラ移動・視点操作を追加する
- [x] `lod2-texture`: LOD2またはLOD3のテクスチャ付きタイルを表示する
- [x] `webp-support`: 埋め込みWebPをDirectXTex / WIC経路で表示確認する
- [x] `multi-tile`: 複数タイルを同時表示する
- [x] `rtc-transform`: `CESIUM_RTC`と座標系・方位変換を適用する
- [x] `near-assets`: 大屋根リングとオルソ床を近景として置く（主要パビリオンの差し替えは残す）
- [x] `height-tune`: 建物Y `-9.010`、リングY `-1.550` で床に合わせた
- [x] `near-pavilions`: LOD2から対象建物を面ごと除き、LOD3の全`gml:name`を英単語名の近景GLBとして重ねる。`null2` の見た目は未解決
- [x] `near-pavilions-all-names`: LOD3葉タイル63枚から124種、タイル単位で約162件を切り出し、ゲームのマニフェストへ追加した
- [x] `near-pavilions-missing-structures`: 名前付き建物のUV無し面を保持し、ルクセンブルクの天井とオーストリアの木製モニュメントを復元した
- [x] `near-pavilions-far-lod2`: 未パンチLOD2原本からタイル4枚の遠景補完を生成し、LOD3とバッチ単位で排他表示する
- [x] `near-ring-texture`: CityGML `frn` の公式appearance画像とUVを大屋根リングへ結合し、複数マテリアルの埋め込みテクスチャGLBを生成した
- [x] `hdr-sunlight`: `pizzo_pernice_puresky_4k.hdr` を輝度しきい値・4連結セグメンテーションで前処理し、抽出した太陽を平行光へ接続。`basic_skybox_3d.fbx` へ表示用HDRテクスチャを適用した
- [ ] `near-pavilions-null2-research`: `null2` が全面グレーな原因（シグネチャー階層、別バッチ／別タイル、共有アトラスの読込）をリサーチ中。実装は止めている
- [x] `player-controller`: 空飛ぶタクシー（`flytaxi.glb`）のホバー移動・視点。`Input_GetMoveVector` の前方向だけをカメラヨー基準で使用し、マウス左右で機体旋回とロール傾斜、Wの前進加減速、Space/Shiftの上下加減速とベクトル合成に対応。A/Dの横移動とSの後退は使わない。三人称は `playercamera.cpp`
- [x] `collision-mesh`: 床・リング・LOD2建物・東西ゲート本体の CPU 三角形 AABB。リングは細い桟を除外し、ゲートは高精細形状を正としてゲートAABB内のLOD2重複判定を止め、全対象を XZ フラット格子で近傍判定
- [x] `game-async-load`: Title→Game の初期ロード。描画GLBの直接デコード並列、GPUはチャンク転送、衝突はbinワーカー。会場全体のストリーミングとは別
- [x] `collision-bin`: 描画と衝突の分離（`asset/collision/*.bin`）。仕様は [collision.md](../document_framework/collision.md)
- [x] `model-frustum-culling`: `Sprite3D` でモデルサイズから境界球を作り、カメラの Near / Far と視錐台外のモデルを描画しない
- [x] `memory-budget-profiling`: モデル、メッシュ、テクスチャ、RAM／VRAM使用量とロードキューを計測する（HUDとDXGI予算照会を追加）
- [ ] `collision-simplify`: LOD2建物の衝突メッシュ間引きは未（リングの桟除外は実装済み）
- [x] `tile-culling`: 3D Tiles の`boundingVolume.region`による描画時のタイル単位カリングを追加（LOD2本体と遠景LOD2。ストリーミング判定とは分離）
- [x] `runtime-lod`: 距離ベースのLOD3選択と範囲外破棄を追加する（`geometricError`切り替えは未実装）
- [x] `tile-streaming-cache`: 非同期ロード、キャッシュ、GPUアップロード、破棄を追加する
- [x] `streaming-stall`: 新規建物ロード時の約5秒停止をなくす（直接GLBデコード、CPU/GPU待ち分離、GPU転送チャンク化、6ms予算、先読み、プレースホルダー）
- [x] `streaming-nearby-miss`: 破棄半径内のREADYをGPU化し、モデルXZ半径を距離に足して近くても始まらない欠落を防ぐ
- [x] `nvidia-d3d11-drawcall`: ハイブリッドGPUで NVIDIA だけ数fpsになる問題。原因は塗りではなく `DrawIndexed` 発行。マテリアル結合、Present後GPUポンプ、遠景LOD2の影パス除外、レース開始の `cube.fbx` 廃止。780M 互換は維持。計測はキャプションと `debug-frame-perf.log`
- [x] `glb-vertex-validation`: カタール／中国を含む万博GLBのAccessor境界、インデックス上限、有限値、参照頂点AABBを検証する
- [x] `glb-uv-coordinate-contract`: 直接デコードのUVをglTFの値のまま使用し、不要なV反転によるテクスチャずれを修正する
- [x] `flight-hover`: `flytaxi.glb` の静的表示、カメラヨー基準の前進、マウス追従旋回、旋回時のロール傾斜、Wの前進加減速、Space/Shiftのピッチ付き上下移動を実装する
- [ ] `world-tuning`: 遠景LODと近景アセットを組み合わせて会場全体を調整する
- [x] skydome
- [x] `pbr-sun-directional`: HDR輝度抽出による平行太陽と連動環境光。場のモデル・プレースホルダ・タクシーを `S_PBR` 化。スカイドームはHDRを表示用変換した `S_SKYBOX`。方位既定値は `-170.0°` で、HDR抽出方位との差分により見た目の太陽位置を維持する。DebugビルドのみImGui `Expo Sunlight`
- [x] `pbr-local-shadow`: 全対象モデルへ受影を適用し、床・LOD2・リング・空飛ぶタクシーを投影元にする。LOD3表示中も建物影はLOD2ベース。3段CSM（既定 `0–10m / 10–70m / 70–160m`、第1段は投影余白8m）。会場GLBは近傍XZセル、タクシーはメッシュ全体を使う
- [x] `ssao-crevice`: シーン色を中間RTへ描き、サンプル可能な深度から半解像度SSAOと深度依存ぼかしを生成して3D色へ合成。`Expo Sunlight` から強度・半径・バイアス・カーブを調整でき、UIはAO対象外
- [x] `pbr-maps-all-models`: セルビア館で先行していた glTF の metallic/roughness factor、packed ORM、法線、エミッシブのPBR経路を全GLBへ適用。マップ無しモデルは係数と既定値へフォールバック
- [ ] `pbr-ibl-fog-day-night`: IBL、昼夜サイクル、プレイヤーへの環境マッピングは未着手
- [x] `pbr-distance-height-fog`: PBR描画へ距離＋高度フォグを適用。`Expo Sunlight` から色、距離、高度、密度を調整可能
- [x] `billboard-course-race`: `SCENE_GAME` 内にフリー飛行・コース作成・レースを追加。`P`配置、`asset/course/*.yml`保存、`asset/texture/makulogo.png`のビルボード輪、カウントダウン、タイマー、通過判定、ゴールログに対応
- [x] `game-menu-input`: `SCENE_GAME` のコース操作をImGuiからゲーム内メニューへ移行。`Esc` / パッドSTARTで開閉し、ClickFont・矢印キー・決定入力でフリー飛行、レース、コース作成を操作。新規コース名は自動生成し、ReleaseビルドではPlayer/SunlightのDebug ImGuiを表示しない
- [x] `game-audio`: `gameaudio.cpp` で BGM / SE を再生。パスは [audio_needs.md](audio_needs.md)。`menu.mp3` / `warp.mp3` / `race_abort.mp3` / `land.mp3` / `spawn.mp3` は未配置
- [x] `separate-expo-assets`: 万博モデルを`asset/expomodel`へ分離し、規約同意付き`tool/download_expo_assets.bat`でローカル生成する
- [x] 大屋根リング外側の日本館や企業館のモデルがしょぼい問題の修正（外周8棟のLOD3統合、重心基準ストリーミング、NTTランドマーク半径。DrawIndexed増加を抑制）
- [ ] GLB直接読み込み失敗の謎に迫る
- [ ] タイトル、リザルトをまともに
- [ ] アプリアイコン差し替え（手動）
- [x] 諸々整備してgithubへ上げる（手動）

---

## 関連資料

| 資料 | 内容 |
| :--- | :--- |
| [game_specification.md](game_specification.md) | できること、操作、触るファイル、実装済み仕様 |
| [audio_needs.md](audio_needs.md) | BGM / SE のパス、元ファイル名、発火地点 |
| [rendering_and_lighting.md](rendering_and_lighting.md) | レンダリング、太陽光、PBR、シャドウマップ、スカイドーム仕様 |
| [plateau.md](plateau.md) | PLATEAU 出典、変換、実行時配置 |
| [collision.md](../document_framework/collision.md) | 当たり判定と衝突バイナリ |
| [framework_usage.md](../document_framework/framework_usage.md) | フレームワーク API、起動、ループ |
| [input.md](../document_framework/input.md) | 入力アクション |
| [copylight.md](../document_framework/copylight.md) | 素材の権利表記 |
