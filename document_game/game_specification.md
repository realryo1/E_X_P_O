# expogame ゲーム仕様書

## 1. この資料の目的

この資料は、`expogame`で制作する大阪・関西万博を題材にした
非公式ゲームのゲームデザインと、シーン・操作の実装仕様を整理する。

会場モデルの出典、変換、座標系は
[plateau.md](plateau.md)を参照する。
いま何ができてどう操作するか、どのファイルを触るかは
この資料に書く。次に何をするかは
[task_list.md](task_list.md)を参照する。
描画・ライティング・シャドウの詳細は
[rendering_and_lighting.md](rendering_and_lighting.md)。
BGM / SE のパスと発火地点は
[audio_needs.md](audio_needs.md)。
フレームワークの使い方は
[framework_usage.md](../document_framework/framework_usage.md)。
当たり判定と衝突バイナリは
[collision.md](../document_framework/collision.md)を参照する。

## 2. 作品の方向性

- 自作のC++ / DirectX 11フレームワーク上で動作する3Dゲームとする。
- Project PLATEAUの万博会場モデルをゲーム世界の基盤として利用する。
- 最終的には、プレイヤーが会場を飛行し、大屋根リングやパビリオン周辺を移動する
  オープンワールド型の体験を目指す。
- 空飛ぶタクシーによる会場内の飛行体験を中心要素とする。
- 万博公式作品ではなく、データを独自に加工した非公式作品として扱う。

## 3. 実装済みの最小構成

進捗と次の作業は [task_list.md](task_list.md)。当たり判定は [collision.md](../document_framework/collision.md)。会場モデルは [plateau.md](plateau.md)。描画詳細は [rendering_and_lighting.md](rendering_and_lighting.md)。

起動時は `SCENE_GAME`（`SCENE_TITLE` から決定入力で遷移した場合も同様）。オルソ床、パンチ済みLOD2葉タイル、未パンチLOD2遠景補完、大屋根リング、LOD3の全名前付き近景パビリオンを重ね、空飛ぶタクシーのホバー移動と床・リング・LOD2建物衝突まで利用できる。未パンチLOD2遠景はLOD3がGPU化されるまで表示し、対応するLOD3が常駐した棟のバッチ単位で排他表示するが、衝突メッシュは常時保持する。LOD3パビリオンは原則描画専用で、建物の衝突形状はLOD2を使う。ただし東西ゲート本体は高精細モデルを使い、ゲートのワールドAABB内では重複するLOD2衝突を無効にする。プレイヤーは `asset/model/flytaxi.glb` を表示長辺約0.8に調整する。スカイドーム以外のモデルは共通の `S_PBR` で描画し、glTFのPBRマテリアルを適用する。スカイドームはワールド方向からHDRの正距円筒UVを計算する `S_SKYBOX`。床、パンチ済みLOD2、リング、タクシーがShadowMapへ投影し、LOD3パビリオン・プレースホルダーを含む全対象モデルが影を受ける。遠景LOD2は影パスから除外する。LOD3表示中も建物の影はパンチ済みLOD2を投影元にする。3D描画後には半解像度SSAOをシーン色へ合成し、柱の隙間や入れ組みの淵を補助的に暗くする。Player用ImGui「スロープへ」でリング上へワープできる。描画GLBはAccessor直接デコードを並列実行し、衝突は `asset/collision/*.bin` を優先し、無い場合はGLBフォールバックをワーカーでベイクする。`null2` は全面グレーのままで、見た目の実装は止めている。

### できること

- 起動時またはTitleからGameへ遷移後、フェード後に床・LOD2・遠景LOD2・リングを読み、完了後もLOD3近景を逐次ストリーミングする。
- 空飛ぶタクシー `asset/model/flytaxi.glb`。ロード完了後に `Player_Update` が出現させる。
- カメラヨー基準のW / 左スティック上による前進、マウス左右にゆっくり追従する機体旋回、旋回中のロール傾斜、Space / Shiftによる徐々に変化する上下移動。上昇・下降の速度ベクトルは前進速度と合成する（前進中は斜め、停止中は垂直移動）。三人称視点は `playercamera.cpp`（Far 2000）。
- 当たり判定は床・リング・LOD2建物・東西ゲート本体。東西ゲートだけは高精細モデルを使い、そのワールドAABB内ではパンチ済みLOD2／遠景LOD2の重複判定を止める。それ以外のLOD3パビリオンは描画専用で、衝突はパンチ済みLOD2と遠景LOD2の形状へ行う。
- Courseメニュー：`Esc` / パッドSTARTで開くゲーム内メニューから、レース開始・コース編集・追加を選び、一番下の「戻る」で閉じる。レース開始は `コース1`、`コース2`…、空行、`戻る` の一覧。コース編集・追加は `コース追加`、既存コース、空行、`戻る`。マウスクリックまたは上下キーで選ぶ。左右キーでのコース切替は使わない。飛行に使う`W` / `Space` / 左スティックはメニュー決定・カーソル移動に使わない。メニューを開いた直後、およびページ遷移直後は押しっぱなしのキー・スティック・マウスボタンが離れるまで入力を無視する。サブページの`Esc` / 「戻る」はルートへ戻る。コース作成中は`P`で現在位置を追加し、`U`で最後の座標を取り消す。保存時の名前は`course_YYYYMMDD_HHMMSS`形式で自動生成する。1つ目の座標と2つ目以降の輪はいずれも `asset/texture/makulogo.png` のビルボード（スタートだけ色と大きさを変える）。`cube.fbx` は LOD3 プレースホルダー専用。
- レースモード：コースの1つ目の座標へ移動し、3秒のカウントダウン後に計測を開始する。2つ目以降の輪を座標順に通過し、左上へ`00:00:00`（分:秒:センチ秒）形式で表示する。ゴール時はタイムスタンプとタイムを`asset/course/*.yml`の`logs`へ追記し、リザルトにタイム昇順のTOP5を表示する。今回の記録には`★`を付け、過去ベストより速ければ`更新！`を出す。ゴール中は`Esc` / パッドSTARTでメニューを出さない。
- プレイヤーの通常移動速度は`0.12`。レース中にゴール以外の輪を通過すると、通常の前進速度へ`0.30`の速度を0.4秒間加算する。時間終了後も通常の減速幅で加算成分が滑らかに減衰し、急停止しない。次の輪を通過すると持続時間を更新する。
- Debugビルド用ImGui（`Expo Player` / `Expo Sunlight` / `Expo Debug Camera`）：速度・ワープ操作、太陽・シャドウ・SSAO調整、およびフリーカメラ切替を行う。Releaseビルドでは表示しない。スカイドーム以外のモデルは `S_PBR` とglTF PBRマテリアルを使用し、全対象モデルが影を受ける。影の投影元は床、パンチ済みLOD2、リング、タクシーで、LOD3表示時も建物影はLOD2ベースとする。遠景LOD2は影パスに載せない。スカイドームは `S_SKYBOX`。起動既定は方位 `-170.0°`、仰角・色はHDR抽出値、強度 `2.0`、環境光倍率 `0.8`、ヨーオフセット `0`、粗さ `0.81`、金属度 `0`、シャドウ範囲 `0–20m / 20–70m / 70–160m`、バイアス `0.0005`、影の明るさ `0.25`、SSAOはON・強度 `0.85`・半径 `1.25m`・バイアス `0.04m`・Power `1.20`。
- 建物の固定Y `-9.010`、リングの固定Y `-1.550`、床の沈み込み `-0.080`。カメラ Far は 2000。
- ポインタ操作：`Esc` / パッドSTARTでゲーム内メニューを開き、メニュー中はポインタを表示する。メニューを閉じるとポインタを再ロックする。メニュー中は機体操作を停止する。
- 音声：`gameaudio.cpp` が散策／レース／ゴール／メニューの BGM と、メニュー・レース・ホバー・衝突の SE を再生する。散策とコース作成は `explore.mp3` を共有する。決定は `InputManager` の `kettei.mp3` のみ。パスと元ファイル名は [audio_needs.md](audio_needs.md)。

### 操作

| 場面 | 操作 |
| :--- | :--- |
| 共通 | `F2` スクリーンショット、`F11` ボーダレス切替。Debug の Game では `F5` で局所影パス切替 |
| Title | `Space` / `Enter` / パッドA で Game。Debugビルドは右上 `DEBUG` で検証シーン |
| Game | W / 左スティック上で前進、Spaceで上昇、Shiftで下降、マウス左右で機体旋回、右スティックで視点。`Esc` / パッドSTARTでメニューを開閉し、矢印キー / 十字キーとEnter / パッドAでメニューを操作する。ゴール中はメニューを開かない。メニュー中は機体操作を停止する。上下移動は前進中なら斜め、停止中なら垂直になる。A/Dの横移動とSの後退は行わない。コース作成中は`P`で輪を配置し、`U`で最後の輪を取り消す |
| Debug | `Tab` で MODEL → LIGHTING。`Esc` でマウスロック解除 |

### 触るファイル

`SCENE_GAME` の入口は [`game.cpp`](../SCENE_GAME/game.cpp) で、Initialize / Update / Draw / Finalize は各モジュールの呼び出しと、マウスロック切替である。

| ファイル | 役割 |
| :--- | :--- |
| [`SCENE_GAME/game.cpp`](../SCENE_GAME/game.cpp) | シーン入口。Field / Player / PlayerCamera / Sunlight / Ui / GameAudio の呼び出し。`Game_PumpAfterPresent`。ゲーム内メニュー開閉中のマウスロックを制御 |
| [`SCENE_GAME/field.cpp`](../SCENE_GAME/field.cpp) | マニフェスト、ENU、`ExpoDrawJob`、フェード中スキップ付き `Field_PumpLoad`、初期完了後は `Field_PumpAfterPresent`、マテリアル結合、固定Yオフセット、スカイドームヨー |
| [`SCENE_GAME/course.cpp`](../SCENE_GAME/course.cpp) | ゲーム内メニュー、コース一覧、YAML入出力、作成・レースモード、ビルボード輪の再利用、タイマー、ゴールログ、BGM 切替とメニュー／レース SE |
| [`SCENE_GAME/gameaudio.cpp`](../SCENE_GAME/gameaudio.cpp) | BGM 切替と SE 再生。`LoadMP3` / `PlaySound`。欠損ファイルは無音 |
| [`SCENE_GAME/player.cpp`](../SCENE_GAME/player.cpp) | 空飛ぶタクシーの出現、ホバー移動、描画、ホバー／ヒット／着地 SE、`Player_DrawDebug`（速度・ワープ・`スロープへ`） |
| [`SCENE_GAME/playercamera.cpp`](../SCENE_GAME/playercamera.cpp) | 三人称、Far 2000、`Camera_Initialize`、`SetCameraPosition`、Debugビルド用 `PlayerCamera_DrawDebug`（窓名 `Expo Debug Camera`） |
| [`SCENE_GAME/sunlight.cpp`](../SCENE_GAME/sunlight.cpp) | 平行太陽、連動環境光、スカイドームヨー連動、`Sunlight_DrawDebug`（窓名 `Expo Sunlight`） |
| [`SCENE_GAME/ui.cpp`](../SCENE_GAME/ui.cpp) | DrawFont HUD。タイトル、操作案内、ロード状況、メモリ |
| [`SCENE_GAME/collision.cpp`](../SCENE_GAME/collision.cpp) | bin 読込、ワーカーベイク、格子、AABB |
| [`tool/prepare_collision.py`](../tool/prepare_collision.py) | GLB → 衝突 bin |

万博アセットの作り直しは [plateau.md](plateau.md)。

### 実装の要点

#### 1. シーン実行制御と初期化フロー
- 起動シーン: `SCENE_GAME`（`app/scene.cpp`）。`SCENE_TITLE`はプレースホルダーとして個別に利用可能。
- 決定入力: `SCENE_TITLE`から`SCENE_GAME`へ遷移。`SCENE_GAME`からはシーン遷移しない。
- 表示順序: 床 → パンチ済みLOD2 → 未パンチLOD2遠景 → 近景パビリオン → 大屋根リング → 空飛ぶタクシーの順で描画。
- 初期ロード: `Game_Initialize` は `PlayerCamera_Initialize`、`Field_Initialize`（マニフェスト）、`Course_Initialize`、`Sunlight_Initialize`、`Ui_Initialize` を呼ぶ。フェードが不透明なあいだに床、パンチ済みLOD2、未パンチLOD2遠景、リング、衝突メッシュの初期ロードを進めて完了させる。進捗はフェード前面のプログレスバーに表示し、完了後に明転する。明転後もLOD3近景はカメラ周辺、視線先、移動先を読み込む。
- 衝突判定の初期化: `asset/collision/*.bin` を fread し、ワーカーで一括ベイクする。初期処理が終わってから建物の固定Yオフセット（`-9.010`）、リングの固定Yオフセット（`-1.550`）を適用する。実行時の高さスライダーは設けない。
- 継続的ロード: 初期完了後も `Field_PumpLoad` を論理更新あたり最大1回呼び、CPUインポート開始だけを行う。GPU化とパビリオン破棄は `Present` 後の `Field_PumpAfterPresent`（最大3ms、直前フレームが重いときはスキップ）へ送る。進捗は HUD へ `LOADING IMPORT ... PAV n/N ...` と表示。

#### 2. 非同期ストリーミングとメモリ管理（描画復旧後の最適化）
- 全LOD3常駐の解除: 全LOD3常駐は解除し、RAM／VRAM使用量を抑制。DXGI予算照会とHUDへのRAM/VRAMおよびキュー状況表示（`LOADING IMPORT ... IN n READY n GPU ...`）を行う。
- Accessor直接デコード: 万博GLB（GLB 2.0）はワーカーでAccessor/BufferViewを直接展開し、埋め込みテクスチャ（WebP等）のCPUデコードも最大2ワーカーで並列処理する。
- 6msタイムスライス予算: 初期ロード中の GPU化（SRV / VB / IB転送）およびリソース破棄はメインスレッドで1フレーム6ms（フェード中は最大40ms）の予算内に進める。初期完了後の GPU 転送は Present 後 3ms。頂点・インデックス・テクスチャの大きな転送はチャンク化する。
- マテリアル結合: ワーカーが `MergePreparedMeshesByMaterial` で同一マテリアルのプリミティブを結合し、`expo_batch_id` は描画時の範囲として残す。遠景LOD2の分割プリミティブによる数百回の `DrawIndexed` を、NVIDIA dGPU の発行コスト向けに削減する。詳細は [rendering_and_lighting.md](rendering_and_lighting.md) の 5.6。
- 停止スパイク対策（先読みとプレースホルダー）: カメラの視線方向と移動方向へ先読みしてジョブを追加し、ロード中およびGPU化中はサイズ調整済みの `cube.fbx` プレースホルダーを表示することで、新規建物ロード時の約5秒停止を解消。視線方向の棟は開始順も優先する。
- 近接時の欠落防止: 破棄半径内のREADY状態モデルを優先してGPU化し新規インポート枠を空ける。カメラ距離判定（ロード半径 48、破棄半径 72）にはモデルXZ半径を加算する。
- 外周ランドマークの近景補完: 日本館、パナソニック、住友館、三菱未来館などリング外側の8棟は `expo_pavilion_west_outer.glb` へ統合し、GLB内の重心を基準にLOD3を先読みする。NTT Pavilionもランドマーク半径を適用する。LOD2遠景の対応バッチは維持し、影パスへは追加しない。
- 世代番号による無効化: カメラ範囲外へ移動した未完了ジョブは世代番号で無効化し、遅延完了したモデルを採用しない。再び範囲内へ戻ったジョブと、ファイル無し以外の失敗は再接近でやり直す。
- UV座標および頂点規約: 直接デコードのUVはglTFの値のまま使用し、不要なV反転を行わない（テクスチャずれを防止）。頂点スケール `*100` とZ反転は衝突binと一致させる。
- 回帰確認の代表モデル: `asset/expomodel/expo_pavilion_qatar.glb`（stride付き属性）と `asset/expomodel/expo_pavilion_china.glb`（32bitインデックス）を共通の直接デコード経路で処理する。

#### 3. 階層型カリングと遠景補完
- モデル単位の視錐台カリング: `Sprite3D` でモデルサイズから保守的な境界球を作り、カメラの Near / Far および視錐台外のモデルを描画省略する。空を見たりカメラを背けたりすると描画負荷が軽減される（リソース解放は行わない）。フレームワーク側のリソース寿命とカリング注意事項は [framework_usage.md](../document_framework/framework_usage.md) を参照。
- タイル単位の視錐台カリング: 3D Tiles の `boundingVolume.region` から算出した保守的境界球により、LOD2本体および遠景LOD2をタイル単位で視錐台カリングする。描画専用であり、LOD3のロード／破棄判定（ストリーミング判定）とは完全に分離されている。
- 未パンチLOD2による遠景補完: パンチ済みLOD2の穴を埋めるため、未パンチLOD2原本から生成したタイル4枚を常駐させる。LOD3がGPU化完了するまで遠景を表示し、対応するLOD3が常駐した棟のバッチだけを排他非表示にするため、GPU化中も穴を残さない。埋め込みWebPの複製を抑えるため建物単位へ細分化せず4枚以内で保持する。実行時はマテリアル結合とバッチ範囲描画で発行回数を抑える。影パスには載せない。

#### 4. PBRレンダリング・ライティング・シャドウ・スカイドーム
- PBRシェーディング経路: スカイドームを除く床・LOD2・遠景LOD2・パビリオン・大屋根リング・プレースホルダー・空飛ぶタクシーは共通の `S_PBR` シェーダーを使用。glTFのMetallic-Roughness factor、packed ORM、法線、エミッシブを共通経路で処理し、テクスチャを持たないモデルは係数および既定値へフォールバックする。ピークが1を超えた画素のみトーンマッピング畳み込みを行う。詳細は [rendering_and_lighting.md](rendering_and_lighting.md)。
- スカイドーム: `asset/model/basic_skybox_3d.fbx` に `asset/texture/pizzo_pernice_puresky_4k.hdr` をReinhard変換して貼った `S_SKYBOX`。ワールド方向からHDRの正距円筒UVを計算する。
- HDR太陽光抽出: 起動時にHDRの線形輝度からしきい値 \(0.5 \times L_{\max}\) と4連結セグメンテーション（U方向の円周ラップ接続）により太陽領域を抽出し、仰角と色を決定する。
- 太陽方位とスカイ同期: 方位の既定値は `-170.0°`。HDR抽出方位との差分によってスカイドームのYaw回転を同期させ、背景の見た目太陽位置と平行光を一致させる（仰角ではドームを傾けない）。
- 近傍シャドウマップ（CSM）: カメラ視錐台を3段（既定 `0–20m / 20–70m / 70–160m`、第1段は投影余白8m）に分けた直交ShadowMap。全対象モデルが影を受け（Receive）、投影元（Cast）は床、パンチ済みLOD2、大屋根リング、空飛ぶタクシー。遠景LOD2は影パスから除外する。LOD3表示中もパンチ済みLOD2を投影元にする。会場GLBは近傍XZセル単位でカリングし、タクシーはメッシュ全体を投影。既定の影描画距離は160m。
- リアルタイム調整（`Expo Sunlight`）: 強度 `2.0`、環境光倍率 `0.8`、スカイドームヨーオフセット `0`、粗さ `0.81`、金属度 `0`、シャドウ範囲 `0–20m / 20–70m / 70–160m`、バイアス `0.0005`、影の明るさ `0.25`、SSAOはON・強度 `0.85`・半径 `1.25m`・バイアス `0.04m`・Power `1.20`。

#### 5. 衝突判定・機体物理・カメラ・操作
- 衝突判定: 床、大屋根リング、パンチ済みLOD2、遠景LOD2、東西ゲート本体を対象にする。東ゲート・西ゲート本体だけは高精細LOD3モデルを衝突形状に使い、プレイヤーAABBがゲートのワールドAABB内にある間はLOD2側をスキップする。それ以外のLOD3近景パビリオンは描画専用でLOD2形状へ当てる。`Collision_MoveAABB` で床・リング・建物への侵入を防ぐ。XZフラット格子で近傍三角形だけを判定し、リングの細い桟だけをロード時に除外する。binが無い場合はGLBをワーカーで読み込む。詳細は [collision.md](../document_framework/collision.md)。
- 機体姿勢とホバー物理: `flytaxi.glb`（長辺約0.8スケール）。W / 左スティック上で前進加減速、Space / Shiftで上下加減速と前進速度ベクトル合成（前進中は斜め、停止中は垂直移動）。マウス左右で旋回し、機体はロール方向へゆっくり傾斜、正面へ戻ると水平復帰する。A/D横移動およびS後退は不使用。
- カメラ制御: `playercamera.cpp` による三人称視点（Far 2000）。毎フレーム `RequestRedraw` を呼ぶ。
- マウスロック制御: `Esc` / パッドSTARTでゲーム内メニューを開く。ゴール中は開かない。メニュー中は `UnLockMouse` と操作停止を行い、閉じると `LockMouse` する。メニュー中は視点回転を停止し、通常時は `game.cpp` が `Mouse_GetState` せず視点側が一度だけ読む。

## 4. シーン仕様

### `SCENE_TITLE`

- ゲームタイトルと開始案内を表示する。
- `Space`、`Enter`、ゲームパッドAの決定入力で`SCENE_GAME`へ遷移する。
- Debugビルドでは右上の`DEBUG`から`SCENE_DEBUG`へ遷移できる。

### `SCENE_GAME`

床、LOD2建物、未パンチLOD2遠景、近景パビリオン、大屋根リングの接続と、空飛ぶタクシーのホバー移動確認およびレースモードを目的とする。オービットカメラは使わない。
3D Tiles の `boundingVolume.region` によるタイル単位カリングを描画時に行う。LOD3はカメラ距離でロード・破棄し、各 `Sprite3D` の描画時にはモデル単位の視錐台カリングも働く。タイル境界カリングはストリーミング判定とは分離する。
`null2` の見た目修正はリサーチ復帰後。モデルと座標は [plateau.md](plateau.md)。

- `Game_Initialize`: `PlayerCamera_Initialize`、`Field_Initialize`、`GameAudio_Initialize`、`Course_Initialize`、`Sunlight_Initialize`、`Ui_Initialize` を呼ぶ。`GameAudio_Initialize` で散策 BGM を開始する。`PlayerCamera_Initialize` が `Camera_Initialize` と Far 2000 を設定する。
- `Game_Update`: `Field_PumpLoad`、マウスロック切替、`PlayerCamera_UpdateInput`、`Player_Update`、`Course_Update`、`PlayerCamera_Update`、`Ui_Update`、`Sunlight_Update` の順。初期完了後の `Field_PumpLoad` はインポート開始のみ。カメラ入力を先に更新し、プレイヤーが最新のヨーを使用する。メニュー開閉とコース操作は`Course_Update`で処理し、メニュー中は操作を停止する。ロック中は `game.cpp` が `Mouse_GetState` しない（相対移動量は視点側が一度だけ読む）。
- `Game_Draw`: `Direct3D_BeginScene` でシーン色RTへ切り替え、`PlayerCamera_Draw` のあと、深度ありで `Sunlight_Apply`、注視点周辺の局所 ShadowMap（床・パンチ済みLOD2・リング・タクシーを投影。遠景LOD2は省略）、場とプレイヤーの描画を行う。続いて `Direct3D_ApplySsao` で半解像度AOをバックバッファへ合成し、マテリアルを白へ戻してから `Ui_Draw`、HUD、ゲーム内メニュー、Debugビルド用の`Player_DrawDebug` / `Sunlight_DrawDebug` / `PlayerCamera_DrawDebug`を描画する。SSAOは3D色だけを対象とし、UIには適用しない。
- 初期ロード制御: フェードが不透明なあいだも `Field_PumpLoad` を実行し、コア初期ロード完了まで明転を保留する。描画が終わるまで同じフレームでは再ポンプしない。初期完了後の GPU ポンプは `Present` 後。
- プレイヤー出現: `asset/model/flytaxi.glb`。読み込み後に表示長辺約0.8へ一様スケールし、`Field_GetSpawnPos()` の高度でホバーを開始する。出現時にマウスロックする。
- レース処理: `Course_Draw` は1つ目の座標へスタート用ビルボード、2つ目以降へカメラ方向を向く両面ビルボードを描く。マーカーは `cube.fbx` を使わない（レース開始フレームで Assimp を走らせない）。リングオブジェクトは再利用し、通過済みと遠いゲートは描画しない。スタート付近にいるときはワープしない。レースではプレイヤーの前フレーム位置から現フレーム位置への線分が、次の輪の平面を半径内で通過した場合だけ次の輪へ進む。コースの点は1つ目をスタート位置、2つ目以降を配列順の輪として使用する。レースのカウントダウン中は`Player_SetControlEnabled(false)`で移動を停止する。ゴール時は `logs` からタイムを読み、TOP5ランキングと自己ベスト更新表示を HUD に出す。BGM はモードに合わせて `gameaudio.cpp` が切り替える。 SE の一覧は [audio_needs.md](audio_needs.md)。
- コースYAML入出力: `Course` のYAMLは`name`、`points`、`logs`で構成する。保存時に`asset/course`を作成し、新規コース名とファイル名を自動生成する。
- 画面再描画: `PlayerCamera_Update` が毎フレーム `RequestRedraw` する。通常シーンのPresent間引きと両立させるためである。

現行スコープ外の将来拡張:
- `flytaxi.glb` のup / up_to_drive / driveアニメーションと状態遷移
- 着地など、静的ホバー移動を超える機体状態

### `SCENE_RESULT`

- 結果または終了案内を表示する（現状はプレースホルダー）。
- 決定入力で`SCENE_TITLE`へ戻る。

### `SCENE_DEBUG`

- Debugビルド専用の検証シーン。
- モデルとライティングの確認に利用する。
- フリーカメラ実装自体は`debugcamera.cpp`にあり、`SCENE_DEBUG` が使う。
- `SCENE_DEBUG`はフレームワーク機能の検証用として扱う。

## 5. アセット

描画用の万博モデル、マニフェスト、変換手順は [plateau.md](plateau.md)。
プレイヤーは `asset/model/flytaxi.glb`。現段階では静的表示とホバー移動のみで、内蔵アニメーションは未接続である。上下移動時のピッチと旋回時のロールを実行時の機体姿勢として適用する。
衝突バイナリは `asset/collision/expo_floor.bin`、`expo_ring.bin`、LOD2タイルの `expo_tile_lod2_dataN.bin` / `expo_tile_lod2_far_dataN.bin`、東西ゲートの高精細bin。作り方は [collision.md](../document_framework/collision.md)。

将来の分類:

```text
asset/
├─ model/        # 描画用モデル
├─ texture/      # テクスチャ
├─ collision/    # 衝突バイナリ（EXCL）。作り方は collision.md
└─ metadata/     # 建物属性
```

描画用モデルと衝突用メッシュは分離する。

## 6. ゲーム世界の優先順位

会場全体を最初から高品質化せず、次の順に整備する。

1. 大屋根リング
2. プレイヤーの開始地点周辺
3. 飛行ルート沿いの主要パビリオン
4. 飛行ルート沿いの建物
5. 遠景のその他建物

遠景は低LOD・簡易マテリアルとし、近景だけを高品質モデルへ置き換える。

## 7. 実装上の制約

- 会場全体の`boundingVolume.region`によるタイル単位カリングは描画時のみ実装済み。未パンチLOD2遠景はタイル4枚の常駐とLOD3とのバッチ単位排他、マテリアル結合による発行削減まで実装済み。カメラ距離によるLOD3ストリーミング、範囲外破棄、近くても始まらない欠落の修正、Present後GPUポンプ、RAM／VRAM表示も実装済み。詳細は [plateau.md](plateau.md) と [rendering_and_lighting.md](rendering_and_lighting.md)。
- 新規建物のCPUデコードはワーカーへ移す。初期ロード中のGPU／破棄は1フレーム6ms、完了後は Present 後3ms。カメラの視線方向と移動方向への先読みと、`cube.fbx` のプレースホルダー表示も実装済みである。直接デコードのUVはglTFのままとする。
- `null2` の全面グレー問題は原因リサーチ復帰まで修正を見送る。
- 公式データのZIPや元の`b3dm`、テクスチャは配布物へ含めない。
- 利用条件と出典表記は [plateau.md](plateau.md) に従う。

## 8. 次の実装順序

チェックリストおよび詳細なタスク項目は [task_list.md](task_list.md) を参照する。

1. `null2` の見た目問題の調査・修正（原因リサーチ復帰後）
2. 衝突メッシュの間引き（必要に応じて検討。詳細は [collision.md](../document_framework/collision.md)）
3. 機体アニメーション接続（`flytaxi.glb` の旋回・加速・プロペラ等）
4. 残りのPBR・環境拡張（IBL、昼夜サイクル、プレイヤー環境マッピング）。距離＋高度フォグは実装済み
5. タイトル画面・リザルト画面の本実装
6. 会場全体の調整・配布準備
