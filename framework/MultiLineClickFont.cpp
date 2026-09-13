#include "MultiLineClickFont.h"

#include <algorithm>

#include "define.h"
#include "mouse.h"
#include "renderer.h"

namespace
{
constexpr float kFontHitWidthScale = 0.5f;
}

MultiLineClickFont::MultiLineClickFont(XMFLOAT2 pos, float fontSize, float rotation,
XMFLOAT4 normalColor, XMFLOAT4 hoverColor, const std::string& text, float lineSpacing, TextAlignment align)
: ClickFont(pos, fontSize, rotation, normalColor, hoverColor, text)
, m_NormalColorEx(normalColor)
, m_HoverColorEx(hoverColor)
, m_SourceText(text)
, m_FontSize(fontSize)
, m_LineSpacing((std::max)(lineSpacing, 0.1f))
, m_IsHoverEx(false)
, m_WasLeftDownEx(false)
, m_IsClickEx(false)
, m_ClickedLineIndex(-1)
{
SetAlignment(align);
RebuildLayout();
}

void MultiLineClickFont::Draw()
{
	RebuildLayout();

const XMFLOAT2 originalPos = GetPos();

for (size_t i = 0; i < m_Lines.size(); ++i)
{
const std::string& line = m_Lines[i];
if (line.empty())
{
continue;
}

SetPos({ originalPos.x, originalPos.y + static_cast<float>(i) * m_FontSize * m_LineSpacing });
DrawFont::SetText(line);
DrawFont::Draw();
}

SetPos(originalPos);
DrawFont::SetText(m_SourceText);
}

void MultiLineClickFont::Update()
{
	RebuildLayout();

Mouse_State ms{};
Mouse_GetState(&ms);

const int hoverLineIndex = HitTestLineIndex(ms.x, ms.y);
const bool hover = (hoverLineIndex >= 0);
if (hover != m_IsHoverEx)
{
m_IsHoverEx = hover;
SetColor(m_IsHoverEx ? m_HoverColorEx : m_NormalColorEx);
}

const bool leftDown = ms.leftButton;
const bool pressedThisFrame = (leftDown && !m_WasLeftDownEx);
m_WasLeftDownEx = leftDown;

m_IsClickEx = (pressedThisFrame && m_IsHoverEx);
m_ClickedLineIndex = m_IsClickEx ? hoverLineIndex : -1;
}

void MultiLineClickFont::SetText(const std::string& text)
{
m_SourceText = text;
DrawFont::SetText(text);
RebuildLayout();
}

void MultiLineClickFont::SetLineSpacing(float lineSpacing)
{
m_LineSpacing = (std::max)(lineSpacing, 0.1f);
RebuildLayout();
}

void MultiLineClickFont::RebuildLayout()
{
m_Lines = SplitLines(m_SourceText);
m_LineRects.clear();
m_LineRects.reserve(m_Lines.size());

const XMFLOAT2 basePos = GetPos();
const float lineHeight = m_FontSize * m_LineSpacing;

for (size_t i = 0; i < m_Lines.size(); ++i)
{
const int codePointCount = CountUtf8CodePoints(m_Lines[i]);
const float width = static_cast<float>(codePointCount) * m_FontSize * kFontHitWidthScale;
const float centerY = basePos.y + static_cast<float>(i) * lineHeight;

FontLineRect rect{};
if (GetAlignment() == TA_MIDDLE) {
    rect.left = basePos.x - width * 0.5f;
    rect.right = basePos.x + width * 0.5f;
} else if (GetAlignment() == TA_START) {
    rect.left = basePos.x;
    rect.right = basePos.x + width;
} else if (GetAlignment() == TA_END) {
    rect.left = basePos.x - width;
    rect.right = basePos.x;
}
rect.top = centerY - lineHeight * 0.5f;
rect.bottom = centerY + lineHeight * 0.5f;
m_LineRects.push_back(rect);
}
}

int MultiLineClickFont::HitTestLineIndex(int mouseX, int mouseY) const
{
const float clientW = Direct3D_GetClientWidth();
const float clientH = Direct3D_GetClientHeight();
const float targetAspect = SCREEN_X / SCREEN_Y;
const float windowAspect = clientW / clientH;

float vpX;
float vpY;
float vpW;
float vpH;
if (windowAspect > targetAspect)
{
vpH = clientH;
vpW = clientH * targetAspect;
vpX = (clientW - vpW) * 0.5f;
vpY = 0.0f;
}
else
{
vpW = clientW;
vpH = clientW / targetAspect;
vpX = 0.0f;
vpY = (clientH - vpH) * 0.5f;
}

const float logicalX = (mouseX - vpX) / vpW * SCREEN_X;
const float logicalY = (mouseY - vpY) / vpH * SCREEN_Y;

for (size_t i = 0; i < m_LineRects.size(); ++i)
{
const FontLineRect& rect = m_LineRects[i];
if (logicalX >= rect.left && logicalX <= rect.right
&& logicalY >= rect.top && logicalY <= rect.bottom)
{
return static_cast<int>(i);
}
}

return -1;
}

std::vector<std::string> MultiLineClickFont::SplitLines(const std::string& text) const
{
std::vector<std::string> lines;
std::string current;

for (char c : text)
{
if (c == '\r')
{
continue;
}
if (c == '\n')
{
lines.push_back(current);
current.clear();
continue;
}
current.push_back(c);
}

lines.push_back(current);
if (lines.empty())
{
lines.push_back("");
}

return lines;
}

int MultiLineClickFont::CountUtf8CodePoints(const std::string& text) const
{
int count = 0;
for (size_t i = 0; i < text.size();)
{
const unsigned char c = static_cast<unsigned char>(text[i]);
if ((c & 0x80) == 0)
{
i += 1;
}
else if ((c & 0xE0) == 0xC0)
{
i += 2;
}
else if ((c & 0xF0) == 0xE0)
{
i += 3;
}
else if ((c & 0xF8) == 0xF0)
{
i += 4;
}
else
{
i += 1;
}
count++;
}
return count;
}
