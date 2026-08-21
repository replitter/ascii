#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "menu.h"
#include "config.h"
#include "imgui/backends/imgui_impl_dx11.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/imgui.h"

#include <d3d11.h>
#include <windows.h>

#include <stdio.h>
#include <string.h>

// Action hooks owned by the main application.
extern void ActionSaveConfig();
extern void ActionLoadConfig();
extern void ActionResetAll();
extern void ActionResetLimits();
extern void ActionApplyChanges();
extern void ActionRelaunch();
extern int ActionImportConfig(const char *path);
extern void ActionRebuildRendererResources();
extern int ActionGetAsciiPaused();
extern void ActionSetAsciiPaused(int paused);

namespace {
static const int MAX_INSTALLED_FONTS = 512;
static const int UI_RECT_COUNT = 4;
static const int MIN_GLYPH_SPACING = -(MAX_CELL_SIZE - 1);
static const int MAX_GLYPH_SPACING = 64;
char g_installedFonts[MAX_INSTALLED_FONTS][64];
int g_installedFontCount = 0;
bool g_fontsLoaded = false;
bool g_imguiContextCreated = false;
bool g_win32BackendReady = false;
bool g_dx11BackendReady = false;
HWND g_menuHwnd = nullptr;
ID3D11Device *g_menuDevice = nullptr;
ID3D11ShaderResourceView *g_rampPreviewSrv = nullptr;
ID3D11Texture2D *g_rampPreviewTexture = nullptr;
unsigned int g_rampPreviewSignature = 0;
int g_rampPreviewRows = 0;
int g_rampPreviewW = 0;
int g_rampPreviewH = 0;
int g_rampEditorPage = 0;

enum UiRectSlot {
  UI_RECT_SETTINGS = 0,
  UI_RECT_HELP = 1,
  UI_RECT_METRICS = 2,
  UI_RECT_RAMP_EDITOR = 3
};

struct UiRect {
  bool active;
  float x;
  float y;
  float w;
  float h;
};

UiRect g_uiRects[UI_RECT_COUNT] = {};

int ClampInt(int value, int low, int high) {
  if (high < low)
    return low;
  if (value < low)
    return low;
  if (value > high)
    return high;
  return value;
}

float ClampFloat(float value, float low, float high) {
  if (high < low)
    return low;
  if (value < low)
    return low;
  if (value > high)
    return high;
  return value;
}

const char *SafeText(const char *text, const char *fallback = "n/a") {
  return (text && text[0]) ? text : fallback;
}

void ClearUiRects() {
  for (int i = 0; i < UI_RECT_COUNT; ++i) {
    g_uiRects[i].active = false;
    g_uiRects[i].x = 0.0f;
    g_uiRects[i].y = 0.0f;
    g_uiRects[i].w = 0.0f;
    g_uiRects[i].h = 0.0f;
  }
}

void TrackCurrentWindowRect(UiRectSlot slot) {
  if (slot < 0 || slot >= UI_RECT_COUNT)
    return;
  ImVec2 pos = ImGui::GetWindowPos();
  ImVec2 size = ImGui::GetWindowSize();
  g_uiRects[slot].active = true;
  g_uiRects[slot].x = pos.x;
  g_uiRects[slot].y = pos.y;
  g_uiRects[slot].w = size.x;
  g_uiRects[slot].h = size.y;
}

char LowerAsciiChar(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

int CompareIgnoreCase(const char *a, const char *b) {
  if (!a) a = "";
  if (!b) b = "";
  while (*a && *b) {
    char ca = LowerAsciiChar(*a++);
    char cb = LowerAsciiChar(*b++);
    if (ca != cb)
      return (int)(unsigned char)ca - (int)(unsigned char)cb;
  }
  return (int)(unsigned char)LowerAsciiChar(*a) -
         (int)(unsigned char)LowerAsciiChar(*b);
}

bool ContainsFont(const char *name) {
  for (int i = 0; i < g_installedFontCount; ++i) {
    if (CompareIgnoreCase(g_installedFonts[i], name) == 0)
      return true;
  }
  return false;
}

bool WideToUtf8(const wchar_t *value, char *out, int outSize, int wcharCount = -1) {
  if (out && outSize > 0)
    out[0] = '\0';
  if (!value)
    return false;

  int needed = WideCharToMultiByte(CP_UTF8, 0, value, wcharCount, nullptr, 0,
                                   nullptr, nullptr);
  if (needed <= 0 || !out || outSize <= 0)
    return false;

  int writable = needed < outSize ? needed : outSize - 1;
  int written = WideCharToMultiByte(CP_UTF8, 0, value, wcharCount, out,
                                    writable, nullptr, nullptr);
  if (written <= 0)
    return false;

  out[written] = '\0';
  return out[0] != '\0';
}

int SafeRampCount() { return ClampInt(g_config.rampCount, 0, MAX_RAMPS); }

bool HasValidRamp(int index) { return index >= 0 && index < SafeRampCount(); }

bool HasValidTheme(int index) { return index >= 0 && index < THEME_COUNT; }

int ClampSpacingForCell(int spacing, int glyphSize) {
  int minSpacing = 1 - ClampInt(glyphSize, MIN_CELL_SIZE, MAX_CELL_SIZE);
  return ClampInt(spacing, minSpacing, MAX_GLYPH_SPACING);
}

void SanitizeConfigForUi() {
  g_config.cellSize = ClampInt(g_config.cellSize, MIN_CELL_SIZE, MAX_CELL_SIZE);
  g_config.opacity = ClampInt(g_config.opacity, 0, 255);
  g_config.desktopCopyMode = g_config.desktopCopyMode ? 1 : 0;
  g_config.ambientMode = g_config.ambientMode ? 1 : 0;
  g_config.ambientFromSource = g_config.ambientFromSource ? 1 : 0;
  g_config.ambientFromRamp = g_config.ambientFromRamp ? 1 : 0;
  if (!g_config.ambientFromSource && !g_config.ambientFromRamp)
    g_config.ambientFromSource = 1;
  g_config.ambientLitPercent = ClampFloat(g_config.ambientLitPercent, 0.0f, 100.0f);
  g_config.ambientGlowStrength = ClampFloat(g_config.ambientGlowStrength, 0.0f, 5.0f);
  g_config.ambientSubdivisions = ClampInt(g_config.ambientSubdivisions, 1, 16);
  g_config.ambientRadius = ClampFloat(g_config.ambientRadius, 0.0f, 256.0f);
  g_config.ambientProgressiveBleed = ClampFloat(g_config.ambientProgressiveBleed, 0.0f, 1.0f);
  g_config.ambientColorMatch = ClampFloat(g_config.ambientColorMatch, 0.0f, 100.0f);
  g_config.targetFPS = ClampInt(g_config.targetFPS, 1, 240);
  g_config.frameBudgetMs = ClampFloat(g_config.frameBudgetMs, 1.0f, 100.0f);
  g_config.glyphSpacingX = ClampSpacingForCell(g_config.glyphSpacingX, g_config.cellSize);
  g_config.glyphSpacingY = ClampSpacingForCell(g_config.glyphSpacingY, g_config.cellSize);
  g_config.variableFontMode = g_config.variableFontMode ? 1 : 0;
  g_config.strictNoFontOverlap = g_config.strictNoFontOverlap ? 1 : 0;
  g_config.variableFontMinScale = ClampFloat(g_config.variableFontMinScale, 0.10f, 3.00f);
  g_config.variableFontMaxScale = ClampFloat(g_config.variableFontMaxScale, 0.10f, 3.00f);
  if (g_config.variableFontMaxScale < g_config.variableFontMinScale) {
    float t = g_config.variableFontMinScale;
    g_config.variableFontMinScale = g_config.variableFontMaxScale;
    g_config.variableFontMaxScale = t;
  }
  if (g_config.strictNoFontOverlap && g_config.variableFontMaxScale > 1.0f)
    g_config.variableFontMaxScale = 1.0f;
  g_config.variableFontRandomness = ClampFloat(g_config.variableFontRandomness, 0.0f, 1.0f);
  g_config.variableFontRegionSize = ClampInt(g_config.variableFontRegionSize, 1, 128);
  g_config.variableFontPulseSpeed = ClampFloat(g_config.variableFontPulseSpeed, 0.0f, 10.0f);
  g_config.variableFontIndependentAxes = g_config.variableFontIndependentAxes ? 1 : 0;
  g_config.variableFontMinWidthScale = ClampFloat(g_config.variableFontMinWidthScale, 0.10f, 3.00f);
  g_config.variableFontMaxWidthScale = ClampFloat(g_config.variableFontMaxWidthScale, 0.10f, 3.00f);
  g_config.variableFontMinHeightScale = ClampFloat(g_config.variableFontMinHeightScale, 0.10f, 3.00f);
  g_config.variableFontMaxHeightScale = ClampFloat(g_config.variableFontMaxHeightScale, 0.10f, 3.00f);
  if (g_config.variableFontMaxWidthScale < g_config.variableFontMinWidthScale) {
    float t = g_config.variableFontMinWidthScale;
    g_config.variableFontMinWidthScale = g_config.variableFontMaxWidthScale;
    g_config.variableFontMaxWidthScale = t;
  }
  if (g_config.variableFontMaxHeightScale < g_config.variableFontMinHeightScale) {
    float t = g_config.variableFontMinHeightScale;
    g_config.variableFontMinHeightScale = g_config.variableFontMaxHeightScale;
    g_config.variableFontMaxHeightScale = t;
  }
  if (g_config.strictNoFontOverlap) {
    if (g_config.variableFontMaxWidthScale > 1.0f) g_config.variableFontMaxWidthScale = 1.0f;
    if (g_config.variableFontMaxHeightScale > 1.0f) g_config.variableFontMaxHeightScale = 1.0f;
  }
  g_config.variableCellMode = g_config.variableCellMode ? 1 : 0;
  g_config.strictNoCellOverlap = g_config.strictNoCellOverlap ? 1 : 0;
  g_config.variableCellMinScale = ClampFloat(g_config.variableCellMinScale, 0.10f, 3.00f);
  g_config.variableCellMaxScale = ClampFloat(g_config.variableCellMaxScale, 0.10f, 3.00f);
  if (g_config.variableCellMaxScale < g_config.variableCellMinScale) {
    float t = g_config.variableCellMinScale;
    g_config.variableCellMinScale = g_config.variableCellMaxScale;
    g_config.variableCellMaxScale = t;
  }
  if (g_config.strictNoCellOverlap && g_config.variableCellMaxScale > 1.0f)
    g_config.variableCellMaxScale = 1.0f;
  g_config.variableCellRandomness = ClampFloat(g_config.variableCellRandomness, 0.0f, 1.0f);
  g_config.variableCellRegionSize = ClampInt(g_config.variableCellRegionSize, 1, 128);
  g_config.variableCellPulseSpeed = ClampFloat(g_config.variableCellPulseSpeed, 0.0f, 10.0f);
  g_config.variableCellAffectsSampling = g_config.variableCellAffectsSampling ? 1 : 0;
  g_config.variableCellAffectsFont = g_config.variableCellAffectsFont ? 1 : 0;
  g_config.experimentalCellSampling = g_config.experimentalCellSampling ? 1 : 0;
  g_config.cellSampleMode = ClampInt(g_config.cellSampleMode, 0, 2);
  g_config.cellSampleColorMode = ClampInt(g_config.cellSampleColorMode, 0, 3);
  g_config.cellSampleGrid = ClampInt(g_config.cellSampleGrid, 1, 50);
  g_config.cellSampleRadiusScale = ClampFloat(g_config.cellSampleRadiusScale, 0.10f, 3.0f);
  g_config.cellSampleEdgeBoost = ClampFloat(g_config.cellSampleEdgeBoost, 0.0f, 2.0f);
  g_config.cellSampleJitter = ClampFloat(g_config.cellSampleJitter, 0.0f, 1.0f);
  g_config.cellSampleCenterWeight = ClampFloat(g_config.cellSampleCenterWeight, 0.0f, 8.0f);
  g_config.cellSampleDetailMix = ClampFloat(g_config.cellSampleDetailMix, 0.0f, 1.0f);
  g_config.cellSampleLuminanceCompensation =
      ClampFloat(g_config.cellSampleLuminanceCompensation, 0.0f, 2.0f);
  g_config.cellSampleHighlightPreserve =
      ClampFloat(g_config.cellSampleHighlightPreserve, 0.0f, 1.0f);
  g_config.frameSkip = ClampInt(g_config.frameSkip, 0, 10);
  g_config.motionSensitivity = ClampInt(g_config.motionSensitivity, 0, 100);
  g_config.motionDecayMs = ClampFloat(g_config.motionDecayMs, 0.0f, 1000.0f);
  g_config.motionRampUpdateDurationMs =
      ClampInt(g_config.motionRampUpdateDurationMs, 0, 10000);
  if (g_config.motionMaxRampUpdates < 0)
    g_config.motionMaxRampUpdates = 0;
  g_config.motionMaxConcurrentPercent =
      ClampFloat(g_config.motionMaxConcurrentPercent, 0.0f, 100.0f);
  g_config.motionHoldUntilNewDraw = g_config.motionHoldUntilNewDraw ? 1 : 0;
  g_config.motionAutoKillStaticRamps = g_config.motionAutoKillStaticRamps ? 1 : 0;

  int rampCount = SafeRampCount();
  g_config.currentRamp =
      rampCount > 0 ? ClampInt(g_config.currentRamp, 0, rampCount - 1) : 0;
  g_config.colorTheme =
      ClampInt(g_config.colorTheme, 0, THEME_COUNT > 0 ? THEME_COUNT - 1 : 0);

  g_config.zoneX = ClampInt(g_config.zoneX, 0, 100);
  g_config.zoneY = ClampInt(g_config.zoneY, 0, 100);
  g_config.zoneW = ClampInt(g_config.zoneW, 0, 100);
  g_config.zoneH = ClampInt(g_config.zoneH, 0, 100);
}

int CALLBACK EnumFontFamProc(const LOGFONTW *lf, const TEXTMETRICW *, DWORD,
                             LPARAM) {
  if (!lf || !lf->lfFaceName[0])
    return 1;

  char name[64];
  if (!WideToUtf8(lf->lfFaceName, name, sizeof(name)) || ContainsFont(name) ||
      g_installedFontCount >= MAX_INSTALLED_FONTS)
    return 1;

  snprintf(g_installedFonts[g_installedFontCount],
                sizeof(g_installedFonts[g_installedFontCount]), "%s", name);
  g_installedFontCount++;
  return 1;
}

void LoadInstalledFonts() {
  if (g_fontsLoaded)
    return;

  g_installedFontCount = 0;

  HDC hdc = GetDC(nullptr);
  if (hdc) {
    LOGFONTW lf = {};
    lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(hdc, &lf,
                        reinterpret_cast<FONTENUMPROCW>(EnumFontFamProc), 0, 0);
    ReleaseDC(nullptr, hdc);
  }

  if (!ContainsFont("Consolas")) {
    snprintf(g_installedFonts[g_installedFontCount],
                  sizeof(g_installedFonts[g_installedFontCount]), "%s",
                  "Consolas");
    g_installedFontCount++;
  }

  for (int i = 0; i < g_installedFontCount - 1; ++i) {
    for (int j = i + 1; j < g_installedFontCount; ++j) {
      if (CompareIgnoreCase(g_installedFonts[i], g_installedFonts[j]) > 0) {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%s", g_installedFonts[i]);
        snprintf(g_installedFonts[i], sizeof(g_installedFonts[i]), "%s",
                      g_installedFonts[j]);
        snprintf(g_installedFonts[j], sizeof(g_installedFonts[j]), "%s",
                      tmp);
      }
    }
  }

  g_fontsLoaded = true;
}

bool EndsWithIni(const char *path) {
  if (!path)
    return false;
  int len = (int)strlen(path);
  return len >= 4 && CompareIgnoreCase(path + len - 4, ".ini") == 0;
}

void BuildPresetPath(const char *typedName, char *out, int outSize) {
  if (!out || outSize <= 0)
    return;
  out[0] = '\0';
  char name[160] = "";
  snprintf(name, sizeof(name), "%s", SafeText(typedName, "preset"));
  int start = 0;
  while (name[start] == ' ' || name[start] == '\t')
    start++;
  int end = (int)strlen(name);
  while (end > start && (name[end - 1] == ' ' || name[end - 1] == '\t'))
    name[--end] = '\0';
  const char *clean = name + start;
  if (!clean[0])
    clean = "preset";

  bool explicitPath = strchr(clean, '\\') || strchr(clean, '/') || strchr(clean, ':');
  if (!explicitPath) {
    CreateDirectoryA("presets", nullptr);
    snprintf(out, outSize, "presets\\%s", clean);
  } else {
    snprintf(out, outSize, "%s", clean);
  }

  if (!EndsWithIni(out)) {
    int len = (int)strlen(out);
    if (len < outSize - 4)
      snprintf(out + len, outSize - len, ".ini");
  }
}

void RampToUtf8ForEditor(const UnicodeRamp *ramp, char *out, int outSize) {
  if (!out || outSize <= 0)
    return;
  out[0] = '\0';
  if (!ramp)
    return;

  int pos = 0;
  for (int i = 0; i < ramp->count && pos < outSize - 16; ++i) {
    int written = WideCharToMultiByte(CP_UTF8, 0, ramp->clusters[i].chars,
                                      ramp->clusters[i].wcharLen, out + pos,
                                      outSize - pos - 1, nullptr, nullptr);
    if (written <= 0)
      continue;
    pos += written;
  }
  out[pos] = '\0';
}

unsigned int HashBytes(unsigned int h, const void *data, int len) {
  const unsigned char *p = (const unsigned char *)data;
  for (int i = 0; i < len; ++i) {
    h ^= p[i];
    h *= 16777619u;
  }
  return h;
}

unsigned int BuildRampPreviewSignature() {
  unsigned int h = 2166136261u;
  int rampCount = SafeRampCount();
  h = HashBytes(h, &rampCount, sizeof(rampCount));
  h = HashBytes(h, &g_config.cellSize, sizeof(g_config.cellSize));
  h = HashBytes(h, g_config.fontName, (int)strlen(g_config.fontName));
  for (int i = 0; i < rampCount; ++i) {
    const UnicodeRamp *ramp = &g_config.ramps[i];
    h = HashBytes(h, ramp->name, (int)strlen(ramp->name));
    h = HashBytes(h, &ramp->count, sizeof(ramp->count));
    int previewCount = ramp->count < 80 ? ramp->count : 80;
    for (int j = 0; j < previewCount; ++j) {
      h = HashBytes(h, &ramp->clusters[j].wcharLen, sizeof(ramp->clusters[j].wcharLen));
      h = HashBytes(h, ramp->clusters[j].chars,
                    ramp->clusters[j].wcharLen * (int)sizeof(WCHAR));
    }
  }
  return h ? h : 1u;
}

unsigned int DecodePreviewCodepoint(const GraphemeCluster *g) {
  if (!g || g->wcharLen <= 0)
    return 0;
  WCHAR w = g->chars[0];
  if (w >= 0xD800 && w <= 0xDBFF && g->wcharLen >= 2) {
    WCHAR w2 = g->chars[1];
    if (w2 >= 0xDC00 && w2 <= 0xDFFF)
      return 0x10000u + (((unsigned int)w - 0xD800u) << 10) +
             ((unsigned int)w2 - 0xDC00u);
  }
  return (unsigned int)w;
}

bool IsPreviewEmoji(unsigned int cp) {
  if (cp >= 0x1F300 && cp <= 0x1F9FF) return true;
  if (cp >= 0x1FA00 && cp <= 0x1FAFF) return true;
  if (cp >= 0x2600 && cp <= 0x27BF) return true;
  if (cp >= 0x1F600 && cp <= 0x1F64F) return true;
  if (cp >= 0x1F680 && cp <= 0x1F6FF) return true;
  if (cp >= 0x1F1E0 && cp <= 0x1F1FF) return true;
  return cp == 0x2764 || cp == 0x2B50 || cp == 0x2728 || cp == 0x2705 ||
         cp == 0x274C;
}

bool IsPreviewCjkOrFullWidth(unsigned int cp) {
  if (cp >= 0x4E00 && cp <= 0x9FFF) return true;
  if (cp >= 0x3040 && cp <= 0x30FF) return true;
  if (cp >= 0x31F0 && cp <= 0x31FF) return true;
  if (cp >= 0xAC00 && cp <= 0xD7AF) return true;
  if (cp >= 0x1100 && cp <= 0x11FF) return true;
  if (cp >= 0x3130 && cp <= 0x318F) return true;
  if (cp >= 0xFF01 && cp <= 0xFF5E) return true;
  if (cp >= 0x2580 && cp <= 0x259F) return true;
  if (cp >= 0x2800 && cp <= 0x28FF) return true;
  return false;
}

HFONT SelectPreviewFont(const GraphemeCluster *g, HFONT mainFont,
                        HFONT emojiFont, HFONT cjkFont) {
  unsigned int cp = DecodePreviewCodepoint(g);
  if (IsPreviewEmoji(cp)) return emojiFont;
  if (IsPreviewCjkOrFullWidth(cp)) return cjkFont;
  return mainFont;
}

void ReleaseRampPreviewTexture() {
  if (g_rampPreviewSrv) {
    g_rampPreviewSrv->Release();
    g_rampPreviewSrv = nullptr;
  }
  if (g_rampPreviewTexture) {
    g_rampPreviewTexture->Release();
    g_rampPreviewTexture = nullptr;
  }
  g_rampPreviewSignature = 0;
  g_rampPreviewRows = 0;
  g_rampPreviewW = 0;
  g_rampPreviewH = 0;
}

bool RebuildRampPreviewTexture() {
  if (!g_menuDevice)
    return false;
  unsigned int signature = BuildRampPreviewSignature();
  if (g_rampPreviewSrv && g_rampPreviewSignature == signature)
    return true;

  ReleaseRampPreviewTexture();

  int rampCount = SafeRampCount();
  if (rampCount <= 0)
    return false;

  const int previewW = 520;
  const int rowH = 28;
  const int previewH = rampCount * rowH;
  if (previewH <= 0)
    return false;

  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = previewW;
  bmi.bmiHeader.biHeight = -previewH;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  HDC screenDc = GetDC(nullptr);
  HDC memDc = CreateCompatibleDC(screenDc);
  BYTE *pixels = nullptr;
  HBITMAP bmp = CreateDIBSection(memDc, &bmi, DIB_RGB_COLORS, (void **)&pixels,
                                 nullptr, 0);
  if (!bmp || !pixels) {
    if (bmp) DeleteObject(bmp);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    return false;
  }

  HGDIOBJ oldBmp = SelectObject(memDc, bmp);
  memset(pixels, 0, (size_t)previewW * (size_t)previewH * 4);
  SetBkMode(memDc, TRANSPARENT);
  SetTextColor(memDc, RGB(245, 245, 250));

  WCHAR selectedFont[64] = L"Segoe UI";
  if (g_config.fontName[0]) {
    MultiByteToWideChar(CP_UTF8, 0, g_config.fontName, -1, selectedFont, 64);
    selectedFont[63] = L'\0';
  }

  int fontH = ClampInt(g_config.cellSize * 2, 14, 24);
  HFONT mainFont = CreateFontW(fontH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_TT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               FIXED_PITCH | FF_MODERN, selectedFont);
  HFONT emojiFont = CreateFontW(fontH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_TT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH, L"Segoe UI Emoji");
  HFONT cjkFont = CreateFontW(fontH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_TT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH, L"MS Gothic");

  for (int i = 0; i < rampCount; ++i) {
    const UnicodeRamp *ramp = &g_config.ramps[i];
    int y = i * rowH;
    int x = 6;
    RECT rowClip = {0, y, previewW, y + rowH};
    for (int j = 0; j < ramp->count && x < previewW - 8; ++j) {
      const GraphemeCluster *cluster = &ramp->clusters[j];
      if (cluster->wcharLen <= 0)
        continue;
      HFONT font = SelectPreviewFont(cluster, mainFont, emojiFont, cjkFont);
      HGDIOBJ oldFont = SelectObject(memDc, font);
      SIZE textSize = {};
      GetTextExtentPoint32W(memDc, cluster->chars, cluster->wcharLen, &textSize);
      if (textSize.cx <= 0) textSize.cx = fontH / 2;
      if (x + textSize.cx > previewW - 8) {
        SelectObject(memDc, oldFont);
        break;
      }
      int baselineY = y + (rowH - textSize.cy) / 2;
      ExtTextOutW(memDc, x, baselineY, ETO_CLIPPED, &rowClip, cluster->chars,
                  cluster->wcharLen, nullptr);
      x += textSize.cx + 2;
      SelectObject(memDc, oldFont);
    }
  }

  if (mainFont) DeleteObject(mainFont);
  if (emojiFont) DeleteObject(emojiFont);
  if (cjkFont) DeleteObject(cjkFont);

  for (int y = 0; y < previewH; ++y) {
    for (int x = 0; x < previewW; ++x) {
      int idx = (y * previewW + x) * 4;
      BYTE b = pixels[idx];
      BYTE g = pixels[idx + 1];
      BYTE r = pixels[idx + 2];
      BYTE a = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
      pixels[idx] = 245;
      pixels[idx + 1] = 245;
      pixels[idx + 2] = 250;
      pixels[idx + 3] = a;
    }
  }

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = previewW;
  desc.Height = previewH;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  D3D11_SUBRESOURCE_DATA initData = {};
  initData.pSysMem = pixels;
  initData.SysMemPitch = previewW * 4;

  HRESULT hr = g_menuDevice->CreateTexture2D(&desc, &initData,
                                             &g_rampPreviewTexture);
  if (SUCCEEDED(hr)) {
    hr = g_menuDevice->CreateShaderResourceView(g_rampPreviewTexture, nullptr,
                                                &g_rampPreviewSrv);
  }

  SelectObject(memDc, oldBmp);
  DeleteObject(bmp);
  DeleteDC(memDc);
  ReleaseDC(nullptr, screenDc);

  if (FAILED(hr)) {
    ReleaseRampPreviewTexture();
    return false;
  }

  g_rampPreviewSignature = signature;
  g_rampPreviewRows = rampCount;
  g_rampPreviewW = previewW;
  g_rampPreviewH = previewH;
  return true;
}
} // namespace

bool InitMenu(HWND hwnd, ID3D11Device *device, ID3D11DeviceContext *context) {
  if (!hwnd || !device || !context)
    return false;
  if (g_imguiContextCreated)
    return true;
  g_menuHwnd = hwnd;
  g_menuDevice = device;
  g_menuDevice->AddRef();

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  g_imguiContextCreated = true;

  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui::StyleColorsDark();

  ImGuiStyle &style = ImGui::GetStyle();
  style.WindowRounding = 6.0f;
  style.FrameRounding = 4.0f;
  style.GrabRounding = 4.0f;
  style.PopupRounding = 4.0f;
  style.ScrollbarRounding = 6.0f;
  style.WindowBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;
  style.ItemSpacing = ImVec2(8.0f, 6.0f);
  style.WindowPadding = ImVec2(12.0f, 12.0f);

  ImVec4 *colors = style.Colors;
  colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.97f, 1.00f);
  colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.56f, 0.62f, 1.00f);
  colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.11f, 0.96f);
  colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.14f, 0.80f);
  colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.14f, 0.98f);
  colors[ImGuiCol_Border] = ImVec4(0.22f, 0.22f, 0.28f, 1.00f);
  colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.15f, 0.21f, 1.00f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.31f, 1.00f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.28f, 0.43f, 1.00f);
  colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.15f, 0.24f, 1.00f);
  colors[ImGuiCol_CheckMark] = ImVec4(0.57f, 0.46f, 0.96f, 1.00f);
  colors[ImGuiCol_SliderGrab] = ImVec4(0.57f, 0.46f, 0.96f, 1.00f);
  colors[ImGuiCol_SliderGrabActive] = ImVec4(0.67f, 0.57f, 1.00f, 1.00f);
  colors[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.28f, 1.00f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.31f, 0.30f, 0.45f, 1.00f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.42f, 0.39f, 0.62f, 1.00f);
  colors[ImGuiCol_Header] = ImVec4(0.20f, 0.20f, 0.28f, 1.00f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.31f, 0.30f, 0.45f, 1.00f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.42f, 0.39f, 0.62f, 1.00f);
  colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.22f, 1.00f);
  colors[ImGuiCol_TabHovered] = ImVec4(0.31f, 0.30f, 0.45f, 1.00f);
  colors[ImGuiCol_TabActive] = ImVec4(0.25f, 0.24f, 0.38f, 1.00f);

  if (!ImGui_ImplWin32_Init(hwnd)) {
    CleanupMenu();
    return false;
  }
  g_win32BackendReady = true;

  if (!ImGui_ImplDX11_Init(device, context)) {
    CleanupMenu();
    return false;
  }
  g_dx11BackendReady = true;

  return true;
}

void CleanupMenu() {
  ReleaseRampPreviewTexture();

  if (g_dx11BackendReady) {
    ImGui_ImplDX11_Shutdown();
    g_dx11BackendReady = false;
  }

  if (g_win32BackendReady) {
    ImGui_ImplWin32_Shutdown();
    g_win32BackendReady = false;
  }

  if (g_imguiContextCreated && ImGui::GetCurrentContext()) {
    ImGui::DestroyContext();
    g_imguiContextCreated = false;
  }
  g_menuHwnd = nullptr;
  if (g_menuDevice) {
    g_menuDevice->Release();
    g_menuDevice = nullptr;
  }
  ClearUiRects();
}

void SetRampEditorPage(int page) {
  g_rampEditorPage = ClampInt(page, 0, 2);
}

static bool PassesFilter(const char *label, const char *search) {
  if (!search || !search[0])
    return true;
  if (!label || !label[0])
    return false;

  int searchLen = (int)strlen(search);
  if (searchLen <= 0)
    return true;

  for (const char *p = label; *p; ++p) {
    int i = 0;
    while (i < searchLen && p[i] &&
           LowerAsciiChar(p[i]) == LowerAsciiChar(search[i])) {
      ++i;
    }
    if (i == searchLen)
      return true;
  }
  return false;
}

static void DrawSettingHeader(const char *title) {
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::TextColored(ImVec4(0.70f, 0.65f, 0.95f, 1.0f), "%s",
                     SafeText(title, ""));
  ImGui::Separator();
}

static void DrawVariableFontControls() {
  bool enabled = g_config.variableFontMode != 0;
  if (ImGui::Checkbox("Variable Font", &enabled)) {
    g_config.variableFontMode = enabled ? 1 : 0;
  }
  bool strict = g_config.strictNoFontOverlap != 0;
  if (ImGui::Checkbox("Strict No Font Overlap", &strict)) {
    g_config.strictNoFontOverlap = strict ? 1 : 0;
    if (g_config.strictNoFontOverlap && g_config.variableFontMaxScale > 1.0f)
      g_config.variableFontMaxScale = 1.0f;
  }
  ImGui::TextWrapped("Variable Font assigns stable per-region glyph variation: size, optional width stretch, optional height stretch, randomness, seed, and pulse.");

  float minScalePct = g_config.variableFontMinScale * 100.0f;
  float maxScalePct = g_config.variableFontMaxScale * 100.0f;
  if (ImGui::SliderFloat("Minimum Font Size (%)", &minScalePct, 10.0f,
                         300.0f, "%.0f")) {
    g_config.variableFontMinScale = minScalePct / 100.0f;
  }
  float maxLimit = g_config.strictNoFontOverlap ? 100.0f : 300.0f;
  if (ImGui::SliderFloat("Maximum Font Size (%)", &maxScalePct, 10.0f,
                         maxLimit, "%.0f")) {
    g_config.variableFontMaxScale = maxScalePct / 100.0f;
  }
  if (g_config.variableFontMaxScale < g_config.variableFontMinScale)
    g_config.variableFontMaxScale = g_config.variableFontMinScale;
  if (g_config.strictNoFontOverlap && g_config.variableFontMaxScale > 1.0f)
    g_config.variableFontMaxScale = 1.0f;

  ImGui::SliderFloat("Randomness", &g_config.variableFontRandomness, 0.0f,
                     1.0f, "%.2f");
  ImGui::SliderInt("Region Size (cells)", &g_config.variableFontRegionSize, 1,
                   128);
  ImGui::InputInt("Random Seed", &g_config.variableFontSeed, 1, 17);
  ImGui::SliderFloat("Pulse Speed", &g_config.variableFontPulseSpeed, 0.0f,
                     10.0f, "%.2f");
  bool independentAxes = g_config.variableFontIndependentAxes != 0;
  if (ImGui::Checkbox("Independent Width / Height", &independentAxes)) {
    g_config.variableFontIndependentAxes = independentAxes ? 1 : 0;
  }
  if (g_config.variableFontIndependentAxes) {
    float minW = g_config.variableFontMinWidthScale * 100.0f;
    float maxW = g_config.variableFontMaxWidthScale * 100.0f;
    float minH = g_config.variableFontMinHeightScale * 100.0f;
    float maxH = g_config.variableFontMaxHeightScale * 100.0f;
    float axisMax = g_config.strictNoFontOverlap ? 100.0f : 300.0f;
    if (ImGui::SliderFloat("Minimum Width (%)", &minW, 10.0f, 300.0f, "%.0f"))
      g_config.variableFontMinWidthScale = minW / 100.0f;
    if (ImGui::SliderFloat("Maximum Width (%)", &maxW, 10.0f, axisMax, "%.0f"))
      g_config.variableFontMaxWidthScale = maxW / 100.0f;
    if (ImGui::SliderFloat("Minimum Height (%)", &minH, 10.0f, 300.0f, "%.0f"))
      g_config.variableFontMinHeightScale = minH / 100.0f;
    if (ImGui::SliderFloat("Maximum Height (%)", &maxH, 10.0f, axisMax, "%.0f"))
      g_config.variableFontMaxHeightScale = maxH / 100.0f;
  }
  if (ImGui::Button("Variable Font Defaults")) {
    g_config.variableFontMode = 0;
    g_config.strictNoFontOverlap = 1;
    g_config.variableFontMinScale = 0.75f;
    g_config.variableFontMaxScale = 1.0f;
    g_config.variableFontRandomness = 1.0f;
    g_config.variableFontRegionSize = 4;
    g_config.variableFontSeed = 1337;
    g_config.variableFontPulseSpeed = 0.0f;
    g_config.variableFontIndependentAxes = 0;
    g_config.variableFontMinWidthScale = 1.0f;
    g_config.variableFontMaxWidthScale = 1.0f;
    g_config.variableFontMinHeightScale = 1.0f;
    g_config.variableFontMaxHeightScale = 1.0f;
  }
}

static void DrawVariableCellControls() {
  bool enabled = g_config.variableCellMode != 0;
  if (ImGui::Checkbox("Variable Cell Size", &enabled)) {
    g_config.variableCellMode = enabled ? 1 : 0;
  }
  bool strict = g_config.strictNoCellOverlap != 0;
  if (ImGui::Checkbox("Strict No Cell Overlap", &strict)) {
    g_config.strictNoCellOverlap = strict ? 1 : 0;
    if (g_config.strictNoCellOverlap && g_config.variableCellMaxScale > 1.0f)
      g_config.variableCellMaxScale = 1.0f;
  }
  ImGui::TextWrapped("Variable cell size is virtual: it changes sampling footprint and optionally glyph scale while keeping the core grid stable.");

  bool affectsSampling = g_config.variableCellAffectsSampling != 0;
  if (ImGui::Checkbox("Affect Sampling Footprint", &affectsSampling))
    g_config.variableCellAffectsSampling = affectsSampling ? 1 : 0;
  bool affectsFont = g_config.variableCellAffectsFont != 0;
  if (ImGui::Checkbox("Affect Glyph Size", &affectsFont))
    g_config.variableCellAffectsFont = affectsFont ? 1 : 0;

  float minScalePct = g_config.variableCellMinScale * 100.0f;
  float maxScalePct = g_config.variableCellMaxScale * 100.0f;
  if (ImGui::SliderFloat("Minimum Cell Size (%)", &minScalePct, 10.0f,
                         300.0f, "%.0f")) {
    g_config.variableCellMinScale = minScalePct / 100.0f;
  }
  float maxLimit = g_config.strictNoCellOverlap ? 100.0f : 300.0f;
  if (ImGui::SliderFloat("Maximum Cell Size (%)", &maxScalePct, 10.0f,
                         maxLimit, "%.0f")) {
    g_config.variableCellMaxScale = maxScalePct / 100.0f;
  }
  if (g_config.variableCellMaxScale < g_config.variableCellMinScale)
    g_config.variableCellMaxScale = g_config.variableCellMinScale;
  if (g_config.strictNoCellOverlap && g_config.variableCellMaxScale > 1.0f)
    g_config.variableCellMaxScale = 1.0f;

  ImGui::SliderFloat("Cell Randomness", &g_config.variableCellRandomness, 0.0f,
                     1.0f, "%.2f");
  ImGui::SliderInt("Cell Region Size", &g_config.variableCellRegionSize, 1,
                   128);
  ImGui::InputInt("Cell Random Seed", &g_config.variableCellSeed, 1, 17);
  ImGui::SliderFloat("Cell Pulse Speed", &g_config.variableCellPulseSpeed,
                     0.0f, 10.0f, "%.2f");
  if (ImGui::Button("Variable Cell Defaults")) {
    g_config.variableCellMode = 0;
    g_config.strictNoCellOverlap = 1;
    g_config.variableCellMinScale = 0.75f;
    g_config.variableCellMaxScale = 1.0f;
    g_config.variableCellRandomness = 1.0f;
    g_config.variableCellRegionSize = 4;
    g_config.variableCellSeed = 7331;
    g_config.variableCellPulseSpeed = 0.0f;
    g_config.variableCellAffectsSampling = 1;
    g_config.variableCellAffectsFont = 0;
  }
}

static void DrawExperimentalSamplingControls() {
  bool enabled = g_config.experimentalCellSampling != 0;
  if (ImGui::Checkbox("Experimental Cell Supersampling", &enabled)) {
    g_config.experimentalCellSampling = enabled ? 1 : 0;
  }
  ImGui::TextWrapped("Supersampling averages multiple desktop pixels per cell, preserving more detail at larger cell sizes. Luminance compensation and highlight preserve can correct darker averaged cells. It costs extra CUDA time.");
  const char *modes[] = {"Grid", "Cross", "Diamond"};
  ImGui::Combo("Sample Pattern", &g_config.cellSampleMode, modes, 3);
  const char *colorModes[] = {"Average", "Bright Detail", "Dark Detail", "Contrast Detail"};
  ImGui::Combo("Sample Color Mode", &g_config.cellSampleColorMode, colorModes, 4);
  ImGui::SliderInt("Sample Grid", &g_config.cellSampleGrid, 1, 50);
  ImGui::SliderFloat("Sampling Radius Scale", &g_config.cellSampleRadiusScale,
                     0.10f, 3.0f, "%.2f");
  ImGui::SliderFloat("Edge/Contrast Boost", &g_config.cellSampleEdgeBoost,
                     0.0f, 2.0f, "%.2f");
  ImGui::SliderFloat("Stable Sample Jitter", &g_config.cellSampleJitter, 0.0f,
                     1.0f, "%.2f");
  ImGui::SliderFloat("Center Sample Weight", &g_config.cellSampleCenterWeight,
                     0.0f, 8.0f, "%.2f");
  ImGui::SliderFloat("Detail Mix", &g_config.cellSampleDetailMix, 0.0f,
                     1.0f, "%.2f");
  ImGui::SliderFloat("Luminance Compensation",
                     &g_config.cellSampleLuminanceCompensation, 0.0f, 2.0f,
                     "%.2f");
  ImGui::SliderFloat("Preserve Highlights",
                     &g_config.cellSampleHighlightPreserve, 0.0f, 1.0f,
                     "%.2f");
  if (ImGui::Button("Quality Preset Balanced")) {
    g_config.experimentalCellSampling = 1;
    g_config.cellSampleMode = 0;
    g_config.cellSampleColorMode = 0;
    g_config.cellSampleGrid = 3;
    g_config.cellSampleRadiusScale = 1.0f;
    g_config.cellSampleEdgeBoost = 0.25f;
    g_config.cellSampleJitter = 0.0f;
    g_config.cellSampleCenterWeight = 1.0f;
    g_config.cellSampleDetailMix = 0.20f;
    g_config.cellSampleLuminanceCompensation = 0.10f;
    g_config.cellSampleHighlightPreserve = 0.20f;
  }
  ImGui::SameLine();
  if (ImGui::Button("Quality Preset Heavy")) {
    g_config.experimentalCellSampling = 1;
    g_config.cellSampleMode = 0;
    g_config.cellSampleColorMode = 3;
    g_config.cellSampleGrid = 7;
    g_config.cellSampleRadiusScale = 1.15f;
    g_config.cellSampleEdgeBoost = 0.45f;
    g_config.cellSampleJitter = 0.10f;
    g_config.cellSampleCenterWeight = 1.5f;
    g_config.cellSampleDetailMix = 0.35f;
    g_config.cellSampleLuminanceCompensation = 0.18f;
    g_config.cellSampleHighlightPreserve = 0.35f;
  }
  ImGui::SameLine();
  if (ImGui::Button("Quality Preset Extreme")) {
    g_config.experimentalCellSampling = 1;
    g_config.cellSampleMode = 0;
    g_config.cellSampleColorMode = 3;
    g_config.cellSampleGrid = 15;
    g_config.cellSampleRadiusScale = 1.25f;
    g_config.cellSampleEdgeBoost = 0.65f;
    g_config.cellSampleJitter = 0.15f;
    g_config.cellSampleCenterWeight = 2.0f;
    g_config.cellSampleDetailMix = 0.50f;
    g_config.cellSampleLuminanceCompensation = 0.25f;
    g_config.cellSampleHighlightPreserve = 0.50f;
  }
  ImGui::SameLine();
  if (ImGui::Button("Quality Preset Max 50")) {
    g_config.experimentalCellSampling = 1;
    g_config.cellSampleMode = 0;
    g_config.cellSampleColorMode = 3;
    g_config.cellSampleGrid = 50;
    g_config.cellSampleRadiusScale = 1.35f;
    g_config.cellSampleEdgeBoost = 0.75f;
    g_config.cellSampleJitter = 0.20f;
    g_config.cellSampleCenterWeight = 2.5f;
    g_config.cellSampleDetailMix = 0.60f;
    g_config.cellSampleLuminanceCompensation = 0.35f;
    g_config.cellSampleHighlightPreserve = 0.70f;
  }
}

static void ClampMotionSettingsForUi() {
  g_config.motionMode = g_config.motionMode ? 1 : 0;
  g_config.motionSensitivity = ClampInt(g_config.motionSensitivity, 0, 100);
  g_config.motionDecayMs = ClampFloat(g_config.motionDecayMs, 0.0f, 1000.0f);
  g_config.motionRampUpdateDurationMs =
      ClampInt(g_config.motionRampUpdateDurationMs, 0, 10000);
  if (g_config.motionMaxRampUpdates < 0)
    g_config.motionMaxRampUpdates = 0;
  g_config.motionMaxConcurrentPercent =
      ClampFloat(g_config.motionMaxConcurrentPercent, 0.0f, 100.0f);
  g_config.motionHoldUntilNewDraw = g_config.motionHoldUntilNewDraw ? 1 : 0;
  g_config.motionAutoKillStaticRamps = g_config.motionAutoKillStaticRamps ? 1 : 0;
}

static void SetMotionPreset(int preset) {
  switch (preset) {
  case 0: // stable default
    g_config.motionSensitivity = 100;
    g_config.motionDecayMs = 240.0f;
    g_config.motionHoldUntilNewDraw = 1;
    g_config.motionAutoKillStaticRamps = 0;
    g_config.motionRampUpdateDurationMs = 1000;
    g_config.motionMaxRampUpdates = 3;
    g_config.motionMaxConcurrentPercent = 100.0f;
    break;
  case 1: // snappy
    g_config.motionSensitivity = 92;
    g_config.motionDecayMs = 110.0f;
    g_config.motionHoldUntilNewDraw = 1;
    g_config.motionAutoKillStaticRamps = 0;
    g_config.motionRampUpdateDurationMs = 750;
    g_config.motionMaxRampUpdates = 2;
    g_config.motionMaxConcurrentPercent = 100.0f;
    break;
  case 2: // long trails
    g_config.motionSensitivity = 88;
    g_config.motionDecayMs = 620.0f;
    g_config.motionHoldUntilNewDraw = 1;
    g_config.motionAutoKillStaticRamps = 0;
    g_config.motionRampUpdateDurationMs = 1000;
    g_config.motionMaxRampUpdates = 3;
    g_config.motionMaxConcurrentPercent = 85.0f;
    break;
  case 3: // advanced refresh-limited
    g_config.motionSensitivity = 95;
    g_config.motionDecayMs = 180.0f;
    g_config.motionHoldUntilNewDraw = 0;
    g_config.motionAutoKillStaticRamps = 0;
    g_config.motionRampUpdateDurationMs = 500;
    g_config.motionMaxRampUpdates = 2;
    g_config.motionMaxConcurrentPercent = 100.0f;
    break;
  }
  ClampMotionSettingsForUi();
  ActionResetLimits();
}

static void DrawMotionModeEditor(const char *search, bool compact) {
  ClampMotionSettingsForUi();
  bool showAll = !search || !search[0] || compact;
  bool anyMotionFilter = PassesFilter(
      "motion mode ctrl h detection sensitivity decay hold ramps new draw "
      "auto kill static ramps refresh limit max individual concurrent preset stable snappy trails "
      "black box colored character opacity zero",
      search);
  if (!showAll && !anyMotionFilter)
    return;

  if (showAll || PassesFilter("Motion Mode Enable ctrl h", search)) {
    bool enabled = g_config.motionMode != 0;
    if (ImGui::Checkbox("Enable Motion Mode (Ctrl+H)", &enabled)) {
      g_config.motionMode = enabled ? 1 : 0;
      ActionResetLimits();
    }
    ImGui::TextWrapped("When enabled, background opacity is forced to 0. Motion cells render as black boxes with colored UTF-8 glyphs; non-motion cells stay transparent.");
  }

  if (showAll || PassesFilter("Motion Presets stable snappy trails advanced", search)) {
    if (ImGui::Button("Stable")) {
      SetMotionPreset(0);
    }
    ImGui::SameLine();
    if (ImGui::Button("Snappy")) {
      SetMotionPreset(1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Long Trails")) {
      SetMotionPreset(2);
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh-Limited")) {
      SetMotionPreset(3);
    }
  }

  if (showAll || PassesFilter("Motion Sensitivity detection threshold ctrl left right", search)) {
    int sensitivity = g_config.motionSensitivity;
    if (ImGui::SliderInt("Sensitivity", &sensitivity, 0, 100)) {
      g_config.motionSensitivity = ClampInt(sensitivity, 0, 100);
    }
    ImGui::TextDisabled("Higher = easier to trigger. Ctrl+Left/Right adjusts this while Motion Mode is active.");
  }

  if (showAll || PassesFilter("Motion Decay visibility ms trail hold screen", search)) {
    float decay = g_config.motionDecayMs;
    if (ImGui::SliderFloat("Visible Decay (ms)", &decay, 0.0f, 1000.0f, "%.2f")) {
      g_config.motionDecayMs = ClampFloat(decay, 0.0f, 1000.0f);
    }
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::InputFloat("Visible Decay Exact", &g_config.motionDecayMs, 1.0f,
                          10.0f, "%.2f")) {
      g_config.motionDecayMs = ClampFloat(g_config.motionDecayMs, 0.0f, 1000.0f);
    }
    ImGui::TextDisabled("This controls how long a cell remains visible after motion is detected.");
  }

  if (showAll || PassesFilter("Hold Ramps Until New Draw anti flicker low ms keep ramp", search)) {
    bool hold = g_config.motionHoldUntilNewDraw != 0;
    if (ImGui::Checkbox("Hold Ramp Until New Draw", &hold)) {
      g_config.motionHoldUntilNewDraw = hold ? 1 : 0;
      ActionResetLimits();
    }
    ImGui::TextDisabled(g_config.motionHoldUntilNewDraw
                            ? "Active: visible cells stay on-screen and only refresh on a fresh motion event."
                            : "Off: ramp/color may refresh while the cell remains visible, using the limiter below.");
  }

  if (showAll || PassesFilter("Auto Kill Static Ramps hold stale stuck ui video decay", search)) {
    bool autoKill = g_config.motionAutoKillStaticRamps != 0;
    if (ImGui::Checkbox("Auto Kill Static Ramps", &autoKill)) {
      g_config.motionAutoKillStaticRamps = autoKill ? 1 : 0;
      ActionResetLimits();
    }
    ImGui::TextDisabled(g_config.motionHoldUntilNewDraw
                            ? "Active with hold: cells that stop detecting motion expire using Visible Decay."
                            : "Armed, but only affects Motion Hold mode.");
  }

  if (!g_config.motionHoldUntilNewDraw) {
    if (showAll || PassesFilter("Motion Ramp Update Duration window ms limit reset", search)) {
      int windowMs = g_config.motionRampUpdateDurationMs;
      if (ImGui::SliderInt("Refresh Window (ms)", &windowMs, 0, 10000)) {
        g_config.motionRampUpdateDurationMs = ClampInt(windowMs, 0, 10000);
      }
      ImGui::SetNextItemWidth(180.0f);
      if (ImGui::InputInt("Refresh Window Exact", &g_config.motionRampUpdateDurationMs,
                          10, 100)) {
        g_config.motionRampUpdateDurationMs =
            ClampInt(g_config.motionRampUpdateDurationMs, 0, 10000);
      }
    }
    if (showAll || PassesFilter("Motion Max Individual Ramp Refreshes textbox slider unlimited zero update cap", search)) {
      int sliderValue = ClampInt(g_config.motionMaxRampUpdates, 0, 1000);
      if (ImGui::SliderInt("Max Refreshes Per Cell", &sliderValue, 0, 1000)) {
        g_config.motionMaxRampUpdates = sliderValue;
      }
      ImGui::SetNextItemWidth(180.0f);
      if (ImGui::InputInt("Max Refreshes Exact", &g_config.motionMaxRampUpdates,
                          1, 100)) {
        if (g_config.motionMaxRampUpdates < 0)
          g_config.motionMaxRampUpdates = 0;
      }
      ImGui::TextDisabled("0 means unlimited. Positive values cap each cell until the refresh window resets.");
    }
  } else if (showAll || PassesFilter("refresh window max refreshes inactive hold new draw", search)) {
    ImGui::TextDisabled("Refresh Window and Max Refreshes are inactive while Hold Ramp Until New Draw is enabled.");
  }

  if (showAll || PassesFilter("Motion Max Concurrent Ramps percentage percent precision textbox onscreen", search)) {
    float percent = g_config.motionMaxConcurrentPercent;
    if (ImGui::SliderFloat("Max Concurrent Screen Cells (%)", &percent, 0.0f,
                           100.0f, "%.2f")) {
      g_config.motionMaxConcurrentPercent = ClampFloat(percent, 0.0f, 100.0f);
    }
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::InputFloat("Concurrent Cells Exact", &g_config.motionMaxConcurrentPercent,
                          0.01f, 1.0f, "%.2f")) {
      g_config.motionMaxConcurrentPercent =
          ClampFloat(g_config.motionMaxConcurrentPercent, 0.0f, 100.0f);
    }
  }

  if (showAll || PassesFilter("Motion reset defaults stable remastered cache", search)) {
    if (ImGui::Button("Reset Motion Defaults")) {
      SetMotionPreset(0);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Motion Cache")) {
      ActionResetLimits();
    }
  }
  ClampMotionSettingsForUi();
}

static void DrawSearchableSettings(bool &showSettings) {
  SanitizeConfigForUi();
  static char search[128] = "";
  ImGui::SetNextWindowSize(ImVec2(720, 680), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Settings", &showSettings, ImGuiWindowFlags_NoCollapse)) {
    TrackCurrentWindowRect(UI_RECT_SETTINGS);
    ImGui::End();
    return;
  }
  TrackCurrentWindowRect(UI_RECT_SETTINGS);

  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputTextWithHint("##SettingsSearch", "Search every setting...",
                           search, sizeof(search));
  ImGui::BeginChild("SettingsScroll", ImVec2(0, 0), true,
                    ImGuiWindowFlags_AlwaysVerticalScrollbar);

  if (PassesFilter("display renderer mode desktop copy cell size background opacity current ramp target "
                   "fps frame budget frame skip font selector installed fonts variable font strict overlap",
                   search)) {
    DrawSettingHeader("Display");
  }
  if (PassesFilter("Renderer Mode desktop copy pass through ctrl o utf8 glyph", search)) {
    bool copyMode = g_config.desktopCopyMode != 0;
    if (ImGui::Checkbox("Desktop Copy Mode (Ctrl+O)", &copyMode)) {
      g_config.desktopCopyMode = copyMode ? 1 : 0;
      ActionResetLimits();
    }
    ImGui::TextWrapped("Desktop Copy Mode samples the actual visible desktop output in real time, then broadcasts that pixel feed through the renderer with filters, themes, motion, heat, ramp limits, color limits, zones, and FPS controls.");
  }
  if (PassesFilter("Ambient Mode ctrl a glow lit pixels subdivision radius progressive bleed color match source ramp",
                   search)) {
    DrawSettingHeader("Ambient Mode");
    bool ambient = g_config.ambientMode != 0;
    if (ImGui::Checkbox("Ambient Mode (Ctrl+A)", &ambient)) {
      g_config.ambientMode = ambient ? 1 : 0;
    }
    ImGui::TextWrapped("Ambient Mode lights a stable percentage of samples and bleeds their glow through the current renderer.");
    bool fromSource = g_config.ambientFromSource != 0;
    if (ImGui::Checkbox("Ambient From Source Pixels", &fromSource)) {
      g_config.ambientFromSource = fromSource ? 1 : 0;
      if (!g_config.ambientFromSource && !g_config.ambientFromRamp)
        g_config.ambientFromRamp = 1;
    }
    bool fromRamp = g_config.ambientFromRamp != 0;
    if (ImGui::Checkbox("Ambient Mode From Ramp", &fromRamp)) {
      g_config.ambientFromRamp = fromRamp ? 1 : 0;
      if (!g_config.ambientFromSource && !g_config.ambientFromRamp)
        g_config.ambientFromSource = 1;
    }
    ImGui::SliderFloat("Lit Pixels (%)", &g_config.ambientLitPercent, 0.0f,
                       100.0f, "%.2f");
    ImGui::SliderFloat("Glow Strength", &g_config.ambientGlowStrength, 0.0f,
                       5.0f, "%.2f");
    ImGui::SliderInt("Subdivisions", &g_config.ambientSubdivisions, 1, 16);
    ImGui::SliderFloat("Glow Radius (px)", &g_config.ambientRadius, 0.0f,
                       256.0f, "%.1f");
    ImGui::SliderFloat("Progressive Bleed", &g_config.ambientProgressiveBleed,
                       0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Color Match (%)", &g_config.ambientColorMatch, 0.0f,
                       100.0f, "%.1f");
  }
  if (PassesFilter("Cell Size display resolution density ctrl left right non motion", search)) {
    int cSize = g_config.cellSize;
    if (ImGui::SliderInt("Cell Size", &cSize, MIN_CELL_SIZE, MAX_CELL_SIZE)) {
      g_config.cellSize = cSize;
      ActionRebuildRendererResources();
    }
  }
  if (PassesFilter("variable font strict no font overlap random regions scale min max width height pulse seed ctrl tilde",
                   search)) {
    DrawSettingHeader("Variable Font");
    DrawVariableFontControls();
  }
  if (PassesFilter("variable cell size strict no cell overlap virtual sampling glyph footprint scale random regions seed pulse",
                   search)) {
    DrawSettingHeader("Variable Cell Size");
    DrawVariableCellControls();
  }
  if (PassesFilter("experimental cell supersampling quality sample grid cross diamond radius edge contrast jitter large cell detail luminance compensation preserve highlights",
                   search)) {
    DrawSettingHeader("Experimental Cell Sampling");
    DrawExperimentalSamplingControls();
  }
  if (PassesFilter("Background Opacity terminal black backdrop", search)) {
    int op = g_config.opacity;
    if (ImGui::SliderInt("Background Opacity", &op, 0, 255)) {
      g_config.opacity = op;
      ActionApplyChanges();
    }
  }
  if (PassesFilter("Current Ramp utf8 unicode ramp glyph set", search)) {
    const int rampCount = SafeRampCount();
    if (rampCount > 0) {
      const char *rampNames[MAX_RAMPS];
      for (int i = 0; i < rampCount; ++i) {
        rampNames[i] = SafeText(g_config.ramps[i].name, "Unnamed Ramp");
      }

      int curRamp = ClampInt(g_config.currentRamp, 0, rampCount - 1);
      if (ImGui::Combo("Current Ramp", &curRamp, rampNames, rampCount)) {
        g_config.currentRamp = curRamp;
        ActionRebuildRendererResources();
      }
    } else {
      ImGui::TextDisabled("No UTF-8 ramps are loaded.");
    }
  }
  if (PassesFilter("Font selector installed font family utf8 unicode glyph typeface",
                   search)) {
    LoadInstalledFonts();
    if (ImGui::BeginCombo("Font", g_config.fontName[0] ? g_config.fontName
                                                       : "Consolas")) {
      for (int i = 0; i < g_installedFontCount; ++i) {
        bool selected = CompareIgnoreCase(SafeText(g_config.fontName, ""),
                                          g_installedFonts[i]) == 0;
        if (ImGui::Selectable(g_installedFonts[i], selected)) {
          snprintf(g_config.fontName, sizeof(g_config.fontName), "%s",
                        g_installedFonts[i]);
          ActionRebuildRendererResources();
        }
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
  }
  if (PassesFilter("Target FPS frame rate timer", search)) {
    int fps = g_config.targetFPS;
    if (ImGui::SliderInt("Target FPS", &fps, 1, 240)) {
      g_config.targetFPS = fps;
      ActionApplyChanges();
    }
  }
  if (PassesFilter("Frame Budget budget ms profiler", search)) {
    ImGui::SliderFloat("Frame Budget (ms)", &g_config.frameBudgetMs, 1.0f,
                       100.0f, "%.2f");
  }
  if (PassesFilter("Frame Skip performance cadence", search)) {
    ImGui::SliderInt("Frame Skip", &g_config.frameSkip, 0, 10);
  }

  if (PassesFilter("runtime pause utf8 ramp frame ctrl p freeze characters not screen", search))
    DrawSettingHeader("Runtime");
  if (PassesFilter("UTF-8 Pause ctrl p pause unpause ramp frame freeze characters not screen", search)) {
    bool paused = ActionGetAsciiPaused() != 0;
    if (ImGui::Checkbox("UTF-8 Pause", &paused)) {
      ActionSetAsciiPaused(paused ? 1 : 0);
    }
    ImGui::TextWrapped("Ctrl+P pauses only the UTF-8 glyph/ramp frame. The screen capture and background keep updating.");
  }

  if (PassesFilter("motion mode detection sensitivity ctrl h moving utf8 unicode "
                   "previous frame delta black box colored character opacity zero",
                   search))
    DrawSettingHeader("Motion Mode");
  DrawMotionModeEditor(search, false);

  if (PassesFilter("heat mode style circle square diamond soft box bar radius "
                   "brightness black utf8 hidden glyph",
                   search))
    DrawSettingHeader("Heat Mode");
  if (PassesFilter("Heat Mode Enable toggle glow filled", search)) {
    bool v = g_config.heatMode != 0;
    if (ImGui::Checkbox("Heat Mode Enable", &v))
      g_config.heatMode = v ? 1 : 0;
  }
  if (PassesFilter("Heat Radius", search))
    ImGui::SliderFloat("Heat Radius", &g_config.heatRadius, 0.0f, 1.0f);
  if (PassesFilter("Heat Brightness", search))
    ImGui::SliderFloat("Heat Brightness", &g_config.heatBrightness, 0.0f, 3.0f);
  if (PassesFilter("Heat Style circle square diamond soft box bar", search))
    ImGui::Combo("Heat Style", &g_config.heatStyle, HEAT_STYLE_NAMES,
                 HEAT_STYLE_COUNT);
  if (PassesFilter("Heat Glyph Mode black utf8 hidden no character", search))
    ImGui::Combo("Heat Glyph Mode", &g_config.heatGlyphMode,
                 HEAT_GLYPH_MODE_NAMES, HEAT_GLYPH_COUNT);

  if (PassesFilter("capture zone x y width height", search))
    DrawSettingHeader("Capture Zone");
  if (PassesFilter("Zone Enable capture area", search)) {
    bool v = g_config.zoneEnable != 0;
    if (ImGui::Checkbox("Zone Enable", &v))
      g_config.zoneEnable = v ? 1 : 0;
  }
  if (PassesFilter("Zone X", search))
    ImGui::SliderInt("Zone X %", &g_config.zoneX, 0, 100);
  if (PassesFilter("Zone Y", search))
    ImGui::SliderInt("Zone Y %", &g_config.zoneY, 0, 100);
  if (PassesFilter("Zone W width", search))
    ImGui::SliderInt("Zone W %", &g_config.zoneW, 0, 100);
  if (PassesFilter("Zone H height", search))
    ImGui::SliderInt("Zone H %", &g_config.zoneH, 0, 100);

  if (PassesFilter("ASCII Random percent density coverage scatter cells", search))
    DrawSettingHeader("ASCII Random");
  if (PassesFilter("ASCII Random percent density coverage scatter cells", search)) {
    float rp = g_config.asciiRandomPercent;
    if (ImGui::SliderFloat("ASCII Random %", &rp, 1.0f, 100.0f, "%.8f %%")) {
      if (rp < 1.0f) rp = 1.0f;
      if (rp > 100.0f) rp = 100.0f;
      g_config.asciiRandomPercent = rp;
    }
  }

  if (PassesFilter("filters colors theme invert grayscale brightness contrast "
                   "gamma luminance",
                   search))
    DrawSettingHeader("Filters & Colors");
  if (PassesFilter("Color Theme palette", search)) {
    if (THEME_COUNT > 0) {
      ImGui::Combo("Color Theme", &g_config.colorTheme, THEME_NAMES,
                   THEME_COUNT);
    } else {
      ImGui::TextDisabled("No color themes are loaded.");
    }
  }
  if (PassesFilter("Invert Colors", search)) {
    bool v = g_config.invert != 0;
    if (ImGui::Checkbox("Invert Colors", &v))
      g_config.invert = v ? 1 : 0;
  }
  if (PassesFilter("Invert Random percent cells scatter partial", search)) {
    float rp = g_config.invertRandomPercent;
    if (ImGui::SliderFloat("Invert Random %", &rp, 1.0f, 100.0f, "%.8f %%")) {
      if (rp < 1.0f) rp = 1.0f;
      if (rp > 100.0f) rp = 100.0f;
      g_config.invertRandomPercent = rp;
    }
  }
  if (PassesFilter("Grayscale Random percent cells scatter partial", search)) {
    float rp = g_config.grayscaleRandomPercent;
    if (ImGui::SliderFloat("Grayscale Random %", &rp, 1.0f, 100.0f, "%.8f %%")) {
      if (rp < 1.0f) rp = 1.0f;
      if (rp > 100.0f) rp = 100.0f;
      g_config.grayscaleRandomPercent = rp;
    }
  }
  if (PassesFilter("Theme Random percent retrowave palette cells scatter partial", search)) {
    float rp = g_config.themeRandomPercent;
    if (ImGui::SliderFloat("Theme Random %", &rp, 1.0f, 100.0f, "%.8f %%")) {
      if (rp < 1.0f) rp = 1.0f;
      if (rp > 100.0f) rp = 100.0f;
      g_config.themeRandomPercent = rp;
    }
  }
  if (PassesFilter("Random Effects Exclusive prevent stealing overlap share cells", search)) {
    bool v = g_config.randomEffectsExclusive != 0;
    if (ImGui::Checkbox("Random Effects Exclusive", &v))
      g_config.randomEffectsExclusive = v ? 1 : 0;
    ImGui::TextDisabled("Random effects never claim the same cells");
  }
  if (PassesFilter("Grayscale Mode", search)) {
    bool v = g_config.grayscale != 0;
    if (ImGui::Checkbox("Grayscale Mode", &v))
      g_config.grayscale = v ? 1 : 0;
  }
  if (PassesFilter("Brightness", search))
    ImGui::SliderFloat("Brightness", &g_config.brightness, 0.1f, 3.0f);
  if (PassesFilter("Contrast", search))
    ImGui::SliderFloat("Contrast", &g_config.contrast, 0.1f, 3.0f);
  if (PassesFilter("Gamma", search))
    ImGui::SliderFloat("Gamma", &g_config.gamma, 0.5f, 2.0f);
  if (PassesFilter("Final Luminance Multiplier base lum", search))
    ImGui::SliderFloat("Final Luminance Multiplier",
                       &g_config.finalLuminanceMultiplier, 0.0f, 10.0f);

  if (PassesFilter("luminance sequence random favour pattern parse", search))
    DrawSettingHeader("Luminance Sequence");
  if (PassesFilter("Luminance Sequence Enabled", search)) {
    bool v = g_config.luminanceSeqEnabled != 0;
    if (ImGui::Checkbox("Luminance Sequence Enabled", &v))
      g_config.luminanceSeqEnabled = v ? 1 : 0;
  }
  if (PassesFilter("Luminance Change Time", search))
    ImGui::SliderInt("Luminance Change Time (ms)",
                     &g_config.luminanceChangeTime, 50, 10000);
  if (PassesFilter("Sequence Pattern", search)) {
    ImGui::InputText("Sequence Pattern", g_config.luminanceSeqRaw,
                     sizeof(g_config.luminanceSeqRaw));
    if (ImGui::Button("Parse Pattern"))
      ParseLuminanceSequence(g_config.luminanceSeqRaw);
  }

  if (PassesFilter("limits color ramp refresh threshold temporal", search))
    DrawSettingHeader("Temporal Limits");
  if (PassesFilter("Color Limit Enable", search)) {
    bool v = g_config.enableColorLimit != 0;
    if (ImGui::Checkbox("Color Limit Enable", &v))
      g_config.enableColorLimit = v ? 1 : 0;
  }
  if (PassesFilter("Color Limit Value", search))
    ImGui::SliderInt("Color Limit Value", &g_config.colorLimit, 1, 50);
  if (PassesFilter("Color Refresh Time", search))
    ImGui::SliderInt("Color Refresh Time (ms)", &g_config.colorRefresh, 100,
                     10000);
  if (PassesFilter("Color Threshold", search))
    ImGui::SliderInt("Color Threshold", &g_config.colorThreshold, 1, 100);
  if (PassesFilter("Ramp Limit Enable", search)) {
    bool v = g_config.enableRampLimit != 0;
    if (ImGui::Checkbox("Ramp Limit Enable", &v))
      g_config.enableRampLimit = v ? 1 : 0;
  }
  if (PassesFilter("Ramp Limit Value", search))
    ImGui::SliderInt("Ramp Limit Value", &g_config.rampLimit, 1, 50);
  if (PassesFilter("Ramp Refresh Time", search))
    ImGui::SliderInt("Ramp Refresh Time (ms)", &g_config.rampRefresh, 100,
                     10000);

  if (PassesFilter("presets export import config ini replace current save ctrl s textbox", search))
    DrawSettingHeader("Presets");
  if (PassesFilter("Export Current Preset ini textbox name settings font ramp enabled", search)) {
    static char exportPresetName[160] = "my_preset";
    static char presetStatus[256] = "";
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("Export Preset Name", exportPresetName,
                     sizeof(exportPresetName));
    if (ImGui::Button("Export Current Preset")) {
      char path[MAX_PATH];
      BuildPresetPath(exportPresetName, path, sizeof(path));
      if (SaveConfigToNamedFile(path)) {
        snprintf(presetStatus, sizeof(presetStatus), "Exported: %s", path);
      } else {
        snprintf(presetStatus, sizeof(presetStatus), "Export failed: %s", path);
      }
    }
    if (presetStatus[0])
      ImGui::TextDisabled("%s", presetStatus);
  }
  if (PassesFilter("Import Preset ini load config textbox settings font ramp enabled", search)) {
    static char importPresetName[160] = "my_preset";
    static char importStatus[256] = "";
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("Import Preset Name", importPresetName,
                     sizeof(importPresetName));
    if (ImGui::Button("Import Preset")) {
      char path[MAX_PATH];
      BuildPresetPath(importPresetName, path, sizeof(path));
      if (ActionImportConfig(path)) {
        snprintf(importStatus, sizeof(importStatus), "Imported: %s", path);
      } else {
        snprintf(importStatus, sizeof(importStatus), "Import failed: %s", path);
      }
    }
    if (importStatus[0])
      ImGui::TextDisabled("%s", importStatus);
  }
  if (PassesFilter("Replace config.ini with current save config ctrl s", search)) {
    if (ImGui::Button("Replace config.ini With Current")) {
      ActionSaveConfig();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save (Ctrl+S)")) {
      ActionSaveConfig();
    }
  }

  if (PassesFilter("actions save load reset relaunch temporal cache", search))
    DrawSettingHeader("Actions");
  if (PassesFilter("Save Configuration", search) &&
      ImGui::Button("Save Configuration"))
    ActionSaveConfig();
  if (PassesFilter("Load Configuration", search) &&
      ImGui::Button("Load Configuration"))
    ActionLoadConfig();
  if (PassesFilter("Reset All Settings", search) &&
      ImGui::Button("Reset All Settings"))
    ActionResetAll();
  if (PassesFilter("Reset Temporal Cache", search) &&
      ImGui::Button("Reset Temporal Cache"))
    ActionResetLimits();
  if (PassesFilter("Relaunch Program", search) &&
      ImGui::Button("Relaunch Program"))
    ActionRelaunch();

  ImGui::EndChild();
  ImGui::End();
}

struct HelpItem {
  const char *feature;
  const char *activation;
  const char *description;
  const char *tags;
};

static const HelpItem g_helpItems[] = {
    {"Frames Per Second (FPS)",
     "Frames Per Second (FPS) Settings (cntrl alt)>fps slider",
     "Limit rendering speed", "fps performance framerate speed target"},
    {"Cell Size",
     "Cell Size (cntrl alt)>Cell Size slider or (ctrl)>left/right while Motion Mode is off",
     "Block pixel size",
     "display size density grid blocks resolution left right non motion"},
    {"Background Opacity",
     "Background Opacity (cntrl alt)>Background Opacity slider",
     "Backdrop opacity level",
     "display backdrop opacity alpha transparency background"},
    {"Desktop Copy Mode",
     "Desktop Copy Mode (ctrl)>o or (cntrl alt)>Desktop Copy Mode checkbox",
     "Switch from UTF-8 glyph rendering to a real visible-desktop pixel feed while keeping filters, themes, limits, motion, heat, zones, and FPS controls available",
     "renderer mode desktop copy pass through ctrl o filters themes motion heat limits fps"},
    {"Ambient Mode",
     "Ambient Mode (ctrl)>a or (cntrl alt)>Ambient Mode checkbox",
     "Light a stable percentage of samples with glow strength, subdivisions, radius, progressive bleed, source pixels, or live ramp colors",
     "ambient glow ctrl a lit pixels percentage subdivision radius progressive bleed color match source ramp"},
    {"Current Ramp Selection",
     "Current Ramp Selection (cntrl alt)>Current Ramp dropdown or (ctrl)>[ or "
     "(ctrl)>]",
     "Select UTF-8 glyph set",
     "display ramp character set glyph utf8 unicode alphabet"},
    {"Spacing Editor",
     "Spacing Editor (ctrl)>tilde",
     "Open the same UTF-8 Ramp Editor GUI on the Spacing page for glyph size and cell gutter controls",
     "display spacing editor glyph gap cell gutter horizontal vertical ctrl tilde"},
    {"Variable Font",
     "Variable Font (cntrl alt)>Variable Font or (ctrl)>tilde>Variable Size",
     "Randomize glyph scale and optional width/height stretch by stable screen regions with min/max size, randomness, seed, pulse speed, and Strict No Font Overlap",
     "variable font strict no overlap random scale width height region seed pulse ctrl tilde"},
    {"Variable Cell Size",
     "Variable Cell Size (cntrl alt)>Variable Cell Size or (ctrl)>tilde>Variable Size",
     "Apply stable virtual cell scaling to sampling footprint and optionally glyph size, with strict overlap, region, seed, randomness, and pulse controls",
     "variable cell size strict no overlap virtual sampling glyph scale region seed pulse"},
    {"Experimental Cell Sampling",
     "Experimental Cell Sampling (cntrl alt)>Experimental Cell Sampling or (ctrl)>tilde>Variable Size",
     "Use grid/cross/diamond supersampling up to 50x50 per cell, with radius, edge boost, jitter, detail mix, luminance compensation, and preserve highlights controls",
     "experimental cell supersampling quality large cell detail grid cross diamond edge contrast jitter luminance compensation preserve highlights"},
    {"Font Family Selection", "Font Family Selection (cntrl alt)>Font dropdown",
     "Change rendering typeface",
     "display font typeface graphics family typography"},
    {"Frame Budget", "Frame Budget (cntrl alt)>Frame Budget slider",
     "Target execution duration", "display performance budget latency ms"},
    {"Frame Skip", "Frame Skip (cntrl alt)>Frame Skip slider",
     "Render cadence divisor", "display performance skip cadence delay"},
    {"Motion Mode Enable",
     "Motion Mode Enable (cntrl alt)>Motion Mode Enable checkbox or (ctrl)>h",
     "Remastered stable per-cell trails: detected motion becomes black cells with colored UTF-8 glyphs; background opacity is temporarily forced to 0",
     "motion active detect movement highlight delta black box colored character opacity ctrl h remastered stable"},
    {"Motion Sensitivity",
     "Motion Sensitivity (cntrl alt)>Motion Sensitivity slider or "
     "(ctrl)>left/right while Motion Mode is on",
     "Sensitivity threshold", "motion threshold sensitivity zoom adjust scale left right"},
    {"Motion Decay",
     "Motion Decay (cntrl alt)>Visible Decay slider",
     "How long a detected Motion Mode cell remains visible after movement",
     "motion decay ms precise individual ramp glyph wait hold trail"},
    {"Hold Ramps Until New Draw",
     "Hold Ramps Until New Draw (cntrl alt)>Hold Ramp Until New Draw checkbox",
     "Primary anti-flicker mode: keeps visible Motion Mode ramps on-screen and refreshes each cell only on fresh motion",
     "motion hold ramps until new draw request low ms anti flicker"},
    {"Auto Kill Static Ramps",
     "Auto Kill Static Ramps (cntrl alt)>Motion Mode",
     "Companion for hold mode: cells that stop detecting motion expire using Visible Decay, so one-shot UI trails disappear while constantly updating video regions remain",
     "motion auto kill static ramps stuck ui video hold decay fresh motion"},
    {"Motion Ramp Update Duration",
     "Refresh Window (cntrl alt)>Refresh Window slider, active only when hold mode is off",
     "Time window for each cell's Motion Mode refresh cap",
     "motion ramp update duration window ms limit reset"},
    {"Motion Max Individual Ramp Refreshes",
     "Max Refreshes Per Cell (cntrl alt)>slider or exact textbox, active only when hold mode is off",
     "Per-cell Motion Mode refresh cap; 0 means unlimited",
     "motion ramp refreshes updates max cap textbox unlimited zero individual"},
    {"Motion Max Concurrent Ramps",
     "Max Concurrent Screen Cells (cntrl alt)>percent slider or exact textbox",
     "Maximum percentage of screen cells allowed to show Motion Mode ramps",
     "motion concurrent ramps percent percentage onscreen exact precision"},
    {"Heat Mode Enable",
     "Heat Mode Enable (cntrl alt)>Heat Mode Enable checkbox or (ctrl)>m "
     "(black text) or (ctrl)>b (hidden glyphs)",
     "Render thermal color map", "heat thermal active map glow color"},
    {"Heat Radius", "Heat Radius (cntrl alt)>Heat Radius slider",
     "Glow hotspot size", "heat radius size hotspot circle geometry"},
    {"Heat Brightness", "Heat Brightness (cntrl alt)>Heat Brightness slider",
     "Thermal glow boost level", "heat brightness intensity glow strength"},
    {"Heat Style", "Heat Style (cntrl alt)>Heat Style dropdown",
     "Spot shape geometry",
     "heat style shape circle square diamond soft box bar"},
    {"Heat Glyph Mode", "Heat Glyph Mode (cntrl alt)>Heat Glyph Mode dropdown",
     "Hides text character",
     "heat glyph cutout black hidden characters transparency"},
    {"Zone Enable", "Zone Enable (cntrl alt)>Zone Enable checkbox",
     "Partial area capture", "zone active region crop area"},
    {"Zone X Position", "Zone X Position (cntrl alt)>Zone X slider",
     "Left area offset percent", "zone offset left x horizontal positioning"},
    {"Zone Y Position", "Zone Y Position (cntrl alt)>Zone Y slider",
     "Top area offset percent", "zone offset top y vertical positioning"},
    {"Zone Width", "Zone Width (cntrl alt)>Zone W slider",
     "Capture zone width percent", "zone width sizing horizontal size scale"},
    {"Zone Height", "Zone Height (cntrl alt)>Zone H slider",
     "Capture zone height percent", "zone height sizing vertical size scale"},
    {"ASCII Random Percent",
     "ASCII Random Percent (cntrl alt)>ASCII Random % slider",
     "Only a random fraction of cells render as ASCII; the rest show the live background",
     "ascii random percent density coverage scatter cells sparse partial"},
    {"Color Theme Selection",
     "Color Theme Selection (cntrl alt)>Color Theme dropdown or tilde>Color Themes Index",
     "Select color preset palette",
     "filters theme color palette preset schemes styling"},
    {"Preset Export",
     "Preset Export (cntrl alt)>Presets>Export Current Preset",
     "Write current enabled features, ramp, font, cell size, theme, and values to a named .ini",
     "preset export ini config save font ramp settings"},
    {"Preset Import",
     "Preset Import (cntrl alt)>Presets>Import Preset",
     "Load a named .ini preset and rebuild renderer resources",
     "preset import ini config load settings"},
    {"Replace config.ini",
     "Replace config.ini (cntrl alt)>Presets>Replace config.ini With Current or (ctrl)>s",
     "Save current settings as the main startup config",
     "preset config save replace ctrl s"},
    {"Invert Colors",
     "Invert Colors (cntrl alt)>Invert Colors checkbox or (ctrl)>8",
     "Reverse image colors", "filters invert reverse negative color"},
    {"Grayscale Mode", "Grayscale Mode (cntrl alt)>Grayscale Mode checkbox",
     "Remove color saturation",
     "filters grayscale monochrome black white filter saturation"},
    {"Brightness Adjustment",
     "Brightness Adjustment (cntrl alt)>Brightness slider or (ctrl "
     "shift)>up/down",
     "Adjust image brightness", "filters brightness light gain exposure boost"},
    {"Contrast Adjustment",
     "Contrast Adjustment (cntrl alt)>Contrast slider or (ctrl alt)>up/down",
     "Adjust image contrast",
     "filters contrast separation dynamic range scaling"},
    {"Gamma Correction", "Gamma Correction (cntrl alt)>Gamma slider",
     "Mid-tone scaling correction",
     "filters gamma midtone correction curve scaling"},
    {"Final Luminance Multiplier",
     "Final Luminance Multiplier (cntrl alt)>Final Luminance Multiplier slider "
     "or (ctrl)>up/down",
     "Boost overall brightness",
     "filters brightness luminance gain scaler multiplier"},
    {"Luminance Sequence Enable",
     "Luminance Sequence Enable (cntrl alt)>Luminance Sequence Enabled "
     "checkbox or (ctrl alt)>l",
     "Animate brightness sequence", "sequence animate dynamic sequence active"},
    {"Luminance Change Time",
     "Luminance Change Time (cntrl alt)>Luminance Change Time slider",
     "Interval between steps",
     "sequence timing delay duration speed change ms"},
    {"Sequence Pattern",
     "Sequence Pattern (cntrl alt)>Sequence Pattern textfield + Parse button",
     "Custom animation values", "sequence pattern raw parse input text values"},
    {"Color Limit Enable",
     "Color Limit Enable (cntrl alt)>Color Limit Enable checkbox or (ctrl)>9",
     "Restrict color changes", "limits color active lock temporal reduction"},
    {"Color Limit Value",
     "Color Limit Value (cntrl alt)>Color Limit Value slider",
     "Max color changes", "limits color value frame max updates"},
    {"Color Refresh Time",
     "Color Refresh Time (cntrl alt)>Color Refresh Time slider",
     "Color cache duration", "limits color refresh speed delay updates ms"},
    {"Color Threshold", "Color Threshold (cntrl alt)>Color Threshold slider",
     "Color change sensitivity",
     "limits color threshold update sensitivity delta"},
    {"Ramp Limit Enable",
     "Ramp Limit Enable (cntrl alt)>Ramp Limit Enable checkbox or (ctrl)>0",
     "Restrict glyph changes", "limits ramp active lock character reduction"},
    {"Ramp Limit Value", "Ramp Limit Value (cntrl alt)>Ramp Limit Value slider",
     "Max glyph changes", "limits ramp value frame max updates"},
    {"Ramp Refresh Time",
     "Ramp Refresh Time (cntrl alt)>Ramp Refresh Time slider",
     "Glyph cache duration", "limits ramp refresh speed delay updates ms"},
    {"Save Configuration",
     "Save Configuration Action (cntrl alt)>Save Configuration button or "
     "(ctrl)>s",
     "Store settings values", "core save config configuration disk"},
    {"Load Configuration",
     "Load Configuration Action (cntrl alt)>Load Configuration button or "
     "(ctrl)>r",
     "Restore saved settings", "core load config configuration reload disk"},
    {"Reset All Settings",
     "Reset All Settings Action (cntrl alt)>Reset All Settings button or (ctrl "
     "shift)>r",
     "Wipe config values", "core reset clear defaults factory"},
    {"Reset Temporal Cache",
     "Reset Temporal Cache Action (cntrl alt)>Reset Temporal Cache button",
     "Flush temporal buffers", "core reset clear cache temporal buffers"},
    {"Relaunch Program",
     "Relaunch Program Action (cntrl alt)>Relaunch Program button or (ctrl "
     "shift)>l",
     "Restart overlay process", "core relaunch restart reboot"},
    {"UTF-8 Pause", "UTF-8 Pause (ctrl)>p",
     "Freeze only the UTF-8 glyph/ramp frame; screen capture keeps updating",
     "core pause render freeze utf8 unicode ramp characters not screen"},
    {"Quit Program", "Quit Program (ctrl)>d", "Exit overlay application",
     "core quit exit close shutdown"},
    {"Show Settings Menu", "Show Settings Menu (cntrl alt)>Toggle settings",
     "Open adjustment menu", "core settings navigation gui menu"},
    {"Show Performance Metrics",
     "Show Performance Metrics (cntrl shift)>Toggle metrics",
     "Open performance charts",
     "core metrics performance telemetry profiler gui"},
    {"Show Help Guide", "Show Help Guide (tilde)>Toggle help",
     "Open reference guide", "core help guide reference manual tilde key"}};

static void DrawHelp(bool &showHelp) {
  ImGui::SetNextWindowSize(ImVec2(800, 520), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(60, 80), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Help & Reference Guide", &showHelp,
                    ImGuiWindowFlags_NoCollapse)) {
    TrackCurrentWindowRect(UI_RECT_HELP);
    ImGui::End();
    return;
  }
  TrackCurrentWindowRect(UI_RECT_HELP);

  ImGui::TextColored(ImVec4(0.50f, 0.40f, 0.90f, 1.00f),
                     "UTF-8 CUDA Overlay Help");
  ImGui::SameLine();
  ImGui::TextDisabled("| Toggle this window with tilde (~) key");

  static char helpSearch[128] = "";
  ImGui::Spacing();
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputTextWithHint("##HelpSearch",
                           "Type to search hotkeys and settings...", helpSearch,
                           sizeof(helpSearch));
  ImGui::Spacing();

  if (ImGui::BeginTabBar("HelpTabBar")) {
    if (ImGui::BeginTabItem("Hotkeys & Settings")) {
      ImGui::BeginChild("HotkeysScroll", ImVec2(0, 0), true);
      if (ImGui::BeginTable("HelpTable", 3,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_Resizable |
                                ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Feature", ImGuiTableColumnFlags_WidthFixed,
                                180.0f);
        ImGui::TableSetupColumn("How to Activate",
                                ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthFixed,
                                200.0f);
        ImGui::TableHeadersRow();

        int numItems = sizeof(g_helpItems) / sizeof(g_helpItems[0]);
        for (int i = 0; i < numItems; ++i) {
          const HelpItem &item = g_helpItems[i];
          if (PassesFilter(item.feature, helpSearch) ||
              PassesFilter(item.activation, helpSearch) ||
              PassesFilter(item.description, helpSearch) ||
              PassesFilter(item.tags, helpSearch)) {

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.70f, 0.65f, 0.95f, 1.0f), "%s",
                               item.feature);

            ImGui::TableNextColumn();
            ImGui::Text("%s", item.activation);

            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", item.description);
          }
        }
        ImGui::EndTable();
      }
      ImGui::EndChild();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Color Themes Index")) {
      ImGui::Text("Click any theme below to apply it instantly:");
      ImGui::Spacing();
      ImGui::BeginChild("ThemesScroll", ImVec2(0, 0), true);
      if (ImGui::BeginTable("ThemesTable", 3,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed,
                                60.0f);
        ImGui::TableSetupColumn("Theme Name",
                                ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Hotkey / Activation",
                                ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < THEME_COUNT; ++i) {
          ImGui::TableNextRow();

          // Index
          ImGui::TableNextColumn();
          ImGui::Text("%d", i);

          // Name (Selectable)
          ImGui::TableNextColumn();
          bool isCurrent = (g_config.colorTheme == i);
          if (isCurrent) {
            ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.55f, 1.0f), "%s (Active)",
                               THEME_NAMES[i]);
          } else {
            if (ImGui::Selectable(THEME_NAMES[i], false,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
              g_config.colorTheme = i;
            }
          }

          // Hotkey / Activation
          ImGui::TableNextColumn();
          ImGui::TextDisabled("click row or use settings");
        }
        ImGui::EndTable();
      }
      ImGui::EndChild();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("UTF-8 Ramps Index")) {
      ImGui::Text("Click any ramp below to select it:");
      ImGui::Spacing();
      ImGui::BeginChild("RampsScroll", ImVec2(0, 0), true);
      bool previewReady = RebuildRampPreviewTexture();
      if (ImGui::BeginTable("RampsTable", 3,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed,
                                60.0f);
        ImGui::TableSetupColumn("Ramp Name", ImGuiTableColumnFlags_WidthFixed,
                                180.0f);
        ImGui::TableSetupColumn("Characters Preview",
                                ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int i = 0; i < SafeRampCount(); ++i) {
          ImGui::TableNextRow();

          // Index
          ImGui::TableNextColumn();
          ImGui::Text("%d", i);

          // Ramp Name
          ImGui::TableNextColumn();
          bool isCurrent = (g_config.currentRamp == i);
          if (isCurrent) {
            ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.55f, 1.0f), "%s (Active)",
                               g_config.ramps[i].name);
          } else {
            if (ImGui::Selectable(g_config.ramps[i].name, false,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
              g_config.currentRamp = i;
              ActionRebuildRendererResources();
            }
          }

          // Characters Preview
          ImGui::TableNextColumn();
          if (previewReady && g_rampPreviewSrv && g_rampPreviewRows > i &&
              g_rampPreviewH > 0) {
            const int rowPixels = 28;
            float v0 = (float)(i * rowPixels) / (float)g_rampPreviewH;
            float v1 = (float)((i + 1) * rowPixels) / (float)g_rampPreviewH;
            ImGui::Image((ImTextureID)g_rampPreviewSrv,
                         ImVec2((float)g_rampPreviewW, (float)rowPixels),
                         ImVec2(0.0f, v0), ImVec2(1.0f, v1));
          } else {
            char preview[512];
            preview[0] = '\0';
            int previewLen = 0;
            for (int j = 0; j < g_config.ramps[i].count && j < 50; ++j) {
              char utf8Char[32];
              int written = WideCharToMultiByte(
                  CP_UTF8, 0, g_config.ramps[i].clusters[j].chars,
                  g_config.ramps[i].clusters[j].wcharLen, utf8Char,
                  sizeof(utf8Char) - 1, NULL, NULL);
              if (written > 0) {
                utf8Char[written] = '\0';
                int remaining = (int)sizeof(preview) - previewLen - 1;
                if (remaining > 0) {
                  int copied = snprintf(preview + previewLen,
                                             (size_t)remaining + 1, "%s",
                                             utf8Char);
                  if (copied > 0)
                    previewLen += copied < remaining ? copied : remaining;
                }
              }
            }
            if (g_config.ramps[i].count > 50) {
              int remaining = (int)sizeof(preview) - previewLen - 1;
              if (remaining > 0)
                snprintf(preview + previewLen, (size_t)remaining + 1, "%s",
                              " ...");
            }
            ImGui::Text("%s", preview);
          }
        }
        ImGui::EndTable();
      }
      ImGui::EndChild();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Quick Guide")) {
      ImGui::BeginChild("QuickGuideScroll", ImVec2(0, 0), true);
      ImGui::TextColored(ImVec4(0.50f, 0.40f, 0.90f, 1.00f),
                         "Welcome to UTF-8 CUDA Overlay!");
      ImGui::TextWrapped("This application captures your desktop using Windows "
                         "Desktop Duplication API and uses custom CUDA kernels "
                         "to transform the pixels in real-time into animated "
                         "UTF-8 glyphs on an overlay window.");
      ImGui::Spacing();
      DrawSettingHeader("Interaction Model");
      ImGui::BulletText("By default, the overlay is click-through so it does "
                        "not block your apps or games.");
      ImGui::BulletText("Ctrl+O switches between UTF-8 glyph rendering and "
                        "Desktop Copy Mode, which samples the real visible desktop pixels.");
      ImGui::BulletText("Ctrl+A toggles Ambient Mode for stable captured-pixel "
                        "glow with radius, subdivision, bleed, and color-match controls.");
      ImGui::BulletText(
          "When you open Settings (cntrl alt), Metrics (cntrl shift), Help "
          "(tilde), clicks inside those panels control the overlay.");
      ImGui::BulletText("Clicks outside open panels pass through to the "
                        "desktop while the UI stays visible.");
      ImGui::Spacing();
      DrawSettingHeader("Useful Tips");
      ImGui::BulletText("Toggle Heat Mode (ctrl+m / ctrl+b) to render pixel "
                        "movement as thermal energy mapping.");
      ImGui::BulletText("Adjust opacity in the settings to let more or less of "
                        "your desktop show through the characters.");
      ImGui::BulletText("Save your preferred layout/ramps/themes using "
                        "(ctrl+s) to keep them across relaunches.");
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  ImGui::End();
}

static ImVec4 MetricColor(float value, float warn, float bad,
                          bool lowerIsBetter) {
  if (lowerIsBetter) {
    if (value >= bad)
      return ImVec4(1.0f, 0.35f, 0.30f, 1.0f);
    if (value >= warn)
      return ImVec4(1.0f, 0.75f, 0.25f, 1.0f);
    return ImVec4(0.35f, 0.95f, 0.55f, 1.0f);
  }
  if (value <= bad)
    return ImVec4(1.0f, 0.35f, 0.30f, 1.0f);
  if (value <= warn)
    return ImVec4(1.0f, 0.75f, 0.25f, 1.0f);
  return ImVec4(0.35f, 0.95f, 0.55f, 1.0f);
}

static void DrawMetricCell(const char *label, const char *value,
                           const ImVec4 &color) {
  ImGui::TableNextColumn();
  ImGui::TextDisabled("%s", label);
  ImGui::TextColored(color, "%s", value);
}

static void DrawMetricCellF(const char *label, const char *fmt, float value,
                            const ImVec4 &color) {
  char buf[64];
  snprintf(buf, sizeof(buf), fmt, value);
  DrawMetricCell(label, buf, color);
}

static void DrawOptionalMetricCellF(const char *label, const char *fmt,
                                    float value, const ImVec4 &color) {
  if (value < 0.0f) {
    DrawMetricCell(label, "n/a", ImVec4(0.55f, 0.55f, 0.60f, 1.0f));
    return;
  }
  DrawMetricCellF(label, fmt, value, color);
}

static void DrawPercentBar(const char *label, float value,
                           const ImVec4 &color) {
  ImGui::TextDisabled("%s", label);
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
  ImGui::ProgressBar(value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value),
                     ImVec2(-1.0f, 0.0f));
  ImGui::PopStyleColor();
}

static void DrawMetrics(bool &showMetrics, const OverlayMetrics &m) {
  ImGui::SetNextWindowSize(ImVec2(860, 720), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(90, 90), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Performance Profiler", &showMetrics,
                    ImGuiWindowFlags_NoCollapse)) {
    TrackCurrentWindowRect(UI_RECT_METRICS);
    ImGui::End();
    return;
  }
  TrackCurrentWindowRect(UI_RECT_METRICS);

  SanitizeConfigForUi();

  const int historyCount = m.historyCount > 0 ? m.historyCount : 0;
  const int targetFps = ClampInt(g_config.targetFPS, 1, 240);
  const float targetFrameMs = m.targetFrameMs > 0.001f
                                  ? m.targetFrameMs
                                  : (1000.0f / static_cast<float>(targetFps));
  const int frameSkipStep = (g_config.frameSkip + 1) > 1 ? (g_config.frameSkip + 1) : 1;
  const int frameSkipPhase = ClampInt(m.frameSkipPhase, 0, frameSkipStep - 1);

  ImVec4 fpsColor = MetricColor(m.fps, static_cast<float>(targetFps) * 0.85f,
                                static_cast<float>(targetFps) * 0.65f, false);
  ImVec4 budgetColor = MetricColor(m.frameBudgetUse, 0.85f, 1.0f, true);
  ImVec4 p95Color = MetricColor(m.p95FrameMs, targetFrameMs * 0.9f,
                                targetFrameMs * 1.1f, true);
  ImVec4 failColor = MetricColor(m.failureRate, 0.01f, 0.05f, true);

  if (ImGui::BeginTable("MetricTop", 4, ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextRow();
    DrawMetricCellF("FPS", "%.1f", m.fps, fpsColor);
    DrawMetricCellF("Avg Frame", "%.2f ms", m.avgFrameMs, budgetColor);
    DrawMetricCellF("P95 Frame", "%.2f ms", m.p95FrameMs, p95Color);
    DrawMetricCell("Renderer", SafeText(m.rendererMode),
                   ImVec4(0.50f, 0.80f, 1.00f, 1.0f));
    ImGui::EndTable();
  }
  if (ImGui::BeginTable("HardwareTop", 4, ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextRow();
    DrawMetricCellF("CPU Usage", "%.1f%%", m.cpuUsagePercent,
                    MetricColor(m.cpuUsagePercent, 55.0f, 80.0f, true));
    DrawOptionalMetricCellF("GPU Usage", "%.1f%%", m.gpuUsagePercent,
                            MetricColor(m.gpuUsagePercent, 65.0f, 88.0f, true));
    DrawOptionalMetricCellF(
        "VRAM Used", "%.0f MB", m.vramUsedMB,
        MetricColor(m.vramUsagePercent, 70.0f, 88.0f, true));
    DrawOptionalMetricCellF(
        "VRAM Load", "%.1f%%", m.vramUsagePercent,
        MetricColor(m.vramUsagePercent, 70.0f, 88.0f, true));
    ImGui::EndTable();
  }

  DrawSettingHeader("Frame Budget");
  if (ImGui::BeginTable("BudgetTable", 3, ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    DrawPercentBar("Budget Used", m.frameBudgetUse, budgetColor);
    ImGui::Text("Budget %.2f ms | Timer target %d FPS", targetFrameMs,
                g_config.targetFPS);
    ImGui::TableNextColumn();
    DrawPercentBar("Over Budget",
                   historyCount > 0
                       ? (float)m.overBudgetFrames / (float)historyCount
                       : 0.0f,
                   p95Color);
    ImGui::Text("%d / %d recent frames", m.overBudgetFrames, historyCount);
    ImGui::TableNextColumn();
    DrawPercentBar("Failure Rate", m.failureRate, failColor);
    ImGui::Text("Skipped %.1f%% | Failed %.1f%%", m.skipRate * 100.0f,
                m.failureRate * 100.0f);
    ImGui::EndTable();
  }

  ImGui::PlotLines("Frame Time", m.frameHistory, historyCount, m.historyOffset,
                   "ms", 0.0f, targetFrameMs * 2.5f, ImVec2(0, 125));

  DrawSettingHeader("Hardware");
  if (ImGui::BeginTable("HardwareBars", 3, ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    DrawPercentBar("CPU Usage", m.cpuUsagePercent / 100.0f,
                   MetricColor(m.cpuUsagePercent, 55.0f, 80.0f, true));
    ImGui::TableNextColumn();
    DrawPercentBar("GPU Usage / Frame",
                   m.gpuUsagePercent < 0.0f ? 0.0f : m.gpuUsagePercent / 100.0f,
                   MetricColor(m.gpuUsagePercent, 65.0f, 88.0f, true));
    ImGui::TableNextColumn();
    DrawPercentBar("VRAM Usage",
                   m.vramUsagePercent < 0.0f ? 0.0f
                                             : m.vramUsagePercent / 100.0f,
                   MetricColor(m.vramUsagePercent, 70.0f, 88.0f, true));
    ImGui::EndTable();
  }
  if (ImGui::CollapsingHeader("Hardware Graphs",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::PlotLines("GPU Utilization %", m.gpuHistory, historyCount,
                     m.historyOffset, nullptr, 0.0f, 100.0f, ImVec2(0, 70));
    ImGui::PlotLines("VRAM Usage %", m.vramHistory, historyCount,
                     m.historyOffset, nullptr, 0.0f, 100.0f, ImVec2(0, 70));
    ImGui::PlotLines("CPU Usage %", m.cpuHistory, historyCount, m.historyOffset,
                     nullptr, 0.0f, 100.0f, ImVec2(0, 70));
  }

  DrawSettingHeader("Pipeline");
  if (ImGui::BeginTable("PipelineTable", 5,
                        ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextRow();
    DrawMetricCellF("Capture", "%.2f ms", m.captureMs,
                    MetricColor(m.captureMs, targetFrameMs * 0.25f,
                                targetFrameMs * 0.45f, true));
    DrawMetricCellF("CUDA", "%.2f ms", m.cudaMs,
                    MetricColor(m.cudaMs, targetFrameMs * 0.45f,
                                targetFrameMs * 0.75f, true));
    DrawMetricCellF("Draw Screen", "%.2f ms", m.drawScreenMs,
                    MetricColor(m.drawScreenMs, targetFrameMs * 0.15f,
                                targetFrameMs * 0.30f, true));
    DrawMetricCellF("Present", "%.2f ms", m.presentMs,
                    MetricColor(m.presentMs, targetFrameMs * 0.20f,
                                targetFrameMs * 0.40f, true));
    DrawMetricCellF("CPU Next Frame", "%.2f ms", m.cpuOverheadMs,
                    MetricColor(m.cpuOverheadMs, targetFrameMs * 0.20f,
                                targetFrameMs * 0.40f, true));
    ImGui::EndTable();
  }
  if (ImGui::BeginTable("PipelineBars", 3, ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    DrawPercentBar("Capture Share", m.captureShare,
                   ImVec4(0.35f, 0.60f, 1.00f, 1.0f));
    ImGui::TableNextColumn();
    DrawPercentBar("CUDA Share", m.cudaShare,
                   ImVec4(0.55f, 0.95f, 0.55f, 1.0f));
    ImGui::TableNextColumn();
    DrawPercentBar("Present Share", m.presentShare,
                   ImVec4(1.00f, 0.65f, 0.35f, 1.0f));
    ImGui::EndTable();
  }
  if (ImGui::CollapsingHeader("Pipeline Graphs",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::PlotLines("Capture ms", m.captureHistory, historyCount,
                     m.historyOffset, nullptr, 0.0f, targetFrameMs,
                     ImVec2(0, 64));
    ImGui::PlotLines("CUDA ms", m.cudaHistory, historyCount, m.historyOffset,
                     nullptr, 0.0f, targetFrameMs, ImVec2(0, 64));
    ImGui::PlotLines("Draw Screen ms", m.drawHistory, historyCount,
                     m.historyOffset, nullptr, 0.0f, targetFrameMs,
                     ImVec2(0, 64));
    ImGui::PlotLines("CPU Next Frame ms", m.cpuOverheadHistory, historyCount,
                     m.historyOffset, nullptr, 0.0f, targetFrameMs,
                     ImVec2(0, 64));
    ImGui::PlotLines("Present ms", m.presentHistory, historyCount,
                     m.historyOffset, nullptr, 0.0f, targetFrameMs,
                     ImVec2(0, 64));
  }

  DrawSettingHeader("Detailed Stage Timing");
  if (ImGui::BeginTable("StageDetail", 2,
                        ImGuiTableFlags_BordersInnerV |
                            ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("Per Frame");
    ImGui::Text("Processing time/frame: %.2f ms", m.frameMs);
    ImGui::Text("Capture screen: %.2f ms", m.captureMs);
    ImGui::Text("CUDA render: %.2f ms", m.cudaMs);
    ImGui::Text("Draw screen copy: %.2f ms", m.drawScreenMs);
    ImGui::Text("Present/UI: %.2f ms", m.presentMs);
    ImGui::Text("CPU next frame: %.2f ms", m.cpuOverheadMs);
    ImGui::Text("GPU usage/frame: %s", m.gpuUsagePercent < 0.0f ? "n/a" : "");
    if (m.gpuUsagePercent >= 0.0f) {
      ImGui::SameLine();
      ImGui::Text("%.1f%%", m.gpuUsagePercent);
    }
    ImGui::Text("CPU render usage/frame: %.1f%%", m.cpuUsagePercent);

    ImGui::TableNextColumn();
    ImGui::Text("Initialization / State");
    ImGui::Text("D3D init: %.2f ms", m.d3dInitMs);
    ImGui::Text("CUDA init: %.2f ms", m.cudaInitMs);
    ImGui::Text("Desktop duplication init: %.2f ms", m.duplicationInitMs);
    ImGui::Text("Font atlas build: %.2f ms", m.atlasBuildMs);
    ImGui::Text("Resource register: %.2f ms", m.resourceRegisterMs);
    ImGui::Text("Frame min/max: %.2f / %.2f ms", m.minFrameMs, m.maxFrameMs);
    ImGui::Text("Frame jitter: %.2f ms", m.jitterMs);
    ImGui::Text("VRAM: %.0f / %.0f MB", m.vramUsedMB, m.vramTotalMB);
    ImGui::EndTable();
  }

  DrawSettingHeader("Workload");
  const char *rampName =
      HasValidRamp(m.currentRamp)
          ? SafeText(g_config.ramps[m.currentRamp].name, "unnamed")
          : "none";
  const char *themeName = HasValidTheme(m.currentTheme)
                              ? SafeText(THEME_NAMES[m.currentTheme], "unknown")
                              : "unknown";
  if (ImGui::BeginTable("WorkloadTable", 3,
                        ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextRow();
    char gridBuf[64];
    snprintf(gridBuf, sizeof(gridBuf), "%dx%d (%d)", m.cols, m.rows,
             m.cellCount);
    DrawMetricCell("UTF-8 Grid", gridBuf, ImVec4(0.85f, 0.85f, 0.95f, 1.0f));
    char screenBuf[64];
    snprintf(screenBuf, sizeof(screenBuf), "%dx%d", m.screenW, m.screenH);
    DrawMetricCell("Screen", screenBuf, ImVec4(0.85f, 0.85f, 0.95f, 1.0f));
    char glyphBuf[64];
    snprintf(glyphBuf, sizeof(glyphBuf), "%d glyphs", m.activeRampGlyphs);
    DrawMetricCell("Ramp", glyphBuf, ImVec4(0.85f, 0.85f, 0.95f, 1.0f));
    ImGui::TableNextRow();
    DrawMetricCell("Ramp Name", rampName, ImVec4(0.70f, 0.85f, 1.00f, 1.0f));
    DrawMetricCell("Theme", themeName, ImVec4(0.70f, 0.85f, 1.00f, 1.0f));
    char bgBuf[64];
    snprintf(bgBuf, sizeof(bgBuf), "%d / 255", m.backgroundOpacity);
    DrawMetricCell("Background", bgBuf, ImVec4(0.70f, 0.85f, 1.00f, 1.0f));
    ImGui::TableNextRow();
    DrawMetricCell("UTF-8 Pause", m.asciiPaused ? "on" : "off",
                   m.asciiPaused ? ImVec4(1.00f, 0.75f, 0.25f, 1.0f)
                                 : ImVec4(0.70f, 0.85f, 1.00f, 1.0f));
    DrawMetricCell("Motion Mode", m.motionMode ? "remastered on" : "off",
                   m.motionMode ? ImVec4(0.35f, 0.95f, 0.55f, 1.0f)
                                : ImVec4(0.70f, 0.85f, 1.00f, 1.0f));
    char motionBuf[64];
    snprintf(motionBuf, sizeof(motionBuf), "sens %d | %.2f%%", m.motionSensitivity,
             m.motionMaxConcurrentPercent);
    DrawMetricCell("Motion Gate", motionBuf, ImVec4(0.70f, 0.85f, 1.00f, 1.0f));
    ImGui::TableNextRow();
    DrawMetricCell("Motion Hold", m.motionHoldUntilNewDraw ? "until new draw" : "refresh-limited",
                   m.motionHoldUntilNewDraw ? ImVec4(0.35f, 0.95f, 0.55f, 1.0f)
                                            : ImVec4(1.00f, 0.75f, 0.25f, 1.0f));
    char decayBuf[64];
    snprintf(decayBuf, sizeof(decayBuf), "%.2f ms", m.motionDecayMs);
    DrawMetricCell("Motion Decay", decayBuf, ImVec4(0.70f, 0.85f, 1.00f, 1.0f));
    DrawMetricCell("Static Kill",
                   m.motionAutoKillStaticRamps ? "armed" : "off",
                   m.motionAutoKillStaticRamps
                       ? ImVec4(0.35f, 0.95f, 0.55f, 1.0f)
                       : ImVec4(0.70f, 0.85f, 1.00f, 1.0f));
    ImGui::EndTable();
  }

  DrawSettingHeader("Limiters & Sequence");
  if (ImGui::BeginTable("LimitSeqTable", 2,
                        ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("Color limiter: %s", g_config.enableColorLimit ? "on" : "off");
    ImGui::Text("Limit %d | Refresh %d ms | Threshold %d", g_config.colorLimit,
                g_config.colorRefresh, g_config.colorThreshold);
    ImGui::Text("Ramp limiter: %s", g_config.enableRampLimit ? "on" : "off");
    ImGui::Text("Limit %d | Refresh %d ms", g_config.rampLimit,
                g_config.rampRefresh);
    ImGui::Text("Frame skip: %d | Phase %d / %d", g_config.frameSkip,
                frameSkipPhase, frameSkipStep);
    ImGui::Separator();
    ImGui::Text("Motion Mode: %s", g_config.motionMode ? "remastered on" : "off");
    ImGui::Text("Decay %.2f ms | Concurrent %.2f%%",
                m.motionDecayMs, m.motionMaxConcurrentPercent);
    ImGui::Text("Hold ramps until new draw: %s",
                m.motionHoldUntilNewDraw ? "on" : "off");
    ImGui::Text("Auto kill static ramps: %s",
                m.motionAutoKillStaticRamps ? "on" : "off");
    if (m.motionMaxRampUpdates <= 0)
      ImGui::Text("Individual ramp refreshes unlimited | Window %d ms",
                  m.motionRampUpdateDurationMs);
    else
      ImGui::Text("Individual ramp refreshes %d max | Window %d ms",
                  m.motionMaxRampUpdates, m.motionRampUpdateDurationMs);

    ImGui::TableNextColumn();
    ImGui::Text("Sequence: %s", g_config.luminanceSeqEnabled ? "on" : "off");
    ImGui::Text("Item %d / %d | Effective %.3f", m.lumSeqIndex + 1,
                m.lumSeqCount, m.effectiveLuminance);
    DrawPercentBar("Sequence Progress", m.sequenceProgress,
                   ImVec4(0.55f, 0.75f, 1.00f, 1.0f));
    ImGui::Text("Elapsed %d ms | Remaining %d ms", m.sequenceElapsedMs,
                m.sequenceRemainingMs);
    ImGui::EndTable();
  }

  if (ImGui::CollapsingHeader("Sequence Items")) {
    ImGui::BeginChild("LumSeqItems", ImVec2(0, 135), true);
    for (int i = 0; i < g_config.luminanceSeqCount; ++i) {
      const LuminanceSeqItem *item = &g_config.luminanceSeq[i];
      if (i == m.lumSeqIndex)
        ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.55f, 1.0f), "> active");
      else
        ImGui::TextDisabled("  item");
      ImGui::SameLine();
      if (item->isRandom) {
        ImGui::Text("%02d random %.3f..%.3f favour %.2f", i + 1, item->minVal,
                    item->maxVal, item->favour);
      } else {
        ImGui::Text("%02d fixed %.3f", i + 1, item->minVal);
      }
    }
    ImGui::EndChild();
  }
  ImGui::End();
}

static void DrawRampEditor(bool &showRampEditor) {
  static char rampBuffer[MAX_LINE_LENGTH] = "";
  static int loadedRampIndex = -1;
  static bool wasOpen = false;
  static char status[160] = "";

  SanitizeConfigForUi();
  const int rampCount = SafeRampCount();
  const int currentRamp = rampCount > 0 ? ClampInt(g_config.currentRamp, 0, rampCount - 1) : -1;
  if (showRampEditor && (!wasOpen || loadedRampIndex != currentRamp)) {
    rampBuffer[0] = '\0';
    status[0] = '\0';
    if (currentRamp >= 0) {
      RampToUtf8ForEditor(&g_config.ramps[currentRamp], rampBuffer,
                          sizeof(rampBuffer));
    }
    loadedRampIndex = currentRamp;
  }
  wasOpen = showRampEditor;

  ImGui::SetNextWindowSize(ImVec2(680, 420), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(140, 120), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("UTF-8 Ramp Editor", &showRampEditor,
                    ImGuiWindowFlags_NoCollapse)) {
    TrackCurrentWindowRect(UI_RECT_RAMP_EDITOR);
    ImGui::End();
    return;
  }
  TrackCurrentWindowRect(UI_RECT_RAMP_EDITOR);

  if (currentRamp < 0) {
    ImGui::TextDisabled("No ramp is currently selected.");
    if (ImGui::Button("Cancel")) {
      showRampEditor = false;
    }
    ImGui::End();
    return;
  }

  if (ImGui::Button(g_rampEditorPage == 0 ? "[ Ramp ]" : "Ramp")) {
    g_rampEditorPage = 0;
  }
  ImGui::SameLine();
  if (ImGui::Button(g_rampEditorPage == 1 ? "[ Spacing ]" : "Spacing")) {
    g_rampEditorPage = 1;
  }
  ImGui::SameLine();
  if (ImGui::Button(g_rampEditorPage == 2 ? "[ Variable Size ]" : "Variable Size")) {
    g_rampEditorPage = 2;
  }
  ImGui::SameLine();
  ImGui::TextDisabled("| Ctrl+` spacing");

  if (g_rampEditorPage == 0) {
    ImGui::Text("Editing: %s", SafeText(g_config.ramps[currentRamp].name, "Current Ramp"));
    ImGui::TextWrapped("Type or paste UTF-8 glyphs in brightness order, darkest to brightest. Unicode blocks, symbols, CJK, emoji, and plain text are accepted when the selected font can draw them.");

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextMultiline("##RampEditorText", rampBuffer,
                              sizeof(rampBuffer), ImVec2(-1.0f, 210.0f),
                              ImGuiInputTextFlags_AllowTabInput);

    GraphemeCluster parsed[MAX_RAMP_CHARS];
    int parsedCount = ParseUTF8ToGraphemes(rampBuffer, parsed, MAX_RAMP_CHARS);
    int byteCount = (int)strlen(rampBuffer);
    ImGui::TextDisabled("%d UTF-8 bytes | %d glyph clusters | max %d",
                        byteCount, parsedCount, MAX_RAMP_CHARS);
    if (parsedCount <= 0) {
      ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
                         "Enter at least one valid UTF-8 glyph before saving.");
    }
    if (parsedCount >= MAX_RAMP_CHARS) {
      ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                         "Only the first %d glyph clusters will be used.",
                         MAX_RAMP_CHARS);
    }

    bool canSave = parsedCount > 0;
    if (!canSave)
      ImGui::BeginDisabled();
    if (ImGui::Button("Save")) {
      UnicodeRamp nextRamp;
      memset(&nextRamp, 0, sizeof(nextRamp));
      snprintf(nextRamp.name, sizeof(nextRamp.name), "%s",
               SafeText(g_config.ramps[currentRamp].name, "Edited Ramp"));
      nextRamp.count = ParseUTF8ToGraphemes(rampBuffer, nextRamp.clusters,
                                            MAX_RAMP_CHARS);
      if (nextRamp.count > 0) {
        g_config.ramps[currentRamp] = nextRamp;
        ActionRebuildRendererResources();
        snprintf(status, sizeof(status), "Saved %d glyph clusters to %s.",
                 nextRamp.count, SafeText(nextRamp.name, "current ramp"));
        showRampEditor = false;
      }
    }
    if (!canSave)
      ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
      rampBuffer[0] = '\0';
      status[0] = '\0';
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      showRampEditor = false;
      status[0] = '\0';
    }

    if (status[0]) {
      ImGui::Spacing();
      ImGui::TextDisabled("%s", status);
    }
  } else if (g_rampEditorPage == 1) {
    ImGui::TextWrapped("Spacing changes the render grid around each glyph. Positive values add gutter pixels; negative values tighten or overlap cells. Effective cell size is clamped to at least 1 px.");
    bool changed = false;
    int cellSize = g_config.cellSize;
    if (ImGui::SliderInt("Glyph Size", &cellSize, MIN_CELL_SIZE, MAX_CELL_SIZE)) {
      g_config.cellSize = ClampInt(cellSize, MIN_CELL_SIZE, MAX_CELL_SIZE);
      g_config.glyphSpacingX = ClampSpacingForCell(g_config.glyphSpacingX, g_config.cellSize);
      g_config.glyphSpacingY = ClampSpacingForCell(g_config.glyphSpacingY, g_config.cellSize);
      changed = true;
    }
    int sx = g_config.glyphSpacingX;
    if (ImGui::SliderInt("Horizontal Spacing (px)", &sx, MIN_GLYPH_SPACING, MAX_GLYPH_SPACING)) {
      g_config.glyphSpacingX = ClampSpacingForCell(sx, g_config.cellSize);
      changed = true;
    }
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::InputInt("Horizontal Spacing Exact", &g_config.glyphSpacingX, 1, 4)) {
      g_config.glyphSpacingX = ClampSpacingForCell(g_config.glyphSpacingX, g_config.cellSize);
      changed = true;
    }
    int sy = g_config.glyphSpacingY;
    if (ImGui::SliderInt("Vertical Spacing (px)", &sy, MIN_GLYPH_SPACING, MAX_GLYPH_SPACING)) {
      g_config.glyphSpacingY = ClampSpacingForCell(sy, g_config.cellSize);
      changed = true;
    }
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::InputInt("Vertical Spacing Exact", &g_config.glyphSpacingY, 1, 4)) {
      g_config.glyphSpacingY = ClampSpacingForCell(g_config.glyphSpacingY, g_config.cellSize);
      changed = true;
    }
    int atlasCellH = (int)(g_config.cellSize * FONT_ASPECT_RATIO);
    if (atlasCellH < 8) atlasCellH = 8;
    ImGui::TextDisabled("Effective grid cell: %d x %d px",
                        g_config.cellSize + g_config.glyphSpacingX,
                        atlasCellH + g_config.glyphSpacingY);
    if (changed) {
      ActionRebuildRendererResources();
    }
    if (ImGui::Button("Reset Spacing")) {
      g_config.glyphSpacingX = 0;
      g_config.glyphSpacingY = 0;
      ActionRebuildRendererResources();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
      showRampEditor = false;
    }
  } else {
    ImGui::TextWrapped("Variable size controls keep the core grid stable while changing glyph scale, virtual cell scale, and optional per-cell sampling quality.");
    DrawSettingHeader("Variable Font");
    DrawVariableFontControls();
    DrawSettingHeader("Variable Cell");
    DrawVariableCellControls();
    DrawSettingHeader("Experimental Sampling");
    DrawExperimentalSamplingControls();
    if (ImGui::Button("Close")) {
      showRampEditor = false;
    }
  }

  ImGui::End();
}

bool IsOverlayUiPointInteractive(int screenX, int screenY) {
  if (!g_menuHwnd)
    return false;
  POINT pt;
  pt.x = screenX;
  pt.y = screenY;
  if (!ScreenToClient(g_menuHwnd, &pt))
    return false;
  for (int i = 0; i < UI_RECT_COUNT; ++i) {
    const UiRect &r = g_uiRects[i];
    if (!r.active)
      continue;
    float pad = 6.0f;
    if ((float)pt.x >= r.x - pad && (float)pt.x <= r.x + r.w + pad &&
        (float)pt.y >= r.y - pad && (float)pt.y <= r.y + r.h + pad) {
      return true;
    }
  }
  return false;
}

void DrawOverlayUI(bool &showSettings, bool &showHelp, bool &showMetrics,
                   bool &showRampEditor,
                   const OverlayMetrics &metrics) {
  if (!showSettings && !showHelp && !showMetrics && !showRampEditor)
    return;

  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();
  ClearUiRects();

  if (showSettings)
    DrawSearchableSettings(showSettings);
  if (showHelp)
    DrawHelp(showHelp);
  if (showMetrics)
    DrawMetrics(showMetrics, metrics);
  if (showRampEditor)
    DrawRampEditor(showRampEditor);

  ImGui::Render();
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}
