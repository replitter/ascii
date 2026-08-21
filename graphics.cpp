#include "graphics.h"
#include <stdio.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

GraphicsContext g_graphics;

static bool RectContainsPoint(const RECT& r, LONG x, LONG y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

static int RectArea(const RECT& r) {
    int w = (int)(r.right - r.left);
    int h = (int)(r.bottom - r.top);
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    return w * h;
}

static bool FindVisibleDesktopOutput(IDXGIAdapter1** outAdapter, UINT* outOutputIndex,
                                     RECT* outDesktopRect) {
    if (outAdapter) *outAdapter = nullptr;
    if (outOutputIndex) *outOutputIndex = 0;
    if (outDesktopRect) *outDesktopRect = {};

    IDXGIFactory1* factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory);
    if (FAILED(hr) || !factory) return false;

    IDXGIAdapter1* bestAdapter = nullptr;
    UINT bestOutputIndex = 0;
    RECT bestRect = {};
    int bestScore = -1;

    for (UINT adapterIndex = 0;; ++adapterIndex) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        if (!adapter) continue;

        DXGI_ADAPTER_DESC1 adapterDesc = {};
        adapter->GetDesc1(&adapterDesc);
        if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapter->Release();
            continue;
        }

        for (UINT outputIndex = 0;; ++outputIndex) {
            IDXGIOutput* output = nullptr;
            if (adapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND) break;
            if (!output) continue;

            DXGI_OUTPUT_DESC outputDesc = {};
            hr = output->GetDesc(&outputDesc);
            output->Release();
            if (FAILED(hr) || !outputDesc.AttachedToDesktop) continue;

            RECT r = outputDesc.DesktopCoordinates;
            int area = RectArea(r);
            int score = area;
            if (RectContainsPoint(r, 0, 0)) score += 1000000000;

            if (score > bestScore) {
                if (bestAdapter) bestAdapter->Release();
                bestAdapter = adapter;
                bestAdapter->AddRef();
                bestOutputIndex = outputIndex;
                bestRect = r;
                bestScore = score;
            }
        }

        adapter->Release();
    }

    factory->Release();
    if (!bestAdapter || bestScore < 0) {
        if (bestAdapter) bestAdapter->Release();
        return false;
    }

    if (outAdapter) {
        *outAdapter = bestAdapter;
    } else {
        bestAdapter->Release();
    }
    if (outOutputIndex) *outOutputIndex = bestOutputIndex;
    if (outDesktopRect) *outDesktopRect = bestRect;
    return true;
}

void GetRealScreenDimensions(int* x, int* y, int* w, int* h) {
    RECT desktopRect = {};
    if (FindVisibleDesktopOutput(nullptr, nullptr, &desktopRect)) {
        *x = desktopRect.left;
        *y = desktopRect.top;
        *w = (int)(desktopRect.right - desktopRect.left);
        *h = (int)(desktopRect.bottom - desktopRect.top);
        if (*w > 0 && *h > 0) return;
    }

    *x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    *y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    *w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    *h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (*w <= 0 || *h <= 0) { 
        *x = 0; *y = 0;
        *w = GetSystemMetrics(SM_CXSCREEN); 
        *h = GetSystemMetrics(SM_CYSCREEN); 
    }
    if (*w <= 0) *w = 1920; 
    if (*h <= 0) *h = 1080;
}

bool InitD3D11(HWND hwnd) {
    IDXGIAdapter1* desktopAdapter = nullptr;
    UINT desktopOutputIndex = 0;
    RECT desktopRect = {};
    if (FindVisibleDesktopOutput(&desktopAdapter, &desktopOutputIndex, &desktopRect)) {
        g_graphics.screenX = desktopRect.left;
        g_graphics.screenY = desktopRect.top;
        g_graphics.screenW = (int)(desktopRect.right - desktopRect.left);
        g_graphics.screenH = (int)(desktopRect.bottom - desktopRect.top);
        g_graphics.outputIndex = desktopOutputIndex;
    } else {
        GetRealScreenDimensions(&g_graphics.screenX, &g_graphics.screenY, &g_graphics.screenW, &g_graphics.screenH);
        g_graphics.outputIndex = 0;
    }

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.BufferDesc.Width = g_graphics.screenW;
    scd.BufferDesc.Height = g_graphics.screenH;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        desktopAdapter,
        desktopAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        featureLevels,
        1,
        D3D11_SDK_VERSION,
        &scd,
        &g_graphics.swapChain,
        &g_graphics.device,
        &featureLevel,
        &g_graphics.context
    );
    if (desktopAdapter) desktopAdapter->Release();

    if (FAILED(hr)) {
        return false;
    }

    ID3D11Texture2D* backBuffer = nullptr;
    hr = g_graphics.swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    if (FAILED(hr)) return false;

    hr = g_graphics.device->CreateRenderTargetView(backBuffer, nullptr, &g_graphics.renderTargetView);
    backBuffer->Release();
    if (FAILED(hr)) return false;

    // Create texture to hold the screen capture
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = g_graphics.screenW;
    desc.Height = g_graphics.screenH;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.CPUAccessFlags = 0;

    hr = g_graphics.device->CreateTexture2D(&desc, nullptr, &g_graphics.capturedTexture);
    if (FAILED(hr)) return false;

    hr = g_graphics.device->CreateTexture2D(&desc, nullptr, &g_graphics.outputTexture);
    if (FAILED(hr)) return false;

    return true;
}

void CleanupD3D11() {
    CleanupDesktopDuplication();
    if (g_graphics.outputTexture) { g_graphics.outputTexture->Release(); g_graphics.outputTexture = nullptr; }
    if (g_graphics.capturedTexture) { g_graphics.capturedTexture->Release(); g_graphics.capturedTexture = nullptr; }
    if (g_graphics.renderTargetView) { g_graphics.renderTargetView->Release(); g_graphics.renderTargetView = nullptr; }
    if (g_graphics.swapChain) { g_graphics.swapChain->Release(); g_graphics.swapChain = nullptr; }
    if (g_graphics.context) { g_graphics.context->Release(); g_graphics.context = nullptr; }
    if (g_graphics.device) { g_graphics.device->Release(); g_graphics.device = nullptr; }
}

bool InitDesktopDuplication() {
    CleanupDesktopDuplication();

    if (!g_graphics.device) return false;
    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter1* adapter = nullptr;
    HRESULT hr = g_graphics.device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    if (FAILED(hr)) return false;
    hr = dxgiDevice->GetAdapter((IDXGIAdapter**)&adapter);
    dxgiDevice->Release();
    if (FAILED(hr) || !adapter) return false;

    IDXGIOutput* output = nullptr;
    hr = adapter->EnumOutputs(g_graphics.outputIndex, &output);
    if (FAILED(hr)) {
        hr = adapter->EnumOutputs(0, &output);
    }
    adapter->Release();
    if (FAILED(hr)) return false;

    IDXGIOutput1* output1 = nullptr;
    hr = output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);
    output->Release();
    if (FAILED(hr)) return false;

    hr = output1->DuplicateOutput(g_graphics.device, &g_graphics.deskDupl);
    output1->Release();
    if (FAILED(hr)) {
        return false;
    }

    return true;
}

void CleanupDesktopDuplication() {
    if (g_graphics.deskDupl) {
        g_graphics.deskDupl->Release();
        g_graphics.deskDupl = nullptr;
    }
}

bool CaptureScreenFrame() {
    if (!g_graphics.deskDupl) {
        if (!InitDesktopDuplication()) return false;
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    IDXGIResource* desktopResource = nullptr;
    
    HRESULT hr = g_graphics.deskDupl->AcquireNextFrame(0, &frameInfo, &desktopResource);
    if (hr == DXGI_ERROR_ACCESS_LOST) {
        CleanupDesktopDuplication();
        return false;
    }
    if (FAILED(hr)) {
        // Timeout or other error; return true to keep going, using previous frame content
        return true;
    }

    ID3D11Texture2D* tex = nullptr;
    hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
    desktopResource->Release();

    if (SUCCEEDED(hr)) {
        g_graphics.context->CopyResource(g_graphics.capturedTexture, tex);
        tex->Release();
    }

    g_graphics.deskDupl->ReleaseFrame();
    return SUCCEEDED(hr);
}
