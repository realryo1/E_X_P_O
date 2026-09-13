#pragma once

#include <DirectXMath.h>
#include <string>
#include <vector>

#include "font.h"

using namespace DirectX;

struct FontLineRect
{
float left;
float top;
float right;
float bottom;
};

class MultiLineDrawFont : public DrawFont
{
public:
MultiLineDrawFont(XMFLOAT2 pos, float fontSize, float rotation,
XMFLOAT4 color, const std::string& text, float lineSpacing = 1.5f, TextAlignment align = TA_MIDDLE);
~MultiLineDrawFont() = default;

void Draw() override;
void SetText(const std::string& text) override;

void SetLineSpacing(float lineSpacing);
void SetAlignment(TextAlignment align);
float GetLineSpacing() const { return m_LineSpacing; }
const std::vector<FontLineRect>& GetLineRects() const { return m_LineRects; }

protected:
void RebuildLayout();

private:
std::vector<std::string> SplitLines(const std::string& text) const;
int CountUtf8CodePoints(const std::string& text) const;

std::string m_SourceText;
float m_FontSize;
float m_LineSpacing;
std::vector<std::string> m_Lines;
std::vector<FontLineRect> m_LineRects;
};
