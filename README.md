# ascii2 — CUDA ASCII Desktop Overlay

A real-time ASCII art overlay for Windows. It captures your desktop with the DXGI Desktop
Duplication API, converts every screen cell to a Unicode glyph on the GPU (CUDA ↔ D3D11
interop), and draws the result back over your screen as a transparent, click-through overlay.
Your entire desktop becomes live ASCII art at up to 240 FPS.

```
.'`^",:;Il!i><~+_-?][}{1)(|\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$
```

---

## Table of Contents

1. [What It Does](#what-it-does)
2. [Requirements](#requirements)
3. [Building](#building)
4. [Running](#running)
5. [What To Expect](#what-to-expect)
6. [Tutorial — First Run](#tutorial--first-run)
7. [Complete Hotkey Reference](#complete-hotkey-reference)
8. [Feature Guide](#feature-guide)
   - [Ramps (Glyph Sets)](#ramps-glyph-sets)
   - [Themes](#themes)
   - [Filters & Random Filter Percents](#filters--random-filter-percents)
   - [ASCII Random Density](#ascii-random-density)
   - [Motion Mode](#motion-mode)
   - [Heat Mode](#heat-mode)
   - [Desktop Copy Mode](#desktop-copy-mode)
   - [Ambient Glow](#ambient-glow)
   - [Capture Zones](#capture-zones)
   - [Variable Glyph / Cell Sizing](#variable-glyph--cell-sizing)
   - [Cell Sampling (Experimental)](#cell-sampling-experimental)
   - [Ramp & Color Limiters](#ramp--color-limiters)
   - [Luminance Sequences](#luminance-sequences)
   - [Performance Controls](#performance-controls)
9. [config.ini Reference](#configini-reference)
10. [Metrics Overlay](#metrics-overlay)
11. [Troubleshooting](#troubleshooting)
12. [Architecture Notes](#architecture-notes)

---

## What It Does

Every frame:

1. **Capture** — the desktop image is captured via DXGI Desktop Duplication (zero-copy on GPU).
2. **Grid** — the screen is divided into a grid of cells (default 12 px wide, 24 px tall).
3. **Sample** — each cell's pixels are filtered (brightness / contrast / gamma / grayscale /
   invert) and reduced to a luminance value.
4. **Glyph selection** — the luminance picks a character from your ramp: dark areas get sparse
   characters like `.` and `'`, bright areas get dense ones like `@` and `$`.
5. **Color** — the cell is tinted by the active theme (or keeps the source pixel color).
6. **Render** — CUDA writes the final frame into a D3D11 texture that is presented full-screen
   in a layered, topmost, click-through overlay window that is **excluded from screen capture**
   (OBS/Discord/discussion screenshots never see it).

Everything runs on the GPU. The CPU only feeds parameters and draws the ImGui control panel.

## Requirements

- Windows 10 (1903+) or Windows 11
- **NVIDIA GPU** (CUDA compute capability 5.0+; the build targets `sm_75` / Turing and newer)
- NVIDIA display drivers (any recent version)
- [CUDA Toolkit 13.2](https://developer.nvidia.com/cuda-downloads) (only for building)
- Visual Studio 2026 (v18) with "Desktop development with C++" (only for building)
- An active display connected to the GPU (Desktop Duplication requires a real output)

## Building

The one-shot script (PowerShell):

```powershell
.\build.ps1
```

It compiles `cuda_renderer.cu` with `nvcc` (`-O3 -arch=sm_75`), the C++ sources with MSVC
(`/O2 /MT`), and links everything into **`ascii_cuda.exe`**.

Manual steps, if you prefer:

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
nvcc -c cuda_renderer.cu -o cuda_renderer.obj -O3 -arch=sm_75
cl /c /O2 /MT /utf-8 /DUNICODE /D_UNICODE /I. /Iimgui /Iimgui/backends config.cpp graphics.cpp font_atlas.cpp menu.cpp main.cpp
cl /O2 /MT /utf-8 cuda_renderer.obj imgui*.obj config.obj graphics.obj font_atlas.obj menu.obj main.obj ^
   /Fe:ascii_cuda.exe /link /LIBPATH:"%CUDA_PATH%\lib\x64" cudart.lib d3d11.lib dxgi.lib user32.lib gdi32.lib shell32.lib
```

> **Tip:** if you build from Git Bash, note that Git Bash exports `TMP=/tmp`, which nvcc cannot
> use — set `TMP`/`TEMP` to a real Windows directory first or nvcc fails silently.

If paths in `build.ps1` don't match your machine, adjust the `vcvars64.bat` path and
`CUDA_PATH` at the top/bottom of the script.

## Running

```
ascii_cuda.exe
```

That's it. On first launch it:

1. Detects your primary/largest display and creates a full-screen topmost overlay on it.
2. Creates a default `config.ini` next to the exe.
3. Builds the glyph atlas from your current ramp + font.
4. Starts rendering the ASCII version of your desktop.

Close it with **Ctrl+D**. All settings live in `config.ini` and the in-app menu — nothing is
installed, no registry, no admin rights needed.

## What To Expect

- **The whole screen turns into ASCII.** It is an overlay: your real desktop keeps running
  underneath and remains fully interactive — the overlay is click-through unless a UI panel
  is open.
- **Default look is monochrome glyphs on a darkened version of your desktop.** Set a
  *Color Theme* (59 built in) for the classic green-terminal / retrowave / etc. looks, or
  enable *Desktop Copy Mode* for a stylized full-color pixel feed.
- **It only sees your monitors, not itself.** The overlay window is excluded from capture
  (`WDA_EXCLUDEFROMCAPTURE`), so recordings and screenshots show the clean desktop while you
  see ASCII live.
- **Performance:** default target is 240 FPS; typical GPU cost is a few percent on a modern
  card at 1080p–1440p. Capture, CUDA, and present times are visible in the metrics overlay.
- **Motion is free realism:** in *Motion Mode* only moving regions render glyphs — static
  areas fade to the live background, so video playback pops out of the ASCII.
- **Text stays readable-ish at default cell size** (12×24 px, ~160×45 cells at 1080p).
  Lower cell size = finer detail and heavier GPU cost.
- If a game goes exclusive-fullscreen, the overlay may vanish over it — use borderless
  windowed mode in games.
- HDR displays: the capture is tone-mapped to SDR by the OS pipeline; expect slightly washed
  colors with HDR active.

## Tutorial — First Run

1. **Launch** `ascii_cuda.exe`. You should immediately see your desktop as gray ASCII.
   If the whole screen looks like dense `@`s or empty, adjust *Brightness* /
   *Final Luminance* in the menu (Ctrl+Alt+S) or with Ctrl+Up / Ctrl+Down.
2. **Pick a ramp** — the character set that draws the picture. Cycle built-ins with
   **Ctrl+]** / **Ctrl+[**. Classic ASCII, blocks (`░▒▓█`), binary, katakana (Matrix) and
   more are included.
3. **Pick a theme** — menu → *Filters & Colors* → *Color Theme*. Try *Matrix*,
   *Amber Terminal*, *Retro Wave*, *Dracula*.
4. **Dial the detail** — *Glyph Size* slider (or Ctrl+Left/Right). Smaller = sharper.
   Add *Glyph Spacing* for a gappier terminal feel.
5. **Try Motion Mode** — menu → *Motion Mode* → enable. Now only moving things are drawn.
   Tune *Sensitivity* (Ctrl+Left/Right also works while motion mode is on) and *Decay*.
6. **Try Heat Mode** — Ctrl+M turns bright areas into glowing blobs (great on dark
   wallpapers/videos). Ctrl+B is the "clean" variant without glyph cutouts.
7. **Random density** — press **Ctrl+Alt** (nothing else held). That toggles ASCII Random %
   between 100% and your last value. Set the exact value in the menu under *ASCII Random*:
   e.g. 10% renders only a random tenth of the screen as ASCII.
8. **Random filters** — in *Filters & Colors*, set *Invert Random %* to 30 and *Theme
   Random %* to 30: 30% of cells invert and a different 30% get the theme, in stable random
   scatter. Tick *Random Effects Exclusive* so the effects never claim the same cells.
9. **Zone it** — *Capture Zone* sliders restrict ASCII to any sub-rectangle of the screen
   (e.g. X=0 W=50 = left half only).
10. **Save** — Ctrl+S writes everything to `config.ini`. Export named presets from the
    menu's *Presets* section.

## Complete Hotkey Reference

| Keys | Action |
|---|---|
| `Ctrl+D` | Quit |
| `Ctrl+P` | Pause glyph updates (background stays live) |
| `Ctrl+H` or `` ` `` (tilde) | Help overlay |
| `Ctrl+R` | Reload `config.ini` |
| `Ctrl+S` | Save current settings to `config.ini` |
| `Ctrl+Alt+S` | Settings menu |
| `Ctrl+Shift` | Metrics overlay |
| `Ctrl+Alt` (bare) | **Toggle ASCII Random %** on/off (100% ↔ last value) |
| `Ctrl+`` ` `` | Spacing / ramp editor |
| `Ctrl+]` / `Ctrl+[` | Next / previous ramp |
| `Ctrl+Right` / `Ctrl+Left` | Glyph size +1 / −1 (in Motion Mode: sensitivity ±5) |
| `Ctrl+M` | Heat mode on/off (black glyph cutout) |
| `Ctrl+B` | Heat mode on/off (no cutout) |
| `Ctrl+O` | Desktop Copy Mode on/off |
| `Ctrl+A` | Ambient glow on/off |
| `Ctrl+8` | Invert colors on/off |
| `Ctrl+9` | Color limit on/off |
| `Ctrl+0` | Ramp limit on/off |
| `Ctrl+Up` / `Ctrl+Down` | Final luminance ±0.05 |
| `Ctrl+Shift+Up` / `Down` | Brightness ±0.1 |
| `Ctrl+Alt+Up` / `Down` | Contrast ±0.1 |
| `Ctrl+Alt+L` | Luminance sequence on/off |
| `Ctrl+Shift+R` | Reset everything to defaults |
| `Ctrl+Shift+L` | Relaunch the app |

Hotkeys are global — they work while any application has focus. The bare modifier chords
(Ctrl+Alt, Ctrl+Shift) only fire when no other key is held, so they never collide with the
combo hotkeys above.

## Feature Guide

### Ramps (Glyph Sets)

The ramp is the ordered list of characters from darkest to brightest. Ten+ built-in ramps
ship with the app (classic ASCII, blocks, shades, binary, braille, katakana, emoji mixes…).
The **Ramp Editor** (Ctrl+`) lets you paste any UTF-8 text — emoji, CJK, symbols — and shows
a live byte/cluster count. Glyphs are rendered with the configured font; wide characters
(emoji, CJK) automatically occupy two cells. A built-in **truncation engine** measures every
glyph against its cell and re-renders it scaled down until it fits, so nothing is ever
clipped, and a per-frame **collision guard** on the GPU clamps any glyph that would spill
into a neighboring cell.

Font: any installed font by name (`FontName` in config; default **Consolas**). Emoji fall
back to Segoe UI Emoji, CJK to MS Gothic automatically.

### Themes

59 color themes: Terminal Green, Matrix, Amber Terminal, Solarized (light/dark), Dracula,
Retro Wave, Vaporwave, Synthwave Gold, Firewatch, Aurora, Plasma, Signal Loss, Prism,
Night Vision, and many more. `THEME_NONE` keeps the true source colors. Some themes are
animated (hue drift, pulses) — they use time-based effects, so they keep moving even on a
static desktop.

### Filters & Random Filter Percents

Standard filters: brightness, contrast, gamma, grayscale, invert, final luminance
multiplier — plus **random per-cell application**:

- **Invert Random %** — only a random fraction of cells gets inverted.
- **Grayscale Random %** — only a random fraction becomes grayscale.
- **Theme Random %** — only a random fraction gets the theme tint (rest keep raw color).
- **Random Effects Exclusive** — when ticked, the random effects partition the screen: a
  cell claimed by one random effect can never be claimed by another (no "stealing"), and
  cells claimed by ASCII Random are off-limits to the filter effects entirely.

All of them use the same stable per-cell hash as ASCII Random, so the scatter pattern is
consistent frame to frame (no shimmering), they compose with each other and with ASCII
Random %, and they work identically in Desktop Copy Mode.

### ASCII Random Density

`ASCII Random %` (1–100, 8 decimal places) renders only that fraction of all cells as
ASCII; the rest show the live darkened background as if they were glass. At 10%, roughly
one cell in ten is ASCII. Selection is a stable hash of the cell index — the same cells
stay on/off until you change the value. Toggle it instantly with bare **Ctrl+Alt**.

### Motion Mode

Only cells where the image changed render glyphs; static cells fade back to the desktop.

- **Sensitivity** — diff threshold (higher = more sensitive).
- **Decay (ms)** — how long a cell stays lit after motion stops.
- **Ramp Update Duration / Max Ramp Updates** — throttle how often a moving cell may
  change its character (gives that chunky digital-refresh feel).
- **Max Concurrent %** — cap on the fraction of cells allowed to update per window.
- **Hold Until New Draw** — glyphs freeze until the cell sees new motion.
- **Auto Kill Static Ramps** — force-fades cells whose ramp index is static.

In motion mode the background behind active cells is pure black for maximum contrast, and
the glyph grid only exists where something happened — great for webcam-style silhouettes
of whatever moves on your screen.

### Heat Mode

Bright areas bloom into solid glowing shapes (circle / square / diamond / soft box / bar),
colored by the theme at that cell's intensity. *Heat Radius* controls shape fill, *Heat
Brightness* the glow. The glyph can be cut out of the blob in black (Ctrl+M) or hidden
entirely (Ctrl+B) for a pure "thermal vision" look.

### Desktop Copy Mode

Instead of glyphs, the raw captured desktop is shown through the filter/theme/random-effect
pipeline — a stylized live pixel feed. All limits, heat, motion tinting, zones, and the
random per-cell effects still apply. Toggle with Ctrl+O.

### Ambient Glow

A bloom layer sampled from the surrounding screen: bright areas bleed their color into
nearby cells. Configure source (captured pixels and/or ramp colors), lit percentage,
radius, subdivisions, strength, progressive bleed, and color match. Works in both glyph
and desktop-copy modes, and respects capture zones.

### Capture Zones

Restrict all ASCII rendering to a sub-rectangle of the screen, in percent
(X, Y, W, H). Outside the zone the desktop passes through untouched — the overlay
effectively becomes a shaped ASCII window. Combine with ASCII Random % for e.g.
"a 50%-wide band where only 25% of cells are ASCII".

### Variable Glyph / Cell Sizing

Per-region random glyph scale (with pulse animation and optional independent X/Y axes)
for an organic, hand-typed look. *Strict No Overlap* caps scale at 1.0; the GPU-side
collision guard enforces this every frame regardless. Variable Cell Size does the same
for the sampling footprint, and can optionally drive the glyph scale too.

### Cell Sampling (Experimental)

Instead of one center pixel per cell, sample a grid: configurable grid size, cross or
diamond sample patterns, jitter, center weighting, min/max/detail color modes, edge
boost, luminance compensation, and highlight preservation. Great for extracting detail
from low-contrast video.

### Ramp & Color Limiters

Anti-flicker throttles: *Ramp Limit* caps how many times a cell may change character per
refresh window; *Color Limit* + *Color Threshold* caps color changes (changes smaller than
the threshold are free). Both have configurable refresh windows. Ctrl+9 / Ctrl+0 toggle.

### Luminance Sequences

Animate the global luminance multiplier through a sequence of steps or random ranges with
a favour curve, e.g. `(1)(random 0.4 1.2 favour 0.7)(0.8)`. Ctrl+Alt+L toggles. Great for
breathing/pulsing ASCII looks synced to nothing but time.

### Performance Controls

- **Target FPS** (1–240) — frame timer.
- **Frame Budget (ms)** — used by the metrics overlay for over-budget stats.
- **Frame Skip** (0–10) — render ASCII every N+1th frame (UI still presents every frame).

## config.ini Reference

Key sections (all values are clamped on load, so out-of-range entries are safe):

| Key | Meaning |
|---|---|
| `Ramp0..N` / `Ramp0Name..N` | UTF-8 ramps and display names |
| `CurrentRamp` | Active ramp index |
| `ColorTheme` | Theme index (0 = none, see menu for order) |
| `Brightness` / `Contrast` / `Gamma` | Tone filters |
| `FinalLuminanceMultiplier` | Master glyph brightness |
| `Invert` / `Grayscale` | Global filters |
| `InvertRandomPercent` | % of cells the invert filter applies to |
| `GrayscaleRandomPercent` | % of cells the grayscale filter applies to |
| `ThemeRandomPercent` | % of cells the theme applies to |
| `RandomEffectsExclusive` | 1 = random effects never share cells |
| `AsciiRandomPercent` | % of cells rendered as ASCII |
| `CellSize` | Glyph cell width in px (height = 2×) |
| `GlyphSpacingX/Y` | Extra spacing between cells |
| `FontName` | Any installed font (default Consolas) |
| `Opacity` | Terminal background darkness (0–255) |
| `DesktopCopyMode` | 1 = pixel feed instead of glyphs |
| `ZoneEnable/X/Y/W/H` | Capture zone rectangle (%) |
| `MotionMode`, `Motion*` | Motion detection settings |
| `HeatMode`, `Heat*` | Heat bloom settings |
| `AmbientMode`, `Ambient*` | Ambient glow settings |
| `EnableRampLimit/RampLimit/RampRefresh` | Character change throttle |
| `EnableColorLimit/ColorLimit/ColorRefresh/ColorThreshold` | Color change throttle |
| `TargetFPS` / `FrameBudgetMs` / `FrameSkip` | Performance |
| `LuminanceSequence*` | Animated luminance |
| `VariableFont*` / `VariableCell*` / `CellSample*` | Advanced sizing/sampling |

## Metrics Overlay

Ctrl+Shift opens a live dashboard: FPS, frame/capture/CUDA/present timings, p95 and
jitter, over-budget counts, CPU/GPU/VRAM usage (GPU stats via NVML), skip/failure rates,
grid dimensions, renderer mode, and scrolling history graphs for every major timing.

## Troubleshooting

| Symptom | Fix |
|---|---|
| "No CUDA compatible GPU found" | Install/update NVIDIA drivers; confirm `nvidia-smi` works |
| "Failed to initialize Desktop Duplication" | Ensure a monitor is connected to the GPU you're using; some DRM/secure-desktop states block duplication — retry |
| Overlay invisible over a game | Switch the game to borderless windowed |
| Overlay visible in recordings? | It shouldn't be — the window is capture-excluded. Some capture tools with custom hooks may bypass this |
| Everything is one character | Adjust Brightness / Contrast / Final Luminance; check the ramp isn't a single repeated char |
| Colors look wrong with HDR on | Toggle HDR off, or expect SDR tone-mapping |
| AltGr types weird chars in other apps | Bare Ctrl+Alt is bound to ASCII Random toggle; it only fires when no other key is held, so typing AltGr characters is unaffected |
| Settings lost after restart | You didn't save — Ctrl+S, or enable saving before quitting |
| nvcc fails silently when scripting builds | Set `TMP`/`TEMP` to a Windows path (see Building) |

## Architecture Notes

```
main.cpp            window, hotkeys, frame loop, telemetry
graphics.cpp        D3D11 device/swapchain, Desktop Duplication, display change handling
font_atlas.cpp      GDI glyph atlas (ramp → texture), truncation engine, font fallbacks
cuda_renderer.cu    all GPU passes:
                      compute_cell_commands_kernel   luminance → glyph/color per cell,
                                                     motion, limiters, random effects
                      render_command_buffer_kernel   glyph raster from atlas, heat,
                                                     variable sizing, collision guard
                      render_desktop_copy_kernel     pixel-feed mode
                      ambient_postprocess_kernel     bloom pass
config.cpp/.h       config parse/save, UTF-8 grapheme clustering, width classification
menu.cpp            ImGui UI: settings, ramp editor, metrics, help, presets
```

- **Interop path**: CUDA registers the D3D11 textures and renders in-place (fastest).
- **Staged path**: automatic fallback if interop is unavailable (copies through pinned
  host buffers).
- **Multi-GPU**: CUDA device is selected to match the adapter driving the display, so
  hybrid-graphics laptops use the right GPU.
- **Resolution changes** trigger a full teardown/rebuild of the graphics and interop
  resources; the overlay follows the new geometry automatically.
- Temporal cell state (limiter counters, motion history) resets automatically whenever
  the grid geometry, ramp, or config changes.

Built with the CUDA 13.2 toolkit, D3D11, GDI, and Dear ImGui. Runs entirely locally — no
network access, no telemetry, nothing leaves your machine.

## License

MIT — see [LICENSE](LICENSE). Dear ImGui is vendored under its own MIT license
([imgui/LICENSE.txt](imgui/LICENSE.txt)).
