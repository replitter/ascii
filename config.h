#ifndef CONFIG_H
#define CONFIG_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>

#define MAX_RAMP_CHARS      512
#define MAX_GRAPHEME_LEN    16
#define MAX_LINE_LENGTH     16384
#define MAX_RAMPS           160
#define MAX_LUMINANCE_SEQ   64

#define DEFAULT_FPS         240
#define DEFAULT_CELL_SIZE   12
#define MIN_CELL_SIZE       1
#define MAX_CELL_SIZE       32
#define FONT_ASPECT_RATIO   2.0f

typedef enum {
    HEAT_STYLE_CIRCLE = 0,
    HEAT_STYLE_SQUARE,
    HEAT_STYLE_DIAMOND,
    HEAT_STYLE_SOFT_BOX,
    HEAT_STYLE_BAR,
    HEAT_STYLE_COUNT
} HeatStyle;

typedef enum {
    HEAT_GLYPH_BLACK = 0,
    HEAT_GLYPH_HIDDEN,
    HEAT_GLYPH_COUNT
} HeatGlyphMode;

#define DEFAULT_RAMP_STR " .'`^\",:;Il!i><~+_-?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$"

typedef enum {
    THEME_NONE = 0,
    THEME_TEAL_GREEN,
    THEME_CYBERPUNK,
    THEME_SUNSET,
    THEME_RETRO_WAVE,
    THEME_LEMON_LIME,
    THEME_CHROMA_GLOW,
    THEME_DEEP_BLURPLE,
    THEME_RAINBOW,
    THEME_AMBER_TERMINAL,
    THEME_MATRIX,
    THEME_PAPERWHITE,
    THEME_SOLARIZED_LIGHT,
    THEME_SOLARIZED_DARK,
    THEME_DRACULA,
    THEME_MONOCHROME,
    THEME_ICE_BLUE,
    THEME_MINT,
    THEME_ROSE_GOLD,
    THEME_VAPORWAVE,
    THEME_OCEAN,
    THEME_FOREST,
    THEME_LAVA,
    THEME_ARCTIC,
    THEME_CANDY,
    THEME_NEON_NOIR,
    THEME_PASTEL,
    THEME_SEPIA,
    THEME_EMERALD,
    THEME_SAPPHIRE,
    THEME_RUBY,
    THEME_GOLD,
    THEME_GHOST,
    THEME_TOXIC,
    THEME_MIDNIGHT,
    THEME_PEACH,
    THEME_LAVENDER,
    THEME_FIREWATCH,
    THEME_COPPER,
    THEME_AURORA,
    THEME_PLASMA,
    THEME_ULTRAVIOLET,
    THEME_TERMINAL_GREEN,
    THEME_CRIMSON_NIGHT,
    THEME_BLUEPRINT,
    THEME_CHERRY_BLOSSOM,
    THEME_ACID_RAIN,
    THEME_DESERT_DUSK,
    THEME_GLACIER,
    THEME_SYNTHWAVE_GOLD,
    THEME_SIGNAL_LOSS,
    THEME_PRISM,
    THEME_MOONLIGHT,
    THEME_CORAL_REEF,
    THEME_STEEL,
    THEME_JADE,
    THEME_INFERNO,
    THEME_COTTON_CANDY,
    THEME_NIGHT_VISION,
    THEME_COUNT
} ColorTheme;

extern const char* THEME_NAMES[];
extern const char* HEAT_STYLE_NAMES[];
extern const char* HEAT_GLYPH_MODE_NAMES[];

typedef struct {
    WCHAR chars[MAX_GRAPHEME_LEN];
    int wcharLen;
    int displayWidth;
} GraphemeCluster;

typedef struct {
    GraphemeCluster clusters[MAX_RAMP_CHARS];
    int count;
    char name[64];
} UnicodeRamp;

typedef struct {
    int isRandom;
    float minVal;
    float maxVal;
    float favour;
} LuminanceSeqItem;

typedef struct {
    int currentRamp;
    UnicodeRamp ramps[MAX_RAMPS];
    int rampCount;
    int colorTheme;
    int invert;
    int grayscale;
    float brightness;
    float contrast;
    float gamma;
    float finalLuminanceMultiplier;
    int luminanceSeqEnabled;
    int luminanceChangeTime;
    LuminanceSeqItem luminanceSeq[MAX_LUMINANCE_SEQ];
    int luminanceSeqCount;
    char luminanceSeqRaw[MAX_LINE_LENGTH];
    int zoneEnable;
    int zoneX;
    int zoneY;
    int zoneW;
    int zoneH;
    float asciiRandomPercent;
    float invertRandomPercent;
    float grayscaleRandomPercent;
    float themeRandomPercent;
    int randomEffectsExclusive;
    int opacity;
    int desktopCopyMode;
    int ambientMode;
    int ambientFromSource;
    int ambientFromRamp;
    float ambientLitPercent;
    float ambientGlowStrength;
    int ambientSubdivisions;
    float ambientRadius;
    float ambientProgressiveBleed;
    float ambientColorMatch;
    char fontName[64];
    int heatMode;
    int heatStyle;
    int heatGlyphMode;
    float heatRadius;
    float heatBrightness;
    int motionMode;
    int motionSensitivity;
    float motionDecayMs;
    int motionRampUpdateDurationMs;
    int motionMaxRampUpdates;
    float motionMaxConcurrentPercent;
    int motionHoldUntilNewDraw;
    int motionAutoKillStaticRamps;
    int targetFPS;
    float frameBudgetMs;
    int cellSize;
    int glyphSpacingX;
    int glyphSpacingY;
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
    int frameSkip;
    int enableColorLimit;
    int colorLimit;
    int colorRefresh;
    int colorThreshold;
    int enableRampLimit;
    int rampLimit;
    int rampRefresh;
} Config;

extern Config g_config;

void SetDefaultConfig();
void LoadConfig();
int LoadConfigFromFile(const char* path);
void SaveConfigToFile();
int SaveConfigToNamedFile(const char* path);
void AppendBuiltInRamps();
int ParseUTF8ToGraphemes(const char* utf8, GraphemeCluster* out, int maxClusters);
void ParseLuminanceSequence(const char* seqStr);
float GetRandomWithFavour(float minVal, float maxVal, float favour);

#endif
