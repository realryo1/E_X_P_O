#pragma once

#include <DirectXMath.h>
#include <string>

#include "ClickFont.h"
#include "MultiLineDrawFont.h"

using namespace DirectX;

class MultiLineClickFont : public ClickFont
{
public:
MultiLineClickFont(XMFLOAT2 pos, float fontSize, float rotation,
XMFLOAT4 normalColor, XMFLOAT4 hoverColor, const std::string& text,
float lineSpacing = 1.5f, TextAlignment align = TA_MIDDLE);
~MultiLineClickFont() = default;

void Draw() override;
void Update();
void SetText(const std::string& text) override;

void SetLineSpacing(float lineSpacing);
float GetLineSpacing() const { return m_LineSpacing; }

bool IsHover() const { return m_IsHoverEx; }
bool IsClick() const { return m_IsClickEx; }
int GetClickedLineIndex() const { return m_ClickedLineIndex; }

private:
void RebuildLayout();
int HitTestLineIndex(int mouseX, int mouseY) const;
std::vector<std::string> SplitLines(const std::string& text) const;
int CountUtf8CodePoints(const std::string& text) const;

XMFLOAT4 m_NormalColorEx;
XMFLOAT4 m_HoverColorEx;
std::string m_SourceText;
float m_FontSize;
float m_LineSpacing;
std::vector<std::string> m_Lines;
std::vector<FontLineRect> m_LineRects;
bool m_IsHoverEx;
bool m_WasLeftDownEx;
bool m_IsClickEx;
int m_ClickedLineIndex;
};
