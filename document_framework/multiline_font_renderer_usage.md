# MultiLineDrawFont 使い方

## 概要
MultiLineDrawFont は DrawFont を継承した複数行描画クラスです（`framework/MultiLineDrawFont.h`）。
テキスト中の `\n` で行分割し、行間設定と行矩形取得に対応します。

## 主な API
- コンストラクタ
  - `MultiLineDrawFont(XMFLOAT2 pos, float fontSize, float rotation, XMFLOAT4 color, const std::string& text, float lineSpacing = 1.5f, TextAlignment align = TA_MIDDLE)`
- 描画
  - `Draw()`
- テキスト変更
  - `SetText(const std::string& text)`
- 行間設定
  - `SetLineSpacing(float lineSpacing)` / `GetLineSpacing()`
- アライメント
  - `SetAlignment(TextAlignment align)`
- 行矩形取得
  - `GetLineRects()` → `std::vector<FontLineRect>`（left/top/right/bottom）

## 最小サンプル
```cpp
#include "MultiLineDrawFont.h"

static MultiLineDrawFont* g_pMulti = nullptr;

void Sample_Initialize()
{
	g_pMulti = new MultiLineDrawFont(
		{ SCREEN_X * 0.5f, 140.0f },
		30.0f,
		0.0f,
		{ 0.9f, 0.95f, 1.0f, 1.0f },
		"1行目: 操作説明\n2行目: Enterで決定\n3行目: Escで戻る",
		1.4f,
		TA_MIDDLE
	);
}

void Sample_Update()
{
	g_pMulti->SetText("A\nB\nC");
}

void Sample_Draw()
{
	g_pMulti->Draw();
}

void Sample_Finalize()
{
	SAFE_DELETE(g_pMulti);
}
```

## 注意点
- 自動折り返しはありません。改行は `\n` を明示してください。
- GetLineRects の矩形は行単位判定（MultiLineClickFont 等）に利用できます。
- 描画座標の基準は SCREEN_X/SCREEN_Y です。
