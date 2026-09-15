# 当たり判定

`SCENE_GAME` のプレイヤーホバー移動で使う CPU 三角形 AABB の仕様と、衝突バイナリの作り方。
実装は [`SCENE_GAME/collision.cpp`](../SCENE_GAME/collision.cpp) / [`SCENE_GAME/collision.h`](../SCENE_GAME/collision.h)。
プレイヤー側の呼び出しは [`SCENE_GAME/player.cpp`](../SCENE_GAME/player.cpp)。

基準日: 2026年9月13日

---

## 1. いまできること

- 当たる対象は床、大屋根リング、LOD2建物、東西ゲート本体。東西ゲートだけは `expo_pavilion_east_gate.glb` / `expo_pavilion_west_gate.glb` の高精細形状を使い、そのワールドAABB内では重複するLOD2衝突を無効にする。それ以外のLOD3パビリオンは衝突対象にせずLOD2形状へ当てる。
- 実行時は `asset/collision/*.bin` を優先して fread し、bin が無いときだけ対応する描画GLBを Assimp で読み込む。どちらもワーカーでワールド変換と XZ 格子を一括構築する。
- 実動作の近傍判定は格子参照だけで、計測ではおよそ 300us。
- リングの確定 Y オフセット `-1.550` は、ロード完了時の `Collision_SetWorld` が `yBias` だけずらす。実行時の高さスライダーは無い。
- bin が無いときだけ描画GLBへフォールバックする（Assimp は三角形化・左手化・GlobalScale のみ。頂点結合はしない）。

LOD2建物の間引きによる簡略メッシュは未導入。リングだけ細い桟を除外する。bin の三角形数は元GLBと同じ（リングは約 90.6 万）。

ゲームのできることと操作は [game_specification.md](../document_game/game_specification.md)。
次の作業は [task_list.md](../document_game/task_list.md)。
仕様の詳細は [game_specification.md](../document_game/game_specification.md)。
会場モデルは [plateau.md](../document_game/plateau.md)。

---

## 2. 実行時の流れ

```text
フェード明け
  ├─ Collision_StartAdd(floor)  沈み込み済みのワールド行列
  ├─ Collision_StartAdd(ring)   RTC 位置のワールド行列
  ├─ Collision_StartAdd(LOD2)   建物Yオフセット込み
  ├─ Collision_StartAdd(LOD2 far) パビリオン穴の遠景補完
  └─ Collision_StartAdd(東西ゲート) 高精細形状
  └─ 描画GLBは別ワーカーで直接デコード
        │
        ▼
ワーカー（1本、キュー順）
  bin fread（無ければ Assimp）→ ローカル三角形化 → ワールド変換
  → リングだけ格子桟を捨てる → XZ フラット格子
        │
        ▼
Collision_Pump が完成メッシュを g_Meshes へ移す
        │
        ▼
全描画と衝突が終わったら ApplyFixedYOffsets
  リング Y は Collision_SetWorld → yBias
        │
        ▼
Player_Update が出現したあと Collision_MoveAABB
  高精細ゲートのAABB内ではLOD2をスキップ
```

パス規約: `asset\model\X.glb` または `asset\expomodel\X.glb` →
`asset\collision\X.bin`。
現行ファイルは床、リング、LOD2タイル、LOD2遠景タイル、東西ゲート高精細メッシュ。

| 描画 | 衝突 |
| :--- | :--- |
| `asset/expomodel/expo_floor.glb` | `asset/collision/expo_floor.bin` |
| `asset/expomodel/expo_ring.glb` | `asset/collision/expo_ring.bin` |
| `asset/expomodel/expo_tile_lod2_dataN.glb` | `asset/collision/expo_tile_lod2_dataN.bin` |
| `asset/expomodel/expo_tile_lod2_far_dataN.glb` | `asset/collision/expo_tile_lod2_far_dataN.bin` |
| `asset/expomodel/expo_pavilion_east_gate.glb` | `asset/collision/expo_pavilion_east_gate.bin` |
| `asset/expomodel/expo_pavilion_west_gate.glb` | `asset/collision/expo_pavilion_west_gate.bin` |

床はロード時に `EXPO_FLOOR_SINK`（0.08）を位置へ含めて焼く。
リングは RTC 位置で焼き、確定オフセット `-1.550` は完了後の `yBias`。

---

## 3. API

```cpp
bool Collision_StartAdd(
    const char* glbPath, const XMMATRIX& world, bool filterLattice);
CollisionPumpResult Collision_Pump(int* outMeshId);
void Collision_GetPumpProgress(size_t* done, size_t* total, int* stage);
void Collision_GetSourceStatus(char* out, size_t outSize);
void Collision_SetWorld(int meshId, const XMMATRIX& world);
void Collision_Clear(void);
bool Collision_GetBounds(int meshId, XMFLOAT3* bmin, XMFLOAT3* bmax);
bool Collision_MoveAABB(
    XMFLOAT3 center, XMFLOAT3 halfExtents, XMFLOAT3 delta,
    XMFLOAT3* outCenter, bool* grounded);
```

`Collision_StartAdd` は複数回呼べる。第3引数 `filterLattice` はリングだけ `true` にし、ワーカーはキューを順に処理する。
`Collision_Pump` は完成したメッシュを1つ取り出す。戻り値は `IDLE` / `BUSY` / `DONE` / `FAILED`。
進捗の `stage` は `LOAD` / `TRANSFORM` / `GRID`。

`Collision_SetWorld` は線形部分と XZ が同じなら Y 差分だけ `yBias` に足す。
それ以外はメインスレッドで再ベイクする。固定リングオフセットはこの Y 専用経路を一度使う。

`Collision_MoveAABB` は高精細ゲートのワールドAABBとプレイヤーAABBが重なる間、
通常LOD2および遠景LOD2の三角形を判定対象から外す。これにより、ゲートの通過口で
LOD2の同一形状が高精細ゲートへ重なって通行を阻害することを防ぐ。ゲートAABB外では
LOD2の衝突を通常どおり維持する。

HUD の `衝突 AABB 床:BIN リング:BIN` 表示で、各メッシュが bin と GLB のどちらから読み込まれたかを確認できる。

`Collision_Clear` はワーカーを合流してから全メッシュを捨てる。シーン終了で呼ぶ。

---

## 4. 移動と接地

プレイヤーは AABB（`asset/model/flytaxi.glb`、表示長辺約 0.8。半サイズは表示サイズの半分）。

`Collision_MoveAABB` は水平（X/Z）と垂直（Y）を分け、各軸を半 AABB ごとに最大 32 分割して三角形へ押し出す。
ホバー中の上下移動でも同じ分割を使い、薄い床やリングを突き抜けないようにする。

押し出しは三角形上の最近点と AABB の重なりから、いちばん短い軸へ戻す。
Y 正方向へ戻したときだけ接地として扱う。プレイヤー側では重力による落下や接地判定を使うジャンプは行わず、`Space` / `Shift` の上下速度を衝突移動へ渡す。

前進速度と上下速度は独立した成分として合成する。前進中の上下移動は斜めのベクトルになり、前進していない場合は垂直移動だけになる。上下速度はプレイヤー側で目標速度へ徐々に近づける。

ワールド 1m = 0.2。セル辺の既定は `0.4`（約 2m）。

---

## 5. リングの格子除外

リングだけ、三角形数に関係なくワールド変換のあと `KeepRingCollisionTriangle` で残す。LOD2建物・床にはこのフィルタを適用しない。

| 条件 | 残す面 |
| :--- | :--- |
| 上向き（法線 Y > 0.35）かつ面積 >= 0.004 | スロープ・床 |
| 面積 >= 0.02 | 大きな壁 |
| それ以外 | 捨てる（細い桟） |

描画GLBは変えない。移動面が消える／桟が残るときはこの閾値だけを触る。

---

## 6. XZ フラット格子

64 三角形未満は全走査。それ以上は XZ の密配列（CSR）にする。

- セル辺は 0.4 から始め、総セルが 200 万を超えたら辺を倍にしてやり直す。
- リングは 64 セル、LOD2建物・床は 256 セルを超えると `largeTris` へ逃がし、毎フレーム見る。
- セル内は `cellStart` / `cellItems`。ハッシュマップは使わない。
- 参照時はプレイヤー AABB が重なるセルと `largeTris` だけを見る。同一三角形は `visitStamp` で一度だけ。

Y 平行移動は格子を作り直さない。

---

## 7. 衝突バイナリ（EXCL）

オフラインの座標は glTF のまま（メートル、右手 Y-up）。実行時に Assimp と同じローカル空間へ揃える。
描画の直接デコードも同じ変換（100 倍、Z 反転、巻き順入れ替え）を頂点へ焼く。UV だけは glTF のままとする。

- スケール 100 倍
- Z 反転
- Z 反転に伴う巻き順反転（b と c を入れ替え）

巻き順を戻さないとリング除外の `ny > 0.35` が反転する。

リトルエンディアン。

| オフセット | 型 | 内容 |
| :--- | :--- | :--- |
| 0 | `char[4]` | `"EXCL"` |
| 4 | `uint32` | `version` = 1 |
| 8 | `uint32` | 頂点数 |
| 12 | `uint32` | 三角形数 |
| 16 | `uint32` | `flags` = 0 |
| 20 | `float3 × 頂点数` | POSITION |
| 続く | `uint32 × 三角形数 × 3` | インデックス |

現行サイズの目安: リング約 28MB、床 92 バイト。LOD2はタイルごとに生成される。

---

## 8. バイナリの作り方

ツールは [`tool/prepare_collision.py`](../tool/prepare_collision.py)（標準ライブラリのみ）。
GLB の JSON / BIN を直接読み、ノード行列を累積して `mode == 4` のプリミティブから POSITION と indices を出す。

既定（リング、床、存在するLOD2タイル／遠景タイル、東西ゲート）:

```powershell
python tool/prepare_collision.py
```

任意 GLB:

```powershell
python tool/prepare_collision.py asset/model/foo.glb
```

出力先は既定で `asset/collision/<stem>.bin`。`--output-dir` で変えられる。

`prepare_expo_ring.py` と `prepare_expo_floor.py` の末尾からも同じ関数を呼ぶ。`prepare_expo_pavilion.py` はパンチ済みLOD2と遠景LOD2を生成したあと、それぞれのbinも生成する。
描画GLBを作り直したら bin も更新される。bin が無くてもゲームはGLBフォールバックで動く。

リング／床の描画GLBには `NORMAL` を焼いてある。衝突 bin には法線を入れない。
リングのテクスチャは衝突と無関係で、描画GLBを更新したときだけ同じGLBからbinを再生成する。

---

## 9. 入口

| ファイル | 役割 |
| :--- | :--- |
| [`SCENE_GAME/collision.h`](../SCENE_GAME/collision.h) | API |
| [`SCENE_GAME/collision.cpp`](../SCENE_GAME/collision.cpp) | bin/GLB読込、ベイク、格子、AABB |
| [`SCENE_GAME/player.cpp`](../SCENE_GAME/player.cpp) | ホバー移動、衝突押し出し、ImGui `スロープへ` |
| [`SCENE_GAME/field.cpp`](../SCENE_GAME/field.cpp) | ロード時の `StartAdd`、固定Yオフセット |
| [`tool/prepare_collision.py`](../tool/prepare_collision.py) | GLB → EXCL |

---

## 10. まだやらないこと

- 衝突メッシュの間引き
- 物理エンジン
- 会場ストリーミング用の動的コリジョン
