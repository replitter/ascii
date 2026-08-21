#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <time.h>

// Forward declare the ImGui Win32 message handler to prevent C3861/linkage errors
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include "config.h"
#include "graphics.h"
#include "font_atlas.h"
#include "cuda_renderer.h"
#include "menu.h"

#pragma comment(lib, "dwmapi.lib")

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

typedef void* NvmlDeviceHandle;
typedef struct {
    unsigned int gpu;
    unsigned int memory;
} NvmlUtilization;
typedef struct {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
} NvmlMemory;
typedef int (*NvmlInitFn)(void);
typedef int (*NvmlShutdownFn)(void);
typedef int (*NvmlGetHandleFn)(unsigned int, NvmlDeviceHandle*);
typedef int (*NvmlGetUtilFn)(NvmlDeviceHandle, NvmlUtilization*);
typedef int (*NvmlGetMemoryFn)(NvmlDeviceHandle, NvmlMemory*);
typedef unsigned int (WINAPI *TimeBeginPeriodFn)(unsigned int);
typedef unsigned int (WINAPI *TimeEndPeriodFn)(unsigned int);

struct GpuTelemetryState {
    HMODULE dll = nullptr;
    NvmlDeviceHandle device = nullptr;
    NvmlInitFn init = nullptr;
    NvmlShutdownFn shutdown = nullptr;
    NvmlGetHandleFn getHandle = nullptr;
    NvmlGetUtilFn getUtil = nullptr;
    NvmlGetMemoryFn getMemory = nullptr;
    bool available = false;
};

// Hotkey IDs
enum {
    HK_QUIT = 1, HK_ZOOM_IN, HK_ZOOM_OUT, HK_PAUSE, HK_HELP, HK_RELOAD,
    HK_HEAT_TOGGLE, HK_HEAT_CLEAN_TOGGLE, HK_TEXT_TOGGLE, HK_COLOR_TOGGLE,
    HK_INVERT, HK_COLOR_LIMIT_TOGGLE, HK_RAMP_LIMIT_TOGGLE, HK_RESET,
    HK_SETTINGS_MENU, HK_SAVE_CONFIG, HK_RELAUNCH,
    HK_BRIGHTNESS_UP, HK_BRIGHTNESS_DOWN, HK_CONTRAST_UP, HK_CONTRAST_DOWN,
    HK_LUMINANCE_UP, HK_LUMINANCE_DOWN, HK_NEXT_RAMP, HK_PREV_RAMP,
    HK_LUM_SEQ_TOGGLE, HK_TILDE_HELP, HK_SPACING_EDITOR, HK_DESKTOP_COPY_TOGGLE,
    HK_AMBIENT_TOGGLE
};

struct AppState {
    CRITICAL_SECTION lock;
    HWND hwnd = nullptr;
    HINSTANCE hInstance = nullptr;
    volatile int running = 1;
    volatile int paused = 0;
    bool showHelp = false;
    bool menuVisible = false;
    bool metricsVisible = false;
    bool rampEditorVisible = false;
    bool ctrlAltLatched = false;
    bool ctrlShiftLatched = false;
    char exePath[MAX_PATH];
    DWORD startTime = 0;
    int frameCount = 0;
    int renderedFrames = 0;
    int skippedFrames = 0;
    int failedFrames = 0;
    LARGE_INTEGER perfFreq = {};
    float frameHistory[240] = {};
    float captureHistory[240] = {};
    float cudaHistory[240] = {};
    float presentHistory[240] = {};
    float cpuHistory[240] = {};
    float gpuHistory[240] = {};
    float vramHistory[240] = {};
    float drawHistory[240] = {};
    float cpuOverheadHistory[240] = {};
    int frameHistoryCount = 0;
    int frameHistoryOffset = 0;
    float lastFrameMs = 0.0f;
    float lastCaptureMs = 0.0f;
    float lastCudaMs = 0.0f;
    float lastPresentMs = 0.0f;
    float lastDrawScreenMs = 0.0f;
    float lastCpuOverheadMs = 0.0f;
    float lastCpuUsagePercent = 0.0f;
    float lastGpuUsagePercent = -1.0f;
    float lastVramUsedMB = -1.0f;
    float lastVramTotalMB = -1.0f;
    float lastVramUsagePercent = -1.0f;
    float cudaInitMs = 0.0f;
    float d3dInitMs = 0.0f;
    float duplicationInitMs = 0.0f;
    float atlasBuildMs = 0.0f;
    float resourceRegisterMs = 0.0f;
    float lastFps = 0.0f;
    int lastCols = 0;
    int lastRows = 0;
    FILETIME lastCpuKernel = {};
    FILETIME lastCpuUser = {};
    double lastCpuSampleMs = 0.0;
    int cpuCount = 1;
    GpuTelemetryState gpuTelemetry;
    HMODULE winmmDll = nullptr;
    TimeBeginPeriodFn timeBeginPeriodFn = nullptr;
    TimeEndPeriodFn timeEndPeriodFn = nullptr;
    bool highResTimerSet = false;
    
    int lumSeqIndex = 0;
    DWORD lumSeqLastChange = 0;
    float lumSeqCurrentValue = 1.0f;
};

static AppState g_state;

static float GetEffectiveLuminanceMultiplier();
static void UpdateOverlayInteractivity();
void ActionApplyChanges();

static double NowMs() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return ((double)now.QuadPart * 1000.0) / (double)g_state.perfFreq.QuadPart;
}

static unsigned long long FileTimeToULL(FILETIME ft) {
    ULARGE_INTEGER value;
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return value.QuadPart;
}

static void InitCpuTelemetry() {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    g_state.cpuCount = (info.dwNumberOfProcessors > 0) ? (int)info.dwNumberOfProcessors : 1;
    FILETIME createTime, exitTime;
    GetProcessTimes(GetCurrentProcess(), &createTime, &exitTime, &g_state.lastCpuKernel, &g_state.lastCpuUser);
    g_state.lastCpuSampleMs = NowMs();
}

static void InitGpuTelemetry() {
    GpuTelemetryState* t = &g_state.gpuTelemetry;
    t->dll = LoadLibraryA("nvml.dll");
    if (!t->dll) t->dll = LoadLibraryA("nvidia-ml.dll");
    if (!t->dll) return;

    t->init = (NvmlInitFn)GetProcAddress(t->dll, "nvmlInit_v2");
    if (!t->init) t->init = (NvmlInitFn)GetProcAddress(t->dll, "nvmlInit");
    t->shutdown = (NvmlShutdownFn)GetProcAddress(t->dll, "nvmlShutdown");
    t->getHandle = (NvmlGetHandleFn)GetProcAddress(t->dll, "nvmlDeviceGetHandleByIndex_v2");
    if (!t->getHandle) t->getHandle = (NvmlGetHandleFn)GetProcAddress(t->dll, "nvmlDeviceGetHandleByIndex");
    t->getUtil = (NvmlGetUtilFn)GetProcAddress(t->dll, "nvmlDeviceGetUtilizationRates");
    t->getMemory = (NvmlGetMemoryFn)GetProcAddress(t->dll, "nvmlDeviceGetMemoryInfo");

    if (!t->init || !t->shutdown || !t->getHandle || !t->getUtil || !t->getMemory) return;
    if (t->init() != 0) return;
    if (t->getHandle(0, &t->device) != 0) return;
    t->available = true;
}

static void CleanupGpuTelemetry() {
    GpuTelemetryState* t = &g_state.gpuTelemetry;
    if (t->available && t->shutdown) t->shutdown();
    if (t->dll) FreeLibrary(t->dll);
    *t = GpuTelemetryState();
}

static void InitHighResolutionTimer() {
    g_state.winmmDll = LoadLibraryA("winmm.dll");
    if (!g_state.winmmDll) return;
    g_state.timeBeginPeriodFn = (TimeBeginPeriodFn)GetProcAddress(g_state.winmmDll, "timeBeginPeriod");
    g_state.timeEndPeriodFn = (TimeEndPeriodFn)GetProcAddress(g_state.winmmDll, "timeEndPeriod");
    if (g_state.timeBeginPeriodFn && g_state.timeBeginPeriodFn(1) == 0) {
        g_state.highResTimerSet = true;
    }
}

static void CleanupHighResolutionTimer() {
    if (g_state.highResTimerSet && g_state.timeEndPeriodFn) {
        g_state.timeEndPeriodFn(1);
    }
    if (g_state.winmmDll) FreeLibrary(g_state.winmmDll);
    g_state.winmmDll = nullptr;
    g_state.timeBeginPeriodFn = nullptr;
    g_state.timeEndPeriodFn = nullptr;
    g_state.highResTimerSet = false;
}

static void UpdateHardwareTelemetry() {
    double now = NowMs();
    FILETIME createTime, exitTime, kernelTime, userTime;
    if (GetProcessTimes(GetCurrentProcess(), &createTime, &exitTime, &kernelTime, &userTime)) {
        double elapsedMs = now - g_state.lastCpuSampleMs;
        unsigned long long prevCpu = FileTimeToULL(g_state.lastCpuKernel) + FileTimeToULL(g_state.lastCpuUser);
        unsigned long long currCpu = FileTimeToULL(kernelTime) + FileTimeToULL(userTime);
        if (elapsedMs > 0.0 && currCpu >= prevCpu) {
            double cpuMs = (double)(currCpu - prevCpu) / 10000.0;
            g_state.lastCpuUsagePercent = (float)((cpuMs / (elapsedMs * (double)g_state.cpuCount)) * 100.0);
            if (g_state.lastCpuUsagePercent < 0.0f) g_state.lastCpuUsagePercent = 0.0f;
            if (g_state.lastCpuUsagePercent > 100.0f) g_state.lastCpuUsagePercent = 100.0f;
        }
        g_state.lastCpuKernel = kernelTime;
        g_state.lastCpuUser = userTime;
        g_state.lastCpuSampleMs = now;
    }

    GpuTelemetryState* t = &g_state.gpuTelemetry;
    if (t->available) {
        NvmlUtilization util = {};
        NvmlMemory mem = {};
        if (t->getUtil(t->device, &util) == 0) {
            g_state.lastGpuUsagePercent = (float)util.gpu;
        }
        if (t->getMemory(t->device, &mem) == 0 && mem.total > 0) {
            g_state.lastVramUsedMB = (float)((double)mem.used / (1024.0 * 1024.0));
            g_state.lastVramTotalMB = (float)((double)mem.total / (1024.0 * 1024.0));
            g_state.lastVramUsagePercent = (float)((double)mem.used / (double)mem.total * 100.0);
        }
    }
}

static void PushFrameMetric(float frameMs) {
    int idx = g_state.frameHistoryOffset;
    g_state.frameHistory[idx] = frameMs;
    g_state.captureHistory[idx] = g_state.lastCaptureMs;
    g_state.cudaHistory[idx] = g_state.lastCudaMs;
    g_state.presentHistory[idx] = g_state.lastPresentMs;
    g_state.cpuHistory[idx] = g_state.lastCpuUsagePercent;
    g_state.gpuHistory[idx] = g_state.lastGpuUsagePercent < 0.0f ? 0.0f : g_state.lastGpuUsagePercent;
    g_state.vramHistory[idx] = g_state.lastVramUsagePercent < 0.0f ? 0.0f : g_state.lastVramUsagePercent;
    g_state.drawHistory[idx] = g_state.lastDrawScreenMs;
    g_state.cpuOverheadHistory[idx] = g_state.lastCpuOverheadMs;
    g_state.frameHistoryOffset = (g_state.frameHistoryOffset + 1) % 240;
    if (g_state.frameHistoryCount < 240) g_state.frameHistoryCount++;
    g_state.lastFrameMs = frameMs;
    g_state.lastFps = frameMs > 0.0f ? 1000.0f / frameMs : 0.0f;
}

static void SortSmall(float* values, int count) {
    for (int i = 1; i < count; ++i) {
        float v = values[i];
        int j = i - 1;
        while (j >= 0 && values[j] > v) {
            values[j + 1] = values[j];
            --j;
        }
        values[j + 1] = v;
    }
}

static void ComputeHistoryStats(float* avg, float* minVal, float* maxVal, float* p95, float* jitter, int* overBudget) {
    int count = g_state.frameHistoryCount;
    float target = g_config.frameBudgetMs > 0.0f ? g_config.frameBudgetMs : ((g_config.targetFPS > 0) ? (1000.0f / (float)g_config.targetFPS) : 33.333f);
    if (count <= 0) {
        *avg = *minVal = *maxVal = *p95 = *jitter = 0.0f;
        *overBudget = 0;
        return;
    }

    float samples[240];
    float sum = 0.0f;
    *minVal = 1000000.0f;
    *maxVal = 0.0f;
    *overBudget = 0;
    for (int i = 0; i < count; ++i) {
        float v = g_state.frameHistory[i];
        samples[i] = v;
        sum += v;
        if (v < *minVal) *minVal = v;
        if (v > *maxVal) *maxVal = v;
        if (v > target) (*overBudget)++;
    }
    *avg = sum / (float)count;

    float variance = 0.0f;
    for (int i = 0; i < count; ++i) {
        float d = g_state.frameHistory[i] - *avg;
        variance += d * d;
    }
    *jitter = sqrtf(variance / (float)count);

    SortSmall(samples, count);
    int p95Index = (int)((float)(count - 1) * 0.95f + 0.5f);
    if (p95Index < 0) p95Index = 0;
    if (p95Index >= count) p95Index = count - 1;
    *p95 = samples[p95Index];
}

static OverlayMetrics BuildOverlayMetrics() {
    OverlayMetrics m = {};
    m.frameMs = g_state.lastFrameMs;
    m.captureMs = g_state.lastCaptureMs;
    m.cudaMs = g_state.lastCudaMs;
    m.presentMs = g_state.lastPresentMs;
    m.fps = g_state.lastFps;
    m.targetFrameMs = g_config.frameBudgetMs > 0.0f ? g_config.frameBudgetMs : ((g_config.targetFPS > 0) ? (1000.0f / (float)g_config.targetFPS) : 33.333f);
    ComputeHistoryStats(&m.avgFrameMs, &m.minFrameMs, &m.maxFrameMs, &m.p95FrameMs, &m.jitterMs, &m.overBudgetFrames);
    m.frameBudgetUse = (m.targetFrameMs > 0.0f) ? (m.avgFrameMs / m.targetFrameMs) : 0.0f;
    float pipeline = m.captureMs + m.cudaMs + m.presentMs;
    if (pipeline > 0.0f) {
        m.captureShare = m.captureMs / pipeline;
        m.cudaShare = m.cudaMs / pipeline;
        m.presentShare = m.presentMs / pipeline;
    }
    m.cpuUsagePercent = g_state.lastCpuUsagePercent;
    m.gpuUsagePercent = g_state.lastGpuUsagePercent;
    m.vramUsedMB = g_state.lastVramUsedMB;
    m.vramTotalMB = g_state.lastVramTotalMB;
    m.vramUsagePercent = g_state.lastVramUsagePercent;
    m.effectiveLuminance = GetEffectiveLuminanceMultiplier();
    if (g_config.luminanceSeqEnabled && g_config.luminanceChangeTime > 0) {
        DWORD now = GetTickCount();
        DWORD elapsed = now - g_state.lumSeqLastChange;
        m.sequenceElapsedMs = (int)elapsed;
        m.sequenceRemainingMs = g_config.luminanceChangeTime - m.sequenceElapsedMs;
        if (m.sequenceRemainingMs < 0) m.sequenceRemainingMs = 0;
        m.sequenceProgress = (float)m.sequenceElapsedMs / (float)g_config.luminanceChangeTime;
        if (m.sequenceProgress > 1.0f) m.sequenceProgress = 1.0f;
    }
    m.frameSkipPhase = (g_config.frameSkip > 0) ? (g_state.frameCount % (g_config.frameSkip + 1)) : 0;
    m.renderedFrames = g_state.renderedFrames;
    m.skippedFrames = g_state.skippedFrames;
    m.failedFrames = g_state.failedFrames;
    {
        int total = g_state.renderedFrames + g_state.skippedFrames + g_state.failedFrames;
        if (total > 0) {
            m.skipRate = (float)g_state.skippedFrames / (float)total;
            m.failureRate = (float)g_state.failedFrames / (float)total;
        }
    }
    m.lumSeqIndex = g_state.lumSeqIndex;
    m.lumSeqCount = g_config.luminanceSeqCount;
    m.screenW = g_graphics.screenW;
    m.screenH = g_graphics.screenH;
    m.cols = g_state.lastCols;
    m.rows = g_state.lastRows;
    m.cellCount = g_state.lastCols * g_state.lastRows;
    m.activeRampGlyphs = (g_config.currentRamp >= 0 && g_config.currentRamp < g_config.rampCount) ? g_config.ramps[g_config.currentRamp].count : 0;
    m.backgroundOpacity = g_config.motionMode ? 0 : g_config.opacity;
    m.currentTheme = g_config.colorTheme;
    m.currentRamp = g_config.currentRamp;
    m.asciiPaused = g_state.paused ? 1 : 0;
    m.motionMode = g_config.motionMode;
    m.motionSensitivity = g_config.motionSensitivity;
    m.motionDecayMs = g_config.motionDecayMs;
    m.motionRampUpdateDurationMs = g_config.motionRampUpdateDurationMs;
    m.motionMaxRampUpdates = g_config.motionMaxRampUpdates;
    m.motionMaxConcurrentPercent = g_config.motionMaxConcurrentPercent;
    m.motionHoldUntilNewDraw = g_config.motionHoldUntilNewDraw;
    m.motionAutoKillStaticRamps = g_config.motionAutoKillStaticRamps;
    strncpy(m.rendererMode, GetCudaRenderModeName(), sizeof(m.rendererMode) - 1);
    m.rendererMode[sizeof(m.rendererMode) - 1] = '\0';
    m.drawScreenMs = g_state.lastDrawScreenMs;
    m.cpuOverheadMs = g_state.lastCpuOverheadMs;
    m.cudaInitMs = g_state.cudaInitMs;
    m.d3dInitMs = g_state.d3dInitMs;
    m.duplicationInitMs = g_state.duplicationInitMs;
    m.atlasBuildMs = g_state.atlasBuildMs;
    m.resourceRegisterMs = g_state.resourceRegisterMs;
    m.historyCount = g_state.frameHistoryCount;
    m.historyOffset = g_state.frameHistoryOffset;
    for (int i = 0; i < 240; ++i) {
        m.frameHistory[i] = g_state.frameHistory[i];
        m.captureHistory[i] = g_state.captureHistory[i];
        m.cudaHistory[i] = g_state.cudaHistory[i];
        m.presentHistory[i] = g_state.presentHistory[i];
        m.cpuHistory[i] = g_state.cpuHistory[i];
        m.gpuHistory[i] = g_state.gpuHistory[i];
        m.vramHistory[i] = g_state.vramHistory[i];
        m.drawHistory[i] = g_state.drawHistory[i];
        m.cpuOverheadHistory[i] = g_state.cpuOverheadHistory[i];
    }
    return m;
}

static void HandleDisplayChange() {
    // Teardown in dependency order: CUDA interop (needs the D3D device) first,
    // then ImGui, the font atlas, D3D11 resources/device, and CUDA itself.
    UnregisterD3D11Resources();
    CleanupMenu();
    CleanupFontAtlas();
    CleanupD3D11();
    CleanupCuda();

    // Re-detect the new desktop geometry and rebuild everything.
    GetRealScreenDimensions(&g_graphics.screenX, &g_graphics.screenY,
                          &g_graphics.screenW, &g_graphics.screenH);
    if (!InitD3D11(g_state.hwnd)) return;
    SetWindowPos(g_state.hwnd, HWND_TOPMOST,
                g_graphics.screenX, g_graphics.screenY,
                g_graphics.screenW, g_graphics.screenH,
                SWP_NOACTIVATE);
    if (!InitCuda()) return;
    if (!InitDesktopDuplication()) return;
    BuildFontAtlas(g_config.cellSize);
    if (!RegisterD3D11Resources()) return;
    InitMenu(g_state.hwnd, g_graphics.device, g_graphics.context);
    ResetCudaTemporalState();
}

static void DrawUiAndPresent() {
    if (!g_graphics.context || !g_graphics.swapChain) return;
    g_graphics.context->OMSetRenderTargets(1, &g_graphics.renderTargetView, nullptr);
    OverlayMetrics metrics = BuildOverlayMetrics();
    DrawOverlayUI(g_state.menuVisible, g_state.showHelp, g_state.metricsVisible, g_state.rampEditorVisible, metrics);
    UpdateOverlayInteractivity();
    double presentStart = NowMs();
    g_graphics.swapChain->Present(0, 0);
    g_state.lastPresentMs = (float)(NowMs() - presentStart);
}

static void UpdateOverlayInteractivity() {
    if (!g_state.hwnd) return;
    LONG_PTR exStyle = GetWindowLongPtrW(g_state.hwnd, GWL_EXSTYLE);
    bool wantsInput = g_state.menuVisible || g_state.showHelp || g_state.metricsVisible || g_state.rampEditorVisible;
    if (wantsInput) {
        exStyle &= ~WS_EX_TRANSPARENT;
    } else {
        exStyle |= WS_EX_TRANSPARENT;
    }
    SetWindowLongPtrW(g_state.hwnd, GWL_EXSTYLE, exStyle);
}

static bool AnyOtherKeyDown() {
    // True when any non-modifier key is held; used to keep modifier chords
    // from double-firing alongside registered Ctrl+Alt+/Ctrl+Shift+ hotkeys
    for (int vk = 0x08; vk <= 0xFE; ++vk) {
        if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
            vk == VK_LSHIFT || vk == VK_RSHIFT ||
            vk == VK_LCONTROL || vk == VK_RCONTROL ||
            vk == VK_LMENU || vk == VK_RMENU) {
            continue;
        }
        if (GetAsyncKeyState(vk) & 0x8000) return true;
    }
    return false;
}

static void PollModifierChords() {
    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    bool ctrlAlt = ctrl && alt && !shift && !AnyOtherKeyDown();
    if (ctrlAlt && !g_state.ctrlAltLatched) {
        g_state.menuVisible = !g_state.menuVisible;
        UpdateOverlayInteractivity();
        g_state.ctrlAltLatched = true;
    } else if (!ctrlAlt) {
        g_state.ctrlAltLatched = false;
    }

    bool ctrlShift = ctrl && shift && !alt && !AnyOtherKeyDown();
    if (ctrlShift && !g_state.ctrlShiftLatched) {
        g_state.metricsVisible = !g_state.metricsVisible;
        UpdateOverlayInteractivity();
        g_state.ctrlShiftLatched = true;
    } else if (!ctrlShift) {
        g_state.ctrlShiftLatched = false;
    }
}

// Implementation of actions declared in config/menu
void ActionSaveConfig() { SaveConfigToFile(); }

int ActionGetAsciiPaused() {
    return g_state.paused ? 1 : 0;
}

void ActionSetAsciiPaused(int paused) {
    g_state.paused = paused ? 1 : 0;
}

void ActionLoadConfig() {
    LoadConfig();
    BuildFontAtlas(g_config.cellSize);
    RegisterD3D11Resources();
    ResetCudaTemporalState();
}

int ActionImportConfig(const char* path) {
    if (!LoadConfigFromFile(path)) return 0;
    BuildFontAtlas(g_config.cellSize);
    RegisterD3D11Resources();
    ResetCudaTemporalState();
    ActionApplyChanges();
    return 1;
}

void ActionRebuildRendererResources() {
    BuildFontAtlas(g_config.cellSize);
    RegisterD3D11Resources();
    ResetCudaTemporalState();
}

void ActionRelaunch() {
    if (g_state.exePath[0]) {
        STARTUPINFOA si = {};
        PROCESS_INFORMATION pi = {};
        si.cb = sizeof(si);
        if (CreateProcessA(g_state.exePath, nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            PostQuitMessage(0);
        }
    }
}

void ActionResetAll() {
    SetDefaultConfig();
    g_config.ramps[0].count = ParseUTF8ToGraphemes(DEFAULT_RAMP_STR, g_config.ramps[0].clusters, MAX_RAMP_CHARS);
    strcpy(g_config.ramps[0].name, "Default");
    g_config.rampCount = 1;
    AppendBuiltInRamps();
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
    BuildFontAtlas(g_config.cellSize);
    RegisterD3D11Resources();
    ResetCudaTemporalState();
    g_state.paused = 0;
    g_state.lumSeqIndex = 0;
    g_state.lumSeqLastChange = GetTickCount();
    g_state.lumSeqCurrentValue = 1.0f;
}

void ActionResetLimits() {
    ResetCudaTemporalState();
}

void ActionApplyChanges() {
    if (g_state.hwnd) {
        SetLayeredWindowAttributes(g_state.hwnd, 0, 255, LWA_ALPHA);
        KillTimer(g_state.hwnd, 1);
        int timerMs = (g_config.targetFPS > 0) ? (1000 + g_config.targetFPS / 2) / g_config.targetFPS : 33;
        if (timerMs < 1) timerMs = 1;
        SetTimer(g_state.hwnd, 1, timerMs, nullptr);
    }
}

static void UpdateLuminanceSequence() {
    if (!g_config.luminanceSeqEnabled || g_config.luminanceSeqCount <= 0) {
        g_state.lumSeqCurrentValue = 1.0f;
        return;
    }
    DWORD now = GetTickCount();
    DWORD elapsed = now - g_state.lumSeqLastChange;
    if (elapsed >= (DWORD)g_config.luminanceChangeTime) {
        g_state.lumSeqIndex = (g_state.lumSeqIndex + 1) % g_config.luminanceSeqCount;
        g_state.lumSeqLastChange = now;
        LuminanceSeqItem* item = &g_config.luminanceSeq[g_state.lumSeqIndex];
        if (item->isRandom) {
            g_state.lumSeqCurrentValue = GetRandomWithFavour(
                item->minVal, item->maxVal, item->favour);
        } else {
            g_state.lumSeqCurrentValue = item->minVal;
        }
    }
}

static float GetEffectiveLuminanceMultiplier() {
    if (g_config.luminanceSeqEnabled && g_config.luminanceSeqCount > 0) {
        return g_state.lumSeqCurrentValue * g_config.finalLuminanceMultiplier;
    }
    return g_config.finalLuminanceMultiplier;
}

static void RenderFrame() {
    if (!g_state.hwnd) return;
    PollModifierChords();
    double frameStart = NowMs();

    if (!g_state.paused) {
        UpdateLuminanceSequence();
    }
    
    if (!g_state.paused && g_config.frameSkip > 0) {
        g_state.frameCount++;
        if (g_state.frameCount % (g_config.frameSkip + 1) != 0) {
            g_state.skippedFrames++;
            g_state.lastCaptureMs = 0.0f;
            g_state.lastCudaMs = 0.0f;
            g_state.lastDrawScreenMs = 0.0f;
            DrawUiAndPresent();
            float frameMs = (float)(NowMs() - frameStart);
            g_state.lastCpuOverheadMs = frameMs - g_state.lastPresentMs;
            if (g_state.lastCpuOverheadMs < 0.0f) g_state.lastCpuOverheadMs = 0.0f;
            UpdateHardwareTelemetry();
            PushFrameMetric(frameMs);
            return;
        }
    }

    double captureStart = NowMs();
    if (!CaptureScreenFrame()) {
        g_state.failedFrames++;
        return;
    }
    g_state.lastCaptureMs = (float)(NowMs() - captureStart);

    // Render configuration
    CudaRenderParams params = {};
    params.screenW = g_graphics.screenW;
    params.screenH = g_graphics.screenH;
    int atlasCellH = (int)(g_config.cellSize * FONT_ASPECT_RATIO);
    if (atlasCellH < 8) atlasCellH = 8;
    params.cellW = g_config.cellSize + g_config.glyphSpacingX;
    params.cellH = atlasCellH + g_config.glyphSpacingY;
    if (params.cellW < 1) params.cellW = 1;
    if (params.cellH < 8) params.cellH = 8;
    params.cols = (params.screenW + params.cellW - 1) / params.cellW;
    params.rows = (params.screenH + params.cellH - 1) / params.cellH;
    if (params.cols <= 0) params.cols = 1;
    if (params.rows <= 0) params.rows = 1;
    g_state.lastCols = params.cols;
    g_state.lastRows = params.rows;

    params.brightness = g_config.brightness;
    params.contrast = g_config.contrast;
    params.gamma = g_config.gamma;
    params.grayscale = g_config.grayscale;
    params.invert = g_config.invert;
    params.finalLuminanceMultiplier = GetEffectiveLuminanceMultiplier();

    params.colorTheme = g_config.colorTheme;
    params.time = (float)(GetTickCount() - g_state.startTime) / 1000.0f;

    params.enableColorLimit = g_config.enableColorLimit;
    params.colorLimit = g_config.colorLimit;
    params.colorRefresh = g_config.colorRefresh;
    params.colorThreshold = g_config.colorThreshold;
    params.enableRampLimit = g_config.enableRampLimit;
    params.rampLimit = g_config.rampLimit;
    params.rampRefresh = g_config.rampRefresh;
    params.currentTimeMs = GetTickCount();

    params.heatMode = g_config.heatMode;
    params.heatStyle = g_config.heatStyle;
    params.heatGlyphMode = g_config.heatGlyphMode;
    params.heatRadius = g_config.heatRadius;
    params.heatBrightness = g_config.heatBrightness;
    params.asciiPaused = g_state.paused ? 1 : 0;
    params.motionMode = g_config.motionMode;
    params.motionSensitivity = g_config.motionSensitivity;
    params.motionDecayMs = g_config.motionDecayMs;
    params.motionRampUpdateDurationMs = g_config.motionRampUpdateDurationMs;
    params.motionMaxRampUpdates = g_config.motionMaxRampUpdates;
    params.motionMaxConcurrentPercent = g_config.motionMaxConcurrentPercent;
    params.motionHoldUntilNewDraw = g_config.motionHoldUntilNewDraw;
    params.motionAutoKillStaticRamps = g_config.motionAutoKillStaticRamps;

    params.zoneEnable = g_config.zoneEnable;
    params.zoneX = g_config.zoneX;
    params.zoneY = g_config.zoneY;
    params.zoneW = g_config.zoneW;
    params.zoneH = g_config.zoneH;

    // Random effect quotas in basis points; double math keeps the
    // 8-decimal percents exact through the *100 conversion
    auto percentQuota = [](float pct) {
        if (pct >= 100.0f) return 10000;
        int q = (int)((double)pct * 100.0 + 0.5);
        if (q < 0) q = 0;
        if (q > 10000) q = 10000;
        return q;
    };
    params.randomAsciiQuota = percentQuota(g_config.asciiRandomPercent);
    params.invertQuota = percentQuota(g_config.invertRandomPercent);
    params.grayscaleQuota = percentQuota(g_config.grayscaleRandomPercent);
    params.themeQuota = percentQuota(g_config.themeRandomPercent);
    params.randomExclusive = g_config.randomEffectsExclusive ? 1 : 0;
    params.invertBandStart = 0;
    params.grayscaleBandStart = 0;
    params.themeBandStart = 0;
    if (params.randomExclusive) {
        // Claim order: ASCII cells first (filter randomness may not steal
        // them), then theme, invert, grayscale
        int cursor = (params.randomAsciiQuota > 0 && params.randomAsciiQuota < 10000)
                         ? params.randomAsciiQuota : 0;
        params.themeBandStart = cursor;
        if (params.themeQuota > 0 && params.themeQuota < 10000) cursor += params.themeQuota;
        if (cursor > 10000) cursor = 10000;
        params.invertBandStart = cursor;
        if (params.invertQuota > 0 && params.invertQuota < 10000) cursor += params.invertQuota;
        if (cursor > 10000) cursor = 10000;
        params.grayscaleBandStart = cursor;
    }
    params.backgroundOpacity = g_config.motionMode ? 0 : g_config.opacity;
    params.desktopCopyMode = g_config.desktopCopyMode;
    params.ambientMode = g_config.ambientMode;
    params.ambientFromSource = g_config.ambientFromSource;
    params.ambientFromRamp = g_config.ambientFromRamp;
    params.ambientLitPercent = g_config.ambientLitPercent;
    params.ambientGlowStrength = g_config.ambientGlowStrength;
    params.ambientSubdivisions = g_config.ambientSubdivisions;
    params.ambientRadius = g_config.ambientRadius;
    params.ambientProgressiveBleed = g_config.ambientProgressiveBleed;
    params.ambientColorMatch = g_config.ambientColorMatch;

    params.fontCount = g_fontAtlas.count;
    params.variableFontMode = g_config.variableFontMode;
    params.strictNoFontOverlap = g_config.strictNoFontOverlap;
    params.variableFontMinScale = g_config.variableFontMinScale;
    params.variableFontMaxScale = g_config.variableFontMaxScale;
    params.variableFontRandomness = g_config.variableFontRandomness;
    params.variableFontRegionSize = g_config.variableFontRegionSize;
    params.variableFontSeed = g_config.variableFontSeed;
    params.variableFontPulseSpeed = g_config.variableFontPulseSpeed;
    params.variableFontIndependentAxes = g_config.variableFontIndependentAxes;
    params.variableFontMinWidthScale = g_config.variableFontMinWidthScale;
    params.variableFontMaxWidthScale = g_config.variableFontMaxWidthScale;
    params.variableFontMinHeightScale = g_config.variableFontMinHeightScale;
    params.variableFontMaxHeightScale = g_config.variableFontMaxHeightScale;
    params.variableCellMode = g_config.variableCellMode;
    params.strictNoCellOverlap = g_config.strictNoCellOverlap;
    params.variableCellMinScale = g_config.variableCellMinScale;
    params.variableCellMaxScale = g_config.variableCellMaxScale;
    params.variableCellRandomness = g_config.variableCellRandomness;
    params.variableCellRegionSize = g_config.variableCellRegionSize;
    params.variableCellSeed = g_config.variableCellSeed;
    params.variableCellPulseSpeed = g_config.variableCellPulseSpeed;
    params.variableCellAffectsSampling = g_config.variableCellAffectsSampling;
    params.variableCellAffectsFont = g_config.variableCellAffectsFont;
    params.experimentalCellSampling = g_config.experimentalCellSampling;
    params.cellSampleMode = g_config.cellSampleMode;
    params.cellSampleColorMode = g_config.cellSampleColorMode;
    params.cellSampleGrid = g_config.cellSampleGrid;
    params.cellSampleRadiusScale = g_config.cellSampleRadiusScale;
    params.cellSampleEdgeBoost = g_config.cellSampleEdgeBoost;
    params.cellSampleJitter = g_config.cellSampleJitter;
    params.cellSampleCenterWeight = g_config.cellSampleCenterWeight;
    params.cellSampleDetailMix = g_config.cellSampleDetailMix;
    params.cellSampleLuminanceCompensation = g_config.cellSampleLuminanceCompensation;
    params.cellSampleHighlightPreserve = g_config.cellSampleHighlightPreserve;

    // Unbind D3D11 Pipeline Resources
    ID3D11ShaderResourceView* nullSRVs[8] = { nullptr };
    g_graphics.context->PSSetShaderResources(0, 8, nullSRVs);
    g_graphics.context->VSSetShaderResources(0, 8, nullSRVs);

    ID3D11RenderTargetView* nullRTVs[8] = { nullptr };
    g_graphics.context->OMSetRenderTargets(8, nullRTVs, nullptr);

    g_graphics.context->Flush();

    // Execute CUDA rendering
    double cudaStart = NowMs();
    if (!RunCudaRender(params)) {
        g_state.failedFrames++;
        return;
    }
    g_state.lastCudaMs = (float)(NowMs() - cudaStart);

    // Blit the render target to swapchain backbuffer
    double drawStart = NowMs();
    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = g_graphics.swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    if (SUCCEEDED(hr)) {
        g_graphics.context->CopyResource(backBuffer, g_graphics.outputTexture);
        backBuffer->Release();
    }
    g_state.lastDrawScreenMs = (float)(NowMs() - drawStart);

    g_state.renderedFrames++;
    DrawUiAndPresent();
    float frameMs = (float)(NowMs() - frameStart);
    g_state.lastCpuOverheadMs = frameMs - g_state.lastCaptureMs - g_state.lastCudaMs - g_state.lastDrawScreenMs - g_state.lastPresentMs;
    if (g_state.lastCpuOverheadMs < 0.0f) g_state.lastCpuOverheadMs = 0.0f;
    UpdateHardwareTelemetry();
    PushFrameMetric(frameMs);
}

static void RegisterHotkeys(HWND hwnd) {
    RegisterHotKey(hwnd, HK_QUIT, MOD_CONTROL, 'D');
    RegisterHotKey(hwnd, HK_PAUSE, MOD_CONTROL, 'P');
    RegisterHotKey(hwnd, HK_HELP, MOD_CONTROL, 'H');
    RegisterHotKey(hwnd, HK_RELOAD, MOD_CONTROL, 'R');
    RegisterHotKey(hwnd, HK_SAVE_CONFIG, MOD_CONTROL, 'S');
    RegisterHotKey(hwnd, HK_DESKTOP_COPY_TOGGLE, MOD_CONTROL, 'O');
    RegisterHotKey(hwnd, HK_AMBIENT_TOGGLE, MOD_CONTROL, 'A');
    RegisterHotKey(hwnd, HK_SPACING_EDITOR, MOD_CONTROL, VK_OEM_3);
    RegisterHotKey(hwnd, HK_SETTINGS_MENU, MOD_CONTROL | MOD_ALT, 'S');
    RegisterHotKey(hwnd, HK_ZOOM_IN, MOD_CONTROL, VK_RIGHT);
    RegisterHotKey(hwnd, HK_ZOOM_OUT, MOD_CONTROL, VK_LEFT);
    RegisterHotKey(hwnd, HK_HEAT_TOGGLE, MOD_CONTROL, 'M');
    RegisterHotKey(hwnd, HK_HEAT_CLEAN_TOGGLE, MOD_CONTROL, 'B');
    RegisterHotKey(hwnd, HK_INVERT, MOD_CONTROL, '8');
    RegisterHotKey(hwnd, HK_COLOR_LIMIT_TOGGLE, MOD_CONTROL, '9');
    RegisterHotKey(hwnd, HK_RAMP_LIMIT_TOGGLE, MOD_CONTROL, '0');
    RegisterHotKey(hwnd, HK_RESET, MOD_CONTROL | MOD_SHIFT, 'R');
    RegisterHotKey(hwnd, HK_RELAUNCH, MOD_CONTROL | MOD_SHIFT, 'L');
    RegisterHotKey(hwnd, HK_BRIGHTNESS_UP, MOD_CONTROL | MOD_SHIFT, VK_UP);
    RegisterHotKey(hwnd, HK_BRIGHTNESS_DOWN, MOD_CONTROL | MOD_SHIFT, VK_DOWN);
    RegisterHotKey(hwnd, HK_CONTRAST_UP, MOD_CONTROL | MOD_ALT, VK_UP);
    RegisterHotKey(hwnd, HK_CONTRAST_DOWN, MOD_CONTROL | MOD_ALT, VK_DOWN);
    RegisterHotKey(hwnd, HK_LUMINANCE_UP, MOD_CONTROL, VK_UP);
    RegisterHotKey(hwnd, HK_LUMINANCE_DOWN, MOD_CONTROL, VK_DOWN);
    RegisterHotKey(hwnd, HK_NEXT_RAMP, MOD_CONTROL, VK_OEM_6);
    RegisterHotKey(hwnd, HK_PREV_RAMP, MOD_CONTROL, VK_OEM_4);
    RegisterHotKey(hwnd, HK_LUM_SEQ_TOGGLE, MOD_CONTROL | MOD_ALT, 'L');
    RegisterHotKey(hwnd, HK_TILDE_HELP, 0, VK_OEM_3);
}

static void UnregisterHotkeys(HWND hwnd) {
    for (int i = HK_QUIT; i <= HK_AMBIENT_TOGGLE; i++) UnregisterHotKey(hwnd, i);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    bool uiVisible = g_state.menuVisible || g_state.showHelp || g_state.metricsVisible || g_state.rampEditorVisible;
    if (msg == WM_NCHITTEST && uiVisible) {
        int screenX = (int)(short)LOWORD(lParam);
        int screenY = (int)(short)HIWORD(lParam);
        return IsOverlayUiPointInteractive(screenX, screenY) ? HTCLIENT : HTTRANSPARENT;
    }

    if (uiVisible) {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
            return true;
    }

    switch (msg) {
        case WM_CREATE:
            g_state.startTime = GetTickCount();
            {
                int timerMs = (g_config.targetFPS > 0) ? (1000 + g_config.targetFPS / 2) / g_config.targetFPS : 33;
                if (timerMs < 1) timerMs = 1;
                SetTimer(hwnd, 1, timerMs, nullptr);
            }
            RegisterHotkeys(hwnd);
            return 0;
        case WM_TIMER:
            if (wParam == 1) RenderFrame();
            return 0;
        case WM_HOTKEY:
            switch (wParam) {
                case HK_QUIT: PostQuitMessage(0); break;
                case HK_PAUSE: g_state.paused = !g_state.paused; break;
                case HK_HELP:
                    g_state.showHelp = !g_state.showHelp;
                    UpdateOverlayInteractivity();
                    break;
                case HK_TILDE_HELP:
                    g_state.showHelp = !g_state.showHelp;
                    UpdateOverlayInteractivity();
                    break;
                case HK_RELOAD: ActionLoadConfig(); break;
                case HK_SAVE_CONFIG: ActionSaveConfig(); break;
                case HK_DESKTOP_COPY_TOGGLE:
                    g_config.desktopCopyMode = !g_config.desktopCopyMode;
                    ResetCudaTemporalState();
                    break;
                case HK_AMBIENT_TOGGLE:
                    g_config.ambientMode = !g_config.ambientMode;
                    break;
                case HK_SPACING_EDITOR:
                    SetRampEditorPage(1);
                    g_state.rampEditorVisible = true;
                    UpdateOverlayInteractivity();
                    break;
                case HK_SETTINGS_MENU: g_state.menuVisible = !g_state.menuVisible; UpdateOverlayInteractivity(); break;
                case HK_ZOOM_IN:
                    if (g_config.motionMode) {
                        g_config.motionSensitivity = (g_config.motionSensitivity < 100) ? g_config.motionSensitivity + 5 : 100;
                    } else {
                        g_config.cellSize = (g_config.cellSize < MAX_CELL_SIZE) ? g_config.cellSize + 1 : MAX_CELL_SIZE;
                        BuildFontAtlas(g_config.cellSize);
                        RegisterD3D11Resources();
                        ResetCudaTemporalState();
                    }
                    break;
                case HK_ZOOM_OUT:
                    if (g_config.motionMode) {
                        g_config.motionSensitivity = (g_config.motionSensitivity > 0) ? g_config.motionSensitivity - 5 : 0;
                    } else {
                        g_config.cellSize = (g_config.cellSize > MIN_CELL_SIZE) ? g_config.cellSize - 1 : MIN_CELL_SIZE;
                        BuildFontAtlas(g_config.cellSize);
                        RegisterD3D11Resources();
                        ResetCudaTemporalState();
                    }
                    break;
                case HK_HEAT_TOGGLE:
                    g_config.heatMode = !g_config.heatMode;
                    g_config.heatGlyphMode = HEAT_GLYPH_BLACK;
                    break;
                case HK_HEAT_CLEAN_TOGGLE:
                    g_config.heatMode = !g_config.heatMode;
                    g_config.heatGlyphMode = HEAT_GLYPH_HIDDEN;
                    break;
                case HK_INVERT: g_config.invert = !g_config.invert; break;
                case HK_COLOR_LIMIT_TOGGLE: g_config.enableColorLimit = !g_config.enableColorLimit; break;
                case HK_RAMP_LIMIT_TOGGLE: g_config.enableRampLimit = !g_config.enableRampLimit; break;
                case HK_RESET: ActionResetAll(); break;
                case HK_RELAUNCH: ActionRelaunch(); break;
                case HK_BRIGHTNESS_UP: g_config.brightness += 0.1f; break;
                case HK_BRIGHTNESS_DOWN: g_config.brightness -= 0.1f; break;
                case HK_CONTRAST_UP: g_config.contrast += 0.1f; break;
                case HK_CONTRAST_DOWN: g_config.contrast -= 0.1f; break;
                case HK_LUMINANCE_UP: g_config.finalLuminanceMultiplier += 0.05f; break;
                case HK_LUMINANCE_DOWN: g_config.finalLuminanceMultiplier -= 0.05f; break;
                case HK_NEXT_RAMP: g_config.currentRamp = (g_config.currentRamp + 1) % g_config.rampCount; BuildFontAtlas(g_config.cellSize); RegisterD3D11Resources(); ResetCudaTemporalState(); break;
                case HK_PREV_RAMP: g_config.currentRamp = (g_config.currentRamp - 1 + g_config.rampCount) % g_config.rampCount; BuildFontAtlas(g_config.cellSize); RegisterD3D11Resources(); ResetCudaTemporalState(); break;
                case HK_LUM_SEQ_TOGGLE:
                    g_config.luminanceSeqEnabled = !g_config.luminanceSeqEnabled;
                    g_state.lumSeqIndex = 0; g_state.lumSeqLastChange = GetTickCount();
                    g_state.lumSeqCurrentValue = 1.0f;
                    break;
            }
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            UnregisterHotkeys(hwnd);
            PostQuitMessage(0);
            return 0;
        case WM_DISPLAYCHANGE:
            HandleDisplayChange();
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static HWND CreateOverlayWindow(HINSTANCE hInstance) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"UTF8OverlayClass";
    
    if (!RegisterClassExW(&wc)) return nullptr;

    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
        L"UTF8OverlayClass",
        L"UTF-8 Overlay (CUDA Accelerated)",
        WS_POPUP,
        g_graphics.screenX,
        g_graphics.screenY,
        g_graphics.screenW,
        g_graphics.screenH,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!hwnd) return nullptr;

    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (user32) {
        typedef BOOL (WINAPI *SetWindowDisplayAffinityFn)(HWND, DWORD);
        SetWindowDisplayAffinityFn fn = (SetWindowDisplayAffinityFn)GetProcAddress(user32, "SetWindowDisplayAffinity");
        if (fn) fn(hwnd, WDA_EXCLUDEFROMCAPTURE);
    }

    return hwnd;
}

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
typedef HANDLE DPI_AWARENESS_CONTEXT;
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

#ifndef PROCESS_DPI_AWARENESS
typedef enum { 
    PROCESS_DPI_UNAWARE = 0, 
    PROCESS_SYSTEM_DPI_AWARE = 1, 
    PROCESS_PER_MONITOR_DPI_AWARE = 2 
} PROCESS_DPI_AWARENESS;
#endif

static void EnableDPIAwareness(void) {
    HMODULE user32 = GetModuleHandleA("user32.dll");
    HMODULE shcore = LoadLibraryA("shcore.dll");
    if (user32) {
        typedef BOOL (WINAPI *Fn)(DPI_AWARENESS_CONTEXT);
        Fn fn = (Fn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (fn && fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            if (shcore) FreeLibrary(shcore);
            return;
        }
    }
    if (shcore) {
        typedef HRESULT (WINAPI *Fn)(PROCESS_DPI_AWARENESS);
        Fn fn = (Fn)GetProcAddress(shcore, "SetProcessDpiAwareness");
        if (fn) fn(PROCESS_PER_MONITOR_DPI_AWARE);
        FreeLibrary(shcore);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    EnableDPIAwareness();

    srand((unsigned int)time(nullptr));
    InitializeCriticalSection(&g_state.lock);
    QueryPerformanceFrequency(&g_state.perfFreq);
    InitHighResolutionTimer();
    InitCpuTelemetry();
    InitGpuTelemetry();

    GetModuleFileNameA(nullptr, g_state.exePath, MAX_PATH);

    SetDefaultConfig();
    LoadConfig();

    GetRealScreenDimensions(&g_graphics.screenX, &g_graphics.screenY, &g_graphics.screenW, &g_graphics.screenH);

    g_state.hInstance = hInstance;
    g_state.hwnd = CreateOverlayWindow(hInstance);
    if (!g_state.hwnd) {
        MessageBoxA(nullptr, "Failed to create overlay window!", "Error", MB_ICONERROR);
        DeleteCriticalSection(&g_state.lock);
        return 1;
    }

    double initStart = NowMs();
    if (!InitD3D11(g_state.hwnd)) {
        MessageBoxA(nullptr, "Failed to initialize D3D11 graphics!", "Error", MB_ICONERROR);
        DestroyWindow(g_state.hwnd);
        DeleteCriticalSection(&g_state.lock);
        return 1;
    }
    SetWindowPos(g_state.hwnd, HWND_TOPMOST,
                 g_graphics.screenX, g_graphics.screenY,
                 g_graphics.screenW, g_graphics.screenH,
                 SWP_NOACTIVATE);
    g_state.d3dInitMs = (float)(NowMs() - initStart);

    initStart = NowMs();
    if (!InitCuda()) {
        MessageBoxA(nullptr, "No CUDA compatible GPU found! Please install Nvidia CUDA drivers.", "Error", MB_ICONERROR);
        CleanupD3D11();
        DestroyWindow(g_state.hwnd);
        DeleteCriticalSection(&g_state.lock);
        return 1;
    }
    g_state.cudaInitMs = (float)(NowMs() - initStart);

    initStart = NowMs();
    if (!InitDesktopDuplication()) {
        MessageBoxA(nullptr, "Failed to initialize Desktop Duplication! Ensure you have an active display output.", "Error", MB_ICONERROR);
        CleanupD3D11();
        DestroyWindow(g_state.hwnd);
        DeleteCriticalSection(&g_state.lock);
        return 1;
    }
    g_state.duplicationInitMs = (float)(NowMs() - initStart);

    initStart = NowMs();
    if (!BuildFontAtlas(g_config.cellSize)) {
        MessageBoxA(nullptr, "Failed to build GPU font atlas texture!", "Error", MB_ICONERROR);
        CleanupD3D11();
        DestroyWindow(g_state.hwnd);
        DeleteCriticalSection(&g_state.lock);
        return 1;
    }
    g_state.atlasBuildMs = (float)(NowMs() - initStart);

    initStart = NowMs();
    if (!RegisterD3D11Resources()) {
        MessageBoxA(nullptr, "Failed to bind CUDA-D3D11 Interop resources!", "Error", MB_ICONERROR);
        CleanupFontAtlas();
        CleanupD3D11();
        DestroyWindow(g_state.hwnd);
        DeleteCriticalSection(&g_state.lock);
        return 1;
    }
    g_state.resourceRegisterMs = (float)(NowMs() - initStart);

    if (!InitMenu(g_state.hwnd, g_graphics.device, g_graphics.context)) {
        MessageBoxA(nullptr, "Failed to initialize Dear ImGui!", "Error", MB_ICONERROR);
        CleanupFontAtlas();
        CleanupD3D11();
        DestroyWindow(g_state.hwnd);
        DeleteCriticalSection(&g_state.lock);
        return 1;
    }

    ShowWindow(g_state.hwnd, SW_SHOW);
    UpdateWindow(g_state.hwnd);

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CleanupMenu();
    CleanupGpuTelemetry();
    CleanupHighResolutionTimer();
    CleanupCuda();
    CleanupFontAtlas();
    CleanupD3D11();
    DeleteCriticalSection(&g_state.lock);

    return (int)msg.wParam;
}
