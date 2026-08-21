# CUDA Renderer Remake Implementation Plan

## Current Build Status

- The source files compile and link when run inside the Visual Studio developer environment.
- `build.ps1` failed because `vcvars64.bat` emits both `PATH` and `Path`; the later inherited value overwrote the initialized MSVC compiler path. The script now preserves the first case-insensitive environment value.
- Verified one-shot build output: `ascii_cuda.exe` was rebuilt successfully on 2026-06-14.

## Core Issues Found

1. CUDA and D3D11 adapter selection are not tied together.
   - `cuda_renderer.cu` sets CUDA device 0.
   - `graphics.cpp` creates the D3D11 device from the default hardware adapter and desktop duplication from adapter/output 0.
   - On hybrid or multi-GPU systems this can make CUDA-D3D11 interop registration fail or produce an unusable executable.

2. CUDA resource registration has partial-failure leaks.
   - `RegisterD3D11Resources()` returns immediately after failures without unregistering resources that were already registered.
   - Rebuilding the font atlas or changing cell size can leave stale interop state if a later registration fails.

3. The renderer has per-cell state races.
   - The kernel launches one thread per output pixel, but multiple threads in the same cell read and write `GPUSetupCellState`.
   - The single `offsetX == 0 && offsetY == 0` writer is not synchronized with other pixel threads in the cell, so glyph index and color can flicker or use stale data.

4. Double-width character handling is incomplete.
   - A cell can render pixels from the glyph stored in the left cell, but the right cell is still independently updated as its own cell.
   - This causes unstable state and possible visual overlap for emoji, CJK, braille, and block glyphs.

5. CUDA API error handling is too thin.
   - Temporary texture objects, surface objects, mapped arrays, kernel launch, and synchronization are not checked individually.
   - When the exe fails at runtime, most failures collapse into a silent `false`.

6. Font atlas interop is fragile.
   - The atlas texture is created with only `D3D11_BIND_SHADER_RESOURCE`.
   - CUDA-D3D registration may be more reliable with textures explicitly designed for interop and without SRV-only assumptions.

7. Desktop duplication is hard-coded to adapter/output 0.
   - The capture output may not match the overlay monitor or the D3D device adapter.
   - Display changes recreate duplication but do not rebuild D3D textures, CUDA registrations, swapchain size, or font atlas state.

8. Menu and hotkey changes do not consistently rebuild dependent GPU state.
   - Some menu changes update `g_config.cellSize` or ramp selection without immediately rebuilding the font atlas.
   - `ActionResetLimits()` is a placeholder and does not clear GPU temporal cell state.

## Target Architecture

1. Own one renderer state object instead of scattered globals.
   - Store CUDA device id, registered resources, mapped arrays, texture/surface objects, cell buffers, and dirty flags together.
   - Add explicit `Initialize`, `Resize`, `RegisterResources`, `Render`, `ResetTemporalState`, and `Shutdown` paths.

2. Match CUDA and D3D devices deliberately.
   - Enumerate DXGI adapters.
   - Pick an adapter with CUDA support using CUDA-D3D interop APIs where available.
   - Create the D3D11 device on that adapter, then set the matching CUDA device.
   - Record the adapter/output used for desktop duplication.

3. Split the CUDA work into deterministic passes.
   - Pass A: one thread per cell samples the captured image, computes luminance, applies temporal ramp/color limits, and writes a stable cell command buffer.
   - Pass B: one thread per output pixel renders from the immutable command buffer into the output surface.
   - This removes per-cell write/read races and makes double-width glyph ownership explicit.

4. Normalize all renderer inputs.
   - Clamp params before launching CUDA.
   - Reject zero font counts, invalid cell sizes, zero rows/cols, and impossible atlas dimensions before mapping resources.
   - Keep atlas width/height in `CudaRenderParams` instead of recomputing inside the kernel.

5. Make double-width glyph layout cell-owned.
   - Pass A marks right-hand continuation cells.
   - Pass B renders only from owner cells and clears continuation cell backgrounds.
   - Temporal limits apply to owners only.

6. Add robust CUDA diagnostics.
   - Use a small `CudaCheck()` helper that records the failing call name and CUDA error string.
   - Always unmap mapped resources on failure.
   - Destroy texture/surface objects in a single cleanup block.

7. Rebuild resources through one dirty-state path.
   - Config changes that affect geometry or ramp mark atlas/resources dirty.
   - Display changes rebuild swapchain-sized textures, desktop duplication, font atlas, CUDA registrations, and cell buffers in order.

## Implementation Phases

1. Build and diagnostics
   - Keep the fixed `build.ps1`.
   - Add debug logging for CUDA/D3D failures.
   - Add a lightweight runtime status string visible in the menu.

2. Adapter and resource lifecycle
   - Refactor graphics initialization to select a CUDA-compatible DXGI adapter.
   - Make CUDA registration transactional: either all resources register, or everything registered in that attempt is cleaned up.
   - Add an explicit CUDA temporal-state reset used by `ActionResetLimits()`.

3. Renderer rewrite
   - Replace the pixel kernel's cell-state mutation with a two-pass cell-command pipeline.
   - Add a `GPUCellCommand` buffer with glyph index, RGB color, owner/continuation flags, and display width.
   - Pass atlas dimensions and validated counts in params.

4. Font atlas hardening
   - Validate GDI object creation, DIB creation, and texture creation before use.
   - Store total atlas width and height in `FontAtlas`.
   - Rebuild CUDA registrations whenever atlas texture changes.

5. Display-change and config-change correctness
   - Centralize calls that rebuild GPU resources after cell size, ramp, monitor, or texture-size changes.
   - Make menu cell size/ramp changes call that same rebuild path immediately.

6. Verification
   - Rebuild with `build.ps1`.
   - Run on single-GPU and hybrid-GPU systems if available.
   - Test startup, display change, cell size changes, ramp changes, heat mode, color/ramp limiting, zone rendering, menu toggle, and reset temporal cache.
