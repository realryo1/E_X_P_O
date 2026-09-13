# DrawFont 使い方

## 概要
DrawFont は単一行テキスト描画用の基本クラスです（`framework/font.h`）。
UTF-8 文字列を描画し、色変更・アライメント・事前グリフキャッシュに対応します。

## 主な API
- コンストラクタ
  - `DrawFont(XMFLOAT2 pos, float fontSize, float rotation, XMFLOAT4 color, const std::string& text, TextAlignment align = TA_MIDDLE)`
- 描画
  - `Draw()`
- テキスト変更
  - `SetText(const std::string& text)`
- 色変更
  - `SetColor(XMFLOAT4 color)`
- アライメント
  - `SetAlignment(TextAlignment align)` / `GetAlignment()`
  - `TA_START` / `TA_MIDDLE` / `TA_END`
- 事前キャッシュ
  - `PreCacheGlyphs()`

## 最小サンプル
```cpp
#include "font.h"

static DrawFont* g_pFont = nullptr;

void Sample_Initialize()
{
	g_pFont = new DrawFont(
		{ SCREEN_X * 0.5f, SCREEN_Y * 0.5f },
		36.0f,
		0.0f,
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		"Hello DrawFont",
		TA_MIDDLE
	);
	g_pFont->PreCacheGlyphs();
}

void Sample_Update()
{
	g_pFont->SetText("Score: 12345");
}

void Sample_Draw()
{
	// SetDepthEnable(false) の後で呼ぶ
	g_pFont->Draw();
}

void Sample_Finalize()
{
	SAFE_DELETE(g_pFont);
}
```

## 注意点
- 改行は想定していません。複数行は MultiLineDrawFont を使用してください。
- 座標は SCREEN_X/SCREEN_Y 基準で指定してください。
- SetColor はアトラステクスチャ更新を伴うため、毎フレーム連打は避けてください。
- フォント実体は `asset/font/KaiseiDecol-Medium.ttf`。`Font_InitializeGlobalData()` が事前に必要です。
