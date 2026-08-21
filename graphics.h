#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>

struct GraphicsContext {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* renderTargetView = nullptr;
    
    // Desktop Duplication
    IDXGIOutputDuplication* deskDupl = nullptr;
    ID3D11Texture2D* capturedTexture = nullptr; // GPU texture of screen capture
    ID3D11Texture2D* outputTexture = nullptr;   // GPU texture written by CUDA, blitted to swapchain
    UINT outputIndex = 0;                       // DXGI output used for desktop duplication
    
    int screenX = 0;
    int screenY = 0;
    int screenW = 0;
    int screenH = 0;
};

extern GraphicsContext g_graphics;

bool InitD3D11(HWND hwnd);
void CleanupD3D11();
bool InitDesktopDuplication();
void CleanupDesktopDuplication();
bool CaptureScreenFrame();
void GetRealScreenDimensions(int* x, int* y, int* w, int* h);

#endif
