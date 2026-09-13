/*==============================================================================

   日本語フォント描画システム実装 [font.cpp]
										  Author : Copilot
										  Date   : 2025/01/10
--------------------------------------------------------------------------------

==============================================================================*/
#include "font.h"
#include "renderer.h"
#include "shadermanager.h"

#include "define.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <ft2build.h>
#include FT_FREETYPE_H

using namespace DirectX;

#pragma comment(lib, "d3dcompiler.lib")
#ifdef _DEBUG
#pragma comment(lib, "freetype_d.lib")
#else
#pragma comment(lib, "freetype.lib")
#endif

// ==========================================
// グローバルフォントデータ（全 DrawFont で共有）
// ==========================================
static unsigned char* g_pGlobalFontData = nullptr;
static int g_GlobalFontDataSize = 0;
static FT_Library g_FtLibrary = nullptr;
static FT_Face g_FtFace = nullptr;
static FT_UInt g_FtCurrentPixelSize = 0;

void Font_InitializeGlobalData()
{
	if (g_FtFace != nullptr) {
		return; // 既に初期化済み
	}

	FILE* f = nullptr;
	fopen_s(&f, "asset/font/KaiseiDecol-Medium.ttf", "rb");
	if (!f) {
		return;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	g_pGlobalFontData = (unsigned char*)malloc(size);
	if (!g_pGlobalFontData) {
		fclose(f);
		return;
	}

	fread(g_pGlobalFontData, 1, size, f);
	fclose(f);
	g_GlobalFontDataSize = (int)size;

	if (FT_Init_FreeType(&g_FtLibrary) != 0) {
		free(g_pGlobalFontData);
		g_pGlobalFontData = nullptr;
		g_GlobalFontDataSize = 0;
		g_FtLibrary = nullptr;
		return;
	}

	if (FT_New_Memory_Face(g_FtLibrary, g_pGlobalFontData, g_GlobalFontDataSize, 0, &g_FtFace) != 0) {
		FT_Done_FreeType(g_FtLibrary);
		free(g_pGlobalFontData);
		g_pGlobalFontData = nullptr;
		g_GlobalFontDataSize = 0;
		g_FtLibrary = nullptr;
		g_FtFace = nullptr;
		return;
	}

	g_FtCurrentPixelSize = 0;
}

void Font_FinalizeGlobalData()
{
	if (g_FtFace) {
		FT_Done_Face(g_FtFace);
		g_FtFace = nullptr;
	}
	if (g_FtLibrary) {
		FT_Done_FreeType(g_FtLibrary);
		g_FtLibrary = nullptr;
	}
	if (g_pGlobalFontData) {
		free(g_pGlobalFontData);
		g_pGlobalFontData = nullptr;
		g_GlobalFontDataSize = 0;
	}
	g_FtCurrentPixelSize = 0;
}

// ==========================================
// DrawFont クラス実装
// ==========================================

DrawFont::DrawFont(XMFLOAT2 pos, float fontSize, float rotation,
	XMFLOAT4 color, const std::string& text, TextAlignment align)
	: Transform2D(pos, rotation, { 1.0f, 1.0f }), m_Color(color), m_Text(text),
	m_FontSize(fontSize), m_Alignment(align),
	m_pTexture(nullptr), m_pSRV(nullptr),
	m_pVertexBuffer(nullptr),
	m_VertexCount(0), m_VertexCapacity(0),
	m_AtlasWidth(0), m_AtlasHeight(0),
	m_AtlasNextX(0), m_AtlasNextY(0), m_AtlasRowHeight(0),
	m_pAtlasData(nullptr), m_pAtlasRGBA(nullptr),
	m_FontAscender(0), m_FontDescender(0),
	m_MeshDirty(true),
	m_CachedPos(pos), m_CachedRot(rotation), m_CachedScale({ 1.0f, 1.0f }),
	m_Ready(false)
{
	if (!CreateShaders()) {
		return;
	}

	if (!BakeAtlas()) {
		return;
	}

	// コンストラクタ時点のテキストをアトラスへ載せておく（呼び出し側の PreCache 不要）
	PreCacheGlyphs();
}

DrawFont::~DrawFont() {
	if (m_pVertexBuffer) m_pVertexBuffer->Release();
	if (m_pSRV) m_pSRV->Release();
	if (m_pTexture) m_pTexture->Release();
	if (m_pAtlasData) free(m_pAtlasData);
	if (m_pAtlasRGBA) free(m_pAtlasRGBA);
}

bool DrawFont::CreateShaders() {
	return true;
}

bool DrawFont::EnsurePixelSize() {
	if (!g_FtFace) {
		return false;
	}

	FT_UInt pixelHeight = (FT_UInt)(m_FontSize * DRAW_SCALE_X + 0.5f);
	if (pixelHeight < 1) {
		pixelHeight = 1;
	}

	if (g_FtCurrentPixelSize != pixelHeight) {
		if (FT_Set_Pixel_Sizes(g_FtFace, 0, pixelHeight) != 0) {
			return false;
		}
		g_FtCurrentPixelSize = pixelHeight;
	}
	return true;
}

float DrawFont::GetKerningPx(int prevGlyph, int glyphIndex) const {
	if (!g_FtFace || prevGlyph <= 0 || glyphIndex <= 0) {
		return 0.0f;
	}

	// TT_CONFIG_OPTION_GPOS_KERNING 有効時は GPOS ペアも FT_Get_Kerning 経由で取得できる
	FT_Vector delta;
	if (FT_Get_Kerning(g_FtFace, (FT_UInt)prevGlyph, (FT_UInt)glyphIndex, FT_KERNING_DEFAULT, &delta) != 0) {
		return 0.0f;
	}
	return (float)delta.x / 64.0f;
}

float DrawFont::GetGlyphAdvancePx(int glyphIndex) {
	auto it = m_CharCache.find(glyphIndex);
	if (it != m_CharCache.end()) {
		return it->second.xadvance;
	}

	// スペース等も一度だけ FreeType でロードしてキャッシュする
	if (!AddGlyphToAtlas(glyphIndex)) {
		return 0.0f;
	}

	it = m_CharCache.find(glyphIndex);
	return (it != m_CharCache.end()) ? it->second.xadvance : 0.0f;
}

bool DrawFont::BakeAtlas() {
	ID3D11Device* pDevice = GetDevice();

	if (!pDevice) {
		return false;
	}

	if (!g_FtFace) {
		return false;
	}

	m_Ready = true;
	m_FontAscender = (int)g_FtFace->ascender;
	m_FontDescender = (int)g_FtFace->descender;

	m_AtlasWidth = FONT_ATLAS_WIDTH;
	m_AtlasHeight = FONT_ATLAS_HEIGHT;
	m_pAtlasData = (unsigned char*)calloc(m_AtlasWidth * m_AtlasHeight, 1);
	m_pAtlasRGBA = (unsigned char*)malloc(m_AtlasWidth * m_AtlasHeight * 4);

	if (!m_pAtlasData || !m_pAtlasRGBA) {
		free(m_pAtlasData);
		free(m_pAtlasRGBA);
		m_pAtlasData = nullptr;
		m_pAtlasRGBA = nullptr;
		m_Ready = false;
		return false;
	}

	m_AtlasNextX = 0;
	m_AtlasNextY = 0;
	m_AtlasRowHeight = 0;

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = m_AtlasWidth;
	desc.Height = m_AtlasHeight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	for (int i = 0; i < m_AtlasWidth * m_AtlasHeight; i++) {
		m_pAtlasRGBA[i * 4 + 0] = 255;
		m_pAtlasRGBA[i * 4 + 1] = 255;
		m_pAtlasRGBA[i * 4 + 2] = 255;
		m_pAtlasRGBA[i * 4 + 3] = 255;
	}

	HRESULT hr = pDevice->CreateTexture2D(&desc, nullptr, &m_pTexture);
	if (FAILED(hr)) {
		free(m_pAtlasRGBA);
		free(m_pAtlasData);
		m_pAtlasRGBA = nullptr;
		m_pAtlasData = nullptr;
		m_Ready = false;
		return false;
	}

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	hr = GetDeviceContext()->Map(m_pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (SUCCEEDED(hr)) {
		for (int y = 0; y < m_AtlasHeight; ++y) {
			memcpy(
				static_cast<unsigned char*>(mapped.pData) + y * mapped.RowPitch,
				m_pAtlasRGBA + y * m_AtlasWidth * 4,
				m_AtlasWidth * 4
			);
		}
		GetDeviceContext()->Unmap(m_pTexture, 0);
	}
	else {
		m_pTexture->Release();
		m_pTexture = nullptr;
		free(m_pAtlasRGBA);
		free(m_pAtlasData);
		m_pAtlasRGBA = nullptr;
		m_pAtlasData = nullptr;
		m_Ready = false;
		return false;
	}

	hr = pDevice->CreateShaderResourceView(m_pTexture, nullptr, &m_pSRV);
	if (FAILED(hr)) {
		m_pTexture->Release();
		m_pTexture = nullptr;
		free(m_pAtlasRGBA);
		free(m_pAtlasData);
		m_pAtlasRGBA = nullptr;
		m_pAtlasData = nullptr;
		m_Ready = false;
		return false;
	}

	// 頂点バッファは RebuildMesh 時に必要サイズで確保する
	m_pVertexBuffer = nullptr;
	m_VertexCount = 0;
	m_VertexCapacity = 0;
	m_MeshDirty = true;
	return true;
}

int DrawFont::UTF8ToCodePoint(const std::string& text, size_t& index) {
	unsigned char c = (unsigned char)text[index];

	if ((c & 0x80) == 0) {
		index++;
		return (int)c;
	}

	if ((c & 0xE0) == 0xC0) {
		int codepoint = ((c & 0x1F) << 6) | ((unsigned char)text[index + 1] & 0x3F);
		index += 2;
		return codepoint;
	}

	if ((c & 0xF0) == 0xE0) {
		int codepoint = ((c & 0x0F) << 12) | (((unsigned char)text[index + 1] & 0x3F) << 6) | ((unsigned char)text[index + 2] & 0x3F);
		index += 3;
		return codepoint;
	}

	if ((c & 0xF8) == 0xF0) {
		int codepoint = ((c & 0x07) << 18) | (((unsigned char)text[index + 1] & 0x3F) << 12) | (((unsigned char)text[index + 2] & 0x3F) << 6) | ((unsigned char)text[index + 3] & 0x3F);
		index += 4;
		return codepoint;
	}

	index++;
	return 0;
}

bool DrawFont::AddGlyphToAtlas(int glyphIndex) {
	if (!m_Ready || !g_FtFace) {
		return false;
	}

	if (m_CharCache.find(glyphIndex) != m_CharCache.end()) {
		return true;
	}

	if ((int)m_CharCache.size() >= FONT_MAX_CACHE_GLYPHS) {
		EvictLRUGlyph();
	}

	if (!EnsurePixelSize()) {
		return false;
	}

	if (FT_Load_Glyph(g_FtFace, (FT_UInt)glyphIndex, FT_LOAD_RENDER) != 0) {
		return false;
	}

	FT_GlyphSlot slot = g_FtFace->glyph;
	FT_Bitmap& bitmap = slot->bitmap;

	int glyph_width = (int)bitmap.width;
	int glyph_height = (int)bitmap.rows;

	CharInfo info = {};
	info.glyphIndex = glyphIndex;
	info.xadvance = (float)slot->advance.x / 64.0f;
	info.bitmapLeft = (float)slot->bitmap_left;
	info.bitmapTop = (float)slot->bitmap_top;

	if (glyph_width == 0 || glyph_height == 0) {
		info.x0 = info.y0 = info.x1 = info.y1 = 0.0f;
		m_CharCache[glyphIndex] = info;
		m_CacheLRU.push_back(glyphIndex);
		return true;
	}

	if (m_AtlasNextX + glyph_width > m_AtlasWidth) {
		m_AtlasNextX = 0;
		m_AtlasNextY += m_AtlasRowHeight;
		m_AtlasRowHeight = 0;
	}

	if (m_AtlasNextY + glyph_height > m_AtlasHeight) {
		return false;
	}

	const int pitch = bitmap.pitch;
	for (int y = 0; y < glyph_height; y++) {
		const unsigned char* srcRow = bitmap.buffer + y * pitch;
		for (int x = 0; x < glyph_width; x++) {
			int atlas_idx = (m_AtlasNextY + y) * m_AtlasWidth + (m_AtlasNextX + x);
			m_pAtlasData[atlas_idx] = srcRow[x];
		}
	}

	info.x0 = (float)m_AtlasNextX;
	info.y0 = (float)m_AtlasNextY;
	info.x1 = (float)(m_AtlasNextX + glyph_width);
	info.y1 = (float)(m_AtlasNextY + glyph_height);

	m_CharCache[glyphIndex] = info;
	m_CacheLRU.push_back(glyphIndex);

	m_AtlasNextX += glyph_width;
	m_AtlasRowHeight = (std::max)(m_AtlasRowHeight, glyph_height);

	UpdateAtlasTexture();
	return true;
}

bool DrawFont::AddGlyphToAtlasBatch(int glyphIndex) {
	if (!m_Ready || !g_FtFace) {
		return false;
	}

	if (m_CharCache.find(glyphIndex) != m_CharCache.end()) {
		return false;
	}

	if ((int)m_CharCache.size() >= FONT_MAX_CACHE_GLYPHS) {
		EvictLRUGlyph();
	}

	if (!EnsurePixelSize()) {
		return false;
	}

	if (FT_Load_Glyph(g_FtFace, (FT_UInt)glyphIndex, FT_LOAD_RENDER) != 0) {
		return false;
	}

	FT_GlyphSlot slot = g_FtFace->glyph;
	FT_Bitmap& bitmap = slot->bitmap;

	int glyph_width = (int)bitmap.width;
	int glyph_height = (int)bitmap.rows;

	CharInfo info = {};
	info.glyphIndex = glyphIndex;
	info.xadvance = (float)slot->advance.x / 64.0f;
	info.bitmapLeft = (float)slot->bitmap_left;
	info.bitmapTop = (float)slot->bitmap_top;

	if (glyph_width == 0 || glyph_height == 0) {
		info.x0 = info.y0 = info.x1 = info.y1 = 0.0f;
		m_CharCache[glyphIndex] = info;
		m_CacheLRU.push_back(glyphIndex);
		return false;
	}

	if (m_AtlasNextX + glyph_width > m_AtlasWidth) {
		m_AtlasNextX = 0;
		m_AtlasNextY += m_AtlasRowHeight;
		m_AtlasRowHeight = 0;
	}

	if (m_AtlasNextY + glyph_height > m_AtlasHeight) {
		return false;
	}

	const int pitch = bitmap.pitch;
	for (int y = 0; y < glyph_height; y++) {
		const unsigned char* srcRow = bitmap.buffer + y * pitch;
		for (int x = 0; x < glyph_width; x++) {
			int atlas_idx = (m_AtlasNextY + y) * m_AtlasWidth + (m_AtlasNextX + x);
			m_pAtlasData[atlas_idx] = srcRow[x];
		}
	}

	info.x0 = (float)m_AtlasNextX;
	info.y0 = (float)m_AtlasNextY;
	info.x1 = (float)(m_AtlasNextX + glyph_width);
	info.y1 = (float)(m_AtlasNextY + glyph_height);

	m_CharCache[glyphIndex] = info;
	m_CacheLRU.push_back(glyphIndex);

	m_AtlasNextX += glyph_width;
	m_AtlasRowHeight = (std::max)(m_AtlasRowHeight, glyph_height);

	return true;
}

void DrawFont::EvictLRUGlyph() {
	if (m_CacheLRU.empty()) {
		return;
	}

	int lru_glyph = m_CacheLRU.front();
	m_CacheLRU.pop_front();
	m_CharCache.erase(lru_glyph);
}

void DrawFont::UpdateAtlasTexture() {
	ID3D11DeviceContext* pContext = GetDeviceContext();
	if (!pContext || !m_pTexture || !m_pAtlasData || !m_pAtlasRGBA) {
		return;
	}

	const unsigned char r = (unsigned char)(m_Color.x * 255.0f);
	const unsigned char g = (unsigned char)(m_Color.y * 255.0f);
	const unsigned char b = (unsigned char)(m_Color.z * 255.0f);
	const int pixelCount = m_AtlasWidth * m_AtlasHeight;
	for (int i = 0; i < pixelCount; i++) {
		m_pAtlasRGBA[i * 4 + 0] = r;
		m_pAtlasRGBA[i * 4 + 1] = g;
		m_pAtlasRGBA[i * 4 + 2] = b;
		m_pAtlasRGBA[i * 4 + 3] = m_pAtlasData[i];
	}

	D3D11_MAPPED_SUBRESOURCE msr;
	HRESULT hr = pContext->Map(m_pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
	if (SUCCEEDED(hr)) {
		if (msr.RowPitch == (UINT)(m_AtlasWidth * 4)) {
			memcpy(msr.pData, m_pAtlasRGBA, pixelCount * 4);
		} else {
			unsigned char* dst = (unsigned char*)msr.pData;
			const unsigned char* src = m_pAtlasRGBA;
			for (int y = 0; y < m_AtlasHeight; y++) {
				memcpy(dst + y * msr.RowPitch, src + y * m_AtlasWidth * 4, m_AtlasWidth * 4);
			}
		}
		pContext->Unmap(m_pTexture, 0);
	}
}

bool DrawFont::EnsureVertexCapacity(UINT vertexCount) {
	if (vertexCount == 0) {
		return true;
	}
	if (m_pVertexBuffer && m_VertexCapacity >= vertexCount) {
		return true;
	}

	ID3D11Device* pDevice = GetDevice();
	if (!pDevice) {
		return false;
	}

	UINT newCapacity = (std::max)(vertexCount, 64u);
	if (m_VertexCapacity > 0) {
		newCapacity = (std::max)(newCapacity, m_VertexCapacity * 2u);
	}

	if (m_pVertexBuffer) {
		m_pVertexBuffer->Release();
		m_pVertexBuffer = nullptr;
	}

	D3D11_BUFFER_DESC vbDesc = {};
	vbDesc.Usage = D3D11_USAGE_DYNAMIC;
	vbDesc.ByteWidth = sizeof(FontVertex) * newCapacity;
	vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	HRESULT hr = pDevice->CreateBuffer(&vbDesc, nullptr, &m_pVertexBuffer);
	if (FAILED(hr)) {
		m_VertexCapacity = 0;
		return false;
	}

	m_VertexCapacity = newCapacity;
	return true;
}

void DrawFont::RebuildMesh() {
	m_VertexCount = 0;
	m_MeshDirty = false;
	m_CachedPos = m_Position;
	m_CachedRot = m_Rotation;
	m_CachedScale = m_Scale;

	ID3D11DeviceContext* pContext = GetDeviceContext();
	if (!pContext || !m_pSRV || !m_Ready || !g_FtFace) {
		return;
	}
	if (!EnsurePixelSize()) {
		return;
	}

	// パス1: テキスト幅（アライメント用）
	float text_width = 0.0f;
	size_t temp_i = 0;
	int prev_glyph = 0;
	while (temp_i < m_Text.length()) {
		int codepoint = UTF8ToCodePoint(m_Text, temp_i);
		if (codepoint <= 0) continue;

		FT_UInt glyph_index = FT_Get_Char_Index(g_FtFace, (FT_ULong)codepoint);
		if (glyph_index == 0) continue;

		float kerning = GetKerningPx(prev_glyph, (int)glyph_index);

		if (!AddGlyphToAtlas((int)glyph_index)) continue;

		CharInfo& info = m_CharCache[(int)glyph_index];
		float actual_glyph_width = info.x1 - info.x0;
		float margin = actual_glyph_width * FONT_MARGIN_RATIO;
		text_width += kerning + info.xadvance + margin;
		prev_glyph = (int)glyph_index;
	}

	const float scX = GetScaleX();
	const float scY = GetScaleY();

	float draw_start_x = m_Position.x * DRAW_SCALE_X;
	if (m_Alignment == TA_MIDDLE) {
		draw_start_x -= (text_width * scX) / 2.0f;
	} else if (m_Alignment == TA_END) {
		draw_start_x -= (text_width * scX);
	}
	float draw_current_x = draw_start_x;
	const float draw_current_y = m_Position.y * DRAW_SCALE_Y;

	// 回転中心は Transform2D の位置（アライメント基準点）
	const float pivotX = m_Position.x * DRAW_SCALE_X;
	const float pivotY = draw_current_y;
	const float rad = XMConvertToRadians(m_Rotation);
	const float co = cosf(rad);
	const float si = sinf(rad);

	// 最大頂点数を見積もって一度だけ Map する（グリフあたり三角形2枚 = 6頂点）
	if (m_Text.empty()) {
		return;
	}
	UINT estimatedVerts = (UINT)(m_Text.length() * 6);
	if (estimatedVerts == 0 || !EnsureVertexCapacity(estimatedVerts) || !m_pVertexBuffer) {
		return;
	}

	D3D11_MAPPED_SUBRESOURCE msr;
	HRESULT hr = pContext->Map(m_pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
	if (FAILED(hr)) {
		return;
	}

	FontVertex* verts = (FontVertex*)msr.pData;
	UINT writeIndex = 0;
	size_t i = 0;
	int prev_glyph_draw = 0;

	while (i < m_Text.length()) {
		int codepoint = UTF8ToCodePoint(m_Text, i);
		if (codepoint <= 0) continue;

		FT_UInt glyph_index = FT_Get_Char_Index(g_FtFace, (FT_ULong)codepoint);
		if (glyph_index == 0) continue;

		float kerning = GetKerningPx(prev_glyph_draw, (int)glyph_index);
		draw_current_x += kerning * scX;

		if (!AddGlyphToAtlas((int)glyph_index)) continue;

		CharInfo& info = m_CharCache[(int)glyph_index];
		float actual_glyph_width = info.x1 - info.x0;
		float actual_glyph_height = info.y1 - info.y0;
		float margin = actual_glyph_width * FONT_MARGIN_RATIO;

		const bool isSpace = (codepoint == 0x0020 || codepoint == 0x3000);
		if (!isSpace && actual_glyph_width > 0.0f && actual_glyph_height > 0.0f) {
			if (writeIndex + 6 > m_VertexCapacity) {
				break;
			}

			float u0 = info.x0 / (float)m_AtlasWidth;
			float v0 = info.y0 / (float)m_AtlasHeight;
			float u1 = info.x1 / (float)m_AtlasWidth;
			float v1 = info.y1 / (float)m_AtlasHeight;

			float y0 = -info.bitmapTop;
			float y_offset = draw_current_y + (y0 + m_FontSize * FONT_OFFSET_Y * DRAW_SCALE_Y) * scY;
			float glyph_x = draw_current_x + info.bitmapLeft * scX;
			float char_pixel_width = actual_glyph_width * scX;
			float char_pixel_height = actual_glyph_height * scY;

			const float lx[4] = {
				glyph_x - pivotX,
				glyph_x + char_pixel_width - pivotX,
				glyph_x - pivotX,
				glyph_x + char_pixel_width - pivotX
			};
			const float ly[4] = {
				y_offset - pivotY,
				y_offset - pivotY,
				y_offset + char_pixel_height - pivotY,
				y_offset + char_pixel_height - pivotY
			};
			const XMFLOAT2 uv[4] = {
				{ u0, v0 }, { u1, v0 }, { u0, v1 }, { u1, v1 }
			};

			FontVertex quad[4];
			for (int qi = 0; qi < 4; ++qi) {
				float rx = lx[qi] * co - ly[qi] * si;
				float ry = lx[qi] * si + ly[qi] * co;
				quad[qi] = { { rx + pivotX, ry + pivotY, 0.0f }, { 0, 0, 0 }, m_Color, uv[qi] };
			}

			// 三角形リスト: 0-1-2, 1-3-2
			verts[writeIndex + 0] = quad[0];
			verts[writeIndex + 1] = quad[1];
			verts[writeIndex + 2] = quad[2];
			verts[writeIndex + 3] = quad[1];
			verts[writeIndex + 4] = quad[3];
			verts[writeIndex + 5] = quad[2];
			writeIndex += 6;
		}

		draw_current_x += (info.xadvance + margin) * scX;
		prev_glyph_draw = (int)glyph_index;
	}

	pContext->Unmap(m_pVertexBuffer, 0);
	m_VertexCount = writeIndex;
}

void DrawFont::Draw() {
	ID3D11DeviceContext* pContext = GetDeviceContext();
	ShaderManager* shader = GetShader(S_UNLIT);

	if (!pContext || !shader || !shader->GetVertexLayout() || !shader->GetVertexShader() || !shader->GetPixelShader()) {
		return;
	}

	if (!m_pSRV || !m_Ready || !g_FtFace) {
		return;
	}

	// Transform2D 側の変更は dirty フラグが立たないので、描画時に差分検出する
	if (m_Position.x != m_CachedPos.x || m_Position.y != m_CachedPos.y ||
		m_Rotation != m_CachedRot ||
		m_Scale.x != m_CachedScale.x || m_Scale.y != m_CachedScale.y) {
		m_MeshDirty = true;
	}

	if (m_MeshDirty) {
		RebuildMesh();
	}

	if (!m_pVertexBuffer || m_VertexCount == 0) {
		return;
	}

	SetWorldMatrix(XMMatrixIdentity());
	SetViewMatrix(XMMatrixIdentity());
	SetProjectionMatrix(XMMatrixOrthographicOffCenterLH(0.0f, DRAW_SCREEN_X, DRAW_SCREEN_Y, 0.0f, 0.0f, 1.0f));

	MATERIAL material = {};
	material.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	material.Ambient = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	SetMaterial(material);

	SetDepthEnable(false);

	pContext->IASetInputLayout(shader->GetVertexLayout());
	pContext->VSSetShader(shader->GetVertexShader(), NULL, 0);
	pContext->PSSetShader(shader->GetPixelShader(), NULL, 0);

	pContext->PSSetShaderResources(0, 1, &m_pSRV);
	SetBlendState(BLENDSTATE_ALFA);

	UINT stride = sizeof(FontVertex);
	UINT offset = 0;
	pContext->IASetVertexBuffers(0, 1, &m_pVertexBuffer, &stride, &offset);
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pContext->Draw(m_VertexCount, 0);
}

void DrawFont::SetText(const std::string& text) {
	if (m_Text != text) {
		m_Text = text;
		m_MeshDirty = true;
		RequestRedraw();
	}
}

void DrawFont::PreCacheGlyphs() {
	if (!m_Ready || !g_FtFace) return;
	if (!EnsurePixelSize()) return;

	bool atlasUpdated = false;
	size_t idx = 0;
	while (idx < m_Text.length()) {
		int codepoint = UTF8ToCodePoint(m_Text, idx);
		if (codepoint <= 0) continue;
		if (codepoint == 0x0020 || codepoint == 0x3000) continue;

		FT_UInt glyph_index = FT_Get_Char_Index(g_FtFace, (FT_ULong)codepoint);
		if (glyph_index == 0) continue;

		if (m_CharCache.find((int)glyph_index) != m_CharCache.end()) continue;

		if (AddGlyphToAtlasBatch((int)glyph_index)) {
			atlasUpdated = true;
		}
	}

	if (atlasUpdated) {
		UpdateAtlasTexture();
	}
}
