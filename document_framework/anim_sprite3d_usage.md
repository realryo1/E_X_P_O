# AnimSprite3D 使い方メモ

## 概要

`AnimSprite3D` は `Sprite3D` を継承した 3D モデル描画クラスで、FBX / GLB に含まれるスキニングアニメーションを再生できる。  
実装: `framework/anim_sprite3d.h` / `framework/anim_sprite3d.cpp`

---

## 基本の生成方法

```cpp
AnimSprite3D* character = new AnimSprite3D(
	{ 0.0f, 0.0f, 0.0f },
	{ 1.0f, 1.0f, 1.0f },
	{ 0.0f, 180.0f, 0.0f },
	"asset\\model\\character.fbx",
	S_PHONG
);
```

### 引数

1. 位置 `XMFLOAT3`
2. スケール `XMFLOAT3`
3. 回転 `XMFLOAT3`（度数法）
4. モデルパス（`.fbx` / `.glb`）
5. シェーダー種別（`SHADERTYPE`）

---

## 初期化直後に行うこと

```cpp
character->SetAnimationBlendDuration(0.2);
```

- アニメーション切替時のブレンド秒数（既定は内部で 0.3 秒）
- 読み込み直後に件数確認:

```cpp
unsigned int animCount = character->GetAnimationCount();
```

---

## アニメーション再生

### 名前指定

```cpp
character->PlayAnimationByName("Walk", true);
```

- 第 1 引数: アニメーション名
- 第 2 引数: ループ有無
- 完全一致を優先し、なければ部分一致。クリップが 1 件だけなら名前不一致でもその 1 件を再生する

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

- `UpdateAnimation` の末尾で `UpdateBoneMatrices()` を呼ぶため、通常は別途ボーン更新不要
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

---

## 制御

```cpp
character->PauseAnimation();
character->ResumeAnimation();
character->StopAnimation();
bool playing = character->IsAnimationPlaying();
bool blending = character->IsAnimationBlending();
```

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
	S_PHONG
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
- スキニング非対応、またはアニメ未内包のモデルでは再生できない
- 描画実装の確認は `shader/renderer.cpp` / `framework/model.cpp` を参照（`shader/shader.cpp` は無い）
