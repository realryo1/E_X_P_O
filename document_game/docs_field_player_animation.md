# フィールド・プレイヤーアニメーション ドキュメント

対象: `GM31_00_DirectX初期化` (DirectX11 + Assimp + DirectXTex)

- フィールド: [meshField.cpp](meshField.cpp) / [meshField.h](meshField.h)(現在使用中)、[field.cpp](field.cpp)(平面ポリゴン、現在未使用)
- プレイヤー: [player.cpp](player.cpp) / [player.h](player.h)
- アニメーション: [animationModel.cpp](animationModel.cpp) / [animationModel.h](animationModel.h)

> 注: ソースの文字コードは Shift-JIS(一部 UTF-8)。エディタで文字化けする場合は文字コードを切り替えること。

---

## 1. 使用リソース一覧

### 1.1 モデル(`asset/model/`)

| ファイル | 用途 | 読み込み箇所 |
|---|---|---|
| `Akai.fbx` | プレイヤーのメッシュ・ボーン・テクスチャ(埋め込み) | `Player::Init` → `AnimationModel::Load` |
| `Akai_Idle.fbx` | 待機アニメーション(`"Idle"`) | `AnimationModel::LoadAnimation` |
| `Akai_Run.fbx` | 走りアニメーション(`"Run"`) | 同上 |
| `Akai_Attack.fbx` | 攻撃アニメーション(`"Attack"`) | 同上 |

- アニメーション用 FBX は **ボーンアニメーションのデータ源としてのみ**使用(メッシュは使わない)。`aiScene` の `mAnimations[0]` を参照する。
- `player.obj` は旧実装(コメントアウト)。

### 1.2 テクスチャ(`asset/texture/`)

| ファイル | 用途 |
|---|---|
| `sura.jpg` | MeshField / Field の地面テクスチャ(DirectXTex `LoadFromWICFile` で読込) |
| (Akai.fbx 内蔵) | プレイヤーのテクスチャ。`aiScene->mTextures` から `LoadFromWICMemory` で読込み、`m_Texture[ファイル名]` に保持 |

### 1.3 シェーダー(`shader/`)

| 使用者 | VS | PS | 備考 |
|---|---|---|---|
| Player | `litVS.cso` | `litPS.cso` | ライティング付き |
| MeshField | `fieldVS.cso` | `fieldPS.cso` | .hlsl ソースはリポジトリに無い(.cso のみ) |
| Field(未使用) | `litVS.cso` | `litPS.cso` | |

共通定義は [shader/common.hlsl](shader/common.hlsl)(World/View/Projection/Material/Light/Camera の cbuffer、`VS_IN`/`PS_IN`)。

### 1.4 オーディオ

| ファイル | 用途 |
|---|---|
| `asset/audio/wan.wav` | ジャンプSE(`m_JumpSE`)。ロードのみで、`Play()` はコメントアウト中 |
| `asset/audio/bgm.wav` | MeshField 内の BGM 再生(コメントアウト中) |

### 1.5 ライブラリ

- **Assimp**(`assimp-vc143-mt.lib/.dll`): FBX 読み込み、ボーン・アニメーションデータ取得
- **DirectXTex**: テクスチャ読み込み / SRV 生成
- **DirectX11**: 頂点・インデックスバッファ、シェーダー描画

---

## 2. 全体の流れ(ゲームループ)

```
Manager::Update()           // 毎フレーム(dt = 1/60 固定)
 ├ Scene::Update()          // Game::Update: 敵が0体なら Result へ遷移
 └ 全 GameObject::Update()  // Camera / MeshField / Player / Enemy ...
Manager::Draw()
 ├ Renderer::Begin()
 ├ 全オブジェクトのカメラZ計算 → Z降順ソート(奥→手前)
 ├ Layer 0 → 1 → 2 → ... の順に Draw()
 │    Layer 0: Camera(View/Projection行列設定)
 │    Layer 1: MeshField, Player ほか
 │    Layer 2: Grass ほか
 └ Renderer::End()
```

- シーン初期化 [game.cpp](game.cpp) `Game::Init`: `Camera` → `Sky` → `MeshField` → `Player` → `Enemy`×23 → `Grass` → UI(`Polygon2D`/`Score`/`Fade`)の順に生成。
- `Camera` は Layer 0 なので、その `Draw` で設定された View/Projection が以降の Layer の描画に使われる。
- `GameObject::Draw` は `GetMatrix()`(Scale × Rotate × Translate、親があれば親行列を乗算)をワールド行列として設定し、各 Component の `Draw` を呼ぶ。`AnimationModel` は Player のコンポーネントとしてここで描画される。

---

## 3. フィールド

### 3.1 現在使用しているのは `MeshField`

[game.cpp:27](game.cpp#L27) で `MeshField` を生成。旧 `Field`(60×60 の1枚板)は [game.cpp:26](game.cpp#L26) でコメントアウト。

| 項目 | 内容 |
|---|---|
| グリッド | 21×21 頂点 = 20×20 マス |
| マス間隔 | 10.0f(ワールド範囲 X: -100〜100, Z: -100〜100) |
| 頂点座標 | `(x-10)*10, 高さ, (z-10)*-10` |
| UV | `(x, z)`(1マスでテクスチャ1回繰り返し) |
| 描画 | `TRIANGLESTRIP` + インデックスバッファ(マス間に縮退ポリゴン)、`DrawIndexed(878)` |
| Layer | 1 |

**高さデータについて**
- `g_FieldHeight[21][21]` に起伏データが定義済みだが、[meshField.cpp:41](meshField.cpp#L41) の起伏版の頂点生成はコメントアウトされており、現状は **全頂点 y=0(平坦)**。
- 起伏を有効にするには 41行目のコメントを戻し、43行目を無効化する。

### 3.2 Init 手順

```
MeshField::Init
 1. 頂点配列 m_Vertex[21][21] を生成(Position/Normal(0,1,0)/Diffuse/TexCoord)
 2. 内側の頂点 (1..19) の法線を再計算
      vx = 右隣 - 左隣,  vz = 奥隣 - 手前隣,  normal = normalize(cross(vz, vx))
 3. 頂点バッファ作成(D3D11_USAGE_DEFAULT)
 4. インデックスバッファ作成(列ごとにストリップ、列の切れ目に縮退ポリゴン2個)
 5. fieldVS / fieldPS 読み込み
 6. sura.jpg を SRV 化
```

### 3.3 描画手順(`MeshField::Draw`)

入力レイアウト → VS/PS 設定 → マテリアル(白, TextureEnable) → VB/IB → テクスチャ → トポロジ(TRIANGLESTRIP) → `GameObject::Draw`(ワールド行列) → `DrawIndexed`

### 3.4 高さ取得 `MeshField::GetHeight(Vector3 pos)`

プレイヤーの接地判定に使用。

```
1. ブロック番号算出
     x = (int)(pos.x / 10 + 10),  z = (int)(pos.z / -10 + 10)
2. そのマスの4頂点 pos0(x,z) pos1(x+1,z) pos2(x,z+1) pos3(x+1,z+1) を取得
3. 辺 pos1→pos2(マスの対角線)に対する点の左右を外積 cy で判定
     cy > 0 : 左上ポリゴン (pos0, pos1, pos2) の法線 n = cross(v12, v10)
     cy <= 0: 右下ポリゴン (pos1, pos2, pos3) の法線 n = cross(v13, v12)
4. 平面方程式から高さを算出
     y = -((px - p1.x)*n.x + (pz - p1.z)*n.z) / n.y + p1.y
```

**注意点**
- 範囲チェックが無い。プレイヤーが X/Z = ±100 付近(または外)に出ると `m_Vertex[x][z]` が配列外参照になる。
- 参考: [field.cpp](field.cpp) の `Field` は `litVS/litPS` で 4頂点の板を描くだけで、高さ判定は無い(現在未使用)。

---

## 4. プレイヤー

### 4.1 構成

`Player : GameObject`([player.h](player.h))

| メンバ | 役割 |
|---|---|
| `m_Velocity` | 速度(重力・抵抗・入力で更新) |
| `m_Ground` | 接地フラグ |
| `m_State` | `STATE_GROUND` / `STATE_ATTACK` |
| `m_StateTime` | 状態に入ってからの経過秒 |
| `m_Hit` / `m_HitStop` | 今回の攻撃でヒット済み / ヒットストップ中 |
| `m_AnimationName` / `m_AnimationFrame` | 現在(ブレンド元)アニメ名・フレーム |
| `m_NextAnimationName` / `m_NextAnimationFrame` | 遷移先アニメ名・フレーム |
| `m_Blend` | 0.0(元)〜1.0(先) のブレンド率 |
| `m_AnimationModel` | `AnimationModel` コンポーネント |
| `m_JumpSE` | `Audio` コンポーネント |

### 4.2 Init

```
Player::Init
 - Layer=1, Position=(0,0,0), Scale=(0.01,0.01,0.01)   ← FBXのcm単位をmへ縮小
 - AnimationModel 追加
     Load("Akai.fbx")
     LoadAnimation("Akai_Idle.fbx",   "Idle")
     LoadAnimation("Akai_Run.fbx",    "Run")
     LoadAnimation("Akai_Attack.fbx", "Attack")
 - 初期アニメ: Idle / Idle
 - litVS / litPS 読み込み
 - Audio 追加("wan.wav")
```

### 4.3 Update の流れ

```
Player::Update (毎フレーム)
 1. oldPosition を保存
 2. m_State で分岐
      STATE_GROUND → UpdateGround()
      STATE_ATTACK → UpdateAttack()
 3. 重力        vy += -50 * dt
 4. 空気抵抗    vx,vz += -v * 5 * dt
 5. 位置更新    pos += velocity * dt
 6. 地面判定    MeshField::GetHeight(pos) より下なら pos.y=height, vy=0, 接地
 7. 木との衝突  XZ平面で距離 < 1.5 なら押し出し
 8. ボックスとの衝突
      上面: 乗る(接地)   側面: XZ を oldPosition に戻し速度0
 9. HitStop 中でなければ
      m_AnimationFrame += 2, m_NextAnimationFrame += 2
      m_Blend += 0.1 (上限1.0)
10. GameObject::Update (コンポーネント Update)
```

### 4.4 状態遷移

```
            ↑キー押下(KK_UP)
 ┌────────┐ ──────────────▶ ┌─────────┐
 │ GROUND │                  │ ATTACK  │
 └────────┘ ◀────────────── └─────────┘
          m_StateTime > 1.5秒
```

#### STATE_GROUND(`UpdateGround`)

| 入力 | 動作 |
|---|---|
| W / S | カメラ前方(Y成分除去・正規化)方向に加速(同時押しは無効) |
| A / D | カメラ右方向に加速(同時押しは無効) |
| 移動あり | `SetAnimation("Run")`、向きを `atan2(vx, vz)` で速度方向へ回転 |
| 移動なし | `SetAnimation("Idle")` |
| SPACE(Trigger かつ接地) | `vy += 20` でジャンプ(SEは現在無効) |
| ↑(Pressed) | `m_Hit=false`, `m_State=ATTACK`, `m_StateTime=0` |

- 加速度は `speed(50) * dt` で、空気抵抗(係数5)とのつり合いで最高速度が決まる。

#### STATE_ATTACK(`UpdateAttack`)

| 経過時間 | 処理 |
|---|---|
| 常時 | `SetAnimation("Attack")` |
| 0.5秒超かつ未ヒット | 全 `Enemy` を走査し、距離 < 2.0 の敵に `AddDamage(1)`、`Shake`、`Explosion` 生成、`Score +1`、`m_Hit=true`、`m_HitStop=true`(最初の1体で break) |
| 0.8秒超 | `m_HitStop=false`(ヒットストップ解除) |
| 1.5秒超 | `STATE_GROUND` に戻る |

- ヒットストップ中は 4.3 の手順9がスキップされるため、**アニメーションのフレームとブレンドが止まる**(ゲーム時間 `m_StateTime` は進み続ける)。
- 攻撃中は移動入力を受け付けないが、重力・抵抗・衝突処理は継続して働く。

---

## 5. アニメーション

### 5.1 方式の概要

- **CPU スキニング**(ボーン行列で頂点を CPU 計算し、動的頂点バッファへ書き込む)
- 2つのアニメーションを**線形ブレンド**して滑らかに遷移(クロスフェード)
- アニメーションの時刻は**整数フレーム(キーフレーム番号)**で管理し、`Frame % キー数` でループ

### 5.2 データ構造([animationModel.h](animationModel.h))

| 構造体/メンバ | 内容 |
|---|---|
| `BONE` | `Matrix`(最終行列) / `AnimationMatrix`(現在ポーズのローカル行列) / `OffsetMatrix`(バインドポーズ逆行列) |
| `DEFORM_VERTEX` | 変形後の Position/Normal、`BoneNum`、`BoneName[4]`、`BoneWeight[4]`(最大4ボーン影響) |
| `m_Bone` | ノード名 → `BONE` のマップ |
| `m_Animation` | アニメ名 → `aiScene*`(アニメ用FBX) |
| `m_VertexBuffer[]` / `m_IndexBuffer[]` | メッシュごとの VB(`DYNAMIC`)/ IB |
| `m_Texture` | テクスチャ名 → SRV |
| `m_DeformVertex[]` | メッシュごとの変形後頂点配列 |

### 5.3 ロード(`AnimationModel::Load` / `LoadAnimation`)

```
Load("Akai.fbx")
 1. aiImportFile(… MaxQuality | ConvertToLeftHanded)
 2. CreateBone(rootNode)           … 全ノードを再帰的に m_Bone に登録
 3. メッシュごとに
      - 頂点バッファ(DYNAMIC, CPU書込可)
      - インデックスバッファ
      - DEFORM_VERTEX 初期化
      - aiBone から OffsetMatrix 登録、頂点にボーン名・ウェイトを格納
 4. 内蔵テクスチャを WIC メモリ読込 → SRV

LoadAnimation(file, name)
 - aiImportFile(file, ConvertToLeftHanded) を m_Animation[name] に保存
```

### 5.4 アニメーション切り替え(`Player::SetAnimation`)

```
SetAnimation(newName):
  if (m_NextAnimationName != newName):          // 目標が変わったときだけ
      m_AnimationName      = m_NextAnimationName   // 今の目標が「元」になる
      m_AnimationFrame     = m_NextAnimationFrame
      m_NextAnimationName  = newName
      m_NextAnimationFrame = 0                     // 新アニメは頭から
      m_Blend              = 0.0                   // 元100%から開始
```

その後、毎フレーム `m_Blend += 0.1` され、**約10フレーム(1/6秒)** で新アニメ100%になる。

例:Idle 中に W 押下 → `Idle(元) → Run(先)` で 0→1 へブレンド → ブレンド完了後は Run のみ。

### 5.5 描画前の更新(`Player::Draw` → `AnimationModel::Update`)

```
Player::Draw
 1. 入力レイアウト / litVS / litPS 設定
 2. m_AnimationModel->Update(元名, 元Frame, 先名, 先Frame, Blend)   ← ここでスキニング
 3. GameObject::Draw → AnimationModel::Draw
```

`AnimationModel::Update` の内部手順:

```
 1. 指定アニメが m_Animation に存在し、HasAnimations() か確認(無ければ return)
 2. 各ボーン(ノード)について
      元/先それぞれ該当チャンネル(mNodeName 一致)を線形探索
      回転 = mRotationKeys[Frame % 回転キー数]
      位置 = mPositionKeys[Frame % 位置キー数]
      pos = lerp(pos1, pos2, Blend)
      rot = slerp(rot1, rot2, Blend)       // aiQuaternion::Interpolate
      bone.AnimationMatrix = (scale=1, rot, pos)
 3. UpdateBoneMatrix(rootNode, rootMatrix) を再帰
      worldMatrix = 親行列 * AnimationMatrix
      bone.Matrix = worldMatrix * OffsetMatrix
      rootMatrix = X軸まわり180°回転(座標系補正)
 4. CPUスキニング: メッシュごとに VB を Map(WRITE_DISCARD)
      outMatrix = Σ (bone[i].Matrix * weight[i])   (i = 0..3)
      Position = 元頂点 * outMatrix
      Normal   = 元法線 * outMatrix(平行移動成分除去)
      UV/Diffuse は元のまま書き込み、Unmap
```

### 5.6 描画(`AnimationModel::Draw`)

```
メッシュごとに
  - マテリアルの Diffuse テクスチャ名を取得 → m_Texture から SRV を PSに設定
    (無ければ TextureEnable=false、Diffuse色/Opacity を Material に反映)
  - VB/IB 設定 → DrawIndexed(mNumFaces*3)
```

---

## 6. 動作フロー(通しの例)

### 例1: 立ち止まり → 走り出し

```
フレームN   : W押下
  Player::Update → UpdateGround
    velocity += forward * speed * dt、move = true
    SetAnimation("Run"): 元=Idle(frame保存) / 先=Run(frame=0) / Blend=0
    rotate.y = atan2(vx, vz)
  フレーム進行: frame += 2, Blend += 0.1
  Player::Draw → AnimationModel::Update(Idle, f1, Run, f2, Blend)
                  → Idle と Run を Blend 率で補間 → CPUスキニング → 描画
約10フレーム後: Blend = 1.0 → 実質 Run のみ
```

### 例2: 攻撃

```
↑キー押下 (GROUND)
  → m_State=ATTACK, m_StateTime=0, m_Hit=false
毎フレーム UpdateAttack
  SetAnimation("Attack")  (初回のみブレンド開始)
  0.5秒到達後、距離2.0以内の Enemy がいれば
      ダメージ/揺れ/爆発/スコア、m_HitStop=true
      → Update 手順9がスキップされ、アニメーションが静止(ヒットストップ演出)
  0.8秒到達 → m_HitStop=false (アニメ再開)
  1.5秒到達 → STATE_GROUND へ。次の UpdateGround で Idle / Run にブレンド
```

### 例3: 接地判定

```
重力で vy 減少 → pos += v*dt
GetHeight(pos) を取得し、pos.y < height なら
  pos.y = height, vy = 0, m_Ground = true
→ 接地中のみ SPACE でジャンプ可能
```

---

## 7. 補足・気になる点(コードから読み取れる範囲)

1. **フレーム管理が固定**: アニメは「1更新 = 2フレーム進行」で、FBX 側のフレームレート/時間(`mTicksPerSecond`)は使っていない。アニメの再生速度は FBX のキー間隔に依存。
2. **キー補間なし**: 回転・位置とも `Frame % キー数` のキーをそのまま使う(キー間補間はしない)。スケールキーは無視。
3. **ボーン探索が O(ボーン数×チャンネル数)** で毎フレーム実行(CPU スキニングと合わせてコスト大)。
4. **`GetHeight` の範囲外アクセス**に注意(3.4 参照)。
5. **`Uninit` の `m_Animation[Name]` 重複ロード**: 同名で `LoadAnimation` を再度呼ぶと既存ポインタがリークする。
6. **Attack の入力**は `KeyLogger_IsPressed(KK_UP)`(押しっぱなし)で、1.5秒後に GROUND に戻った際に押し続けていると即再攻撃になる。
7. カメラは [camera.cpp](camera.cpp) でプレイヤー位置+(0,2,0) を線形補間(係数0.1)で追従し、←/→キーで周回する。プレイヤーの移動方向はこのカメラ向きが基準になる。
