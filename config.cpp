#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CONFIG_FILE "./config.ini"

const char* THEME_NAMES[] = {
    "None (Original)",
    "Teal Green",
    "Cyberpunk",
    "Sunset",
    "Retro Wave",
    "Lemon Lime",
    "Chroma Glow",
    "Deep Blurple",
    "Rainbow",
    "Amber Terminal",
    "Matrix",
    "Paperwhite",
    "Solarized Light",
    "Solarized Dark",
    "Dracula",
    "Monochrome",
    "Ice Blue",
    "Mint",
    "Rose Gold",
    "Vaporwave",
    "Ocean",
    "Forest",
    "Lava",
    "Arctic",
    "Candy",
    "Neon Noir",
    "Pastel",
    "Sepia",
    "Emerald",
    "Sapphire",
    "Ruby",
    "Gold",
    "Ghost",
    "Toxic",
    "Midnight",
    "Peach",
    "Lavender",
    "Firewatch",
    "Copper",
    "Aurora",
    "Plasma",
    "Ultraviolet",
    "Terminal Green",
    "Crimson Night",
    "Blueprint",
    "Cherry Blossom",
    "Acid Rain",
    "Desert Dusk",
    "Glacier",
    "Synthwave Gold",
    "Signal Loss",
    "Prism",
    "Moonlight",
    "Coral Reef",
    "Steel",
    "Jade",
    "Inferno",
    "Cotton Candy",
    "Night Vision"
};

const char* HEAT_STYLE_NAMES[] = {
    "Circle",
    "Square",
    "Diamond",
    "Soft Box",
    "Bar"
};

const char* HEAT_GLYPH_MODE_NAMES[] = {
    "Black Glyph",
    "Hidden Glyph"
};

Config g_config;

typedef struct {
    const char* name;
    const char* chars;
} BuiltInRampDef;

static const BuiltInRampDef BUILTIN_RAMPS[] = {
    {"Classic ASCII", " .'`^\",:;Il!i><~+_-?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$"},
    {"Simple Contrast", " .:-=+*#%@"},
    {"Smooth ASCII", "  .'`^\",:;Il!i><~+_-?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$"},
    {"Dense Print", "  .,:;irsXA253hMHGS#9B&@"},
    {"Film Grain", "  ..,,::;;iillttffLLCCGG0088BB@@"},
    {"High Contrast", " .#@"},
    {"Soft Terminal", " `.-':_,^=+*#%@"},
    {"Technical Code", " .,:;(){}[]<>/\\|+=*#%@"},
    {"Circuit Board", " .-:=+<>[]{}#@MW"},
    {"Matrix Clean", " .,:;!|\\/1Il0O8B@"},
    {"Mono Minimal", " .oO0@"},
    {"Round Forms", u8" .\u00B7\u00B0oO0Q@"},
    {"Sharp Edges", " .-_=+*xX#%@"},
    {"Slash Scan", " .-_/\\|X#%@"},
    {"Bracket Cage", " .()[]{}<>#%@"},
    {"Punctuation Noise", " .,:;!?-_=+*#%&@"},
    {"Numeric Meter", " 0123456789"},
    {"Hex Meter", " 0123456789ABCDEF"},
    {"Binary Pulse", " 01"},
    {"Keyboard Flow", " .asdfghjklqwertyuiopZXCVBNM#@"},
    {"Wave ASCII", " .-~≈=+*#%@"},
    {"Water ASCII", " .,..:;~≈≋▓█"},
    {"Fire ASCII", " .`'^:;!i*#%@█"},
    {"Smoke ASCII", "  .,:;sS#@"},
    {"Ice ASCII", " .·*+xX#%@█"},
    {"Neon ASCII", " .:-=+*xX$#@█"},
    {"Chrome ASCII", " .`',:;!+*eE$#@"},
    {"Ink Wash", "  .,:;i!lI1tfLCG08@@"},
    {"Bone Dust", " .,:;clodxkO0Q@"},
    {"Blueprint Lines", " .:-=+*#%WM@"},
    {"Wireframe", " .-+\\/|<>X#%@"},
    {"Carbon Fiber", " .,:;xX%#@MW"},
    {"Stencil Bold", " .:;=+HMW#@"},
    {"Arcade Chunk", " .:-=+*#@MW"},
    {"CRT Beam", u8" _-~=\u2261\u2593\u2588"},
    {"Sparkline", " .`'*+xX#%@"},
    {"Organic Fiber", " .,:;irsXA253hMHGS#9B&@"},
    {"Micro Dots", u8" .\u00B7:\u2234\u2237\u2058\u2059"},
    {"Dot Matrix", " ...,,,:::;;;!!!***###@@@"},
    {"Braille Soft", u8" \u2801\u2803\u2807\u2847\u284F\u285F\u287F\u28FF"},
    {"Braille Dense", u8" \u2800\u2840\u2844\u2846\u2847\u28C7\u28E7\u28F7\u28FF"},
    {"Braille Noise", u8" \u2801\u2802\u2805\u2815\u2837\u2877\u28F7\u28FF"},
    {"Block Classic", u8" \u2591\u2592\u2593\u2588"},
    {"Block Levels", u8" \u2581\u2582\u2583\u2584\u2585\u2586\u2587\u2588"},
    {"Block Wide", u8"  \u2591\u2591\u2592\u2592\u2593\u2593\u2588\u2588"},
    {"Quadrant Mosaic", u8" \u2598\u259D\u2580\u2596\u258C\u259E\u259B\u259C\u259F\u2588"},
    {"Half Block Vertical", u8" \u258F\u258E\u258D\u258C\u258B\u258A\u2589\u2588"},
    {"Half Block Horizontal", u8" \u2581\u2582\u2583\u2584\u2585\u2586\u2587\u2588"},
    {"Shade Smooth", u8"  \u2591\u2592\u2593\u2588"},
    {"Shade Punchy", u8" \u2591\u2591\u2592\u2593\u2593\u2588\u2588"},
    {"Box Drawing", u8" \u2500\u2501\u2503\u254B\u256C\u2588"},
    {"Heavy Box", u8" \u2574\u2578\u2501\u2523\u254B\u257E\u2588"},
    {"Powerline", u8" \uE0B0\uE0B1\uE0B2\uE0B3\u2588"},
    {"Geometric Squares", u8" \u25AB\u25AA\u25FD\u25FE\u25A1\u25A0\u2588"},
    {"Geometric Circles", u8" \u00B7\u2219\u25CF\u25C9\u25CE\u25CD\u25D0\u25D1\u2B24"},
    {"Geometric Triangles", u8" \u25B5\u25B4\u25B8\u25BE\u25BF\u25B2\u25B6\u25BC\u25C0\u2588"},
    {"Geometric Diamonds", u8" \u22C4\u25C7\u25C6\u25C8\u2B16\u2B18\u2B17\u2B19\u2588"},
    {"Arrows Compass", u8" \u00B7\u2190\u2191\u2192\u2193\u2194\u2195\u2196\u2197\u2198\u2199\u21C4\u21C5"},
    {"Math Symbols", u8" .-+\u00D7\u00F7=\u2260\u2248\u221E\u2211\u222B\u2202\u2206\u25CA"},
    {"Logic Symbols", u8" .-\u00AC\u2227\u2228\u22BB\u22BC\u2200\u2203\u2282\u2283\u22A2\u22A8"},
    {"Currency", u8" .,\u00A2\u00A5\u20AC\u00A3\u20A9\u20B9\u20BF\u00A4"},
    {"Chess Set", u8" \u2659\u2658\u2657\u2656\u2655\u2654\u265F\u265E\u265D\u265C\u265B\u265A"},
    {"Card Suits", u8" \u2661\u2662\u2667\u2664\u2665\u2666\u2663\u2660"},
    {"Weather Icons", u8" \u00B7*\u263C\u2601\u2602\u2603\u2604"},
    {"Starfield", u8" \u00B7\u2219\u22C6\u2726\u2727\u2729\u272A\u272B\u272C\u272D\u272E\u272F\u2605"},
    {"Moon Phases", u8" \u00B7\u263E\u263D\u25D0\u25D1\u25D2\u25D3\u25CF"},
    {"Music Notes", u8" .\u2669\u266A\u266B\u266C\u266D\u266E\u266F"},
    {"Greek Lower", u8" \u03B1\u03B2\u03B3\u03B4\u03B5\u03B6\u03B7\u03B8\u03B9\u03BA\u03BB\u03BC\u03BD\u03BE\u03BF\u03C0\u03C1\u03C3\u03C4\u03C5\u03C6\u03C7\u03C8\u03C9"},
    {"Greek Mixed", u8" \u03B1\u03B2\u03B3\u03B4\u03B8\u03BB\u03BE\u03C0\u03C3\u03C6\u03C8\u03C9\u0394\u03A3\u03A6\u03A9"},
    {"Cyrillic Clean", u8" \u0430\u0431\u0432\u0433\u0434\u0435\u0436\u0437\u0438\u0439\u043A\u043B\u043C\u043D\u043E\u043F\u0440\u0441\u0442\u0443\u0444\u0445\u0446\u0447\u0448\u0449\u044E\u044F\u0416"},
    {"Katakana Half", u8" \uFF65\uFF71\uFF72\uFF73\uFF74\uFF75\uFF76\uFF77\uFF78\uFF79\uFF7A\uFF7B\uFF7C\uFF7D\uFF7E\uFF7F\uFF80\uFF81\uFF82\uFF83\uFF84"},
    {"Katakana Full", u8" \u30FB\u30A2\u30A4\u30A6\u30A8\u30AA\u30AB\u30AD\u30AF\u30B1\u30B3\u30B5\u30B7\u30B9\u30BB\u30BD\u30BF\u30C1\u30C4\u30C6\u30C8"},
    {"Hangul Jamo", u8" \u318D\u3131\u3134\u3137\u3139\u3141\u3142\u3145\u3147\u3148\u314A\u314B\u314C\u314D\u314E\uAC00\uB098\uB2E4\uB77C\uB9C8\uBC14\uC0AC\uC544"},
    {"Latin Lower", " abcdefghijklmnopqrstuvwxyz"},
    {"Latin Upper", " ABCDEFGHIJKLMNOPQRSTUVWXYZ"},
    {"Small Caps", u8" \u1D00\u0299\u1D04\u1D05\u1D07\uA730\u0262\u029C\u026A\u1D0A\u1D0B\u029F\u1D0D\u0274\u1D0F\u1D18\u0280\uA731\u1D1B\u1D1C\u1D20\u1D21\u028F\u1D22"},
    {"Fullwidth Latin", u8" \uFF61\uFF41\uFF42\uFF43\uFF44\uFF45\uFF46\uFF47\uFF48\uFF49\uFF4A\uFF4B\uFF4C\uFF4D\uFF4E\uFF4F\uFF50\uFF51\uFF52\uFF53\uFF54\uFF55\uFF56\uFF57\uFF58\uFF59\uFF5A"},
    {"Fullwidth Digits", u8" \uFF10\uFF11\uFF12\uFF13\uFF14\uFF15\uFF16\uFF17\uFF18\uFF19\uFF20\uFF03"},
    {"Pixel UI", u8" \u25A1\u25A3\u25A6\u25A9\u25A7\u25A8\u25A0\u2588"},
    {"Progress Bars", u8" \u2591\u2592\u2593\u2588\u2588\u2588"},
    {"Terminal Oldschool", " .:-=+*#%@$"},
    {"Hacker Thin", " .,:;ilI17tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#@"},
    {"Hacker Bold", " .:oO08B#@MW"},
    {"Data Stream", " .,:;!|\\/<>[]{}01#$@"},
    {"Radar Sweep", u8" .\u00B7-\u2571\u2572\u2500\u2502\u253C\u25CE\u25C9\u2B24"},
    {"Blueprint Fine", " .`-:+*#%@MW"},
    {"Solar Flare", " .`'^:;!*xX#%@█"},
    {"Aurora Soft", u8" .\u00B7~\u2248\u223D\u25CC\u25CE\u25D0\u25D1\u2588"},
    {"Ocean Foam", u8" .\u00B7,\u223C\u2248\u224B\u25CC\u25CE\u2593\u2588"},
    {"Forest Moss", " .,:;irsxzunmqpdbkhao#%@"},
    {"Lava Rock", " .`'^:;!+*xX#%@█"},
    {"Candy Pop", u8" .\u00B7\u00B0oO0\u25CB\u25C9\u25CF\u2605\u2588"},
    {"Cyber Neon", " .:-=+*xX$#@█"},
    {"Vapor Mist", u8"  .\u00B7\u00B0~\u2248\u25CC\u25CB\u25CE\u25CF"},
    {"Noir", "  .'`:;!iI1tfLCG08@#"},
    {"Paper Print", "  ..,,::;;ccooOO00##@@"},
    {"Newspaper", " .,:;!1tfLCG08B#@MW"},
    {"Barcode", " .|¦:!I1l#@█"},
    {"QR Texture", u8" \u2591\u2592\u2596\u2597\u2598\u2599\u259A\u259B\u259C\u259D\u259E\u259F\u2588"},
    {"Noise Texture", " `'.,-~:;=!*#$@█"},
    {"Max Density", u8" .\u2591\u2592\u2593\u2588@"}
};
static const int BUILTIN_RAMP_COUNT = (int)(sizeof(BUILTIN_RAMPS) / sizeof(BUILTIN_RAMPS[0]));

static int DecodeUTF8Codepoint(const unsigned char* src, unsigned int* cp) {
    if (!src || !*src) { *cp = 0; return 0; }
    if (src[0] < 0x80) {
        *cp = src[0];
        return 1;
    } else if ((src[0] & 0xE0) == 0xC0 && src[1]) {
        *cp = ((src[0] & 0x1F) << 6) | (src[1] & 0x3F);
        return 2;
    } else if ((src[0] & 0xF0) == 0xE0 && src[1] && src[2]) {
        *cp = ((src[0] & 0x0F) << 12) | ((src[1] & 0x3F) << 6) | (src[2] & 0x3F);
        return 3;
    } else if ((src[0] & 0xF8) == 0xF0 && src[1] && src[2] && src[3]) {
        *cp = ((src[0] & 0x07) << 18) | ((src[1] & 0x3F) << 12) | 
              ((src[2] & 0x3F) << 6) | (src[3] & 0x3F);
        return 4;
    }
    *cp = 0xFFFD;
    return 1;
}

static int EncodeCodepointToUTF16(unsigned int cp, WCHAR* dst, int maxLen) {
    if (maxLen < 1) return 0;
    if (cp < 0x10000) {
        dst[0] = (WCHAR)cp;
        return 1;
    } else if (cp <= 0x10FFFF && maxLen >= 2) {
        cp -= 0x10000;
        dst[0] = (WCHAR)(0xD800 | (cp >> 10));
        dst[1] = (WCHAR)(0xDC00 | (cp & 0x3FF));
        return 2;
    }
    dst[0] = L'?';
    return 1;
}

static int IsContinuingCodepoint(unsigned int cp) {
    if (cp == 0x200D) return 1;
    if (cp >= 0xFE00 && cp <= 0xFE0F) return 1;
    if (cp >= 0xE0100 && cp <= 0xE01EF) return 1;
    if (cp >= 0x0300 && cp <= 0x036F) return 1;
    if (cp >= 0x1AB0 && cp <= 0x1AFF) return 1;
    if (cp >= 0x1DC0 && cp <= 0x1DFF) return 1;
    if (cp >= 0x20D0 && cp <= 0x20FF) return 1;
    if (cp >= 0xFE20 && cp <= 0xFE2F) return 1;
    if (cp >= 0x1F3FB && cp <= 0x1F3FF) return 1;
    if (cp == 0x20E3) return 1;
    if (cp >= 0xE0020 && cp <= 0xE007F) return 1;
    return 0;
}

static int IsRegionalIndicator(unsigned int cp) {
    return (cp >= 0x1F1E6 && cp <= 0x1F1FF);
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

static int GetGraphemeDisplayWidth(const GraphemeCluster* g) {
    if (g->wcharLen <= 0) return 1;
    int consumed;
    unsigned int firstCp = DecodeUTF16ToCodepoint(g->chars, g->wcharLen, &consumed);
    if (IsEmojiCodepoint(firstCp)) return 2;
    if (IsFullWidthCodepoint(firstCp)) return 2;
    if (IsRegionalIndicator(firstCp)) return 2;
    return 1;
}

int ParseUTF8ToGraphemes(const char* utf8, GraphemeCluster* out, int maxClusters) {
    if (!utf8 || !out || maxClusters <= 0) return 0;
    const unsigned char* p = (const unsigned char*)utf8;
    int clusterCount = 0;
    while (*p && clusterCount < maxClusters) {
        GraphemeCluster* g = &out[clusterCount];
        memset(g, 0, sizeof(GraphemeCluster));
        unsigned int cp;
        int bytes = DecodeUTF8Codepoint(p, &cp);
        if (bytes == 0 || cp == 0) break;
        p += bytes;
        if (cp == 0xFE0F || cp == 0xFE0E) continue;
        g->wcharLen = EncodeCodepointToUTF16(cp, g->chars, MAX_GRAPHEME_LEN);
        unsigned int prevCp = cp;
        int isRegionalSeq = IsRegionalIndicator(cp);
        int regionalCount = isRegionalSeq ? 1 : 0;
        while (*p && g->wcharLen < MAX_GRAPHEME_LEN - 2) {
            unsigned int nextCp;
            int nextBytes = DecodeUTF8Codepoint(p, &nextCp);
            if (nextBytes == 0) break;
            int shouldContinue = 0;
            if (isRegionalSeq && IsRegionalIndicator(nextCp) && regionalCount < 2) {
                shouldContinue = 1;
                regionalCount++;
            }
            else if (IsContinuingCodepoint(nextCp)) {
                shouldContinue = 1;
                if (nextCp == 0x200D) {
                    g->wcharLen += EncodeCodepointToUTF16(nextCp, g->chars + g->wcharLen, 
                                                          MAX_GRAPHEME_LEN - g->wcharLen);
                    p += nextBytes;
                    nextBytes = DecodeUTF8Codepoint(p, &nextCp);
                    if (nextBytes > 0 && nextCp != 0) {
                        g->wcharLen += EncodeCodepointToUTF16(nextCp, g->chars + g->wcharLen,
                                                              MAX_GRAPHEME_LEN - g->wcharLen);
                        p += nextBytes;
                        prevCp = nextCp;
                    }
                    continue;
                }
            }
            else if (nextCp == 0x20E3 && prevCp >= 0x30 && prevCp <= 0x39) {
                shouldContinue = 1;
            }
            if (!shouldContinue) break;
            g->wcharLen += EncodeCodepointToUTF16(nextCp, g->chars + g->wcharLen,
                                                   MAX_GRAPHEME_LEN - g->wcharLen);
            p += nextBytes;
            prevCp = nextCp;
        }
        g->displayWidth = GetGraphemeDisplayWidth(g);
        clusterCount++;
    }
    return clusterCount;
}

static char* ReadFileContents(const char* filename, size_t* outSize) {
    FILE* f = fopen(filename, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    size_t size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buffer = (char*)malloc(size + 1);
    if (!buffer) { fclose(f); return NULL; }
    size_t readSize = fread(buffer, 1, size, f);
    buffer[readSize] = '\0';
    fclose(f);
    if (readSize >= 3 && (unsigned char)buffer[0] == 0xEF && 
        (unsigned char)buffer[1] == 0xBB && (unsigned char)buffer[2] == 0xBF) {
        memmove(buffer, buffer + 3, readSize - 2);
        readSize -= 3;
    }
    if (outSize) *outSize = readSize;
    return buffer;
}

static int GetConfigValue(const char* content, const char* key, char* value, int maxLen) {
    if (!content || !key || !value) return 0;
    value[0] = '\0';
    size_t keyLen = strlen(key);
    const char* p = content;
    while (*p) {
        while (*p == '\r' || *p == '\n') p++;
        if (!*p) break;
        if (*p == ';' || *p == '#' || *p == '[') {
            while (*p && *p != '\n') p++;
            continue;
        }
        const char* lineStart = p;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, key, keyLen) == 0) {
            p += keyLen;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '=') {
                p++;
                int i = 0;
                while (*p && *p != '\r' && *p != '\n' && i < maxLen - 1) {
                    value[i++] = *p++;
                }
                value[i] = '\0';
                while (i > 0 && (value[i-1] == ' ' || value[i-1] == '\t')) {
                    value[--i] = '\0';
                }
                return 1;
            }
            p = lineStart;
        }
        while (*p && *p != '\n') p++;
    }
    return 0;
}

static int GetConfigInt(const char* content, const char* key, int defaultVal) {
    char buf[64];
    if (GetConfigValue(content, key, buf, sizeof(buf)) && buf[0]) {
        return atoi(buf);
    }
    return defaultVal;
}

static float GetConfigFloat(const char* content, const char* key, float defaultVal) {
    char buf[64];
    if (GetConfigValue(content, key, buf, sizeof(buf)) && buf[0]) {
        return (float)atof(buf);
    }
    return defaultVal;
}

void AppendBuiltInRamps(void) {
    for (int i = 0; i < BUILTIN_RAMP_COUNT && g_config.rampCount < MAX_RAMPS; i++) {
        int alreadyLoaded = 0;
        for (int j = 0; j < g_config.rampCount; j++) {
            if (strcmp(g_config.ramps[j].name, BUILTIN_RAMPS[i].name) == 0) {
                alreadyLoaded = 1;
                break;
            }
        }
        if (alreadyLoaded) {
            continue;
        }

        UnicodeRamp* ramp = &g_config.ramps[g_config.rampCount];
        memset(ramp, 0, sizeof(UnicodeRamp));
        strncpy(ramp->name, BUILTIN_RAMPS[i].name, sizeof(ramp->name) - 1);
        ramp->count = ParseUTF8ToGraphemes(BUILTIN_RAMPS[i].chars, ramp->clusters, MAX_RAMP_CHARS);
        if (ramp->count > 0) {
            g_config.rampCount++;
        }
    }
}

static void ParseRampsFromConfig(const char* content) {
    g_config.rampCount = 0;
    for (int i = 0; i < MAX_RAMPS && g_config.rampCount < MAX_RAMPS; i++) {
        char keyName[32];
        char rampBuffer[MAX_LINE_LENGTH];
        char nameBuffer[64];
        sprintf(keyName, "Ramp%d", i);
        sprintf(nameBuffer, "Ramp%dName", i);
        if (GetConfigValue(content, keyName, rampBuffer, sizeof(rampBuffer)) && rampBuffer[0]) {
            UnicodeRamp* ramp = &g_config.ramps[g_config.rampCount];
            memset(ramp, 0, sizeof(UnicodeRamp));
            char nameBuf[64] = "";
            GetConfigValue(content, nameBuffer, nameBuf, sizeof(nameBuf));
            if (nameBuf[0]) {
                strncpy(ramp->name, nameBuf, sizeof(ramp->name) - 1);
            } else {
                sprintf(ramp->name, "Ramp %d", i);
            }
            ramp->count = ParseUTF8ToGraphemes(rampBuffer, ramp->clusters, MAX_RAMP_CHARS);
            if (ramp->count > 0) {
                g_config.rampCount++;
            }
        }
    }

    char customRamp[MAX_LINE_LENGTH];
    if (GetConfigValue(content, "CustomRamp", customRamp, sizeof(customRamp)) && customRamp[0]) {
        if (g_config.rampCount < MAX_RAMPS) {
            UnicodeRamp* ramp = &g_config.ramps[g_config.rampCount];
            memset(ramp, 0, sizeof(UnicodeRamp));
            strcpy(ramp->name, "Custom");
            ramp->count = ParseUTF8ToGraphemes(customRamp, ramp->clusters, MAX_RAMP_CHARS);
            if (ramp->count > 0) {
                g_config.rampCount++;
            }
        }
    }

    AppendBuiltInRamps();

    if (g_config.rampCount == 0) {
        UnicodeRamp* ramp = &g_config.ramps[0];
        memset(ramp, 0, sizeof(UnicodeRamp));
        strcpy(ramp->name, "Default");
        ramp->count = ParseUTF8ToGraphemes(DEFAULT_RAMP_STR, ramp->clusters, MAX_RAMP_CHARS);
        g_config.rampCount = 1;
    }
    if (g_config.currentRamp >= g_config.rampCount) {
        g_config.currentRamp = 0;
    }
}

static int ParseSingleLumSeqItem(const char* str, LuminanceSeqItem* item) {
    if (!str || !item) return 0;
    while (*str == ' ' || *str == '\t' || *str == '(') str++;
    if (!*str || *str == ')') return 0;
    item->isRandom = 0;
    item->minVal = 1.0f;
    item->maxVal = 1.0f;
    item->favour = 0.0f;
    const char* randomPos = strstr(str, "random");
    if (!randomPos) randomPos = strstr(str, "Random");
    if (!randomPos) randomPos = strstr(str, "RANDOM");
    if (randomPos) {
        item->isRandom = 1;
        item->minVal = (float)atof(str);
        const char* afterRandom = randomPos + 6;
        item->maxVal = (float)atof(afterRandom);
        const char* favourPos = strstr(str, "favour=");
        if (!favourPos) favourPos = strstr(str, "Favour=");
        if (!favourPos) favourPos = strstr(str, "FAVOUR=");
        if (!favourPos) favourPos = strstr(str, "favor=");
        if (!favourPos) favourPos = strstr(str, "Favor=");
        if (favourPos) {
            const char* valStart = favourPos;
            while (*valStart && *valStart != '=') valStart++;
            if (*valStart == '=') valStart++;
            item->favour = (float)atof(valStart);
            if (item->favour < 0.0f) item->favour = 0.0f;
            if (item->favour > 1.0f) item->favour = 1.0f;
        }
        if (item->minVal > item->maxVal) {
            float tmp = item->minVal;
            item->minVal = item->maxVal;
            item->maxVal = tmp;
        }
    } else {
        item->minVal = (float)atof(str);
        item->maxVal = item->minVal;
    }
    return 1;
}

void ParseLuminanceSequence(const char* seqStr) {
    g_config.luminanceSeqCount = 0;
    if (!seqStr || !*seqStr) return;
    strncpy(g_config.luminanceSeqRaw, seqStr, sizeof(g_config.luminanceSeqRaw) - 1);
    g_config.luminanceSeqRaw[sizeof(g_config.luminanceSeqRaw) - 1] = '\0';
    const char* p = seqStr;
    char itemBuf[256];
    while (*p && g_config.luminanceSeqCount < MAX_LUMINANCE_SEQ) {
        while (*p && *p != '(') p++;
        if (!*p) break;
        const char* start = p;
        p++;
        int depth = 1;
        while (*p && depth > 0) {
            if (*p == '(') depth++;
            else if (*p == ')') depth--;
            p++;
        }
        int len = (int)(p - start);
        if (len > 0 && len < (int)sizeof(itemBuf)) {
            strncpy(itemBuf, start, len);
            itemBuf[len] = '\0';
            if (ParseSingleLumSeqItem(itemBuf, 
                &g_config.luminanceSeq[g_config.luminanceSeqCount])) {
                g_config.luminanceSeqCount++;
            }
        }
        while (*p == ',' || *p == ' ' || *p == '\t') p++;
    }
}

float GetRandomWithFavour(float minVal, float maxVal, float favour) {
    if (minVal >= maxVal) return minVal;
    float r = (float)rand() / (float)RAND_MAX;
    if (favour > 0.0f) {
        float exponent = 1.0f / (1.0f + favour * 9.0f);
        r = powf(r, exponent);
    }
    return minVal + r * (maxVal - minVal);
}

static int IntClamp(int v, int lo, int hi) { 
    return v < lo ? lo : (v > hi ? hi : v); 
}

static float FloatClamp(float v, float lo, float hi) { 
    return v < lo ? lo : (v > hi ? hi : v); 
}

void SetDefaultConfig(void) {
    memset(&g_config, 0, sizeof(Config));
    g_config.colorTheme = THEME_NONE;
    g_config.brightness = 1.0f;
    g_config.contrast = 1.0f;
    g_config.gamma = 1.0f;
    g_config.finalLuminanceMultiplier = 1.00f;
    g_config.luminanceSeqEnabled = 0;
    g_config.luminanceChangeTime = 500;
    g_config.luminanceSeqCount = 0;
    g_config.luminanceSeqRaw[0] = '\0';
    g_config.zoneW = 100;
    g_config.zoneH = 100;
    g_config.asciiRandomPercent = 100.0f;
    g_config.invertRandomPercent = 100.0f;
    g_config.grayscaleRandomPercent = 100.0f;
    g_config.themeRandomPercent = 100.0f;
    g_config.randomEffectsExclusive = 0;
    g_config.opacity = 255;
    g_config.desktopCopyMode = 0;
    g_config.ambientMode = 0;
    g_config.ambientFromSource = 1;
    g_config.ambientFromRamp = 0;
    g_config.ambientLitPercent = 8.0f;
    g_config.ambientGlowStrength = 1.25f;
    g_config.ambientSubdivisions = 4;
    g_config.ambientRadius = 18.0f;
    g_config.ambientProgressiveBleed = 0.65f;
    g_config.ambientColorMatch = 75.0f;
    strcpy(g_config.fontName, "Consolas");
    g_config.heatMode = 0;
    g_config.heatStyle = HEAT_STYLE_SQUARE;
    g_config.heatGlyphMode = HEAT_GLYPH_BLACK;
    g_config.heatRadius = 1.0f;
    g_config.heatBrightness = 1.0f;
    g_config.motionMode = 0;
    g_config.motionSensitivity = 100;
    g_config.motionDecayMs = 240.0f;
    g_config.motionRampUpdateDurationMs = 1000;
    g_config.motionMaxRampUpdates = 3;
    g_config.motionMaxConcurrentPercent = 100.0f;
    g_config.motionHoldUntilNewDraw = 1;
    g_config.motionAutoKillStaticRamps = 0;
    g_config.targetFPS = DEFAULT_FPS;
    g_config.frameBudgetMs = 1000.0f / (float)DEFAULT_FPS;
    g_config.cellSize = DEFAULT_CELL_SIZE;
    g_config.glyphSpacingX = 0;
    g_config.glyphSpacingY = 0;
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
    g_config.experimentalCellSampling = 0;
    g_config.cellSampleMode = 0;
    g_config.cellSampleColorMode = 0;
    g_config.cellSampleGrid = 3;
    g_config.cellSampleRadiusScale = 1.0f;
    g_config.cellSampleEdgeBoost = 0.0f;
    g_config.cellSampleJitter = 0.0f;
    g_config.cellSampleCenterWeight = 0.0f;
    g_config.cellSampleDetailMix = 0.0f;
    g_config.cellSampleLuminanceCompensation = 0.0f;
    g_config.cellSampleHighlightPreserve = 0.0f;
    g_config.currentRamp = 0;
    g_config.rampCount = 0;
    g_config.frameSkip = 0;
    g_config.enableColorLimit = 0;
    g_config.colorLimit = 3;
    g_config.colorRefresh = 1000;
    g_config.colorThreshold = 20;
    g_config.enableRampLimit = 0;
    g_config.rampLimit = 3;
    g_config.rampRefresh = 1000;
}

static int LoadConfigFromPath(const char* path, int createIfMissing) {
    size_t fileSize;
    if (!path || !path[0]) return 0;
    char* content = ReadFileContents(path, &fileSize);
    if (!content) {
        if (!createIfMissing) return 0;
        FILE* f = fopen(path, "wb");
        if (f) {
            fwrite("\xEF\xBB\xBF", 1, 3, f);
            fprintf(f, "; UTF-8 Overlay v18.0 Config\r\n[ACTIVE]\r\n\r\n");
            fprintf(f, "Ramp0= .'`^\",:;Il!i><~+_-?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%%B@$\r\n");
            fprintf(f, "Ramp0Name=Classic ASCII\r\n\r\n");
            fprintf(f, "\r\n");
            fprintf(f, "CurrentRamp=0\r\n\r\n");
            fprintf(f, "; LUMINANCE SEQUENCE\r\n");
            fprintf(f, "LuminanceSequenceEnabled=0\r\n");
            fprintf(f, "LuminanceChangeTime=500\r\n");
            fprintf(f, "LuminanceSequence=(1)\r\n\r\n");
            fprintf(f, "ColorTheme=0\r\nInvert=0\r\nGrayscale=0\r\n");
            fprintf(f, "Brightness=1.00\r\nContrast=1.00\r\nGamma=1.00\r\n");
            fprintf(f, "FinalLuminanceMultiplier=1.00\r\n\r\n");
            fprintf(f, "Opacity=255\r\nDesktopCopyMode=0\r\nAmbientMode=0\r\nAmbientFromSource=1\r\nAmbientFromRamp=0\r\nAmbientLitPercent=8.00\r\nAmbientGlowStrength=1.25\r\nAmbientSubdivisions=4\r\nAmbientRadius=18.00\r\nAmbientProgressiveBleed=0.65\r\nAmbientColorMatch=75.00\r\nCellSize=8\r\nGlyphSpacingX=0\r\nGlyphSpacingY=0\r\nAsciiRandomPercent=100.00000000\r\nInvertRandomPercent=100.00000000\r\nGrayscaleRandomPercent=100.00000000\r\nThemeRandomPercent=100.00000000\r\nRandomEffectsExclusive=0\r\nVariableFontMode=0\r\nStrictNoFontOverlap=1\r\nVariableFontMinScale=0.75\r\nVariableFontMaxScale=1.00\r\nVariableFontRandomness=1.00\r\nVariableFontRegionSize=4\r\nVariableFontSeed=1337\r\nVariableFontPulseSpeed=0.00\r\nVariableFontIndependentAxes=0\r\nVariableFontMinWidthScale=1.00\r\nVariableFontMaxWidthScale=1.00\r\nVariableFontMinHeightScale=1.00\r\nVariableFontMaxHeightScale=1.00\r\nVariableCellMode=0\r\nStrictNoCellOverlap=1\r\nVariableCellMinScale=0.75\r\nVariableCellMaxScale=1.00\r\nVariableCellRandomness=1.00\r\nVariableCellRegionSize=4\r\nVariableCellSeed=7331\r\nVariableCellPulseSpeed=0.00\r\nVariableCellAffectsSampling=1\r\nVariableCellAffectsFont=0\r\nExperimentalCellSampling=0\r\nCellSampleMode=0\r\nCellSampleColorMode=0\r\nCellSampleGrid=3\r\nCellSampleRadiusScale=1.00\r\nCellSampleEdgeBoost=0.00\r\nCellSampleJitter=0.00\r\nCellSampleCenterWeight=0.00\r\nCellSampleDetailMix=0.00\r\nCellSampleLuminanceCompensation=0.00\r\nCellSampleHighlightPreserve=0.00\r\nTargetFPS=240\r\nFrameBudgetMs=4.17\r\nFontName=Consolas\r\n");
            fprintf(f, "MotionMode=0\r\nMotionSensitivity=100\r\nMotionDecayMs=240.00\r\nMotionRampUpdateDurationMs=1000\r\nMotionMaxRampUpdates=3\r\nMotionMaxConcurrentPercent=100.00\r\nMotionHoldUntilNewDraw=1\r\nMotionAutoKillStaticRamps=0\r\n");
            fclose(f);
        }
        content = ReadFileContents(path, &fileSize);
        if (!content) {
            g_config.ramps[0].count = ParseUTF8ToGraphemes(DEFAULT_RAMP_STR, g_config.ramps[0].clusters, MAX_RAMP_CHARS);
            strcpy(g_config.ramps[0].name, "Default");
            g_config.rampCount = 1;
            return 0;
        }
    }
    ParseRampsFromConfig(content);
    g_config.currentRamp = IntClamp(GetConfigInt(content, "CurrentRamp", 0), 0, g_config.rampCount > 0 ? g_config.rampCount - 1 : 0);
    g_config.colorTheme = IntClamp(GetConfigInt(content, "ColorTheme", 0), 0, THEME_COUNT - 1);
    g_config.invert = GetConfigInt(content, "Invert", 0);
    g_config.grayscale = GetConfigInt(content, "Grayscale", 0);
    g_config.brightness = FloatClamp(GetConfigFloat(content, "Brightness", 1.0f), 0.1f, 3.0f);
    g_config.contrast = FloatClamp(GetConfigFloat(content, "Contrast", 1.0f), 0.1f, 3.0f);
    g_config.gamma = FloatClamp(GetConfigFloat(content, "Gamma", 1.0f), 0.5f, 2.0f);
    g_config.finalLuminanceMultiplier = FloatClamp(GetConfigFloat(content, "FinalLuminanceMultiplier", 1.0f), 0.0f, 10.0f);
    g_config.luminanceSeqEnabled = GetConfigInt(content, "LuminanceSequenceEnabled", 0);
    g_config.luminanceChangeTime = IntClamp(GetConfigInt(content, "LuminanceChangeTime", 500), 50, 60000);
    char seqBuf[MAX_LINE_LENGTH];
    if (GetConfigValue(content, "LuminanceSequence", seqBuf, sizeof(seqBuf)) && seqBuf[0]) {
        ParseLuminanceSequence(seqBuf);
    }
    g_config.zoneEnable = GetConfigInt(content, "ZoneEnable", 0);
    g_config.zoneX = IntClamp(GetConfigInt(content, "ZoneX", 0), 0, 100);
    g_config.zoneY = IntClamp(GetConfigInt(content, "ZoneY", 0), 0, 100);
    g_config.zoneW = IntClamp(GetConfigInt(content, "ZoneW", 100), 0, 100);
    g_config.zoneH = IntClamp(GetConfigInt(content, "ZoneH", 100), 0, 100);
    g_config.asciiRandomPercent = FloatClamp(GetConfigFloat(content, "AsciiRandomPercent", 100.0f), 0.0f, 100.0f);
    g_config.invertRandomPercent = FloatClamp(GetConfigFloat(content, "InvertRandomPercent", 100.0f), 0.0f, 100.0f);
    g_config.grayscaleRandomPercent = FloatClamp(GetConfigFloat(content, "GrayscaleRandomPercent", 100.0f), 0.0f, 100.0f);
    g_config.themeRandomPercent = FloatClamp(GetConfigFloat(content, "ThemeRandomPercent", 100.0f), 0.0f, 100.0f);
    g_config.randomEffectsExclusive = GetConfigInt(content, "RandomEffectsExclusive", 0) ? 1 : 0;
    g_config.opacity = IntClamp(GetConfigInt(content, "Opacity", 255), 0, 255);
    g_config.desktopCopyMode = GetConfigInt(content, "DesktopCopyMode", 0) ? 1 : 0;
    g_config.ambientMode = GetConfigInt(content, "AmbientMode", 0) ? 1 : 0;
    g_config.ambientFromSource = GetConfigInt(content, "AmbientFromSource", 1) ? 1 : 0;
    g_config.ambientFromRamp = GetConfigInt(content, "AmbientFromRamp", 0) ? 1 : 0;
    if (!g_config.ambientFromSource && !g_config.ambientFromRamp) g_config.ambientFromSource = 1;
    g_config.ambientLitPercent = FloatClamp(GetConfigFloat(content, "AmbientLitPercent", 8.0f), 0.0f, 100.0f);
    g_config.ambientGlowStrength = FloatClamp(GetConfigFloat(content, "AmbientGlowStrength", 1.25f), 0.0f, 5.0f);
    g_config.ambientSubdivisions = IntClamp(GetConfigInt(content, "AmbientSubdivisions", 4), 1, 16);
    g_config.ambientRadius = FloatClamp(GetConfigFloat(content, "AmbientRadius", 18.0f), 0.0f, 256.0f);
    g_config.ambientProgressiveBleed = FloatClamp(GetConfigFloat(content, "AmbientProgressiveBleed", 0.65f), 0.0f, 1.0f);
    g_config.ambientColorMatch = FloatClamp(GetConfigFloat(content, "AmbientColorMatch", 75.0f), 0.0f, 100.0f);
    char fontBuf[64];
    if (GetConfigValue(content, "FontName", fontBuf, sizeof(fontBuf)) && fontBuf[0]) {
        strncpy(g_config.fontName, fontBuf, sizeof(g_config.fontName) - 1);
        g_config.fontName[sizeof(g_config.fontName) - 1] = '\0';
    }
    g_config.heatMode = GetConfigInt(content, "HeatMode", 0);
    g_config.heatStyle = IntClamp(GetConfigInt(content, "HeatStyle", HEAT_STYLE_SQUARE), 0, HEAT_STYLE_COUNT - 1);
    g_config.heatGlyphMode = IntClamp(GetConfigInt(content, "HeatGlyphMode", HEAT_GLYPH_BLACK), 0, HEAT_GLYPH_COUNT - 1);
    g_config.heatRadius = FloatClamp(GetConfigFloat(content, "HeatRadius", 1.0f), 0.0f, 1.0f);
    g_config.heatBrightness = FloatClamp(GetConfigFloat(content, "HeatBrightness", 1.0f), 0.0f, 3.0f);
    g_config.motionMode = GetConfigInt(content, "MotionMode", 0);
    g_config.motionSensitivity = IntClamp(GetConfigInt(content, "MotionSensitivity", 100), 0, 100);
    g_config.motionDecayMs = FloatClamp(GetConfigFloat(content, "MotionDecayMs", 240.0f), 0.0f, 1000.0f);
    g_config.motionRampUpdateDurationMs = IntClamp(GetConfigInt(content, "MotionRampUpdateDurationMs", 1000), 0, 10000);
    g_config.motionMaxRampUpdates = GetConfigInt(content, "MotionMaxRampUpdates", 3);
    if (g_config.motionMaxRampUpdates < 0) g_config.motionMaxRampUpdates = 0;
    g_config.motionMaxConcurrentPercent = FloatClamp(GetConfigFloat(content, "MotionMaxConcurrentPercent", 100.0f), 0.0f, 100.0f);
    g_config.motionHoldUntilNewDraw = GetConfigInt(content, "MotionHoldUntilNewDraw", 1);
    g_config.motionAutoKillStaticRamps = GetConfigInt(content, "MotionAutoKillStaticRamps", 0) ? 1 : 0;
    g_config.targetFPS = IntClamp(GetConfigInt(content, "TargetFPS", DEFAULT_FPS), 1, 240);
    g_config.frameBudgetMs = FloatClamp(GetConfigFloat(content, "FrameBudgetMs", 1000.0f / (float)g_config.targetFPS), 1.0f, 100.0f);
    g_config.cellSize = IntClamp(GetConfigInt(content, "CellSize", DEFAULT_CELL_SIZE), MIN_CELL_SIZE, MAX_CELL_SIZE);
    g_config.glyphSpacingX = IntClamp(GetConfigInt(content, "GlyphSpacingX", 0), -(MAX_CELL_SIZE - 1), 64);
    g_config.glyphSpacingY = IntClamp(GetConfigInt(content, "GlyphSpacingY", 0), -(MAX_CELL_SIZE - 1), 64);
    int minSpacingForCell = 1 - g_config.cellSize;
    if (g_config.glyphSpacingX < minSpacingForCell) g_config.glyphSpacingX = minSpacingForCell;
    if (g_config.glyphSpacingY < minSpacingForCell) g_config.glyphSpacingY = minSpacingForCell;
    g_config.variableFontMode = GetConfigInt(content, "VariableFontMode", 0) ? 1 : 0;
    g_config.strictNoFontOverlap = GetConfigInt(content, "StrictNoFontOverlap", 1) ? 1 : 0;
    g_config.variableFontMinScale = FloatClamp(GetConfigFloat(content, "VariableFontMinScale", 0.75f), 0.10f, 3.00f);
    g_config.variableFontMaxScale = FloatClamp(GetConfigFloat(content, "VariableFontMaxScale", 1.0f), 0.10f, 3.00f);
    if (g_config.variableFontMaxScale < g_config.variableFontMinScale) {
        float t = g_config.variableFontMinScale;
        g_config.variableFontMinScale = g_config.variableFontMaxScale;
        g_config.variableFontMaxScale = t;
    }
    if (g_config.strictNoFontOverlap && g_config.variableFontMaxScale > 1.0f) g_config.variableFontMaxScale = 1.0f;
    g_config.variableFontRandomness = FloatClamp(GetConfigFloat(content, "VariableFontRandomness", 1.0f), 0.0f, 1.0f);
    g_config.variableFontRegionSize = IntClamp(GetConfigInt(content, "VariableFontRegionSize", 4), 1, 128);
    g_config.variableFontSeed = GetConfigInt(content, "VariableFontSeed", 1337);
    g_config.variableFontPulseSpeed = FloatClamp(GetConfigFloat(content, "VariableFontPulseSpeed", 0.0f), 0.0f, 10.0f);
    g_config.variableFontIndependentAxes = GetConfigInt(content, "VariableFontIndependentAxes", 0) ? 1 : 0;
    g_config.variableFontMinWidthScale = FloatClamp(GetConfigFloat(content, "VariableFontMinWidthScale", 1.0f), 0.10f, 3.0f);
    g_config.variableFontMaxWidthScale = FloatClamp(GetConfigFloat(content, "VariableFontMaxWidthScale", 1.0f), 0.10f, 3.0f);
    g_config.variableFontMinHeightScale = FloatClamp(GetConfigFloat(content, "VariableFontMinHeightScale", 1.0f), 0.10f, 3.0f);
    g_config.variableFontMaxHeightScale = FloatClamp(GetConfigFloat(content, "VariableFontMaxHeightScale", 1.0f), 0.10f, 3.0f);
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
    g_config.variableCellMode = GetConfigInt(content, "VariableCellMode", 0) ? 1 : 0;
    g_config.strictNoCellOverlap = GetConfigInt(content, "StrictNoCellOverlap", 1) ? 1 : 0;
    g_config.variableCellMinScale = FloatClamp(GetConfigFloat(content, "VariableCellMinScale", 0.75f), 0.10f, 3.00f);
    g_config.variableCellMaxScale = FloatClamp(GetConfigFloat(content, "VariableCellMaxScale", 1.0f), 0.10f, 3.00f);
    if (g_config.variableCellMaxScale < g_config.variableCellMinScale) {
        float t = g_config.variableCellMinScale;
        g_config.variableCellMinScale = g_config.variableCellMaxScale;
        g_config.variableCellMaxScale = t;
    }
    if (g_config.strictNoCellOverlap && g_config.variableCellMaxScale > 1.0f) g_config.variableCellMaxScale = 1.0f;
    g_config.variableCellRandomness = FloatClamp(GetConfigFloat(content, "VariableCellRandomness", 1.0f), 0.0f, 1.0f);
    g_config.variableCellRegionSize = IntClamp(GetConfigInt(content, "VariableCellRegionSize", 4), 1, 128);
    g_config.variableCellSeed = GetConfigInt(content, "VariableCellSeed", 7331);
    g_config.variableCellPulseSpeed = FloatClamp(GetConfigFloat(content, "VariableCellPulseSpeed", 0.0f), 0.0f, 10.0f);
    g_config.variableCellAffectsSampling = GetConfigInt(content, "VariableCellAffectsSampling", 1) ? 1 : 0;
    g_config.variableCellAffectsFont = GetConfigInt(content, "VariableCellAffectsFont", 0) ? 1 : 0;
    g_config.experimentalCellSampling = GetConfigInt(content, "ExperimentalCellSampling", 0) ? 1 : 0;
    g_config.cellSampleMode = IntClamp(GetConfigInt(content, "CellSampleMode", 0), 0, 2);
    g_config.cellSampleColorMode = IntClamp(GetConfigInt(content, "CellSampleColorMode", 0), 0, 3);
    g_config.cellSampleGrid = IntClamp(GetConfigInt(content, "CellSampleGrid", 3), 1, 50);
    g_config.cellSampleRadiusScale = FloatClamp(GetConfigFloat(content, "CellSampleRadiusScale", 1.0f), 0.10f, 3.0f);
    g_config.cellSampleEdgeBoost = FloatClamp(GetConfigFloat(content, "CellSampleEdgeBoost", 0.0f), 0.0f, 2.0f);
    g_config.cellSampleJitter = FloatClamp(GetConfigFloat(content, "CellSampleJitter", 0.0f), 0.0f, 1.0f);
    g_config.cellSampleCenterWeight = FloatClamp(GetConfigFloat(content, "CellSampleCenterWeight", 0.0f), 0.0f, 8.0f);
    g_config.cellSampleDetailMix = FloatClamp(GetConfigFloat(content, "CellSampleDetailMix", 0.0f), 0.0f, 1.0f);
    g_config.cellSampleLuminanceCompensation = FloatClamp(GetConfigFloat(content, "CellSampleLuminanceCompensation", 0.0f), 0.0f, 2.0f);
    g_config.cellSampleHighlightPreserve = FloatClamp(GetConfigFloat(content, "CellSampleHighlightPreserve", 0.0f), 0.0f, 1.0f);
    g_config.frameSkip = IntClamp(GetConfigInt(content, "FrameSkip", 0), 0, 10);
    g_config.enableColorLimit = GetConfigInt(content, "EnableColorLimit", 0);
    g_config.colorLimit = IntClamp(GetConfigInt(content, "ColorLimit", 3), 1, 50);
    g_config.colorRefresh = IntClamp(GetConfigInt(content, "ColorRefresh", 1000), 100, 10000);
    g_config.colorThreshold = IntClamp(GetConfigInt(content, "ColorThreshold", 20), 1, 100);
    g_config.enableRampLimit = GetConfigInt(content, "EnableRampLimit", 0);
    g_config.rampLimit = IntClamp(GetConfigInt(content, "RampLimit", 3), 1, 50);
    g_config.rampRefresh = IntClamp(GetConfigInt(content, "RampRefresh", 1000), 100, 10000);
    free(content);
    return 1;
}

void LoadConfig(void) {
    LoadConfigFromPath(CONFIG_FILE, 1);
}

int LoadConfigFromFile(const char* path) {
    return LoadConfigFromPath(path, 0);
}

static void RampToUTF8(const UnicodeRamp* ramp, char* buf, int maxLen) {
    int pos = 0;
    for (int i = 0; i < ramp->count && pos < maxLen - 20; i++) {
        int written = WideCharToMultiByte(CP_UTF8, 0, ramp->clusters[i].chars, ramp->clusters[i].wcharLen, buf + pos, maxLen - pos - 1, NULL, NULL);
        pos += written;
    }
    buf[pos] = '\0';
}

int SaveConfigToNamedFile(const char* path) {
    if (!path || !path[0]) return 0;
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    fwrite("\xEF\xBB\xBF", 1, 3, f);
    fprintf(f, "; UTF-8 Overlay v18.0 Configuration\r\n\r\n[ACTIVE]\r\n\r\n");
    for (int i = 0; i < g_config.rampCount; i++) {
        char rampBuf[MAX_LINE_LENGTH];
        RampToUTF8(&g_config.ramps[i], rampBuf, sizeof(rampBuf));
        fprintf(f, "Ramp%d=%s\r\nRamp%dName=%s\r\n", i, rampBuf, i, g_config.ramps[i].name);
    }
    fprintf(f, "\r\nCurrentRamp=%d\r\n\r\n", g_config.currentRamp);
    fprintf(f, "LuminanceSequenceEnabled=%d\r\n", g_config.luminanceSeqEnabled);
    fprintf(f, "LuminanceChangeTime=%d\r\n", g_config.luminanceChangeTime);
    fprintf(f, "LuminanceSequence=%s\r\n\r\n", g_config.luminanceSeqRaw[0] ? g_config.luminanceSeqRaw : "(1)");
    fprintf(f, "ColorTheme=%d\r\nInvert=%d\r\nGrayscale=%d\r\n", g_config.colorTheme, g_config.invert, g_config.grayscale);
    fprintf(f, "Brightness=%.2f\r\nContrast=%.2f\r\nGamma=%.2f\r\n", g_config.brightness, g_config.contrast, g_config.gamma);
    fprintf(f, "FinalLuminanceMultiplier=%.2f\r\n\r\n", g_config.finalLuminanceMultiplier);
    fprintf(f, "Opacity=%d\r\nDesktopCopyMode=%d\r\nAmbientMode=%d\r\nAmbientFromSource=%d\r\nAmbientFromRamp=%d\r\nAmbientLitPercent=%.2f\r\nAmbientGlowStrength=%.2f\r\nAmbientSubdivisions=%d\r\nAmbientRadius=%.2f\r\nAmbientProgressiveBleed=%.2f\r\nAmbientColorMatch=%.2f\r\nCellSize=%d\r\nGlyphSpacingX=%d\r\nGlyphSpacingY=%d\r\nAsciiRandomPercent=%.8f\r\nInvertRandomPercent=%.8f\r\nGrayscaleRandomPercent=%.8f\r\nThemeRandomPercent=%.8f\r\nRandomEffectsExclusive=%d\r\nVariableFontMode=%d\r\nStrictNoFontOverlap=%d\r\nVariableFontMinScale=%.2f\r\nVariableFontMaxScale=%.2f\r\nVariableFontRandomness=%.2f\r\nVariableFontRegionSize=%d\r\nVariableFontSeed=%d\r\nVariableFontPulseSpeed=%.2f\r\nVariableFontIndependentAxes=%d\r\nVariableFontMinWidthScale=%.2f\r\nVariableFontMaxWidthScale=%.2f\r\nVariableFontMinHeightScale=%.2f\r\nVariableFontMaxHeightScale=%.2f\r\nVariableCellMode=%d\r\nStrictNoCellOverlap=%d\r\nVariableCellMinScale=%.2f\r\nVariableCellMaxScale=%.2f\r\nVariableCellRandomness=%.2f\r\nVariableCellRegionSize=%d\r\nVariableCellSeed=%d\r\nVariableCellPulseSpeed=%.2f\r\nVariableCellAffectsSampling=%d\r\nVariableCellAffectsFont=%d\r\nExperimentalCellSampling=%d\r\nCellSampleMode=%d\r\nCellSampleColorMode=%d\r\nCellSampleGrid=%d\r\nCellSampleRadiusScale=%.2f\r\nCellSampleEdgeBoost=%.2f\r\nCellSampleJitter=%.2f\r\nCellSampleCenterWeight=%.2f\r\nCellSampleDetailMix=%.2f\r\nCellSampleLuminanceCompensation=%.2f\r\nCellSampleHighlightPreserve=%.2f\r\nTargetFPS=%d\r\nFrameBudgetMs=%.2f\r\nFontName=%s\r\n\r\n",
            g_config.opacity, g_config.desktopCopyMode, g_config.ambientMode,
            g_config.ambientFromSource, g_config.ambientFromRamp,
            g_config.ambientLitPercent, g_config.ambientGlowStrength,
            g_config.ambientSubdivisions, g_config.ambientRadius,
            g_config.ambientProgressiveBleed, g_config.ambientColorMatch,
            g_config.cellSize, g_config.glyphSpacingX, g_config.glyphSpacingY,
            (double)g_config.asciiRandomPercent,
            (double)g_config.invertRandomPercent,
            (double)g_config.grayscaleRandomPercent,
            (double)g_config.themeRandomPercent,
            g_config.randomEffectsExclusive,
            g_config.variableFontMode, g_config.strictNoFontOverlap,
            g_config.variableFontMinScale, g_config.variableFontMaxScale,
            g_config.variableFontRandomness, g_config.variableFontRegionSize,
            g_config.variableFontSeed, g_config.variableFontPulseSpeed,
            g_config.variableFontIndependentAxes,
            g_config.variableFontMinWidthScale, g_config.variableFontMaxWidthScale,
            g_config.variableFontMinHeightScale, g_config.variableFontMaxHeightScale,
            g_config.variableCellMode, g_config.strictNoCellOverlap,
            g_config.variableCellMinScale, g_config.variableCellMaxScale,
            g_config.variableCellRandomness, g_config.variableCellRegionSize,
            g_config.variableCellSeed, g_config.variableCellPulseSpeed,
            g_config.variableCellAffectsSampling, g_config.variableCellAffectsFont,
            g_config.experimentalCellSampling, g_config.cellSampleMode,
            g_config.cellSampleColorMode, g_config.cellSampleGrid,
            g_config.cellSampleRadiusScale,
            g_config.cellSampleEdgeBoost, g_config.cellSampleJitter,
            g_config.cellSampleCenterWeight, g_config.cellSampleDetailMix,
            g_config.cellSampleLuminanceCompensation,
            g_config.cellSampleHighlightPreserve,
            g_config.targetFPS, g_config.frameBudgetMs, g_config.fontName);
    fprintf(f, "HeatMode=%d\r\nHeatStyle=%d\r\nHeatGlyphMode=%d\r\nHeatRadius=%.2f\r\nHeatBrightness=%.2f\r\n\r\n", g_config.heatMode, g_config.heatStyle, g_config.heatGlyphMode, g_config.heatRadius, g_config.heatBrightness);
    fprintf(f, "MotionMode=%d\r\nMotionSensitivity=%d\r\nMotionDecayMs=%.2f\r\nMotionRampUpdateDurationMs=%d\r\nMotionMaxRampUpdates=%d\r\nMotionMaxConcurrentPercent=%.2f\r\nMotionHoldUntilNewDraw=%d\r\nMotionAutoKillStaticRamps=%d\r\n\r\n",
            g_config.motionMode, g_config.motionSensitivity, g_config.motionDecayMs,
            g_config.motionRampUpdateDurationMs, g_config.motionMaxRampUpdates,
            g_config.motionMaxConcurrentPercent, g_config.motionHoldUntilNewDraw,
            g_config.motionAutoKillStaticRamps);
    fprintf(f, "ZoneEnable=%d\r\nZoneX=%d\r\nZoneY=%d\r\nZoneW=%d\r\nZoneH=%d\r\n\r\n", g_config.zoneEnable, g_config.zoneX, g_config.zoneY, g_config.zoneW, g_config.zoneH);
    fprintf(f, "FrameSkip=%d\r\n\r\n", g_config.frameSkip);
    fprintf(f, "EnableColorLimit=%d\r\nColorLimit=%d\r\nColorRefresh=%d\r\nColorThreshold=%d\r\n\r\n", g_config.enableColorLimit, g_config.colorLimit, g_config.colorRefresh, g_config.colorThreshold);
    fprintf(f, "EnableRampLimit=%d\r\nRampLimit=%d\r\nRampRefresh=%d\r\n", g_config.enableRampLimit, g_config.rampLimit, g_config.rampRefresh);
    fclose(f);
    return 1;
}

void SaveConfigToFile(void) {
    SaveConfigToNamedFile(CONFIG_FILE);
}
