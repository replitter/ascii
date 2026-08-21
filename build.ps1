# PowerShell build script for ASCII CUDA Overlay

Write-Host "Configuring MSVC Environment..." -ForegroundColor Cyan

# Import MSVC environmental variables
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    Write-Error "Could not find vcvars64.bat at $vcvars. Please update the path."
    exit 1
}

# Run vcvars64.bat and capture environment variables. The batch file can emit
# both PATH and Path; keep the first case-insensitive value so the initialized
# MSVC path is not overwritten by the inherited shell path.
$seenEnvNames = @{}
cmd.exe /c "call `"$vcvars`" && set" | ForEach-Object {
    if ($_ -match '^(.*?)=(.*)$') {
        $name = $Matches[1]
        $value = $Matches[2]
        if (-not $seenEnvNames.ContainsKey($name)) {
            $seenEnvNames[$name] = $true
            Set-Item "env:$name" $value
        }
    }
}

Write-Host "Compiling CUDA kernels..." -ForegroundColor Cyan
# Compile CUDA file
nvcc -c cuda_renderer.cu -o cuda_renderer.obj -O3 -arch=sm_75
if ($LASTEXITCODE -ne 0) {
    Write-Error "CUDA Compilation failed."
    exit 1
}

Write-Host "Compiling ImGui source files..." -ForegroundColor Cyan
# Compile ImGui files
cl /c /O2 /MT /utf-8 /DUNICODE /D_UNICODE /I. /Iimgui /Iimgui/backends imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_widgets.cpp imgui/imgui_tables.cpp imgui/backends/imgui_impl_win32.cpp imgui/backends/imgui_impl_dx11.cpp
if ($LASTEXITCODE -ne 0) {
    Write-Error "ImGui Compilation failed."
    exit 1
}

Write-Host "Compiling project C++ files..." -ForegroundColor Cyan
# Compile our C++ files
cl /c /O2 /MT /utf-8 /DUNICODE /D_UNICODE /I. /Iimgui /Iimgui/backends config.cpp graphics.cpp font_atlas.cpp menu.cpp main.cpp
if ($LASTEXITCODE -ne 0) {
    Write-Error "C++ Compilation failed."
    exit 1
}

Write-Host "Linking object files..." -ForegroundColor Cyan
# Find CUDA path from env
$cudaPath = $env:CUDA_PATH
if (-not $cudaPath) {
    # Try typical path if not set
    $cudaPath = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2"
}

# Link everything
cl /O2 /MT /utf-8 /DUNICODE /D_UNICODE cuda_renderer.obj imgui.obj imgui_draw.obj imgui_widgets.obj imgui_tables.obj imgui_impl_win32.obj imgui_impl_dx11.obj config.obj graphics.obj font_atlas.obj menu.obj main.obj /Fe:ascii_cuda.exe /link /LIBPATH:"$cudaPath/lib/x64" cudart.lib d3d11.lib dxgi.lib user32.lib gdi32.lib shell32.lib
if ($LASTEXITCODE -ne 0) {
    Write-Error "Linking failed."
    exit 1
}

Write-Host "Build Succeeded! ascii_cuda.exe is ready." -ForegroundColor Green
