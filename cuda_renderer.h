#ifndef CUDA_RENDERER_H
#define CUDA_RENDERER_H

#include <windows.h>
#include "config.h"

struct CudaRenderParams {
    int screenW;
    int screenH;
    int cellW;
    int cellH;
    int cols;
    int rows;
    
    // Filters
    float brightness;
    float contrast;
    float gamma;
    int grayscale;
    int invert;
    float finalLuminanceMultiplier;
    
    // Theme
    int colorTheme;
    float time;
    
    // Limits
    int enableColorLimit;
    int colorLimit;
    int colorRefresh;
    int colorThreshold;
    int enableRampLimit;
    int rampLimit;
    int rampRefresh;
    unsigned long currentTimeMs;
    
    // Heat mode
    int heatMode;
    int heatStyle;
    int heatGlyphMode;
    float heatRadius;
    float heatBrightness;

    // Freeze UTF-8 glyph commands while keeping the captured background live.
    int asciiPaused;

    // Remastered Motion Mode: stable per-cell detection, hold, and ramp throttles.
    int motionMode;
    int motionSensitivity;
    float motionDecayMs;
    int motionRampUpdateDurationMs;
    int motionMaxRampUpdates;
    float motionMaxConcurrentPercent;
    int motionHoldUntilNewDraw;
    int motionAutoKillStaticRamps;
    
    // Zone
    int zoneEnable;
    int zoneX;
    int zoneY;
    int zoneW;
    int zoneH;

    // Random ASCII density: 0..10000 quota of cells (basis points) that
    // render glyphs; suppressed cells show the live background only
    int randomAsciiQuota;

    // Random per-cell filter application (same stable-hash method). Quotas
    // are basis points; 10000 = apply everywhere. In exclusive mode each
    // effect owns a band of the shared hash space so no cell is claimed by
    // two effects (ASCII claims first, then theme, invert, grayscale).
    int invertQuota;
    int grayscaleQuota;
    int themeQuota;
    int randomExclusive;
    int invertBandStart;
    int grayscaleBandStart;
    int themeBandStart;

    // Terminal background
    int backgroundOpacity;

    // Renderer mode: 0 = UTF-8 glyph renderer, 1 = live desktop copy renderer.
    int desktopCopyMode;

    // Ambient glow post layer.
    int ambientMode;
    int ambientFromSource;
    int ambientFromRamp;
    float ambientLitPercent;
    float ambientGlowStrength;
    int ambientSubdivisions;
    float ambientRadius;
    float ambientProgressiveBleed;
    float ambientColorMatch;
    
    // Font atlas
    int fontCount;

    // Variable glyph sizing.
    int variableFontMode;
    int strictNoFontOverlap;
    float variableFontMinScale;
    float variableFontMaxScale;
    float variableFontRandomness;
    int variableFontRegionSize;
    int variableFontSeed;
    float variableFontPulseSpeed;
    int variableFontIndependentAxes;
    float variableFontMinWidthScale;
    float variableFontMaxWidthScale;
    float variableFontMinHeightScale;
    float variableFontMaxHeightScale;
    int variableCellMode;
    int strictNoCellOverlap;
    float variableCellMinScale;
    float variableCellMaxScale;
    float variableCellRandomness;
    int variableCellRegionSize;
    int variableCellSeed;
    float variableCellPulseSpeed;
    int variableCellAffectsSampling;
    int variableCellAffectsFont;
    int experimentalCellSampling;
    int cellSampleMode;
    int cellSampleColorMode;
    int cellSampleGrid;
    float cellSampleRadiusScale;
    float cellSampleEdgeBoost;
    float cellSampleJitter;
    float cellSampleCenterWeight;
    float cellSampleDetailMix;
    float cellSampleLuminanceCompensation;
    float cellSampleHighlightPreserve;
};

bool InitCuda();
void CleanupCuda();
bool RegisterD3D11Resources();
void UnregisterD3D11Resources();
void ResetCudaTemporalState();
const char* GetCudaRenderModeName();
bool RunCudaRender(const CudaRenderParams& params);

#endif
