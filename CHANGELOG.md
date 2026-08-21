# Changelog

All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/), and this
project adheres to [Semantic Versioning](https://semver.org/).

## [v1.0.0-alpha.2] — 2026-08-21

### Fixed
- Bare `Ctrl+Alt` once again opens the settings menu (an earlier build had it
  bound to toggling ASCII Random density). `Ctrl+Alt+S` still works too.
- README hotkey table, tutorial, and troubleshooting entries updated to match
  the restored binding.

### Added
- MIT license (`LICENSE`), with a License section in the README noting the
  vendored Dear ImGui license.

## [v1.0.0-alpha.1] — 2026-08-21

Initial public release.

### Renderer
- Real-time ASCII overlay: DXGI Desktop Duplication capture → CUDA glyph
  rendering → D3D11 interop presentation (automatic staged fallback).
- Multi-GPU support: CUDA device is selected to match the adapter driving
  the display.
- Full-screen cell coverage (ceiling-divided grid; no dead strips at the
  screen edges).
- Glyphs sized exactly to their cells (no clipping/squash), grayscale
  antialiasing for an accurate coverage mask, true font-height vertical
  centering.
- **Truncation engine**: every glyph is measured against its atlas cell and
  automatically re-rendered scaled down until it fits.
- **Per-frame collision guard**: any glyph scale that would spill into a
  neighboring cell is clamped every frame.
- Deleted dead kernel paths; fixed motion+heat background blending, ambient
  glow zone passthrough, and temporal-state resets on grid/ramp/config
  changes.

### Features
- Unicode ramps with grapheme clustering, wide-glyph (emoji/CJK) support,
  built-in ramp library, and a live ramp editor.
- 59 color themes, several animated.
- Filters: brightness, contrast, gamma, grayscale, invert, final luminance,
  opacity — plus per-cell random application:
  - ASCII Random % (1–100, 8-decimal precision)
  - Invert / Grayscale / Theme Random %
  - Exclusive mode so random effects never claim the same cells
- Motion mode (sensitivity, decay, ramp-update throttles, concurrency cap).
- Heat mode (5 shapes, radius/brightness, black or hidden glyph cutout).
- Desktop Copy Mode (stylized live pixel feed).
- Ambient glow post-process (source/ramp colors, radius, bleed controls).
- Capture zones (X/Y/W/H percent rectangle).
- Variable glyph/cell sizing with pulse and independent axes.
- Experimental multi-sample cell pipeline (grid, jitter, edge boost, detail
  mix, highlight preservation).
- Ramp & color change limiters with refresh windows.
- Luminance sequences (steps or favour-weighted random ranges).
- Presets export/import, config auto-save/reload.
- Metrics dashboard: FPS, stage timings, p95/jitter, CPU/GPU/VRAM telemetry,
  history graphs.
- Default font Consolas at cell size 12; any installed font selectable by
  name.
- Resolution changes rebuild all graphics/interop resources automatically.

[unreleased]: https://github.com/replitter/ascii2/compare/v1.0.0-alpha.2...HEAD
[v1.0.0-alpha.2]: https://github.com/replitter/ascii2/compare/v1.0.0-alpha.1...v1.0.0-alpha.2
[v1.0.0-alpha.1]: https://github.com/replitter/ascii2/releases/tag/v1.0.0-alpha.1
