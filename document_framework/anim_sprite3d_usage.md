# AnimSprite3D 使い方ガイド

## 概要

`AnimSprite3D` は `Sprite3D` を継承した 3D モデル描画クラスで、Assimpで読み込んだFBXのスキニングアニメーションを再生できる。
アニメーションはモデル内の `aiScene` から自動抽出され、キーフレームは秒からモデルのティックへ変換して更新する。  
実装: `framework/anim_sprite3d.h` / `framework/anim_sprite3d.cpp`

---

## 基本の生成方法

```cpp
AnimSprite3D* character = new AnimSprite3D(
	{ 0.0f, 0.0f, 0.0f },
	{ 1.0f, 1.0f, 1.0f },
	{ 0.0f, 180.0f, 0.0f },
	"asset\\model\\character.fbx",
	S_PBR
);
```

### 引数

1. 位置 `XMFLOAT3`
2. スケール `XMFLOAT3`
3. 回転 `XMFLOAT3`（度数法）
4. モデルパス（主にスキニング対応の `.fbx`）
5. シェーダー種別（`SHADERTYPE`）

`AnimSprite3D` は `Sprite3D` のコンストラクタ内でモデルを読み込み、続けて
`InitializeBones()` を実行する。したがって、生成後に別のボーン初期化を呼ぶ必要はない。
スキニング情報を持たないモデルでも生成できるが、ボーンアニメーションは適用されない。

---

## 初期化直後に行うこと

```cpp
character->SetAnimationBlendDuration(0.2);
```

- `PlayAnimationByName()` で再生中のアニメーションを切り替えたときのブレンド秒数を設定する
- 既定値は `0.3` 秒。`PlayAnimationByIndex()` による切替はブレンドせず、即時に再生する
- 読み込み直後に件数を確認できる:

```cpp
unsigned int animCount = character->GetAnimationCount();
```

---

## アニメーション再生

### 再生開始

`PlayAnimation()` は、すでに `SetAnimationClip()` 済みの内部クリップを再生する低レベル操作である。
通常は名前指定またはインデックス指定を使用する。

### 名前指定

```cpp
character->PlayAnimationByName("Walk", true);
```

- 第 1 引数: アニメーション名
- 第 2 引数: ループ有無
- 完全一致を優先し、なければ部分一致で検索する
- モデル内のアニメーションが1件だけの場合は、名前が一致しなくてもその1件を再生する
- すでに同じ名前を再生中なら、状態をリセットせず成功として扱う
- 別のアニメーションから切り替える場合は、`SetAnimationBlendDuration()` の時間で遷移する

### インデックス指定

```cpp
character->PlayAnimationByIndex(0, true);
```

---

## アニメーション一覧の確認

```cpp
unsigned int animCount = character->GetAnimationCount();
for (unsigned int i = 0; i < animCount; i++)
{
	const char* animName = character->GetAnimationName(i);
	// nullptr になり得るので null チェック推奨
}
```

---

## 毎フレーム更新

```cpp
character->UpdateAnimation(1.0f / FPS);
```

- `dt` は秒単位で渡す。内部でクリップの `tps`（Ticks Per Second）へ変換する
- 再生中は `UpdateBoneMatrices()` まで実行するため、通常は別途ボーン更新不要
- ループ再生ではクリップ末尾から経過時間を持ち越し、非ループ再生では末尾で停止する
- 固定ステップ運用なら `1.0f / FPS`（60）でよい

---

## 描画

```cpp
SetDepthEnable(true);
character->Draw();

// 影を落とす場合
character->DrawShadowMap(lightView, lightProjection);
```

- ライト利用時は先に `SetLight` 等を済ませる
- `Draw()` は現在のボーン行列を使ってスキニング描画する
- `DrawShadowMap()` も現在のスキニング姿勢を使う。ただし `m_IsGlb` のモデルでは現状何も描画しない

---

## 制御

```cpp
character->PauseAnimation();
character->ResumeAnimation();
character->StopAnimation();
bool playing = character->IsAnimationPlaying();
bool blending = character->IsAnimationBlending();
```

- `PauseAnimation()` と `StopAnimation()` はどちらも再生フラグを停止する
- `ResumeAnimation()` は現在のクリップが設定されている場合だけ再開する

---

## 部分アニメーションの上書き

全身の再生を維持したまま、指定したボーン以下へ別クリップを重ねる。
たとえば、歩行中に上半身だけ別のモーションへ差し替える場合に使用する。

```cpp
character->PlayOverrideAnimation("Wave", "Spine", true);
character->SetOverridePlaySpeed(1.25);

// 停止すると通常アニメーションだけに戻る
character->StopOverrideAnimation();
```

複数の開始ボーンを指定する場合は、`std::vector<std::string>` を渡す。

```cpp
std::vector<std::string> bones = { "Spine", "LeftArm" };
character->PlayOverrideAnimation("Wave", bones, true);
```

- アニメーション名は完全一致を優先し、なければ部分一致で検索する
- `startBoneName` または開始ボーン配列が空の場合は失敗する
- `SetOverridePlaySpeed()` は部分アニメーションだけの再生速度倍率を変更する
- 状態確認には `IsOverrideAnimationActive()` と `GetOverrideAnimationName()` を使う

---

## マテリアルカラーの一時変更

```cpp
character->SetMaterialOverrideColor({ 1.0f, 0.6f, 0.6f, 1.0f });
// 元のモデル色へ戻す
character->ResetMaterialOverride();
```

`SetMaterialOverrideColor()` は `Sprite3D::SetColor()` を通じて描画色を差し替え、
`ResetMaterialOverride()` は元の色へ戻す。

---

## 終了処理

```cpp
SAFE_DELETE(character);
```

---

## 最小構成の流れ

```cpp
AnimSprite3D* character = new AnimSprite3D(
	{ 0.0f, 0.0f, 0.0f },
	{ 1.0f, 1.0f, 1.0f },
	{ 0.0f, 180.0f, 0.0f },
	"asset\\model\\character.fbx",
	S_PBR
);
character->SetAnimationBlendDuration(0.2);

if (!character->PlayAnimationByName("Walk", true))
{
	if (character->GetAnimationCount() > 0)
	{
		character->PlayAnimationByIndex(0, true);
	}
}

character->UpdateAnimation(1.0f / FPS);
character->Draw();

SAFE_DELETE(character);
```

---

## 注意点

- アニメーション名はモデル依存。固定名を使う前に `GetAnimationName()` で確認する
- `GetAnimationName()` は範囲外で `nullptr` を返すため、表示前に null チェックする
- スキニング非対応、またはアニメ未内包のモデルではボーンアニメーションを再生できない
- `PlayAnimationByIndex()` の範囲外、`PlayAnimationByName()` の null 引数、見つからない部分アニメーションは `false` を返す
- アニメーション時間とブレンド時間は `double`、`UpdateAnimation()` のフレーム時間は秒単位の `float`
- 描画実装の確認は `shader/renderer.cpp` / `framework/model.cpp` を参照（`shader/shader.cpp` は無い）
