#ifndef MENU_H
#define MENU_H

#include <d3d11.h>
#include <windows.h>

// Forward declaration of WndProc handler for ImGui
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool InitMenu(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);
void CleanupMenu();
void SetRampEditorPage(int page);

struct OverlayMetrics {
    float frameMs;
    float captureMs;
    float cudaMs;
    float presentMs;
    float fps;
    float avgFrameMs;
    float minFrameMs;
    float maxFrameMs;
    float p95FrameMs;
    float jitterMs;
    float targetFrameMs;
    float frameBudgetUse;
    float captureShare;
    float cudaShare;
    float presentShare;
    float cpuUsagePercent;
    float gpuUsagePercent;
    float vramUsedMB;
    float vramTotalMB;
    float vramUsagePercent;
    float effectiveLuminance;
    float sequenceProgress;
    int sequenceElapsedMs;
    int sequenceRemainingMs;
    int frameSkipPhase;
    int renderedFrames;
    int skippedFrames;
    int failedFrames;
    int overBudgetFrames;
    float skipRate;
    float failureRate;
    int lumSeqIndex;
    int lumSeqCount;
    int screenW;
    int screenH;
    int cols;
    int rows;
    int cellCount;
    int activeRampGlyphs;
    int backgroundOpacity;
    int currentTheme;
    int currentRamp;
    int asciiPaused;
    int motionMode;
    int motionSensitivity;
    float motionDecayMs;
    int motionRampUpdateDurationMs;
    int motionMaxRampUpdates;
    float motionMaxConcurrentPercent;
    int motionHoldUntilNewDraw;
    int motionAutoKillStaticRamps;
    char rendererMode[32];
    float frameHistory[240];
    float captureHistory[240];
    float cudaHistory[240];
    float presentHistory[240];
    float cpuHistory[240];
    float gpuHistory[240];
    float vramHistory[240];
    float drawHistory[240];
    float cpuOverheadHistory[240];
    int historyCount;
    int historyOffset;
    float drawScreenMs;
    float cpuOverheadMs;
    float cudaInitMs;
    float d3dInitMs;
    float duplicationInitMs;
    float atlasBuildMs;
    float resourceRegisterMs;
};

void DrawOverlayUI(bool& showSettings, bool& showHelp, bool& showMetrics, bool& showRampEditor, const OverlayMetrics& metrics);
bool IsOverlayUiPointInteractive(int screenX, int screenY);

#endif
