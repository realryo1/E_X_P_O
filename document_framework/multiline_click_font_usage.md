# MultiLineClickFont 使い方

## 概要
MultiLineClickFont は ClickFont を継承した複数行クリック対応クラスです（`framework/MultiLineClickFont.h`）。
行ごとに当たり判定し、クリックされた行番号を取得できます。

## 主な API
- コンストラクタ
  - `MultiLineClickFont(XMFLOAT2 pos, float fontSize, float rotation, XMFLOAT4 normalColor, XMFLOAT4 hoverColor, const std::string& text, float lineSpacing = 1.5f, TextAlignment align = TA_MIDDLE)`
- 更新
  - `Update()`
- 描画
  - `Draw()`
- テキスト変更
  - `SetText(const std::string& text)`
- 行間設定
  - `SetLineSpacing(float lineSpacing)` / `GetLineSpacing()`
- 状態取得
  - `IsHover()` / `IsClick()` / `GetClickedLineIndex()`

## ClickedLineIndex の仕様
- `-1`: クリックなし
- `0` 以上: クリックされた行番号
- `IsClick` が true のフレームで有効

## 最小サンプル
```cpp
#include "MultiLineClickFont.h"
#include "fade.h"
#include "scene.h"

static MultiLineClickFont* g_pMenu = nullptr;

void Sample_Initialize()
{
	g_pMenu = new MultiLineClickFont(
		{ SCREEN_X * 0.5f, 220.0f },
		40.0f,
		0.0f,
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		{ 1.0f, 0.85f, 0.25f, 1.0f },
		"Start\nConfig\nExit",
		1.35f,
		TA_MIDDLE
	);
}

void Sample_Update()
{
	g_pMenu->Update();

	if (g_pMenu->IsClick())
	{
		switch (g_pMenu->GetClickedLineIndex())
		{
		case 0: SetSceneFade(SCENE_GAME); break;
		case 1: /* 設定画面へ */ break;
		case 2: /* 終了処理 */ break;
		default: break;
		}
	}
}

void Sample_Draw()
{
	g_pMenu->Draw();
}

void Sample_Finalize()
{
	SAFE_DELETE(g_pMenu);
}
```

## 注意点
- 行間の空白領域はヒットしません。
- クリック判定は行矩形を順に検査し、最初にヒットした行番号を返します。
- 既存コードとの互換性のため `IsClick` も利用できます。
- UI 操作時はマウス絶対座標モードが必要です（`LockMouse` 中は先に解除）。
