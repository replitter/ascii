#include "cuda_renderer.h"
#include "graphics.h"
#include "font_atlas.h"
#include <cuda_runtime.h>
#include <cuda_d3d11_interop.h>
#include <device_launch_parameters.h>
#include <math.h>
#include <string.h>

#define THEME_NONE 0
#define THEME_TEAL_GREEN 1
#define THEME_CYBERPUNK 2
#define THEME_SUNSET 3
#define THEME_RETRO_WAVE 4
#define THEME_LEMON_LIME 5
#define THEME_CHROMA_GLOW 6
#define THEME_DEEP_BLURPLE 7
#define THEME_RAINBOW 8
#define THEME_AMBER_TERMINAL 9
#define THEME_MATRIX 10
#define THEME_PAPERWHITE 11
#define THEME_SOLARIZED_LIGHT 12
#define THEME_SOLARIZED_DARK 13
#define THEME_DRACULA 14
#define THEME_MONOCHROME 15
#define THEME_ICE_BLUE 16
#define THEME_MINT 17
#define THEME_ROSE_GOLD 18
#define THEME_VAPORWAVE 19
#define THEME_OCEAN 20
#define THEME_FOREST 21
#define THEME_LAVA 22
#define THEME_ARCTIC 23
#define THEME_CANDY 24
#define THEME_NEON_NOIR 25
#define THEME_PASTEL 26
#define THEME_SEPIA 27
#define THEME_EMERALD 28
#define THEME_SAPPHIRE 29
#define THEME_RUBY 30
#define THEME_GOLD 31
#define THEME_GHOST 32
#define THEME_TOXIC 33
#define THEME_MIDNIGHT 34
#define THEME_PEACH 35
#define THEME_LAVENDER 36
#define THEME_FIREWATCH 37
#define THEME_COPPER 38
#define THEME_AURORA 39
#define THEME_PLASMA 40
#define THEME_ULTRAVIOLET 41
#define THEME_TERMINAL_GREEN 42
#define THEME_CRIMSON_NIGHT 43
#define THEME_BLUEPRINT 44
#define THEME_CHERRY_BLOSSOM 45
#define THEME_ACID_RAIN 46
#define THEME_DESERT_DUSK 47
#define THEME_GLACIER 48
#define THEME_SYNTHWAVE_GOLD 49
#define THEME_SIGNAL_LOSS 50
#define THEME_PRISM 51
#define THEME_MOONLIGHT 52
#define THEME_CORAL_REEF 53
#define THEME_STEEL 54
#define THEME_JADE 55
#define THEME_INFERNO 56
#define THEME_COTTON_CANDY 57
#define THEME_NIGHT_VISION 58

#define HEAT_STYLE_CIRCLE 0
#define HEAT_STYLE_SQUARE 1
#define HEAT_STYLE_DIAMOND 2
#define HEAT_STYLE_SOFT_BOX 3
#define HEAT_STYLE_BAR 4

#define HEAT_GLYPH_BLACK 0
#define HEAT_GLYPH_HIDDEN 1

struct GPUSetupCellState {
    unsigned char colorR;
    unsigned char colorG;
    unsigned char colorB;
    unsigned char colorChangeCount;
    unsigned long colorLastReset;
    int currentClusterIndex;
    unsigned char rampChangeCount;
    unsigned long rampLastReset;
    unsigned int motionRampChangeCount;
    unsigned long motionRampLastReset;
    unsigned long motionLastGlyphUpdate;
    unsigned long motionLastDetected;
    unsigned char motionWasVisible;
    unsigned char motionWasDetected;
};

struct GPUCellCommand {
    unsigned char colorR;
    unsigned char colorG;
    unsigned char colorB;
    unsigned char active;
    int charIndex;
};

// Global GPU variables
static cudaGraphicsResource_t g_capturedRes = nullptr;
static cudaGraphicsResource_t g_outputRes = nullptr;
static cudaGraphicsResource_t g_fontAtlasRes = nullptr;

static GPUSetupCellState* g_d_cells = nullptr;
static GPUCellCommand* g_d_commands = nullptr;
static int g_cellArraySize = 0;
static int g_commandArraySize = 0;
static cudaStream_t g_stream = nullptr;

static bool g_useInterop = false;
static ID3D11Texture2D* g_captureStaging = nullptr;
static ID3D11Texture2D* g_captureStagingRing[2] = { nullptr, nullptr };
static int g_captureRingIndex = 0;
static int g_captureRingValid = 0;
static ID3D11Texture2D* g_fontStaging = nullptr;
static int g_stagingW = 0;
static int g_stagingH = 0;
static int g_fontStagingW = 0;
static int g_fontStagingH = 0;
static uchar4* g_d_capturedPixels = nullptr;
static uchar4* g_d_prevCapturedPixels = nullptr;
static uchar4* g_d_outputPixels = nullptr;
static uchar4* g_d_fontPixels = nullptr;
static size_t g_pixelCapacity = 0;
static size_t g_fontPixelCapacity = 0;
static unsigned char* g_captureHost = nullptr;
static unsigned char* g_outputHost = nullptr;
static unsigned char* g_fontHost = nullptr;
static size_t g_captureHostCapacity = 0;
static size_t g_outputHostCapacity = 0;
static size_t g_fontHostCapacity = 0;
static bool g_fontCacheValid = false;
static int g_fontCacheW = 0;
static int g_fontCacheH = 0;
static bool g_hasPrevCapturedFrame = false;
static bool g_commandsValid = false;
static bool g_fontConstantsValid = false;
static int g_fontConstantsCount = 0;

// Device constants
__constant__ int c_offsets[MAX_RAMP_CHARS];
__constant__ int c_widths[MAX_RAMP_CHARS];
__constant__ int c_displayWidths[MAX_RAMP_CHARS];

__device__ int lerpi(int a, int b, float t) {
    return a + (int)((b - a) * t);
}

__device__ float float_clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

__device__ int int_clamp(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

__device__ int byte_clamp(float v) {
    return int_clamp((int)(v + 0.5f), 0, 255);
}

__device__ unsigned int StableCellHash(unsigned int x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// Shared selection for all random per-cell effects. quota is in basis
// points (10000 = everywhere, 0 = nowhere). Exclusive mode carves bands
// out of one shared hash so effects never claim the same cell.
#define RANDOM_ASCII_SALT   0x51ED270Bu
#define RANDOM_THEME_SALT   0x3C6EF372u
#define RANDOM_INVERT_SALT  0x2F1E2F1Eu
#define RANDOM_GRAY_SALT    0x7F4A7F4Au

__device__ bool RandomEffectSelected(unsigned int cellId, int bandStart,
                                     int quota, unsigned int salt,
                                     CudaRenderParams p) {
    if (quota >= 10000) return true;
    if (quota <= 0) return false;
    unsigned int h = StableCellHash(cellId ^ (p.randomExclusive ? RANDOM_ASCII_SALT : salt));
    int v = (int)(h % 10000U);
    if (p.randomExclusive) return (v >= bandStart && v < bandStart + quota);
    return v < quota;
}

__device__ uchar4 MakeDarkenedBackground(int bgR, int bgG, int bgB, int opacity) {
    float keep = 1.0f - ((float)int_clamp(opacity, 0, 255) / 255.0f);
    return make_uchar4(
        byte_clamp((float)bgB * keep),
        byte_clamp((float)bgG * keep),
        byte_clamp((float)bgR * keep),
        255
    );
}

__device__ uchar4 BlendGlyphOverBackground(int r, int g, int b, float glyphOpacity, uchar4 bg) {
    float a = float_clamp(glyphOpacity, 0.0f, 1.0f);
    float invA = 1.0f - a;
    return make_uchar4(
        byte_clamp((float)b * a + (float)bg.x * invA),
        byte_clamp((float)g * a + (float)bg.y * invA),
        byte_clamp((float)r * a + (float)bg.z * invA),
        255
    );
}

__device__ void ApplyPalette3(float t, int* r, int* g, int* b,
                              int r0, int g0, int b0,
                              int r1, int g1, int b1,
                              int r2, int g2, int b2) {
    if (t < 0.5f) {
        float p = t * 2.0f;
        *r = lerpi(r0, r1, p); *g = lerpi(g0, g1, p); *b = lerpi(b0, b1, p);
    } else {
        float p = (t - 0.5f) * 2.0f;
        *r = lerpi(r1, r2, p); *g = lerpi(g1, g2, p); *b = lerpi(b1, b2, p);
    }
}

__device__ void ApplyPalette4(float t, int* r, int* g, int* b,
                              int r0, int g0, int b0,
                              int r1, int g1, int b1,
                              int r2, int g2, int b2,
                              int r3, int g3, int b3) {
    if (t < 0.333333f) {
        float p = t * 3.0f;
        *r = lerpi(r0, r1, p); *g = lerpi(g0, g1, p); *b = lerpi(b0, b1, p);
    } else if (t < 0.666666f) {
        float p = (t - 0.333333f) * 3.0f;
        *r = lerpi(r1, r2, p); *g = lerpi(g1, g2, p); *b = lerpi(b1, b2, p);
    } else {
        float p = (t - 0.666666f) * 3.0f;
        *r = lerpi(r2, r3, p); *g = lerpi(g2, g3, p); *b = lerpi(b2, b3, p);
    }
}

__device__ float HeatShapeAlpha(int offsetX, int offsetY, int charWidth, int cellH, int style, float radius) {
    radius = float_clamp(radius, 0.0f, 1.0f);
    if (radius <= 0.0f) return 0.0f;
    int w = charWidth > 1 ? charWidth : 1;
    int h = cellH > 1 ? cellH : 1;
    float cx = (float)w * 0.5f;
    float cy = (float)h * 0.5f;
    float nx = fabsf(((float)offsetX + 0.5f - cx) / (cx * radius));
    float ny = fabsf(((float)offsetY + 0.5f - cy) / (cy * radius));

    switch (style) {
        case HEAT_STYLE_SQUARE:
            return (nx <= 1.0f && ny <= 1.0f) ? 1.0f : 0.0f;
        case HEAT_STYLE_DIAMOND:
            return (nx + ny <= 1.0f) ? 1.0f : 0.0f;
        case HEAT_STYLE_SOFT_BOX: {
            float edge = nx > ny ? nx : ny;
            if (edge <= 0.78f) return 1.0f;
            if (edge >= 1.0f) return 0.0f;
            return (1.0f - edge) / 0.22f;
        }
        case HEAT_STYLE_BAR:
            return (ny <= 1.0f && nx <= 0.35f + radius * 0.65f) ? 1.0f : 0.0f;
        case HEAT_STYLE_CIRCLE:
        default:
            return (nx * nx + ny * ny <= 1.0f) ? 1.0f : 0.0f;
    }
}

__device__ uchar4 ApplyBlackGlyphCutout(uchar4 heatColor, float glyphOpacity, int glyphMode) {
    if (glyphMode == HEAT_GLYPH_HIDDEN) return heatColor;
    float inv = 1.0f - float_clamp(glyphOpacity, 0.0f, 1.0f);
    heatColor.x = byte_clamp((float)heatColor.x * inv);
    heatColor.y = byte_clamp((float)heatColor.y * inv);
    heatColor.z = byte_clamp((float)heatColor.z * inv);
    heatColor.w = 255;
    return heatColor;
}

__device__ void ApplyTheme(float intensity, int theme, float time, 
                           int* r, int* g, int* b, int origR, int origG, int origB) {
    float t = float_clamp(intensity, 0.0f, 1.0f);
    switch (theme) {
        case THEME_NONE:
            *r = origR; *g = origG; *b = origB;
            break;
        case THEME_TEAL_GREEN:
            if (t < 0.3f) {
                float p = t / 0.3f;
                *r = lerpi(5, 20, p); *g = lerpi(40, 100, p); *b = lerpi(50, 80, p);
            } else if (t < 0.6f) {
                float p = (t - 0.3f) / 0.3f;
                *r = lerpi(20, 40, p); *g = lerpi(100, 180, p); *b = lerpi(80, 100, p);
            } else {
                float p = (t - 0.6f) / 0.4f;
                *r = lerpi(40, 100, p); *g = lerpi(180, 255, p); *b = lerpi(100, 150, p);
            }
            break;
        case THEME_CYBERPUNK:
            if (t < 0.25f) {
                float p = t / 0.25f;
                *r = lerpi(20, 255, p); *g = lerpi(5, 20, p); *b = lerpi(30, 100, p);
            } else if (t < 0.5f) {
                float p = (t - 0.25f) / 0.25f;
                *r = 255; *g = lerpi(20, 0, p); *b = lerpi(100, 200, p);
            } else if (t < 0.75f) {
                float p = (t - 0.5f) / 0.25f;
                *r = lerpi(255, 0, p); *g = lerpi(0, 200, p); *b = 255;
            } else {
                float p = (t - 0.75f) / 0.25f;
                *r = lerpi(0, 255, p); *g = lerpi(200, 255, p); *b = lerpi(255, 50, p);
            }
            break;
        case THEME_SUNSET:
            if (t < 0.2f) {
                float p = t / 0.2f;
                *r = lerpi(40, 100, p); *g = lerpi(10, 30, p); *b = lerpi(60, 100, p);
            } else if (t < 0.4f) {
                float p = (t - 0.2f) / 0.2f;
                *r = lerpi(100, 200, p); *g = lerpi(30, 50, p); *b = lerpi(100, 80, p);
            } else if (t < 0.6f) {
                float p = (t - 0.4f) / 0.2f;
                *r = lerpi(200, 255, p); *g = lerpi(50, 100, p); *b = lerpi(80, 30, p);
            } else if (t < 0.8f) {
                float p = (t - 0.6f) / 0.2f;
                *r = 255; *g = lerpi(100, 180, p); *b = lerpi(30, 20, p);
            } else {
                float p = (t - 0.8f) / 0.2f;
                *r = 255; *g = lerpi(180, 255, p); *b = lerpi(20, 100, p);
            }
            break;
        case THEME_RETRO_WAVE: {
            float pulse = 0.5f + 0.5f * sinf(time * 3.0f + t * 6.28f);
            if (t < 0.3f) {
                float p = t / 0.3f;
                *r = lerpi(30, 200, p); *g = lerpi(10, 30, p); *b = lerpi(50, 150, p);
            } else if (t < 0.5f) {
                float p = (t - 0.3f) / 0.2f;
                *r = lerpi(200, 255, p) + (int)(pulse * 20); *g = lerpi(30, 50, p); *b = lerpi(150, 200, p);
            } else if (t < 0.7f) {
                float p = (t - 0.5f) / 0.2f;
                *r = lerpi(255, 100, p); *g = lerpi(50, 200, p); *b = lerpi(200, 255, p) + (int)(pulse * 20);
            } else {
                float p = (t - 0.7f) / 0.3f;
                *r = lerpi(100, 50, p); *g = lerpi(200, 255, p); *b = 255;
            }
        } break;
        case THEME_LEMON_LIME:
            if (t < 0.3f) {
                float p = t / 0.3f;
                *r = lerpi(30, 80, p); *g = lerpi(80, 180, p); *b = lerpi(20, 30, p);
            } else if (t < 0.6f) {
                float p = (t - 0.3f) / 0.3f;
                *r = lerpi(80, 180, p); *g = lerpi(180, 230, p); *b = lerpi(30, 50, p);
            } else {
                float p = (t - 0.6f) / 0.4f;
                *r = lerpi(180, 255, p); *g = lerpi(230, 255, p); *b = lerpi(50, 100, p);
            }
            break;
        case THEME_CHROMA_GLOW: {
            float hue = t * 0.8f + time * 0.1f;
            hue = hue - floorf(hue);
            float sat = 0.8f + t * 0.2f;
            float val = 0.4f + t * 0.6f;
            int hi = (int)(hue * 6.0f) % 6;
            float f = hue * 6.0f - floorf(hue * 6.0f);
            int p_val = (int)(255 * val * (1 - sat));
            int q_val = (int)(255 * val * (1 - f * sat));
            int t_val = (int)(255 * val * (1 - (1 - f) * sat));
            int v_val = (int)(255 * val);
            switch (hi) {
                case 0: *r = v_val; *g = t_val; *b = p_val; break;
                case 1: *r = q_val; *g = v_val; *b = p_val; break;
                case 2: *r = p_val; *g = v_val; *b = t_val; break;
                case 3: *r = p_val; *g = q_val; *b = v_val; break;
                case 4: *r = t_val; *g = p_val; *b = v_val; break;
                default: *r = v_val; *g = p_val; *b = q_val; break;
            }
        } break;
        case THEME_DEEP_BLURPLE: {
            float pulse = 0.5f + 0.5f * sinf(time * 2.0f + t * 3.14159f);
            if (t < 0.25f) {
                float p = t / 0.25f;
                *r = lerpi(20, 60, p); *g = lerpi(15, 40, p); *b = lerpi(50, 120, p);
            } else if (t < 0.5f) {
                float p = (t - 0.25f) / 0.25f;
                *r = lerpi(60, 100, p) + (int)(pulse * 15); *g = lerpi(40, 70, p); *b = lerpi(120, 200, p);
            } else if (t < 0.75f) {
                float p = (t - 0.5f) / 0.25f;
                *r = lerpi(100, 130, p); *g = lerpi(70, 120, p); *b = lerpi(200, 255, p) + (int)(pulse * 20);
            } else {
                float p = (t - 0.75f) / 0.25f;
                *r = lerpi(130, 180, p); *g = lerpi(120, 200, p); *b = 255;
            }
        } break;
        case THEME_RAINBOW: {
            float hue = t + time * 0.15f;
            hue = hue - floorf(hue);
            int hi = (int)(hue * 6.0f) % 6;
            float f = hue * 6.0f - floorf(hue * 6.0f);
            int v = (int)(255 * (0.5f + t * 0.5f));
            int p = (int)(v * 0.1f);
            int q = (int)(v * (1 - f * 0.9f));
            int tt = (int)(v * (1 - (1 - f) * 0.9f));
            switch (hi) {
                case 0: *r = v; *g = tt; *b = p; break;
                case 1: *r = q; *g = v; *b = p; break;
                case 2: *r = p; *g = v; *b = tt; break;
                case 3: *r = p; *g = q; *b = v; break;
                case 4: *r = tt; *g = p; *b = v; break;
                default: *r = v; *g = p; *b = q; break;
            }
        } break;
        case THEME_AMBER_TERMINAL:
            ApplyPalette3(t, r, g, b, 32, 18, 0, 220, 120, 12, 255, 210, 90);
            break;
        case THEME_MATRIX:
            ApplyPalette3(t, r, g, b, 0, 12, 0, 0, 180, 45, 180, 255, 180);
            break;
        case THEME_PAPERWHITE:
            ApplyPalette3(t, r, g, b, 45, 45, 42, 150, 145, 130, 255, 250, 225);
            break;
        case THEME_SOLARIZED_LIGHT:
            ApplyPalette4(t, r, g, b, 88, 110, 117, 38, 139, 210, 42, 161, 152, 253, 246, 227);
            break;
        case THEME_SOLARIZED_DARK:
            ApplyPalette4(t, r, g, b, 0, 43, 54, 38, 139, 210, 181, 137, 0, 238, 232, 213);
            break;
        case THEME_DRACULA:
            ApplyPalette4(t, r, g, b, 40, 42, 54, 189, 147, 249, 255, 121, 198, 248, 248, 242);
            break;
        case THEME_MONOCHROME:
            ApplyPalette3(t, r, g, b, 12, 12, 12, 140, 140, 140, 255, 255, 255);
            break;
        case THEME_ICE_BLUE:
            ApplyPalette3(t, r, g, b, 5, 18, 35, 70, 170, 230, 220, 250, 255);
            break;
        case THEME_MINT:
            ApplyPalette3(t, r, g, b, 5, 35, 28, 70, 210, 150, 225, 255, 235);
            break;
        case THEME_ROSE_GOLD:
            ApplyPalette4(t, r, g, b, 45, 20, 28, 180, 95, 110, 245, 170, 145, 255, 230, 190);
            break;
        case THEME_VAPORWAVE:
            ApplyPalette4(t, r, g, b, 30, 15, 80, 255, 90, 210, 90, 245, 255, 255, 245, 180);
            break;
        case THEME_OCEAN:
            ApplyPalette4(t, r, g, b, 0, 22, 45, 0, 105, 150, 30, 210, 220, 210, 255, 245);
            break;
        case THEME_FOREST:
            ApplyPalette3(t, r, g, b, 6, 22, 12, 35, 135, 55, 190, 245, 160);
            break;
        case THEME_LAVA:
            ApplyPalette4(t, r, g, b, 25, 0, 0, 180, 20, 0, 255, 95, 0, 255, 230, 80);
            break;
        case THEME_ARCTIC:
            ApplyPalette3(t, r, g, b, 10, 24, 45, 100, 185, 255, 245, 255, 255);
            break;
        case THEME_CANDY:
            ApplyPalette4(t, r, g, b, 80, 20, 120, 255, 115, 190, 120, 220, 255, 255, 245, 160);
            break;
        case THEME_NEON_NOIR: {
            float pulse = 0.5f + 0.5f * sinf(time * 4.0f + t * 9.0f);
            ApplyPalette4(t, r, g, b, 3, 4, 12, 0, 220, 255, 255, 0, 180, 255, 255, 255);
            *r += (int)(pulse * 18.0f);
            *b += (int)(pulse * 22.0f);
        } break;
        case THEME_PASTEL:
            ApplyPalette4(t, r, g, b, 90, 95, 120, 170, 210, 255, 255, 190, 220, 255, 245, 205);
            break;
        case THEME_SEPIA:
            ApplyPalette3(t, r, g, b, 35, 24, 12, 140, 95, 45, 255, 225, 160);
            break;
        case THEME_EMERALD:
            ApplyPalette3(t, r, g, b, 0, 20, 12, 0, 155, 95, 170, 255, 210);
            break;
        case THEME_SAPPHIRE:
            ApplyPalette3(t, r, g, b, 5, 10, 45, 35, 105, 230, 200, 225, 255);
            break;
        case THEME_RUBY:
            ApplyPalette3(t, r, g, b, 35, 0, 15, 175, 18, 65, 255, 190, 210);
            break;
        case THEME_GOLD:
            ApplyPalette3(t, r, g, b, 35, 24, 0, 210, 150, 20, 255, 245, 170);
            break;
        case THEME_GHOST:
            ApplyPalette3(t, r, g, b, 20, 25, 35, 140, 160, 190, 245, 250, 255);
            break;
        case THEME_TOXIC:
            ApplyPalette4(t, r, g, b, 12, 25, 0, 90, 210, 0, 210, 255, 20, 245, 255, 160);
            break;
        case THEME_MIDNIGHT:
            ApplyPalette4(t, r, g, b, 0, 0, 20, 20, 35, 100, 90, 80, 190, 220, 230, 255);
            break;
        case THEME_PEACH:
            ApplyPalette3(t, r, g, b, 60, 30, 35, 245, 130, 110, 255, 235, 190);
            break;
        case THEME_LAVENDER:
            ApplyPalette3(t, r, g, b, 32, 24, 60, 150, 120, 230, 245, 230, 255);
            break;
        case THEME_FIREWATCH:
            ApplyPalette4(t, r, g, b, 20, 20, 55, 140, 55, 75, 230, 120, 65, 255, 220, 120);
            break;
        case THEME_COPPER:
            ApplyPalette3(t, r, g, b, 38, 20, 10, 185, 95, 35, 255, 205, 135);
            break;
        case THEME_AURORA:
            ApplyPalette4(t, r, g, b, 5, 22, 38, 35, 180, 145, 140, 90, 240, 245, 235, 180);
            break;
        case THEME_PLASMA:
            ApplyPalette4(t, r, g, b, 20, 0, 35, 160, 30, 210, 255, 90, 75, 255, 230, 80);
            break;
        case THEME_ULTRAVIOLET:
            ApplyPalette3(t, r, g, b, 18, 0, 40, 115, 25, 210, 230, 210, 255);
            break;
        case THEME_TERMINAL_GREEN:
            ApplyPalette3(t, r, g, b, 0, 18, 0, 25, 170, 45, 190, 255, 185);
            break;
        case THEME_CRIMSON_NIGHT:
            ApplyPalette4(t, r, g, b, 12, 0, 8, 90, 0, 28, 210, 30, 60, 255, 170, 150);
            break;
        case THEME_BLUEPRINT:
            ApplyPalette3(t, r, g, b, 4, 22, 56, 45, 115, 210, 210, 235, 255);
            break;
        case THEME_CHERRY_BLOSSOM:
            ApplyPalette4(t, r, g, b, 48, 20, 38, 185, 75, 120, 255, 165, 205, 255, 235, 245);
            break;
        case THEME_ACID_RAIN:
            ApplyPalette4(t, r, g, b, 5, 18, 8, 120, 240, 20, 215, 255, 60, 245, 255, 180);
            break;
        case THEME_DESERT_DUSK:
            ApplyPalette4(t, r, g, b, 35, 18, 36, 120, 60, 85, 220, 135, 55, 255, 220, 135);
            break;
        case THEME_GLACIER:
            ApplyPalette4(t, r, g, b, 8, 22, 35, 50, 145, 180, 165, 230, 245, 245, 255, 255);
            break;
        case THEME_SYNTHWAVE_GOLD:
            ApplyPalette4(t, r, g, b, 25, 8, 55, 180, 40, 175, 255, 150, 45, 255, 238, 120);
            break;
        case THEME_SIGNAL_LOSS: {
            float noise = 0.5f + 0.5f * sinf(time * 17.0f + t * 53.0f);
            ApplyPalette3(t, r, g, b, 16, 16, 20, 120, 125, 135, 245, 245, 250);
            *r += (int)(noise * 35.0f);
            *g -= (int)(noise * 18.0f);
            *b += (int)(noise * 24.0f);
        } break;
        case THEME_PRISM: {
            float hue = t * 0.9f + time * 0.04f;
            hue = hue - floorf(hue);
            int hi = (int)(hue * 6.0f) % 6;
            float f = hue * 6.0f - floorf(hue * 6.0f);
            int v = (int)(180 + 75 * t);
            int p = (int)(v * 0.35f);
            int q = (int)(v * (1.0f - f * 0.65f));
            int tt = (int)(v * (1.0f - (1.0f - f) * 0.65f));
            switch (hi) {
                case 0: *r = v; *g = tt; *b = p; break;
                case 1: *r = q; *g = v; *b = p; break;
                case 2: *r = p; *g = v; *b = tt; break;
                case 3: *r = p; *g = q; *b = v; break;
                case 4: *r = tt; *g = p; *b = v; break;
                default: *r = v; *g = p; *b = q; break;
            }
        } break;
        case THEME_MOONLIGHT:
            ApplyPalette4(t, r, g, b, 8, 12, 28, 45, 62, 105, 135, 160, 205, 235, 240, 255);
            break;
        case THEME_CORAL_REEF:
            ApplyPalette4(t, r, g, b, 0, 45, 60, 20, 175, 170, 255, 115, 105, 255, 220, 170);
            break;
        case THEME_STEEL:
            ApplyPalette3(t, r, g, b, 18, 22, 28, 105, 120, 135, 225, 235, 240);
            break;
        case THEME_JADE:
            ApplyPalette4(t, r, g, b, 0, 28, 22, 20, 130, 95, 90, 215, 160, 220, 255, 220);
            break;
        case THEME_INFERNO:
            ApplyPalette4(t, r, g, b, 0, 0, 4, 105, 8, 35, 230, 55, 22, 255, 230, 95);
            break;
        case THEME_COTTON_CANDY:
            ApplyPalette4(t, r, g, b, 70, 45, 100, 255, 120, 205, 135, 225, 255, 255, 245, 245);
            break;
        case THEME_NIGHT_VISION:
            ApplyPalette3(t, r, g, b, 0, 12, 0, 35, 150, 35, 205, 255, 140);
            break;
        default:
            *r = origR; *g = origG; *b = origB;
    }
    *r = int_clamp(*r, 0, 255);
    *g = int_clamp(*g, 0, 255);
    *b = int_clamp(*b, 0, 255);
}

__device__ void TransformDesktopPixel(uchar4 src, CudaRenderParams p,
                                      int* outR, int* outG, int* outB,
                                      int* outLum,
                                      int applyGrayscale, int applyInvert,
                                      int applyTheme) {
    float rf = src.z;
    float gf = src.y;
    float bf = src.x;

    if (p.brightness != 1.0f) {
        rf *= p.brightness;
        gf *= p.brightness;
        bf *= p.brightness;
    }
    if (p.contrast != 1.0f) {
        rf = (rf - 128.0f) * p.contrast + 128.0f;
        gf = (gf - 128.0f) * p.contrast + 128.0f;
        bf = (bf - 128.0f) * p.contrast + 128.0f;
    }
    if (p.gamma != 1.0f) {
        float inv = 1.0f / p.gamma;
        rf = 255.0f * powf(float_clamp(rf / 255.0f, 0.0f, 1.0f), inv);
        gf = 255.0f * powf(float_clamp(gf / 255.0f, 0.0f, 1.0f), inv);
        bf = 255.0f * powf(float_clamp(bf / 255.0f, 0.0f, 1.0f), inv);
    }

    int ir = byte_clamp(rf);
    int ig = byte_clamp(gf);
    int ib = byte_clamp(bf);
    if (applyGrayscale) {
        int gray = (ir * 30 + ig * 59 + ib * 11) / 100;
        ir = ig = ib = gray;
    }
    if (applyInvert) {
        ir = 255 - ir;
        ig = 255 - ig;
        ib = 255 - ib;
    }

    int lum = (ir * 30 + ig * 59 + ib * 11) / 100;
    lum = int_clamp((int)((float)lum * p.finalLuminanceMultiplier + 0.5f), 0, 255);
    if (applyTheme) {
        ApplyTheme((float)lum / 255.0f, p.colorTheme, p.time, outR, outG, outB, ir, ig, ib);
    } else {
        *outR = ir;
        *outG = ig;
        *outB = ib;
    }
    if (outLum) *outLum = lum;
}

__device__ void PullRgbTowardLuminance(int targetLum, int* r, int* g, int* b) {
    targetLum = int_clamp(targetLum, 0, 255);
    int currentLum = int_clamp((*r * 30 + *g * 59 + *b * 11) / 100, 0, 255);
    if (currentLum <= 0) {
        *r = targetLum;
        *g = targetLum;
        *b = targetLum;
        return;
    }
    float scale = (float)targetLum / (float)currentLum;
    *r = byte_clamp((float)*r * scale);
    *g = byte_clamp((float)*g * scale);
    *b = byte_clamp((float)*b * scale);
}

__device__ void BlendRgb(int* r, int* g, int* b, int tr, int tg, int tb, float amount) {
    amount = float_clamp(amount, 0.0f, 1.0f);
    float keep = 1.0f - amount;
    *r = byte_clamp((float)*r * keep + (float)tr * amount);
    *g = byte_clamp((float)*g * keep + (float)tg * amount);
    *b = byte_clamp((float)*b * keep + (float)tb * amount);
}

__device__ float VariableFontScaleForCell(int col, int row, CudaRenderParams p) {
    if (!p.variableFontMode) return 1.0f;
    int region = int_clamp(p.variableFontRegionSize, 1, 128);
    int regionX = col / region;
    int regionY = row / region;
    unsigned int seed = (unsigned int)(regionX * 73856093u) ^
                        (unsigned int)(regionY * 19349663u) ^
                        (unsigned int)p.variableFontSeed;
    float rnd = (float)(StableCellHash(seed) & 0xFFFFu) / 65535.0f;
    float randomness = float_clamp(p.variableFontRandomness, 0.0f, 1.0f);
    rnd = 0.5f + (rnd - 0.5f) * randomness;
    if (p.variableFontPulseSpeed > 0.0f) {
        float pulse = 0.5f + 0.5f * sinf(p.time * p.variableFontPulseSpeed +
                                          (float)(regionX * 17 + regionY * 31));
        rnd = rnd * 0.75f + pulse * 0.25f;
    }
    float minScale = float_clamp(p.variableFontMinScale, 0.10f, 3.0f);
    float maxScale = float_clamp(p.variableFontMaxScale, 0.10f, 3.0f);
    if (maxScale < minScale) {
        float t = minScale;
        minScale = maxScale;
        maxScale = t;
    }
    if (p.strictNoFontOverlap && maxScale > 1.0f) maxScale = 1.0f;
    return float_clamp(minScale + (maxScale - minScale) * rnd, 0.10f,
                       p.strictNoFontOverlap ? 1.0f : 3.0f);
}

__device__ float VariableCellScaleForCell(int col, int row, CudaRenderParams p) {
    if (!p.variableCellMode) return 1.0f;
    int region = int_clamp(p.variableCellRegionSize, 1, 128);
    int regionX = col / region;
    int regionY = row / region;
    unsigned int seed = (unsigned int)(regionX * 83492791u) ^
                        (unsigned int)(regionY * 2654435761u) ^
                        (unsigned int)p.variableCellSeed;
    float rnd = (float)(StableCellHash(seed) & 0xFFFFu) / 65535.0f;
    float randomness = float_clamp(p.variableCellRandomness, 0.0f, 1.0f);
    rnd = 0.5f + (rnd - 0.5f) * randomness;
    if (p.variableCellPulseSpeed > 0.0f) {
        float pulse = 0.5f + 0.5f * sinf(p.time * p.variableCellPulseSpeed +
                                          (float)(regionX * 13 + regionY * 29));
        rnd = rnd * 0.75f + pulse * 0.25f;
    }
    float minScale = float_clamp(p.variableCellMinScale, 0.10f, 3.0f);
    float maxScale = float_clamp(p.variableCellMaxScale, 0.10f, 3.0f);
    if (maxScale < minScale) {
        float t = minScale;
        minScale = maxScale;
        maxScale = t;
    }
    if (p.strictNoCellOverlap && maxScale > 1.0f) maxScale = 1.0f;
    return float_clamp(minScale + (maxScale - minScale) * rnd, 0.10f,
                       p.strictNoCellOverlap ? 1.0f : 3.0f);
}

__device__ float EffectiveGlyphScaleForCell(int col, int row, CudaRenderParams p) {
    float scale = VariableFontScaleForCell(col, row, p);
    if (p.variableCellMode && p.variableCellAffectsFont) {
        scale *= VariableCellScaleForCell(col, row, p);
    }
    float maxScale = (p.strictNoFontOverlap || p.strictNoCellOverlap) ? 1.0f : 3.0f;
    return float_clamp(scale, 0.10f, maxScale);
}

__device__ float VariableFontAxisScaleForCell(int col, int row, CudaRenderParams p, int axis) {
    if (!p.variableFontMode || !p.variableFontIndependentAxes) return 1.0f;
    int region = int_clamp(p.variableFontRegionSize, 1, 128);
    int regionX = col / region;
    int regionY = row / region;
    unsigned int seed = (unsigned int)(regionX * 2246822519u) ^
                        (unsigned int)(regionY * 3266489917u) ^
                        (unsigned int)p.variableFontSeed ^
                        (axis ? 0x9E3779B9u : 0x85EBCA6Bu);
    float rnd = (float)(StableCellHash(seed) & 0xFFFFu) / 65535.0f;
    float randomness = float_clamp(p.variableFontRandomness, 0.0f, 1.0f);
    rnd = 0.5f + (rnd - 0.5f) * randomness;
    float minScale = axis ? p.variableFontMinHeightScale : p.variableFontMinWidthScale;
    float maxScale = axis ? p.variableFontMaxHeightScale : p.variableFontMaxWidthScale;
    minScale = float_clamp(minScale, 0.10f, 3.0f);
    maxScale = float_clamp(maxScale, 0.10f, 3.0f);
    if (maxScale < minScale) {
        float t = minScale;
        minScale = maxScale;
        maxScale = t;
    }
    if (p.strictNoFontOverlap && maxScale > 1.0f) maxScale = 1.0f;
    return float_clamp(minScale + (maxScale - minScale) * rnd, 0.10f,
                       p.strictNoFontOverlap ? 1.0f : 3.0f);
}

__device__ uchar4 SampleCellPixel(const uchar4* pixels, int col, int row, CudaRenderParams p) {
    int centerX = int_clamp(col * p.cellW + p.cellW / 2, 0, p.screenW - 1);
    int centerY = int_clamp(row * p.cellH + p.cellH / 2, 0, p.screenH - 1);
    if (!p.experimentalCellSampling) {
        return pixels[centerY * p.screenW + centerX];
    }

    int grid = int_clamp(p.cellSampleGrid, 1, 50);
    float cellScale = (p.variableCellMode && p.variableCellAffectsSampling)
                          ? VariableCellScaleForCell(col, row, p)
                          : 1.0f;
    float radiusScale = float_clamp(p.cellSampleRadiusScale, 0.10f, 3.0f) * cellScale;
    float halfW = fmaxf(0.5f, (float)p.cellW * 0.5f * radiusScale);
    float halfH = fmaxf(0.5f, (float)p.cellH * 0.5f * radiusScale);
    unsigned int jitterSeed = StableCellHash((unsigned int)(col * 92837111u) ^
                                             (unsigned int)(row * 689287499u) ^
                                             (unsigned int)p.variableCellSeed);
    float jitter = float_clamp(p.cellSampleJitter, 0.0f, 1.0f);
    float jitterX = ((((float)(jitterSeed & 0xFFu) / 255.0f) - 0.5f) * jitter);
    float jitterY = ((((float)((jitterSeed >> 8) & 0xFFu) / 255.0f) - 0.5f) * jitter);

    float sumR = 0.0f, sumG = 0.0f, sumB = 0.0f, sumW = 0.0f;
    float minLum = 255.0f, maxLum = 0.0f;
    uchar4 minPx = pixels[centerY * p.screenW + centerX];
    uchar4 maxPx = minPx;
    int samples = 0;

    for (int gy = 0; gy < grid; ++gy) {
        for (int gx = 0; gx < grid; ++gx) {
            if (p.cellSampleMode == 1 && grid > 1 && gx != grid / 2 && gy != grid / 2) continue;
            if (p.cellSampleMode == 2 && grid > 1 && abs(gx - grid / 2) + abs(gy - grid / 2) > grid / 2) continue;
            float fx = (grid <= 1) ? 0.5f : ((float)gx + 0.5f + jitterX) / (float)grid;
            float fy = (grid <= 1) ? 0.5f : ((float)gy + 0.5f + jitterY) / (float)grid;
            int sx = int_clamp((int)((float)centerX + (fx - 0.5f) * 2.0f * halfW), 0, p.screenW - 1);
            int sy = int_clamp((int)((float)centerY + (fy - 0.5f) * 2.0f * halfH), 0, p.screenH - 1);
            uchar4 px = pixels[sy * p.screenW + sx];
            float lum = (float)((int)px.z * 30 + (int)px.y * 59 + (int)px.x * 11) / 100.0f;
            if (lum <= minLum) {
                minLum = lum;
                minPx = px;
            }
            if (lum >= maxLum) {
                maxLum = lum;
                maxPx = px;
            }
            float weight = 1.0f;
            if (gx == grid / 2 && gy == grid / 2) {
                weight += float_clamp(p.cellSampleCenterWeight, 0.0f, 8.0f);
            }
            sumR += (float)px.z * weight;
            sumG += (float)px.y * weight;
            sumB += (float)px.x * weight;
            sumW += weight;
            samples++;
        }
    }
    if (samples <= 0) return pixels[centerY * p.screenW + centerX];

    if (sumW <= 0.0f) sumW = (float)samples;
    float inv = 1.0f / sumW;
    float r = sumR * inv;
    float g = sumG * inv;
    float b = sumB * inv;
    float detailMix = float_clamp(p.cellSampleDetailMix, 0.0f, 1.0f);
    if (detailMix > 0.0f) {
        float targetR = r, targetG = g, targetB = b;
        if (p.cellSampleColorMode == 1) {
            targetR = (float)maxPx.z; targetG = (float)maxPx.y; targetB = (float)maxPx.x;
        } else if (p.cellSampleColorMode == 2) {
            targetR = (float)minPx.z; targetG = (float)minPx.y; targetB = (float)minPx.x;
        } else if (p.cellSampleColorMode == 3) {
            float range = float_clamp((maxLum - minLum) / 255.0f, 0.0f, 1.0f);
            targetR = (float)minPx.z * (1.0f - range) + (float)maxPx.z * range;
            targetG = (float)minPx.y * (1.0f - range) + (float)maxPx.y * range;
            targetB = (float)minPx.x * (1.0f - range) + (float)maxPx.x * range;
        }
        r = r * (1.0f - detailMix) + targetR * detailMix;
        g = g * (1.0f - detailMix) + targetG * detailMix;
        b = b * (1.0f - detailMix) + targetB * detailMix;
    }
    float edgeBoost = float_clamp(p.cellSampleEdgeBoost, 0.0f, 2.0f);
    if (edgeBoost > 0.0f) {
        float avgLum = (r * 30.0f + g * 59.0f + b * 11.0f) / 100.0f;
        float range = (maxLum - minLum) / 255.0f;
        float factor = 1.0f + edgeBoost * range;
        r = (r - avgLum) * factor + avgLum;
        g = (g - avgLum) * factor + avgLum;
        b = (b - avgLum) * factor + avgLum;
    }

    float compensation = float_clamp(p.cellSampleLuminanceCompensation, 0.0f, 2.0f);
    float preserve = float_clamp(p.cellSampleHighlightPreserve, 0.0f, 1.0f);
    if (compensation > 0.0f || preserve > 0.0f) {
        float currentLum = (r * 30.0f + g * 59.0f + b * 11.0f) / 100.0f;
        currentLum = fmaxf(currentLum, 1.0f);
        float highlightLift = fmaxf(0.0f, maxLum - currentLum) * preserve;
        float targetLum = currentLum + highlightLift + currentLum * compensation;
        float scale = float_clamp(targetLum / currentLum, 0.0f, 4.0f);
        r *= scale;
        g *= scale;
        b *= scale;
    }

    return make_uchar4((unsigned char)byte_clamp(b),
                       (unsigned char)byte_clamp(g),
                       (unsigned char)byte_clamp(r),
                       255);
}

__device__ int AmbientPixelSelected(int x, int y, CudaRenderParams p) {
    float percent = float_clamp(p.ambientLitPercent, 0.0f, 100.0f);
    if (percent <= 0.0f) return 0;
    if (percent >= 100.0f) return 1;
    unsigned int seed = (unsigned int)(x * 73856093u) ^
                        (unsigned int)(y * 19349663u) ^
                        (unsigned int)(p.screenW * 83492791u);
    int quota = int_clamp((int)(percent * 100.0f + 0.5f), 0, 10000);
    return ((int)(StableCellHash(seed) % 10000u) < quota) ? 1 : 0;
}

__device__ void AccumulateAmbientSample(const uchar4* capturedPixels,
                                        const GPUCellCommand* commands,
                                        CudaRenderParams p,
                                        int sx, int sy,
                                        float weight,
                                        float match,
                                        float* glowR,
                                        float* glowG,
                                        float* glowB,
                                        float* glowW) {
    if (sx < 0 || sy < 0 || sx >= p.screenW || sy >= p.screenH) return;
    if (!AmbientPixelSelected(sx, sy, p)) return;

    float localR = 0.0f;
    float localG = 0.0f;
    float localB = 0.0f;
    float sourceCount = 0.0f;

    if (p.ambientFromSource) {
        uchar4 src = capturedPixels[sy * p.screenW + sx];
        int themedR = 0, themedG = 0, themedB = 0, lum = 0;
        TransformDesktopPixel(src, p, &themedR, &themedG, &themedB, &lum, 1, 1, 1);
        int srcR = src.z;
        int srcG = src.y;
        int srcB = src.x;
        localR += (float)byte_clamp((float)themedR * (1.0f - match) + (float)srcR * match);
        localG += (float)byte_clamp((float)themedG * (1.0f - match) + (float)srcG * match);
        localB += (float)byte_clamp((float)themedB * (1.0f - match) + (float)srcB * match);
        sourceCount += 1.0f;
    }

    if (p.ambientFromRamp && commands && p.cols > 0 && p.rows > 0) {
        int col = int_clamp(sx / p.cellW, 0, p.cols - 1);
        int row = int_clamp(sy / p.cellH, 0, p.rows - 1);
        GPUCellCommand cmd = commands[row * p.cols + col];
        if (!p.motionMode || cmd.active) {
            int rampR = cmd.colorR;
            int rampG = cmd.colorG;
            int rampB = cmd.colorB;
            if (cmd.charIndex >= 0 && p.fontCount > 1) {
                int glyphLum = int_clamp((cmd.charIndex * 255) / (p.fontCount - 1), 0, 255);
                PullRgbTowardLuminance(glyphLum, &rampR, &rampG, &rampB);
            }
            localR += (float)rampR;
            localG += (float)rampG;
            localB += (float)rampB;
            sourceCount += 1.0f;
        }
    }

    if (sourceCount <= 0.0f) return;
    *glowR += (localR / sourceCount) * weight;
    *glowG += (localG / sourceCount) * weight;
    *glowB += (localB / sourceCount) * weight;
    *glowW += weight;
}

__device__ uchar4 ApplyAmbientGlow(const uchar4* capturedPixels,
                                   const GPUCellCommand* commands,
                                   int x, int y,
                                   uchar4 baseColor,
                                   CudaRenderParams p) {
    if (!p.ambientMode || !capturedPixels) return baseColor;
    if (!p.ambientFromSource && !p.ambientFromRamp) return baseColor;
    float strength = float_clamp(p.ambientGlowStrength, 0.0f, 5.0f);
    if (strength <= 0.0f) return baseColor;

    float radius = float_clamp(p.ambientRadius, 0.0f, 256.0f);
    int subdivisions = int_clamp(p.ambientSubdivisions, 1, 16);
    float progressive = float_clamp(p.ambientProgressiveBleed, 0.0f, 1.0f);
    float match = float_clamp(p.ambientColorMatch, 0.0f, 100.0f) / 100.0f;

    float glowR = 0.0f;
    float glowG = 0.0f;
    float glowB = 0.0f;
    float glowW = 0.0f;

    AccumulateAmbientSample(capturedPixels, commands, p, x, y, 1.0f, match,
                            &glowR, &glowG, &glowB, &glowW);

    if (radius > 0.0f) {
        for (int ring = 1; ring <= subdivisions; ++ring) {
            float ringT = (float)ring / (float)subdivisions;
            int dist = int_clamp((int)(radius * ringT + 0.5f), 1, 256);
            float falloff = 1.0f - float_clamp((float)dist / radius, 0.0f, 1.0f);
            float curve = 1.0f + (1.0f - progressive) * 3.0f;
            float weight = powf(falloff, curve);
            weight *= (0.35f + progressive * 0.65f);
            if (weight <= 0.001f) continue;

            AccumulateAmbientSample(capturedPixels, commands, p, x + dist, y, weight, match, &glowR, &glowG, &glowB, &glowW);
            AccumulateAmbientSample(capturedPixels, commands, p, x - dist, y, weight, match, &glowR, &glowG, &glowB, &glowW);
            AccumulateAmbientSample(capturedPixels, commands, p, x, y + dist, weight, match, &glowR, &glowG, &glowB, &glowW);
            AccumulateAmbientSample(capturedPixels, commands, p, x, y - dist, weight, match, &glowR, &glowG, &glowB, &glowW);
            AccumulateAmbientSample(capturedPixels, commands, p, x + dist, y + dist, weight * 0.7f, match, &glowR, &glowG, &glowB, &glowW);
            AccumulateAmbientSample(capturedPixels, commands, p, x - dist, y + dist, weight * 0.7f, match, &glowR, &glowG, &glowB, &glowW);
            AccumulateAmbientSample(capturedPixels, commands, p, x + dist, y - dist, weight * 0.7f, match, &glowR, &glowG, &glowB, &glowW);
            AccumulateAmbientSample(capturedPixels, commands, p, x - dist, y - dist, weight * 0.7f, match, &glowR, &glowG, &glowB, &glowW);
        }
    }

    if (glowW <= 0.0f) return baseColor;

    int glowAvgR = byte_clamp(glowR / glowW);
    int glowAvgG = byte_clamp(glowG / glowW);
    int glowAvgB = byte_clamp(glowB / glowW);
    float glowAmount = float_clamp((glowW / (1.0f + (float)subdivisions * 2.0f)) * strength, 0.0f, 1.0f);

    int baseR = baseColor.z;
    int baseG = baseColor.y;
    int baseB = baseColor.x;
    int screenR = 255 - ((255 - baseR) * (255 - glowAvgR)) / 255;
    int screenG = 255 - ((255 - baseG) * (255 - glowAvgG)) / 255;
    int screenB = 255 - ((255 - baseB) * (255 - glowAvgB)) / 255;
    BlendRgb(&baseR, &baseG, &baseB, screenR, screenG, screenB, glowAmount);
    return make_uchar4((unsigned char)baseB,
                       (unsigned char)baseG,
                       (unsigned char)baseR,
                       255);
}

__global__ void compute_cell_commands_kernel(
    const uchar4* capturedPixels,
    const uchar4* previousPixels,
    GPUSetupCellState* cells,
    GPUCellCommand* commands,
    CudaRenderParams p
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int totalCells = p.cols * p.rows;
    if (idx >= totalCells || p.fontCount <= 0) return;

    int col = idx % p.cols;
    int row = idx / p.cols;
    int cellCenterX = int_clamp(col * p.cellW + p.cellW / 2, 0, p.screenW - 1);
    int cellCenterY = int_clamp(row * p.cellH + p.cellH / 2, 0, p.screenH - 1);
    uchar4 screenPixel = SampleCellPixel(capturedPixels, col, row, p);
    unsigned char visible = 1;
    unsigned char detectedNow = 1;
    GPUSetupCellState* cell = &cells[idx];
    bool wasVisibleBeforeDetection = cell->motionWasVisible != 0;
    bool wasDetectedBefore = cell->motionWasDetected != 0;
    if (p.motionMode) {
        uchar4 prevPixel = p.experimentalCellSampling
                                ? SampleCellPixel(previousPixels, col, row, p)
                                : previousPixels[cellCenterY * p.screenW + cellCenterX];
        int diff = abs((int)screenPixel.x - (int)prevPixel.x) +
                   abs((int)screenPixel.y - (int)prevPixel.y) +
                   abs((int)screenPixel.z - (int)prevPixel.z);
        int sensitivity = int_clamp(p.motionSensitivity, 0, 100);
        int threshold = int_clamp((100 - sensitivity) * 3 + 1, 1, 301);
        detectedNow = (diff >= threshold) ? 1 : 0;
        float percent = float_clamp(p.motionMaxConcurrentPercent, 0.0f, 100.0f);
        if (detectedNow && percent < 100.0f) {
            if (percent <= 0.0f) {
                detectedNow = 0;
            } else {
                int quota = int_clamp((int)(percent * 100.0f + 0.5f), 0, 10000);
                // Re-roll the selection each ~32ms window so the same cells
                // are not permanently suppressed
                unsigned int frameSeed = (unsigned int)(p.currentTimeMs >> 5);
                detectedNow = ((int)(StableCellHash(((unsigned int)idx) ^ frameSeed) % 10000U) < quota) ? 1 : 0;
            }
        }
        unsigned long now = p.currentTimeMs;
        float holdMs = p.motionDecayMs;
        if (holdMs < 0.0f) holdMs = 0.0f;
        if (detectedNow) cell->motionLastDetected = now;
        visible = (detectedNow ||
                   (holdMs > 0.0f &&
                    cell->motionLastDetected != 0 &&
                    (float)(now - cell->motionLastDetected) <= holdMs)) ? 1 : 0;
        if (p.motionHoldUntilNewDraw && !p.motionAutoKillStaticRamps &&
            (detectedNow || wasVisibleBeforeDetection)) {
            visible = 1;
        }
    }

    float rf = screenPixel.z;
    float gf = screenPixel.y;
    float bf = screenPixel.x;

    if (p.brightness != 1.0f) {
        rf *= p.brightness;
        gf *= p.brightness;
        bf *= p.brightness;
    }
    if (p.contrast != 1.0f) {
        rf = (rf - 128.0f) * p.contrast + 128.0f;
        gf = (gf - 128.0f) * p.contrast + 128.0f;
        bf = (bf - 128.0f) * p.contrast + 128.0f;
    }
    if (p.gamma != 1.0f) {
        float inv = 1.0f / p.gamma;
        rf = 255.0f * powf(float_clamp(rf / 255.0f, 0.0f, 1.0f), inv);
        gf = 255.0f * powf(float_clamp(gf / 255.0f, 0.0f, 1.0f), inv);
        bf = 255.0f * powf(float_clamp(bf / 255.0f, 0.0f, 1.0f), inv);
    }

    int ir = byte_clamp(rf);
    int ig = byte_clamp(gf);
    int ib = byte_clamp(bf);

    // Random per-cell filter selection (invert / grayscale / theme)
    bool grayApply = p.grayscale &&
        RandomEffectSelected((unsigned int)idx, p.grayscaleBandStart,
                             p.grayscaleQuota, RANDOM_GRAY_SALT, p);
    bool invertApply = p.invert &&
        RandomEffectSelected((unsigned int)idx, p.invertBandStart,
                             p.invertQuota, RANDOM_INVERT_SALT, p);
    bool themeApply = RandomEffectSelected((unsigned int)idx, p.themeBandStart,
                                           p.themeQuota, RANDOM_THEME_SALT, p);

    if (grayApply) {
        int gray = (ir * 30 + ig * 59 + ib * 11) / 100;
        ir = ig = ib = gray;
    }
    if (invertApply) {
        ir = 255 - ir;
        ig = 255 - ig;
        ib = 255 - ib;
    }

    int lum = (ir * 30 + ig * 59 + ib * 11) / 100;
    lum = int_clamp((int)((float)lum * p.finalLuminanceMultiplier + 0.5f), 0, 255);
    int desiredCharIdx = int_clamp((lum * (p.fontCount - 1)) / 255, 0, p.fontCount - 1);

    bool motionCommandUpdated = !p.motionMode;
    if (p.motionMode) {
        unsigned long now = p.currentTimeMs;
        if (cell->motionRampLastReset == 0 ||
            (p.motionRampUpdateDurationMs > 0 &&
             (now - cell->motionRampLastReset) >= (unsigned long)p.motionRampUpdateDurationMs)) {
            cell->motionRampChangeCount = 0;
            cell->motionRampLastReset = now;
        }

        bool firstUpdate = cell->motionLastGlyphUpdate == 0;
        float updateMs = p.motionDecayMs;
        if (updateMs < 0.0f) updateMs = 0.0f;
        bool decayReady = firstUpdate || updateMs <= 0.0f ||
                          ((float)(now - cell->motionLastGlyphUpdate) >= updateMs);
        bool maxReady = p.motionMaxRampUpdates <= 0 || p.motionRampUpdateDurationMs <= 0 ||
                        cell->motionRampChangeCount < (unsigned int)p.motionMaxRampUpdates;
        bool newDrawRequest = detectedNow && !wasDetectedBefore;
        if (visible) {
            if (p.motionHoldUntilNewDraw) {
                if (firstUpdate || newDrawRequest) {
                    cell->currentClusterIndex = desiredCharIdx;
                    cell->motionLastGlyphUpdate = now;
                    motionCommandUpdated = true;
                }
            } else if (detectedNow &&
                       (firstUpdate ||
                        (desiredCharIdx != cell->currentClusterIndex &&
                         decayReady && maxReady))) {
                if (!firstUpdate && p.motionMaxRampUpdates > 0 && p.motionRampUpdateDurationMs > 0) {
                    cell->motionRampChangeCount++;
                }
                cell->currentClusterIndex = desiredCharIdx;
                cell->motionLastGlyphUpdate = now;
                motionCommandUpdated = true;
            }
        }
    } else if (p.enableRampLimit) {
        if ((p.currentTimeMs - cell->rampLastReset) >= (unsigned long)p.rampRefresh) {
            cell->rampChangeCount = 0;
            cell->rampLastReset = p.currentTimeMs;
        }
        if (desiredCharIdx != cell->currentClusterIndex) {
            if (cell->rampChangeCount < (unsigned char)p.rampLimit) {
                cell->rampChangeCount++;
                cell->currentClusterIndex = desiredCharIdx;
            }
        } else {
            cell->currentClusterIndex = desiredCharIdx;
        }
    } else {
        cell->currentClusterIndex = desiredCharIdx;
    }

    int finalCharIdx = cell->currentClusterIndex;
    if (finalCharIdx < 0 || finalCharIdx >= p.fontCount) finalCharIdx = desiredCharIdx;

    int finalR, finalG, finalB;
    if (themeApply) {
        ApplyTheme((float)lum / 255.0f, p.colorTheme, p.time, &finalR, &finalG, &finalB, ir, ig, ib);
    } else {
        finalR = ir;
        finalG = ig;
        finalB = ib;
    }

    if (p.motionMode) {
        if (motionCommandUpdated) {
            cell->colorR = (unsigned char)finalR;
            cell->colorG = (unsigned char)finalG;
            cell->colorB = (unsigned char)finalB;
        }
    } else if (p.enableColorLimit) {
        if ((p.currentTimeMs - cell->colorLastReset) >= (unsigned long)p.colorRefresh) {
            cell->colorChangeCount = 0;
            cell->colorLastReset = p.currentTimeMs;
        }
        int diffR = abs(finalR - (int)cell->colorR);
        int diffG = abs(finalG - (int)cell->colorG);
        int diffB = abs(finalB - (int)cell->colorB);
        if ((diffR + diffG + diffB) >= p.colorThreshold) {
            if (cell->colorChangeCount < (unsigned char)p.colorLimit) {
                cell->colorChangeCount++;
                cell->colorR = (unsigned char)finalR;
                cell->colorG = (unsigned char)finalG;
                cell->colorB = (unsigned char)finalB;
            }
        } else {
            cell->colorR = (unsigned char)finalR;
            cell->colorG = (unsigned char)finalG;
            cell->colorB = (unsigned char)finalB;
        }
    } else {
        cell->colorR = (unsigned char)finalR;
        cell->colorG = (unsigned char)finalG;
        cell->colorB = (unsigned char)finalB;
    }

    // Random ASCII density: cells not selected by the stable hash show the
    // background only (charIndex -1 / active 0 suppress glyph and redirect)
    bool randomSelected = true;
    if (!p.desktopCopyMode && p.randomAsciiQuota < 10000) {
        randomSelected = RandomEffectSelected((unsigned int)idx, 0,
                                              p.randomAsciiQuota,
                                              RANDOM_ASCII_SALT, p);
    }

    commands[idx].charIndex = randomSelected ? finalCharIdx : -1;
    commands[idx].colorR = cell->colorR;
    commands[idx].colorG = cell->colorG;
    commands[idx].colorB = cell->colorB;
    commands[idx].active = (randomSelected && visible) ? 1 : 0;
    if (p.motionMode) {
        cell->motionWasVisible = visible;
        cell->motionWasDetected = detectedNow;
    }
}

__global__ void render_command_buffer_kernel(
    const uchar4* capturedPixels,
    const uchar4* fontPixels,
    const GPUCellCommand* commands,
    uchar4* outputPixels,
    CudaRenderParams p,
    int atlasW,
    int atlasH
) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= p.screenW || y >= p.screenH) return;

    int outIdx = y * p.screenW + x;
    uchar4 bgPixel = capturedPixels[outIdx];
    uchar4 bgColor = MakeDarkenedBackground(bgPixel.z, bgPixel.y, bgPixel.x, p.backgroundOpacity);

    int col = x / p.cellW;
    int row = y / p.cellH;
    if (col >= p.cols || row >= p.rows || p.fontCount <= 0 || atlasW <= 0 || atlasH <= 0) {
        outputPixels[outIdx] = bgColor;
        return;
    }

    if (p.zoneEnable) {
        int zX = (p.zoneX * p.screenW) / 100;
        int zY = (p.zoneY * p.screenH) / 100;
        int zW = (p.zoneW * p.screenW) / 100;
        int zH = (p.zoneH * p.screenH) / 100;
        if (x < zX || x >= (zX + zW) || y < zY || y >= (zY + zH)) {
            outputPixels[outIdx] = MakeDarkenedBackground(bgPixel.z, bgPixel.y, bgPixel.x, 0);
            return;
        }
    }

    int targetCol = col;
    int cmdIdx = row * p.cols + col;
    int offsetX = x - col * p.cellW;
    int offsetY = y - row * p.cellH;

    if (col > 0) {
        int leftIdx = row * p.cols + (col - 1);
        int leftCharIdx = commands[leftIdx].charIndex;
        if ((!p.motionMode || commands[leftIdx].active) && leftCharIdx >= 0 && leftCharIdx < p.fontCount && c_displayWidths[leftCharIdx] > 1) {
            targetCol = col - 1;
            cmdIdx = leftIdx;
            offsetX = x - targetCol * p.cellW;
        }
    }

    GPUCellCommand cmd = commands[cmdIdx];
    if (p.motionMode && !cmd.active) {
        outputPixels[outIdx] = bgColor;
        return;
    }
    uchar4 cellBgColor = (p.motionMode && cmd.active) ? make_uchar4(0, 0, 0, 255) : bgColor;
    int finalCharIdx = cmd.charIndex;
    if (finalCharIdx < 0 || finalCharIdx >= p.fontCount) {
        outputPixels[outIdx] = cellBgColor;
        return;
    }

    int charWidth = c_widths[finalCharIdx];
    int dispW = c_displayWidths[finalCharIdx];
    if (dispW < 1) dispW = 1;
    // Layout span the glyph occupies: full cell advance (includes glyph spacing)
    int cellSpanW = p.cellW * dispW;
    // Glyph pixel rows available in the atlas (cellH can exceed it via glyphSpacingY)
    int glyphH = (atlasH > 0 && atlasH < p.cellH) ? atlasH : p.cellH;
    float glyphScale = EffectiveGlyphScaleForCell(targetCol, row, p);
    float glyphScaleX = glyphScale * VariableFontAxisScaleForCell(targetCol, row, p, 0);
    float glyphScaleY = glyphScale * VariableFontAxisScaleForCell(targetCol, row, p, 1);
    float maxAxisScale = (p.strictNoFontOverlap || p.strictNoCellOverlap) ? 1.0f : 3.0f;
    glyphScaleX = float_clamp(glyphScaleX, 0.10f, maxAxisScale);
    glyphScaleY = float_clamp(glyphScaleY, 0.10f, maxAxisScale);
    // Mandatory collision guard: a scale above 1.0 spills the glyph into
    // neighboring cells; clamp every frame so output is always collision-free
    if (glyphScaleX > 1.0f) glyphScaleX = 1.0f;
    if (glyphScaleY > 1.0f) glyphScaleY = 1.0f;
    // Stretch glyph pixels to fill the layout span, centered in the cell
    float scaledW = (float)cellSpanW * glyphScaleX;
    float scaledH = (float)glyphH * glyphScaleY;
    float originX = ((float)cellSpanW - scaledW) * 0.5f;
    float originY = ((float)p.cellH - scaledH) * 0.5f;
    if ((float)offsetX < originX || (float)offsetX >= originX + scaledW ||
        (float)offsetY < originY || (float)offsetY >= originY + scaledH) {
        outputPixels[outIdx] = cellBgColor;
        return;
    }

    int sampleX = int_clamp((int)(((float)offsetX - originX) * ((float)charWidth / scaledW)), 0, charWidth - 1);
    int sampleY = int_clamp((int)(((float)offsetY - originY) / glyphScaleY), 0, glyphH - 1);
    int fontAtlasX = c_offsets[finalCharIdx] + sampleX;
    int fontAtlasY = sampleY;
    if (fontAtlasX < 0 || fontAtlasX >= atlasW || fontAtlasY < 0 || fontAtlasY >= atlasH) {
        outputPixels[outIdx] = cellBgColor;
        return;
    }

    uchar4 finalColor = cellBgColor;
    float opacity = (float)fontPixels[fontAtlasY * atlasW + fontAtlasX].w / 255.0f;
    if (p.motionMode) {
        finalColor = BlendGlyphOverBackground(cmd.colorR, cmd.colorG, cmd.colorB, opacity, cellBgColor);
    } else if (p.heatMode) {
        float heatAlpha = HeatShapeAlpha(offsetX, offsetY, cellSpanW, p.cellH, p.heatStyle, p.heatRadius);
        if (heatAlpha > 0.0f) {
            uchar4 heatColor = make_uchar4(
                byte_clamp((float)cmd.colorB * p.heatBrightness * heatAlpha + (float)cellBgColor.x * (1.0f - heatAlpha)),
                byte_clamp((float)cmd.colorG * p.heatBrightness * heatAlpha + (float)cellBgColor.y * (1.0f - heatAlpha)),
                byte_clamp((float)cmd.colorR * p.heatBrightness * heatAlpha + (float)cellBgColor.z * (1.0f - heatAlpha)),
                255
            );
            finalColor = ApplyBlackGlyphCutout(heatColor, opacity, p.heatGlyphMode);
        }
    } else {
        finalColor = BlendGlyphOverBackground(cmd.colorR, cmd.colorG, cmd.colorB, opacity, cellBgColor);
    }

    outputPixels[outIdx] = finalColor;
}

__global__ void render_desktop_copy_kernel(
    const uchar4* capturedPixels,
    const GPUCellCommand* commands,
    uchar4* outputPixels,
    CudaRenderParams p,
    int useCommands
) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= p.screenW || y >= p.screenH) return;

    int outIdx = y * p.screenW + x;
    uchar4 src = capturedPixels[outIdx];

    if (p.zoneEnable) {
        int zX = (p.zoneX * p.screenW) / 100;
        int zY = (p.zoneY * p.screenH) / 100;
        int zW = (p.zoneW * p.screenW) / 100;
        int zH = (p.zoneH * p.screenH) / 100;
        if (x < zX || x >= (zX + zW) || y < zY || y >= (zY + zH)) {
            outputPixels[outIdx] = src;
            return;
        }
    }

    int finalR = 0, finalG = 0, finalB = 0, lum = 0;
    int selCol = int_clamp(x / p.cellW, 0, p.cols - 1);
    int selRow = int_clamp(y / p.cellH, 0, p.rows - 1);
    unsigned int selCell = (unsigned int)(selRow * p.cols + selCol);
    int grayApply = (p.grayscale && RandomEffectSelected(selCell,
                        p.grayscaleBandStart, p.grayscaleQuota,
                        RANDOM_GRAY_SALT, p)) ? 1 : 0;
    int invertApply = (p.invert && RandomEffectSelected(selCell,
                          p.invertBandStart, p.invertQuota,
                          RANDOM_INVERT_SALT, p)) ? 1 : 0;
    int themeApply = RandomEffectSelected(selCell, p.themeBandStart,
                                          p.themeQuota,
                                          RANDOM_THEME_SALT, p) ? 1 : 0;
    TransformDesktopPixel(src, p, &finalR, &finalG, &finalB, &lum,
                          grayApply, invertApply, themeApply);

    if (useCommands && commands && p.cols > 0 && p.rows > 0) {
        int col = int_clamp(x / p.cellW, 0, p.cols - 1);
        int row = int_clamp(y / p.cellH, 0, p.rows - 1);
        int cmdIdx = row * p.cols + col;
        GPUCellCommand cmd = commands[cmdIdx];
        int offsetX = x - col * p.cellW;
        int offsetY = y - row * p.cellH;

        if (p.enableRampLimit && cmd.charIndex >= 0 && cmd.charIndex < p.fontCount && p.fontCount > 1) {
            int limitedLum = int_clamp((cmd.charIndex * 255) / (p.fontCount - 1), 0, 255);
            PullRgbTowardLuminance(limitedLum, &finalR, &finalG, &finalB);
        }

        if (p.enableColorLimit) {
            finalR = cmd.colorR;
            finalG = cmd.colorG;
            finalB = cmd.colorB;
        }

        if (p.heatMode) {
            int cellW = p.cellW > 0 ? p.cellW : 1;
            int cellH = p.cellH > 0 ? p.cellH : 1;
            float heatAlpha = HeatShapeAlpha(offsetX, offsetY, cellW, cellH,
                                             p.heatStyle, p.heatRadius);
            if (heatAlpha > 0.0f) {
                int heatR = byte_clamp((float)finalR * p.heatBrightness);
                int heatG = byte_clamp((float)finalG * p.heatBrightness);
                int heatB = byte_clamp((float)finalB * p.heatBrightness);
                BlendRgb(&finalR, &finalG, &finalB, heatR, heatG, heatB,
                         heatAlpha * 0.65f);
            }
        }

        if (p.motionMode && cmd.active) {
            BlendRgb(&finalR, &finalG, &finalB, cmd.colorR, cmd.colorG, cmd.colorB,
                     p.heatMode ? 0.35f : 0.50f);
        }
    }

    uchar4 finalColor = make_uchar4((unsigned char)finalB,
                                    (unsigned char)finalG,
                                    (unsigned char)finalR,
                                    255);
    outputPixels[outIdx] = finalColor;
}

__global__ void ambient_postprocess_kernel(
    const uchar4* capturedPixels,
    const GPUCellCommand* commands,
    uchar4* outputPixels,
    CudaRenderParams p,
    int useCommands
) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= p.screenW || y >= p.screenH) return;

    // Respect the zone: passthrough pixels outside it must stay untouched
    if (p.zoneEnable) {
        int zX = (p.zoneX * p.screenW) / 100;
        int zY = (p.zoneY * p.screenH) / 100;
        int zW = (p.zoneW * p.screenW) / 100;
        int zH = (p.zoneH * p.screenH) / 100;
        if (x < zX || x >= (zX + zW) || y < zY || y >= (zY + zH)) return;
    }

    int outIdx = y * p.screenW + x;
    outputPixels[outIdx] = ApplyAmbientGlow(
        capturedPixels,
        useCommands ? commands : nullptr,
        x,
        y,
        outputPixels[outIdx],
        p
    );
}

static void ReleaseStagingTextures() {
    if (g_captureStaging) { g_captureStaging->Release(); g_captureStaging = nullptr; }
    for (int i = 0; i < 2; ++i) {
        if (g_captureStagingRing[i]) {
            g_captureStagingRing[i]->Release();
            g_captureStagingRing[i] = nullptr;
        }
    }
    g_captureRingIndex = 0;
    g_captureRingValid = 0;
    if (g_fontStaging) { g_fontStaging->Release(); g_fontStaging = nullptr; }
    g_stagingW = 0;
    g_stagingH = 0;
    g_fontStagingW = 0;
    g_fontStagingH = 0;
    g_fontCacheValid = false;
    g_fontCacheW = 0;
    g_fontCacheH = 0;
}

static void ReleasePinnedHostBuffers() {
    if (g_captureHost) { cudaFreeHost(g_captureHost); g_captureHost = nullptr; }
    if (g_outputHost) { cudaFreeHost(g_outputHost); g_outputHost = nullptr; }
    if (g_fontHost) { cudaFreeHost(g_fontHost); g_fontHost = nullptr; }
    g_captureHostCapacity = 0;
    g_outputHostCapacity = 0;
    g_fontHostCapacity = 0;
}

static bool EnsurePinnedHostBuffer(unsigned char** ptr, size_t* capacity, size_t requiredBytes) {
    if (!ptr || !capacity || requiredBytes == 0) return false;
    if (*ptr && *capacity >= requiredBytes) return true;
    if (*ptr) {
        cudaFreeHost(*ptr);
        *ptr = nullptr;
        *capacity = 0;
    }
    cudaError_t err = cudaHostAlloc((void**)ptr, requiredBytes, cudaHostAllocDefault);
    if (err != cudaSuccess) return false;
    *capacity = requiredBytes;
    return true;
}

static bool UploadFontConstants(int fontCount) {
    if (fontCount <= 0 || fontCount > MAX_RAMP_CHARS) return false;
    if (g_fontConstantsValid && g_fontConstantsCount == fontCount) return true;
    cudaError_t err = cudaMemcpyToSymbolAsync(c_offsets, g_fontAtlas.offsets, sizeof(int) * fontCount, 0, cudaMemcpyHostToDevice, g_stream);
    if (err != cudaSuccess) return false;
    err = cudaMemcpyToSymbolAsync(c_widths, g_fontAtlas.widths, sizeof(int) * fontCount, 0, cudaMemcpyHostToDevice, g_stream);
    if (err != cudaSuccess) return false;
    err = cudaMemcpyToSymbolAsync(c_displayWidths, g_fontAtlas.displayWidths, sizeof(int) * fontCount, 0, cudaMemcpyHostToDevice, g_stream);
    if (err != cudaSuccess) return false;
    g_fontConstantsValid = true;
    g_fontConstantsCount = fontCount;
    return true;
}

static bool EnsureCellBuffer(const CudaRenderParams& params) {
    int requiredCellSize = params.cols * params.rows;
    if (requiredCellSize <= 0) return false;
    // Grid geometry changed: temporal cell state (limit counters, motion
    // timestamps) belongs to the old grid and must not leak into the new one
    static int lastGridCols = 0;
    static int lastGridRows = 0;
    bool gridChanged = (lastGridCols != params.cols) || (lastGridRows != params.rows);
    if (gridChanged) {
        lastGridCols = params.cols;
        lastGridRows = params.rows;
        g_commandsValid = false;
        if (g_d_cells && g_cellArraySize > 0) {
            cudaMemset(g_d_cells, 0, sizeof(GPUSetupCellState) * g_cellArraySize);
        }
    }
    if (requiredCellSize > g_cellArraySize || !g_d_cells) {
        if (g_d_cells) cudaFree(g_d_cells);
        g_d_cells = nullptr;
        g_cellArraySize = 0;
        g_commandsValid = false;
        cudaError_t err = cudaMalloc(&g_d_cells, sizeof(GPUSetupCellState) * requiredCellSize);
        if (err != cudaSuccess) return false;
        cudaMemset(g_d_cells, 0, sizeof(GPUSetupCellState) * requiredCellSize);
        g_cellArraySize = requiredCellSize;
    }
    if (requiredCellSize > g_commandArraySize || !g_d_commands) {
        if (g_d_commands) cudaFree(g_d_commands);
        g_d_commands = nullptr;
        g_commandArraySize = 0;
        g_commandsValid = false;
        cudaError_t err = cudaMalloc(&g_d_commands, sizeof(GPUCellCommand) * requiredCellSize);
        if (err != cudaSuccess) return false;
        g_commandArraySize = requiredCellSize;
    }
    return true;
}

static void GetAtlasDimensions(int* atlasW, int* atlasH) {
    *atlasW = 0;
    *atlasH = g_fontAtlas.cellH;
    if (g_fontAtlas.count > 0) {
        int last = g_fontAtlas.count - 1;
        *atlasW = g_fontAtlas.offsets[last] + g_fontAtlas.widths[last];
    }
}

static bool EnsureStagingTexture(ID3D11Texture2D* source, ID3D11Texture2D** staging, int* currentW, int* currentH) {
    if (!source || !staging || !currentW || !currentH || !g_graphics.device) return false;
    D3D11_TEXTURE2D_DESC desc = {};
    source->GetDesc(&desc);
    if (*staging && *currentW == (int)desc.Width && *currentH == (int)desc.Height) return true;
    if (*staging) {
        (*staging)->Release();
        *staging = nullptr;
    }
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;
    HRESULT hr = g_graphics.device->CreateTexture2D(&stagingDesc, nullptr, staging);
    if (FAILED(hr)) return false;
    *currentW = (int)desc.Width;
    *currentH = (int)desc.Height;
    return true;
}

static bool EnsureCaptureStagingRing(ID3D11Texture2D* source, int width, int height) {
    if (!source || !g_graphics.device || width <= 0 || height <= 0) return false;
    if (g_captureStagingRing[0] && g_captureStagingRing[1] &&
        g_stagingW == width && g_stagingH == height) {
        return true;
    }

    for (int i = 0; i < 2; ++i) {
        if (g_captureStagingRing[i]) {
            g_captureStagingRing[i]->Release();
            g_captureStagingRing[i] = nullptr;
        }
    }
    g_captureRingIndex = 0;
    g_captureRingValid = 0;

    D3D11_TEXTURE2D_DESC desc = {};
    source->GetDesc(&desc);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    for (int i = 0; i < 2; ++i) {
        HRESULT hr = g_graphics.device->CreateTexture2D(&desc, nullptr, &g_captureStagingRing[i]);
        if (FAILED(hr)) {
            for (int j = 0; j <= i; ++j) {
                if (g_captureStagingRing[j]) {
                    g_captureStagingRing[j]->Release();
                    g_captureStagingRing[j] = nullptr;
                }
            }
            return false;
        }
    }
    g_stagingW = width;
    g_stagingH = height;
    return true;
}

static bool CopyTextureToHost(ID3D11Texture2D* source, ID3D11Texture2D* staging, int width, int height, unsigned char* host, size_t hostBytes) {
    if (!source || !staging || !g_graphics.context || !host || width <= 0 || height <= 0) return false;
    g_graphics.context->CopyResource(staging, source);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = g_graphics.context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;
    size_t rowBytes = (size_t)width * 4;
    if (hostBytes < rowBytes * (size_t)height) {
        g_graphics.context->Unmap(staging, 0);
        return false;
    }
    for (int y = 0; y < height; ++y) {
        memcpy(host + rowBytes * y, (const unsigned char*)mapped.pData + (size_t)mapped.RowPitch * y, rowBytes);
    }
    g_graphics.context->Unmap(staging, 0);
    return true;
}

static bool CopyCaptureTextureToHostLagged(ID3D11Texture2D* source, int width, int height, unsigned char* host, size_t hostBytes) {
    if (!source || !g_graphics.context || !host || width <= 0 || height <= 0) return false;
    if (!EnsureCaptureStagingRing(source, width, height)) return false;

    int writeIndex = g_captureRingIndex;
    g_graphics.context->CopyResource(g_captureStagingRing[writeIndex], source);
    if (g_captureRingValid < 2) {
        g_captureRingValid++;
    }

    int readIndex = (g_captureRingValid < 2) ? writeIndex : (1 - writeIndex);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = g_graphics.context->Map(g_captureStagingRing[readIndex], 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;

    size_t rowBytes = (size_t)width * 4;
    if (hostBytes < rowBytes * (size_t)height) {
        g_graphics.context->Unmap(g_captureStagingRing[readIndex], 0);
        return false;
    }
    for (int y = 0; y < height; ++y) {
        memcpy(host + rowBytes * y, (const unsigned char*)mapped.pData + (size_t)mapped.RowPitch * y, rowBytes);
    }
    g_graphics.context->Unmap(g_captureStagingRing[readIndex], 0);
    g_captureRingIndex = 1 - g_captureRingIndex;
    return true;
}

static bool EnsurePixelBuffers(const CudaRenderParams& params, int atlasW, int atlasH) {
    size_t pixelCount = (size_t)params.screenW * (size_t)params.screenH;
    size_t fontPixelCount = (size_t)atlasW * (size_t)atlasH;
    cudaError_t err;
    if (pixelCount == 0 || fontPixelCount == 0) return false;
    // Resolution changed: the previous-frame buffer holds stale data from the
    // old capture size; force a refresh so motion diffing starts clean
    static int lastScreenW = 0;
    static int lastScreenH = 0;
    if (lastScreenW != params.screenW || lastScreenH != params.screenH) {
        lastScreenW = params.screenW;
        lastScreenH = params.screenH;
        g_hasPrevCapturedFrame = false;
        g_commandsValid = false;
    }
    if (pixelCount > g_pixelCapacity || !g_d_capturedPixels || !g_d_prevCapturedPixels || !g_d_outputPixels) {
        if (g_d_capturedPixels) cudaFree(g_d_capturedPixels);
        if (g_d_prevCapturedPixels) cudaFree(g_d_prevCapturedPixels);
        if (g_d_outputPixels) cudaFree(g_d_outputPixels);
        g_d_capturedPixels = nullptr;
        g_d_prevCapturedPixels = nullptr;
        g_d_outputPixels = nullptr;
        g_pixelCapacity = 0;
        g_hasPrevCapturedFrame = false;
        err = cudaMalloc(&g_d_capturedPixels, pixelCount * sizeof(uchar4));
        if (err != cudaSuccess) return false;
        err = cudaMalloc(&g_d_prevCapturedPixels, pixelCount * sizeof(uchar4));
        if (err != cudaSuccess) return false;
        err = cudaMalloc(&g_d_outputPixels, pixelCount * sizeof(uchar4));
        if (err != cudaSuccess) return false;
        g_pixelCapacity = pixelCount;
    }
    if (fontPixelCount > g_fontPixelCapacity || !g_d_fontPixels) {
        if (g_d_fontPixels) cudaFree(g_d_fontPixels);
        g_d_fontPixels = nullptr;
        g_fontPixelCapacity = 0;
        g_fontCacheValid = false;
        err = cudaMalloc(&g_d_fontPixels, fontPixelCount * sizeof(uchar4));
        if (err != cudaSuccess) return false;
        g_fontPixelCapacity = fontPixelCount;
    }
    return true;
}

bool RunCudaRenderInterop(const CudaRenderParams& params) {
    if (!g_capturedRes || !g_outputRes || !g_fontAtlasRes) return false;
    int atlasW = 0;
    int atlasH = 0;
    GetAtlasDimensions(&atlasW, &atlasH);

    if (!UploadFontConstants(params.fontCount) || !EnsureCellBuffer(params)) return false;
    if (!EnsurePixelBuffers(params, atlasW, atlasH)) return false;

    size_t pixelBytes = (size_t)params.screenW * (size_t)params.screenH * sizeof(uchar4);

    cudaGraphicsResource_t resources[] = { g_capturedRes, g_outputRes, g_fontAtlasRes };
    cudaError_t err = cudaGraphicsMapResources(3, resources, g_stream);
    if (err != cudaSuccess) return false;

    bool ok = false;
    cudaArray_t capturedArray = nullptr;
    cudaArray_t outputArray = nullptr;
    cudaArray_t fontAtlasArray = nullptr;
    bool copyNeedsCommands = params.desktopCopyMode &&
                             (params.motionMode || params.enableColorLimit ||
                              params.enableRampLimit || params.heatMode ||
                              (params.ambientMode && params.ambientFromRamp));
    bool needsCommands = !params.desktopCopyMode || copyNeedsCommands;

    if (cudaGraphicsSubResourceGetMappedArray(&capturedArray, g_capturedRes, 0, 0) != cudaSuccess) goto cleanup;
    if (cudaGraphicsSubResourceGetMappedArray(&outputArray, g_outputRes, 0, 0) != cudaSuccess) goto cleanup;
    if (cudaGraphicsSubResourceGetMappedArray(&fontAtlasArray, g_fontAtlasRes, 0, 0) != cudaSuccess) goto cleanup;

    if (cudaMemcpy2DFromArrayAsync(g_d_capturedPixels, params.screenW * sizeof(uchar4),
                                   capturedArray, 0, 0,
                                   params.screenW * sizeof(uchar4), params.screenH,
                                   cudaMemcpyDeviceToDevice, g_stream) != cudaSuccess) {
        goto cleanup;
    }

    if (!g_hasPrevCapturedFrame) {
        if (cudaMemcpyAsync(g_d_prevCapturedPixels, g_d_capturedPixels, pixelBytes,
                            cudaMemcpyDeviceToDevice, g_stream) != cudaSuccess) {
            goto cleanup;
        }
        g_hasPrevCapturedFrame = true;
    }

    if (!g_fontCacheValid || g_fontCacheW != atlasW || g_fontCacheH != atlasH) {
        if (cudaMemcpy2DFromArrayAsync(g_d_fontPixels, atlasW * sizeof(uchar4),
                                       fontAtlasArray, 0, 0,
                                       atlasW * sizeof(uchar4), atlasH,
                                       cudaMemcpyDeviceToDevice, g_stream) != cudaSuccess) {
            goto cleanup;
        }
        g_fontCacheValid = true;
        g_fontCacheW = atlasW;
        g_fontCacheH = atlasH;
    }

    if (needsCommands && (!params.asciiPaused || !g_commandsValid)) {
        dim3 cellBlock(256);
        dim3 cellGrid(((params.cols * params.rows) + cellBlock.x - 1) / cellBlock.x);
        compute_cell_commands_kernel<<<cellGrid, cellBlock, 0, g_stream>>>(
            g_d_capturedPixels,
            g_d_prevCapturedPixels,
            g_d_cells,
            g_d_commands,
            params
        );
        if (cudaGetLastError() != cudaSuccess) goto cleanup;
        g_commandsValid = true;
    } else if (!needsCommands) {
        g_commandsValid = false;
    }

    {
        dim3 block(16, 16);
        dim3 grid((params.screenW + block.x - 1) / block.x,
                  (params.screenH + block.y - 1) / block.y);
        if (params.desktopCopyMode) {
            render_desktop_copy_kernel<<<grid, block, 0, g_stream>>>(
                g_d_capturedPixels,
                copyNeedsCommands ? g_d_commands : nullptr,
                g_d_outputPixels,
                params,
                copyNeedsCommands ? 1 : 0
            );
        } else {
            render_command_buffer_kernel<<<grid, block, 0, g_stream>>>(
                g_d_capturedPixels,
                g_d_fontPixels,
                g_d_commands,
                g_d_outputPixels,
                params,
                atlasW,
                atlasH
            );
        }
        if (cudaGetLastError() != cudaSuccess) goto cleanup;
        if (params.ambientMode) {
            int ambientUseCommands = (params.ambientFromRamp && needsCommands) ? 1 : 0;
            ambient_postprocess_kernel<<<grid, block, 0, g_stream>>>(
                g_d_capturedPixels,
                ambientUseCommands ? g_d_commands : nullptr,
                g_d_outputPixels,
                params,
                ambientUseCommands
            );
            if (cudaGetLastError() != cudaSuccess) goto cleanup;
        }
    }

    if (cudaMemcpy2DToArrayAsync(outputArray, 0, 0,
                                 g_d_outputPixels, params.screenW * sizeof(uchar4),
                                 params.screenW * sizeof(uchar4), params.screenH,
                                 cudaMemcpyDeviceToDevice, g_stream) != cudaSuccess) {
        goto cleanup;
    }
    if (cudaMemcpyAsync(g_d_prevCapturedPixels, g_d_capturedPixels, pixelBytes,
                        cudaMemcpyDeviceToDevice, g_stream) != cudaSuccess) {
        goto cleanup;
    }
    ok = cudaStreamSynchronize(g_stream) == cudaSuccess;

cleanup:
    cudaGraphicsUnmapResources(3, resources, g_stream);
    return ok;
}

static bool RunCudaRenderStaged(const CudaRenderParams& params) {
    int atlasW = 0;
    int atlasH = 0;
    GetAtlasDimensions(&atlasW, &atlasH);

    if (!UploadFontConstants(params.fontCount) || !EnsureCellBuffer(params)) return false;
    if (!EnsurePixelBuffers(params, atlasW, atlasH)) return false;

    size_t pixelBytes = (size_t)params.screenW * (size_t)params.screenH * sizeof(uchar4);
    size_t fontBytes = (size_t)atlasW * (size_t)atlasH * sizeof(uchar4);
    if (!EnsurePinnedHostBuffer(&g_captureHost, &g_captureHostCapacity, pixelBytes)) return false;
    if (!EnsurePinnedHostBuffer(&g_outputHost, &g_outputHostCapacity, pixelBytes)) return false;

    if (!CopyCaptureTextureToHostLagged(g_graphics.capturedTexture, params.screenW, params.screenH, g_captureHost, g_captureHostCapacity)) return false;
    if (cudaMemcpyAsync(g_d_capturedPixels, g_captureHost, pixelBytes, cudaMemcpyHostToDevice, g_stream) != cudaSuccess) return false;
    if (!g_hasPrevCapturedFrame) {
        if (cudaMemcpyAsync(g_d_prevCapturedPixels, g_d_capturedPixels, pixelBytes, cudaMemcpyDeviceToDevice, g_stream) != cudaSuccess) return false;
        g_hasPrevCapturedFrame = true;
    }

    if (!g_fontCacheValid || g_fontCacheW != atlasW || g_fontCacheH != atlasH) {
        if (!EnsureStagingTexture(g_fontAtlas.texture, &g_fontStaging, &g_fontStagingW, &g_fontStagingH)) return false;
        if (!EnsurePinnedHostBuffer(&g_fontHost, &g_fontHostCapacity, fontBytes)) return false;
        if (!CopyTextureToHost(g_fontAtlas.texture, g_fontStaging, atlasW, atlasH, g_fontHost, g_fontHostCapacity)) return false;
        if (cudaMemcpyAsync(g_d_fontPixels, g_fontHost, fontBytes, cudaMemcpyHostToDevice, g_stream) != cudaSuccess) return false;
        g_fontCacheValid = true;
        g_fontCacheW = atlasW;
        g_fontCacheH = atlasH;
    }

    bool copyNeedsCommands = params.desktopCopyMode &&
                             (params.motionMode || params.enableColorLimit ||
                              params.enableRampLimit || params.heatMode ||
                              (params.ambientMode && params.ambientFromRamp));
    bool needsCommands = !params.desktopCopyMode || copyNeedsCommands;
    if (needsCommands && (!params.asciiPaused || !g_commandsValid)) {
        dim3 cellBlock(256);
        dim3 cellGrid(((params.cols * params.rows) + cellBlock.x - 1) / cellBlock.x);
        compute_cell_commands_kernel<<<cellGrid, cellBlock, 0, g_stream>>>(
            g_d_capturedPixels,
            g_d_prevCapturedPixels,
            g_d_cells,
            g_d_commands,
            params
        );
        if (cudaGetLastError() != cudaSuccess) return false;
        g_commandsValid = true;
    } else if (!needsCommands) {
        g_commandsValid = false;
    }

    dim3 block(16, 16);
    dim3 grid((params.screenW + block.x - 1) / block.x, (params.screenH + block.y - 1) / block.y);
    if (params.desktopCopyMode) {
        render_desktop_copy_kernel<<<grid, block, 0, g_stream>>>(
            g_d_capturedPixels,
            copyNeedsCommands ? g_d_commands : nullptr,
            g_d_outputPixels,
            params,
            copyNeedsCommands ? 1 : 0
        );
    } else {
        render_command_buffer_kernel<<<grid, block, 0, g_stream>>>(
            g_d_capturedPixels,
            g_d_fontPixels,
            g_d_commands,
            g_d_outputPixels,
            params,
            atlasW,
            atlasH
        );
    }
    if (cudaGetLastError() != cudaSuccess) return false;
    if (params.ambientMode) {
        int ambientUseCommands = (params.ambientFromRamp && needsCommands) ? 1 : 0;
        ambient_postprocess_kernel<<<grid, block, 0, g_stream>>>(
            g_d_capturedPixels,
            ambientUseCommands ? g_d_commands : nullptr,
            g_d_outputPixels,
            params,
            ambientUseCommands
        );
        if (cudaGetLastError() != cudaSuccess) return false;
    }

    if (cudaMemcpyAsync(g_outputHost, g_d_outputPixels, pixelBytes, cudaMemcpyDeviceToHost, g_stream) != cudaSuccess) return false;
    if (cudaMemcpyAsync(g_d_prevCapturedPixels, g_d_capturedPixels, pixelBytes, cudaMemcpyDeviceToDevice, g_stream) != cudaSuccess) return false;
    if (cudaStreamSynchronize(g_stream) != cudaSuccess) return false;
    g_graphics.context->UpdateSubresource(g_graphics.outputTexture, 0, nullptr, g_outputHost, params.screenW * 4, 0);
    return true;
}

bool InitCuda() {
    int devCount = 0;
    cudaError_t err = cudaGetDeviceCount(&devCount);
    if (err != cudaSuccess || devCount == 0) return false;
    err = cudaSetDevice(0);
    if (err != cudaSuccess) return false;
    // Prefer the GPU driving the display so D3D11 interop registers; on
    // multi-GPU systems device 0 is not necessarily the display adapter
    IDXGIDevice* dxgiDevice = nullptr;
    if (g_graphics.device &&
        SUCCEEDED(g_graphics.device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice))) {
        IDXGIAdapter* adapter = nullptr;
        if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) {
            int displayCudaDevice = -1;
            if (cudaD3D11GetDevice(&displayCudaDevice, adapter) == cudaSuccess &&
                displayCudaDevice >= 0) {
                cudaSetDevice(displayCudaDevice);
            }
            adapter->Release();
        }
        dxgiDevice->Release();
    }
    if (!g_stream) {
        err = cudaStreamCreateWithFlags(&g_stream, cudaStreamNonBlocking);
        if (err != cudaSuccess) return false;
    }
    return true;
}

void CleanupCuda() {
    UnregisterD3D11Resources();
    ReleaseStagingTextures();
    ReleasePinnedHostBuffers();
    if (g_d_cells) {
        cudaFree(g_d_cells);
        g_d_cells = nullptr;
        g_cellArraySize = 0;
    }
    if (g_d_commands) {
        cudaFree(g_d_commands);
        g_d_commands = nullptr;
        g_commandArraySize = 0;
    }
    g_commandsValid = false;
    if (g_d_capturedPixels) { cudaFree(g_d_capturedPixels); g_d_capturedPixels = nullptr; }
    if (g_d_prevCapturedPixels) { cudaFree(g_d_prevCapturedPixels); g_d_prevCapturedPixels = nullptr; }
    if (g_d_outputPixels) { cudaFree(g_d_outputPixels); g_d_outputPixels = nullptr; }
    if (g_d_fontPixels) { cudaFree(g_d_fontPixels); g_d_fontPixels = nullptr; }
    g_pixelCapacity = 0;
    g_fontPixelCapacity = 0;
    if (g_stream) {
        cudaStreamDestroy(g_stream);
        g_stream = nullptr;
    }
    g_fontCacheValid = false;
    g_hasPrevCapturedFrame = false;
    g_fontConstantsValid = false;
    g_fontConstantsCount = 0;
}

bool RegisterD3D11Resources() {
    UnregisterD3D11Resources();
    ReleaseStagingTextures();
    g_fontConstantsValid = false;
    g_fontConstantsCount = 0;
    g_fontCacheValid = false;
    g_hasPrevCapturedFrame = false;
    g_commandsValid = false;
    g_useInterop = false;

    if (!g_graphics.capturedTexture || !g_graphics.outputTexture || !g_fontAtlas.texture) return false;

    cudaError_t err = cudaGraphicsD3D11RegisterResource(&g_capturedRes, g_graphics.capturedTexture, cudaGraphicsRegisterFlagsNone);
    if (err == cudaSuccess) {
        err = cudaGraphicsD3D11RegisterResource(&g_outputRes, g_graphics.outputTexture, cudaGraphicsRegisterFlagsNone);
    }
    if (err == cudaSuccess) {
        err = cudaGraphicsD3D11RegisterResource(&g_fontAtlasRes, g_fontAtlas.texture, cudaGraphicsRegisterFlagsNone);
    }

    if (err == cudaSuccess) {
        g_useInterop = true;
    } else {
        UnregisterD3D11Resources();
        g_useInterop = false;
    }

    return true;
}

void UnregisterD3D11Resources() {
    if (g_capturedRes) { cudaGraphicsUnregisterResource(g_capturedRes); g_capturedRes = nullptr; }
    if (g_outputRes) { cudaGraphicsUnregisterResource(g_outputRes); g_outputRes = nullptr; }
    if (g_fontAtlasRes) { cudaGraphicsUnregisterResource(g_fontAtlasRes); g_fontAtlasRes = nullptr; }
    g_useInterop = false;
    g_fontConstantsValid = false;
    g_fontConstantsCount = 0;
}

void ResetCudaTemporalState() {
    if (g_d_cells && g_cellArraySize > 0) {
        cudaMemset(g_d_cells, 0, sizeof(GPUSetupCellState) * g_cellArraySize);
    }
    g_hasPrevCapturedFrame = false;
    g_commandsValid = false;
}

const char* GetCudaRenderModeName() {
    if (g_config.desktopCopyMode) {
        return g_useInterop ? "Desktop copy (D3D11 interop)" : "Desktop copy (staged)";
    }
    return g_useInterop ? "UTF-8 glyphs (D3D11 interop)" : "UTF-8 glyphs (staged)";
}

bool RunCudaRender(const CudaRenderParams& params) {
    if (params.screenW <= 0 || params.screenH <= 0 || params.cellW <= 0 || params.cellH <= 0) return false;
    if (params.cols <= 0 || params.rows <= 0 || params.fontCount <= 0 || params.fontCount > MAX_RAMP_CHARS) return false;

    if (g_useInterop && RunCudaRenderInterop(params)) return true;
    if (g_useInterop) {
        UnregisterD3D11Resources();
        ReleaseStagingTextures();
    }
    return RunCudaRenderStaged(params);
}
