#ifndef FONT_ATLAS_H
#define FONT_ATLAS_H

#include <d3d11.h>
#include "config.h"

struct FontAtlas {
    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    int offsets[MAX_RAMP_CHARS];
    int widths[MAX_RAMP_CHARS];
    int displayWidths[MAX_RAMP_CHARS];
    int count = 0;
    int cellW = 0;
    int cellH = 0;
};

extern FontAtlas g_fontAtlas;

bool BuildFontAtlas(int cellSize);
void CleanupFontAtlas();

#endif
