#include "font_atlas.h"
#include "graphics.h"
#include "cuda_renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FontAtlas g_fontAtlas;

static HFONT g_fontMain = nullptr;
static HFONT g_fontEmoji = nullptr;
static HFONT g_fontCJK = nullptr;

static unsigned int DecodeUTF16ToCodepoint(const WCHAR* src, int len, int* consumed) {
    if (len <= 0 || !src) {
        *consumed = 0;
        return 0;
    }
    WCHAR w = src[0];
    if (w >= 0xD800 && w <= 0xDBFF && len >= 2) {
        WCHAR w2 = src[1];
        if (w2 >= 0xDC00 && w2 <= 0xDFFF) {
            *consumed = 2;
            return 0x10000 + ((w - 0xD800) << 10) + (w2 - 0xDC00);
        }
    }
    *consumed = 1;
    return (unsigned int)w;
}

static int IsEmojiCodepoint(unsigned int cp) {
    if (cp >= 0x1F300 && cp <= 0x1F9FF) return 1;
    if (cp >= 0x1FA00 && cp <= 0x1FAFF) return 1;
    if (cp >= 0x2600 && cp <= 0x26FF) return 1;
    if (cp >= 0x2700 && cp <= 0x27BF) return 1;
    if (cp >= 0x1F600 && cp <= 0x1F64F) return 1;
    if (cp >= 0x1F680 && cp <= 0x1F6FF) return 1;
    if (cp >= 0x1F1E0 && cp <= 0x1F1FF) return 1;
    if (cp == 0x2764) return 1;
    if (cp == 0x2B50) return 1;
    if (cp == 0x2728) return 1;
    if (cp == 0x2705) return 1;
    if (cp == 0x274C) return 1;
    return 0;
}

static int IsFullWidthCodepoint(unsigned int cp) {
    if (cp >= 0x4E00 && cp <= 0x9FFF) return 1;
    if (cp >= 0x3400 && cp <= 0x4DBF) return 1;
    if (cp >= 0x20000 && cp <= 0x2A6DF) return 1;
    if (cp >= 0x2A700 && cp <= 0x2B73F) return 1;
    if (cp >= 0x2B740 && cp <= 0x2B81F) return 1;
    if (cp >= 0x2B820 && cp <= 0x2CEAF) return 1;
    if (cp >= 0xF900 && cp <= 0xFAFF) return 1;
    if (cp >= 0x2F800 && cp <= 0x2FA1F) return 1;
    if (cp >= 0x3040 && cp <= 0x30FF) return 1;
    if (cp >= 0x31F0 && cp <= 0x31FF) return 1;
    if (cp >= 0xAC00 && cp <= 0xD7AF) return 1;
    if (cp >= 0x1100 && cp <= 0x11FF) return 1;
    if (cp >= 0x3130 && cp <= 0x318F) return 1;
    if (cp >= 0xFF01 && cp <= 0xFF5E) return 1;
    return 0;
}

static HFONT SelectFontForCluster(const GraphemeCluster* g) {
    if (!g || g->wcharLen <= 0) return g_fontMain;
    int consumed;
    unsigned int cp = DecodeUTF16ToCodepoint(g->chars, g->wcharLen, &consumed);
    if (IsEmojiCodepoint(cp)) return g_fontEmoji;
    if (IsFullWidthCodepoint(cp)) return g_fontCJK;
    return g_fontMain;
}

// Truncation engine: returns a copy of the font scaled by the given factor
static HFONT CreateScaledFont(HFONT base, float scale) {
    LOGFONTW lf;
    if (!GetObjectW(base, sizeof(lf), &lf)) return nullptr;
    lf.lfHeight = (LONG)(lf.lfHeight * scale);
    lf.lfWidth = (LONG)(lf.lfWidth * scale);
    return CreateFontIndirectW(&lf);
}

void CleanupFontAtlas() {
    // The atlas texture is a registered CUDA interop resource; it must be
    // unregistered before the D3D texture is released
    UnregisterD3D11Resources();
    if (g_fontAtlas.texture) { g_fontAtlas.texture->Release(); g_fontAtlas.texture = nullptr; }
    if (g_fontAtlas.srv) { g_fontAtlas.srv->Release(); g_fontAtlas.srv = nullptr; }
    if (g_fontMain) { DeleteObject(g_fontMain); g_fontMain = nullptr; }
    if (g_fontEmoji) { DeleteObject(g_fontEmoji); g_fontEmoji = nullptr; }
    if (g_fontCJK) { DeleteObject(g_fontCJK); g_fontCJK = nullptr; }
}

bool BuildFontAtlas(int cellSize) {
    CleanupFontAtlas();

    int ri = g_config.currentRamp;
    if (ri < 0 || ri >= g_config.rampCount) ri = 0;
    UnicodeRamp* ramp = &g_config.ramps[ri];
    g_fontAtlas.count = ramp->count;
    g_fontAtlas.cellW = cellSize;
    g_fontAtlas.cellH = (int)(cellSize * FONT_ASPECT_RATIO);
    if (g_fontAtlas.cellH < 8) g_fontAtlas.cellH = 8;

    WCHAR selectedFont[64] = L"Segoe UI";
    if (g_config.fontName[0]) {
        MultiByteToWideChar(CP_UTF8, 0, g_config.fontName, -1, selectedFont, 64);
        selectedFont[63] = L'\0';
    }

    // Explicit advance width per font so glyphs fit their atlas cells exactly:
    // natural-width fonts at height 2*cellW are wider than the cell and get
    // unevenly clipped by ETO_CLIPPED, distorting proportions vs the source
    g_fontMain = CreateFontW(g_fontAtlas.cellH, g_fontAtlas.cellW, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, selectedFont);
    g_fontEmoji = CreateFontW(g_fontAtlas.cellH, g_fontAtlas.cellW * 2, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI Emoji");
    g_fontCJK = CreateFontW(g_fontAtlas.cellH, g_fontAtlas.cellW * 2, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"MS Gothic");

    // Calculate total width of the atlas
    int totalWidth = 0;
    for (int i = 0; i < ramp->count; i++) {
        g_fontAtlas.offsets[i] = totalWidth;
        int gw = g_fontAtlas.cellW * ramp->clusters[i].displayWidth;
        g_fontAtlas.widths[i] = gw;
        g_fontAtlas.displayWidths[i] = ramp->clusters[i].displayWidth;
        totalWidth += gw;
    }

    if (totalWidth <= 0) totalWidth = 1;

    // Create DIB Section to draw characters into
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = totalWidth;
    bmi.bmiHeader.biHeight = -g_fontAtlas.cellH; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    BYTE* pixels = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, (void**)&pixels, NULL, 0);
    if (!hBmp || !pixels) {
        if (hBmp) DeleteObject(hBmp);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        CleanupFontAtlas();
        return false;
    }
    HGDIOBJ hOldBmp = SelectObject(hdcMem, hBmp);

    // Clear bitmap to transparent black
    memset(pixels, 0, (size_t)totalWidth * g_fontAtlas.cellH * 4);

    SetBkMode(hdcMem, TRANSPARENT);
    SetTextColor(hdcMem, RGB(255, 255, 255));

    for (int i = 0; i < ramp->count; i++) {
        const GraphemeCluster* g = &ramp->clusters[i];
        if (g->wcharLen <= 0) continue;

        int cellX = g_fontAtlas.offsets[i];
        int totalWidthForCluster = g_fontAtlas.widths[i];

        // Truncation engine: measure the glyph against its cell and, if it
        // would be clipped, re-render it progressively scaled down until it
        // fits perfectly (max 6 attempts, floor 35%)
        HFONT baseFont = SelectFontForCluster(g);
        HFONT scaledFont = nullptr;
        float scale = 1.0f;
        SIZE textSize = {};
        for (int attempt = 0; attempt < 6; ++attempt) {
            SelectObject(hdcMem, scaledFont ? scaledFont : baseFont);
            GetTextExtentPoint32W(hdcMem, g->chars, g->wcharLen, &textSize);
            if (textSize.cx <= totalWidthForCluster && textSize.cy <= g_fontAtlas.cellH) {
                break;
            }
            float fitX = (float)totalWidthForCluster / (float)textSize.cx;
            float fitY = (float)g_fontAtlas.cellH / (float)textSize.cy;
            float fit = (fitX < fitY ? fitX : fitY) * 0.98f;
            scale *= fit;
            if (scaledFont) DeleteObject(scaledFont);
            scaledFont = CreateScaledFont(baseFont, scale);
            if (!scaledFont) {
                // Fall back to the unscaled font rather than leaving a
                // deleted font selected in the DC
                SelectObject(hdcMem, baseFont);
                GetTextExtentPoint32W(hdcMem, g->chars, g->wcharLen, &textSize);
                break;
            }
        }

        TEXTMETRICW tm;
        GetTextMetricsW(hdcMem, &tm);

        int offsetX = (totalWidthForCluster - textSize.cx) / 2;
        if (offsetX < 0) offsetX = 0;
        // Center by the font's actual height; textSize.cy includes leading
        // that varies per string and skews glyphs off-center vertically
        int offsetY = (g_fontAtlas.cellH - tm.tmHeight) / 2;

        RECT clipRect = { cellX, 0, cellX + totalWidthForCluster, g_fontAtlas.cellH };
        ExtTextOutW(hdcMem, cellX + offsetX, offsetY, ETO_CLIPPED, &clipRect, g->chars, g->wcharLen, NULL);

        if (scaledFont) DeleteObject(scaledFont);
    }

    // Set Alpha channel of pixels where color is drawn to 255 so CUDA/Direct3D knows it is visible
    // Wait, GDI doesn't write alpha, so it leaves alpha as 0. We set alpha to the max of R, G, B
    // for standard text, or 255 if any pixel is non-zero. Let's do that!
    for (int y = 0; y < g_fontAtlas.cellH; ++y) {
        for (int x = 0; x < totalWidth; ++x) {
            int idx = (y * totalWidth + x) * 4;
            BYTE b = pixels[idx];
            BYTE g = pixels[idx+1];
            BYTE r = pixels[idx+2];
            if (r > 0 || g > 0 || b > 0) {
                // If it's a standard text glyph, make it white mask
                pixels[idx+3] = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b); // alpha = max(r,g,b)
            } else {
                pixels[idx+3] = 0;
            }
        }
    }

    // Create GPU Texture2D
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = totalWidth;
    desc.Height = g_fontAtlas.cellH;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = pixels;
    initData.SysMemPitch = totalWidth * 4;

    HRESULT hr = g_graphics.device->CreateTexture2D(&desc, &initData, &g_fontAtlas.texture);
    if (SUCCEEDED(hr)) {
        hr = g_graphics.device->CreateShaderResourceView(g_fontAtlas.texture, nullptr, &g_fontAtlas.srv);
    }

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    return SUCCEEDED(hr);
}
