#include "MouseHighlighter.h"
#include <shellapi.h>
#include <dwmapi.h>
#include <psapi.h>
#include <shlobj.h>
#include <cmath>
#include <algorithm>
#include <fstream>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")

// 全局实例指针 (用于静态回调)
MouseHighlighter* MouseHighlighter::s_pInstance = nullptr;

// 自定义消息 ID (托盘图标)
#define WM_TRAY_ICON (WM_APP + 1)

// 托盘图标 ID
#define ID_TRAY_ICON 1001
#define IDR_ICON 100
#define IDM_EXIT 2001
#define IDM_COLOR_BLUE 2002
#define IDM_COLOR_YELLOW 2003
#define IDM_EXIT_APP 2004
#define IDM_TOGGLE_FILL 2005
#define IDM_HALO_SIZE_SMALL 2010
#define IDM_HALO_SIZE_MEDIUM 2011
#define IDM_HALO_SIZE_LARGE 2012
#define IDM_HALO_SIZE_XL 2013
#define IDM_HALO_SIZE_XXL 2014
#define IDM_RIPPLE_COLOR_GREEN 2020
#define IDM_RIPPLE_COLOR_BLUE 2021
#define IDM_RIPPLE_COLOR_PINK 2022
#define IDM_RIPPLE_SIZE_SMALL 2030
#define IDM_RIPPLE_SIZE_MEDIUM 2031
#define IDM_RIPPLE_SIZE_LARGE 2032
#define IDM_RIPPLE_TOGGLE 2033
#define IDM_RIPPLE_SIZE_XL 2034
#define IDM_RIPPLE_SIZE_XXL 2035
#define IDM_HALO_ALPHA_LOW 2040
#define IDM_HALO_ALPHA_MEDIUM 2041
#define IDM_HALO_ALPHA_HIGH 2042
#define IDM_RIPPLE_ALPHA_LOW 2050
#define IDM_RIPPLE_ALPHA_MEDIUM 2051
#define IDM_RIPPLE_ALPHA_HIGH 2052
#define IDM_AUTO_STARTUP 2060
#define IDM_HALO_QUALITY_NORMAL 2070
#define IDM_HALO_QUALITY_HIGH 2071
#define IDM_HALO_QUALITY_ULTRA 2072
#define IDM_RIPPLE_SPEED_SLOW 2080
#define IDM_RIPPLE_SPEED_MEDIUM 2081
#define IDM_RIPPLE_SPEED_FAST 2082

static uint32_t SetColorAlpha(uint32_t colorARGB, uint8_t alpha) {
    return (static_cast<uint32_t>(alpha) << 24) | (colorARGB & 0x00FFFFFF);
}

static bool ApplyAutoStartupSetting(bool enable) {
    HKEY hKey = nullptr;
    const wchar_t* runKeyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* valueName = L"MouseHighlighter";
    LONG openRes = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        runKeyPath,
        0,
        nullptr,
        0,
        KEY_SET_VALUE,
        nullptr,
        &hKey,
        nullptr
    );
    if (openRes != ERROR_SUCCESS || !hKey) {
        return false;
    }

    bool ok = true;
    if (enable) {
        wchar_t exePath[MAX_PATH] = {};
        DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) {
            ok = false;
        } else {
            wchar_t quotedPath[MAX_PATH + 2] = {};
            quotedPath[0] = L'"';
            wcscpy_s(quotedPath + 1, MAX_PATH + 1, exePath);
            wcscat_s(quotedPath, MAX_PATH + 2, L"\"");

            LONG setRes = RegSetValueExW(
                hKey,
                valueName,
                0,
                REG_SZ,
                reinterpret_cast<const BYTE*>(quotedPath),
                static_cast<DWORD>((wcslen(quotedPath) + 1) * sizeof(wchar_t))
            );
            ok = (setRes == ERROR_SUCCESS);
        }
    } else {
        LONG delRes = RegDeleteValueW(hKey, valueName);
        ok = (delRes == ERROR_SUCCESS || delRes == ERROR_FILE_NOT_FOUND);
    }

    RegCloseKey(hKey);
    return ok;
}

static void TryTrimWorkingSetIfIdle(bool idle, DWORD& lastTrimTick) {
    if (!idle) {
        return;
    }

    DWORD now = GetTickCount();
    // 至多每 8 秒尝试一次，避免频繁扰动
    if (now - lastTrimTick < 8000) {
        return;
    }

    EmptyWorkingSet(GetCurrentProcess());
    lastTrimTick = now;
}

// ============================================================
// 初始化与清理
// ============================================================

MouseHighlighter::~MouseHighlighter() {
    Cleanup();
}

bool MouseHighlighter::Initialize() {
    // 打开日志文件用于诊断
    std::wofstream logFile(L"D:\\MouseHighlighter_init.log");
    logFile << L"=== Initialize Started ===" << std::endl;

    // 0. 设置 DPI 感知（全屏游戏时防止坐标偏移）
    // 注意：main.cpp 已调用 EnableBestEffortDpiAwareness()，此处保留作为防御性编程
    // 但不再重复调用，避免潜在冲突
    hInstance = GetModuleHandle(nullptr);
    if (!hInstance) {
        logFile << L"FAIL: GetModuleHandle returned nullptr" << std::endl;
        logFile.close();
        return false;
    }
    logFile << L"OK: Got module handle" << std::endl;
    
    // 2. 为静态回调设置全局实例指针
    s_pInstance = this;
    logFile << L"OK: Set global instance pointer" << std::endl;
    
    // 3. 获取高精度计时器频率
    if (!QueryPerformanceFrequency(&qpcFrequency)) {
        logFile << L"FAIL: QueryPerformanceFrequency failed" << std::endl;
        logFile.close();
        return false;
    }
    logFile << L"OK: Got QPC frequency" << std::endl;
    
    // 4. 加载配置文件
    if (!LoadConfigFile()) {
        config = GetDefaultConfig();
    }
    if (config.system.autoStartup) {
        ApplyAutoStartupSetting(true);
    }
    logFile << L"OK: Config loaded or using defaults" << std::endl;
    
    // 5. 创建透明窗口
    if (!CreateLayeredWindow()) {
        logFile << L"FAIL: CreateLayeredWindow failed" << std::endl;
        logFile.close();
        MessageBoxW(nullptr, L"失败: 创建透明窗口失败。\n请确保 DWM 已启用并有足够的系统资源。", 
                    L"诊断", MB_OK | MB_ICONERROR);
        return false;
    }
    logFile << L"OK: Layered window created" << std::endl;
    
    // 6. 初始化 DIB 缓冲
    if (!InitializeDIBBuffer()) {
        logFile << L"FAIL: InitializeDIBBuffer failed" << std::endl;
        logFile.close();
        MessageBoxW(nullptr, L"失败: DIB 位图缓冲初始化失败。\n请检查显卡驱动是否支持 ARGB。", 
                    L"诊断", MB_OK | MB_ICONERROR);
        DestroyLayeredWindow();
        return false;
    }
    logFile << L"OK: DIB buffer initialized" << std::endl;
    
    // 7. 创建退出事件
    hExitEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!hExitEvent) {
        logFile << L"FAIL: CreateEvent hExitEvent failed" << std::endl;
        logFile.close();
        MessageBoxW(nullptr, L"失败: 创建退出事件失败。", 
                    L"诊断", MB_OK | MB_ICONERROR);
        DestroyLayeredWindow();
        return false;
    }
    logFile << L"OK: Exit event created" << std::endl;

    // 8. 初始化鼠标状态（MVP 模式：轮询鼠标，不使用全局钩子）
    POINT pt{};
    if (GetCursorPos(&pt)) {
        sharedState.cursorX.store(pt.x, std::memory_order_release);
        sharedState.cursorY.store(pt.y, std::memory_order_release);
        smoothedX = static_cast<float>(pt.x);
        smoothedY = static_cast<float>(pt.y);
    }
    logFile << L"OK: Polling input mode initialized" << std::endl;
    
    // 9. 设置托盘图标
    if (!SetupTrayIcon()) {
        logFile << L"WARN: SetupTrayIcon failed (non-fatal)" << std::endl;
        // 托盘失败不是致命错误
    } else {
        logFile << L"OK: Tray icon setup" << std::endl;
    }
    
    // 10. 检查 DWM 合成是否启用
    BOOL isDwmEnabled = FALSE;
    HRESULT hr = DwmIsCompositionEnabled(&isDwmEnabled);
    if (FAILED(hr) || !isDwmEnabled) {
        logFile << L"WARN: DWM composition not enabled (hr=0x" << std::hex << hr << std::dec << ", enabled=" << isDwmEnabled << ")" << std::endl;
        // 警告但继续运行 (DWM 禁用时分层窗口不会显示，但钩子仍可工作)
    } else {
        logFile << L"OK: DWM composition enabled" << std::endl;
    }
    
    logFile << L"=== Initialize SUCCESS ===" << std::endl;
    logFile.close();
    return true;
}

void MouseHighlighter::Cleanup() {
    // 设置退出信号
    if (hExitEvent) {
        SetEvent(hExitEvent);
    }
    
    // 卸载钩子 (MVP 模式下通常未注册，保持幂等)
    UnregisterMouseHook();
    
    // 关闭托盘
    RemoveTrayIcon();
    
    // 等待线程结束 (最多 5 秒)
    WaitForAllThreads();
    
    // 销毁窗口
    DestroyLayeredWindow();
    
    // 关闭事件句柄
    if (hExitEvent) {
        CloseHandle(hExitEvent);
        hExitEvent = nullptr;
    }
    
    // 清空全局实例指针
    if (s_pInstance == this) {
        s_pInstance = nullptr;
    }
}

// ============================================================
// 窗口管理
// ============================================================

bool MouseHighlighter::CreateLayeredWindow() {
    std::wofstream logFile(L"D:\\MouseHighlighter_init.log", std::ios::app);
    logFile << L"[CreateLayeredWindow][build=2026-04-15-fix1] Starting" << std::endl;

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wcex{};
        wcex.cbSize = sizeof(WNDCLASSEXW);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = WindowProcStatic;
        wcex.cbClsExtra = 0;
        wcex.cbWndExtra = 0;
        wcex.hInstance = hInstance;
        wcex.hIcon = nullptr;
        wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wcex.hbrBackground = nullptr;
        wcex.lpszMenuName = nullptr;
        wcex.lpszClassName = WINDOW_CLASS_NAME;
        wcex.hIconSm = nullptr;

        if (!RegisterClassExW(&wcex)) {
            DWORD dwErr = GetLastError();
            if (dwErr != ERROR_CLASS_ALREADY_EXISTS) {
                logFile << L"[CreateLayeredWindow] RegisterClassExW failed: " << dwErr << std::endl;
                logFile.close();
                return false;
            }
        }
        classRegistered = true;
        logFile << L"[CreateLayeredWindow] Window class ready" << std::endl;
    }

    int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int fullWidth = std::max(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
    int fullHeight = std::max(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));

    logFile << L"[CreateLayeredWindow] Metrics X=" << screenX
            << L" Y=" << screenY
            << L" W=" << fullWidth
            << L" H=" << fullHeight << std::endl;

    hLayeredWindow = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        WINDOW_CLASS_NAME,
        APP_NAME,
        WS_POPUP,
        screenX,
        screenY,
        fullWidth,
        fullHeight,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!hLayeredWindow) {
        DWORD dwErr = GetLastError();
        logFile << L"[CreateLayeredWindow] CreateWindowExW failed: " << dwErr << std::endl;
        logFile.close();
        return false;
    }

    SetWindowPos(
        hLayeredWindow,
        HWND_TOPMOST,
        screenX,
        screenY,
        fullWidth,
        fullHeight,
        SWP_SHOWWINDOW | SWP_NOACTIVATE
    );

    dirtyRectTracker.screenWidth = fullWidth;
    dirtyRectTracker.screenHeight = fullHeight;
    logFile << L"[CreateLayeredWindow] Success" << std::endl;
    logFile.close();

    return true;
}

void MouseHighlighter::DestroyLayeredWindow() {
    if (hLayeredWindow) {
        DestroyWindow(hLayeredWindow);
        hLayeredWindow = nullptr;
    }
}

bool MouseHighlighter::InitializeDIBBuffer() {
    int fullWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int fullHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    
    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) {
        return false;
    }
    
    bool result = dibBuffer.Create(fullWidth, fullHeight, hdcScreen);
    ReleaseDC(nullptr, hdcScreen);
    
    return result;
}

// ============================================================
// MVP 模式：使用轮询输入而非全局钩子/后台线程
// ============================================================

bool MouseHighlighter::RegisterMouseHook() {
    // MVP 模式下未使用全局钩子；此方法返回 true (无操作)
    return true;
}

bool MouseHighlighter::UnregisterMouseHook() {
    // MVP 模式下无钩子需要卸载；返回 true (无操作)
    return true;
}

LRESULT CALLBACK MouseHighlighter::MouseHookProc(
    int nCode, WPARAM wParam, LPARAM lParam) {
    // MVP 模式下未使用全局钩子；此方法已被轮询 GetCursorPos/GetAsyncKeyState 取代
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ============================================================
// 线程管理 (MVP 模式下未使用后台线程)
// ============================================================

bool MouseHighlighter::StartRenderThread() {
    // MVP 模式下无后台渲染线程；此方法返回 true (无操作)
    return true;
}

bool MouseHighlighter::StartMonitorThread() {
    // MVP 模式下无监控线程；此方法返回 true (无操作)
    return true;
}

void MouseHighlighter::WaitForAllThreads() {
    // MVP 模式下无后台线程；无需等待 (无操作、幂等)
}

DWORD WINAPI MouseHighlighter::RenderTickThreadProc(LPVOID lpParam) {
    // MVP 模式下未使用；此方法已被 Run() 中的轮询循环取代
    return 0;
}

DWORD WINAPI MouseHighlighter::MonitorThreadProc(LPVOID lpParam) {
    // MVP 模式下未使用；此方法已被 Run() 中的轮询循环取代
    return 0;
}

// ============================================================
// 配置加载与保存
// ============================================================

bool MouseHighlighter::LoadConfigFile() {
    // 构造配置文件路径 (%APPDATA%\MouseHighlighter\config.ini)
    wchar_t appDataPath[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appDataPath))) {
        return false;
    }
    
    wcscat_s(appDataPath, MAX_PATH, L"\\MouseHighlighter");
    
    // 创建目录 (如果不存在)
    CreateDirectoryW(appDataPath, nullptr);
    
    wcscat_s(appDataPath, MAX_PATH, L"\\config.ini");
    wcscpy_s(configFilePath, MAX_PATH, appDataPath);
    
    // 尝试从文件读取配置
    return config.LoadFromINI(configFilePath);
}

bool MouseHighlighter::SaveConfigFile() {
    return config.SaveToINI(configFilePath);
}

// ============================================================
// 渲染逻辑
// ============================================================

void MouseHighlighter::UpdateSmoothedCursor() {
    int32_t rawX = sharedState.cursorX.load(std::memory_order_acquire);
    int32_t rawY = sharedState.cursorY.load(std::memory_order_acquire);
    
    if (config.smoothing.alpha == 0.0f) {
        // 不平滑，直接赋值
        smoothedX = (float)rawX;
        smoothedY = (float)rawY;
    } else {
        // EMA 平滑
        float alpha = config.smoothing.alpha;
        smoothedX = alpha * rawX + (1.0f - alpha) * smoothedX;
        smoothedY = alpha * rawY + (1.0f - alpha) * smoothedY;
    }
}

void MouseHighlighter::RenderFrame() {
    LARGE_INTEGER nowQPC;
    QueryPerformanceCounter(&nowQPC);
    
    // 开始新一帧：交换脏区
    dirtyRectTracker.BeginFrame();
    
    // 更新平滑坐标
    UpdateSmoothedCursor();
    
    // 累积脏区：光晕
    dirtyRectTracker.UnionCircle(
        (int)smoothedX, (int)smoothedY,
        config.halo.radius);
    
    if (config.ripple.enabled) {
        // 处理点击事件并激活波纹
        ClickEvent clickEvent;
        while (sharedState.TryDequeueClick(clickEvent)) {
            ripplePool.AddRipple(clickEvent.x, clickEvent.y, nowQPC.QuadPart);
        }
        
        // 清理死亡波纹
        ripplePool.CompactDeadRipples(
            nowQPC.QuadPart,
            qpcFrequency.QuadPart,
            config.ripple.durationMS
        );
        
        // 累积脏区：波纹
        for (uint8_t i = 0; i < ripplePool.activeCount; ++i) {
            auto& ripple = ripplePool.ripples[i];
            float radius = ripple.GetCurrentRadius(
                nowQPC.QuadPart,
                qpcFrequency.QuadPart,
                config.ripple.maxRadius,
                config.ripple.durationMS,
                config.ripple.radiusExponent
            );
            if (radius >= 0.0f) {
                dirtyRectTracker.UnionCircle(ripple.centerX, ripple.centerY, radius + config.ripple.thickness + 1.0f);
            }
        }
    } else {
        // 关闭波纹时清空队列，避免恢复时瞬间堆积
        ClickEvent clickEvent;
        while (sharedState.TryDequeueClick(clickEvent)) {}
        ripplePool.activeCount = 0;
    }
    
    // 获取本帧需要更新的矩形
    RECT updateRect = dirtyRectTracker.GetUpdateRect();
    if (IsRectEmpty(&updateRect)) {
        // 无脏区，跳过渲染
        return;
    }
    
    // 清除脏区背景
    dibBuffer.ClearRect(updateRect);
    
    // 绘制光晕圆圈
    DrawHalo((int)smoothedX, (int)smoothedY);
    
    // 绘制波纹
    if (config.ripple.enabled) {
        DrawRipples();
    }
    
    // 输出到分层窗口
    UpdateLayeredWindowOutput(updateRect);
}

void MouseHighlighter::DrawHalo(int centerX, int centerY) {
    if (!dibBuffer.pBits) return;
    
    int radius = (int)config.halo.radius;
    if (radius <= 1) return;

    auto blendPixel = [this](int x, int y, uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
        uint32_t* pPixel = dibBuffer.pBits + y * dibBuffer.width + x;
        uint32_t dst = *pPixel;

        uint8_t dstA = (dst >> 24) & 0xFF;
        uint8_t dstR = (dst >> 16) & 0xFF;
        uint8_t dstG = (dst >> 8) & 0xFF;
        uint8_t dstB = dst & 0xFF;

        float alpha = a / 255.0f;
        uint8_t outA = (uint8_t)(std::min(255.0f, dstA + a * (1.0f - dstA / 255.0f)));
        uint8_t outR = (uint8_t)(r * alpha + dstR * (1.0f - alpha));
        uint8_t outG = (uint8_t)(g * alpha + dstG * (1.0f - alpha));
        uint8_t outB = (uint8_t)(b * alpha + dstB * (1.0f - alpha));

        *pPixel = (outA << 24) | (outR << 16) | (outG << 8) | outB;
    };
    
    uint32_t color = config.halo.colorARGB;
    uint8_t a = (color >> 24) & 0xFF;
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    // 可选超采样：1x(普通)/2x2(高清)/3x3(超清)
    static constexpr float OFFSETS_1[1][2] = {
        {0.0f, 0.0f}
    };
    static constexpr float OFFSETS_4[4][2] = {
        {-0.25f, -0.25f},
        { 0.25f, -0.25f},
        {-0.25f,  0.25f},
        { 0.25f,  0.25f}
    };
    static constexpr float OFFSETS_9[9][2] = {
        {-0.33f, -0.33f}, {0.00f, -0.33f}, {0.33f, -0.33f},
        {-0.33f,  0.00f}, {0.00f,  0.00f}, {0.33f,  0.00f},
        {-0.33f,  0.33f}, {0.00f,  0.33f}, {0.33f,  0.33f}
    };

    const float (*samples)[2] = OFFSETS_4;
    int sampleCount = 4;
    if (config.halo.qualityLevel <= 1) {
        samples = OFFSETS_1;
        sampleCount = 1;
    } else if (config.halo.qualityLevel >= 3) {
        samples = OFFSETS_9;
        sampleCount = 9;
    }

    if (config.halo.filled) {
        int left = std::max(0, centerX - radius - 1);
        int top = std::max(0, centerY - radius - 1);
        int right = std::min((int)dibBuffer.width, centerX + radius + 2);
        int bottom = std::min((int)dibBuffer.height, centerY + radius + 2);

        float radiusSq = static_cast<float>(radius * radius);
        for (int y = top; y < bottom; ++y) {
            for (int x = left; x < right; ++x) {
                int hit = 0;
                for (int i = 0; i < sampleCount; ++i) {
                    float sx = static_cast<float>(x - centerX) + samples[i][0];
                    float sy = static_cast<float>(y - centerY) + samples[i][1];
                    if (sx * sx + sy * sy <= radiusSq) {
                        ++hit;
                    }
                }
                if (hit == 0) continue;

                uint8_t covA = static_cast<uint8_t>((static_cast<int>(a) * hit) / sampleCount);
                blendPixel(x, y, covA, r, g, b);
            }
        }
    } else {
        float halfT = std::max(0.5f, config.halo.thickness * 0.5f);
        float inner = std::max(0.0f, static_cast<float>(radius) - halfT);
        float outer = static_cast<float>(radius) + halfT;
        float innerSq = inner * inner;
        float outerSq = outer * outer;

        int left = std::max(0, static_cast<int>(centerX - outer - 2.0f));
        int top = std::max(0, static_cast<int>(centerY - outer - 2.0f));
        int right = std::min(static_cast<int>(dibBuffer.width), static_cast<int>(centerX + outer + 3.0f));
        int bottom = std::min(static_cast<int>(dibBuffer.height), static_cast<int>(centerY + outer + 3.0f));

        for (int y = top; y < bottom; ++y) {
            for (int x = left; x < right; ++x) {
                int hit = 0;
                for (int i = 0; i < sampleCount; ++i) {
                    float sx = static_cast<float>(x - centerX) + samples[i][0];
                    float sy = static_cast<float>(y - centerY) + samples[i][1];
                    float d2 = sx * sx + sy * sy;
                    if (d2 >= innerSq && d2 <= outerSq) {
                        ++hit;
                    }
                }
                if (hit == 0) continue;

                uint8_t covA = static_cast<uint8_t>((static_cast<int>(a) * hit) / sampleCount);
                blendPixel(x, y, covA, r, g, b);
            }
        }
    }
}

void MouseHighlighter::DrawRipples() {
    if (!dibBuffer.pBits) return;
    
    LARGE_INTEGER nowQPC;
    QueryPerformanceCounter(&nowQPC);
    
    for (uint8_t i = 0; i < ripplePool.activeCount; ++i) {
        auto& ripple = ripplePool.ripples[i];
        
        float radius = ripple.GetCurrentRadius(
            nowQPC.QuadPart,
            qpcFrequency.QuadPart,
            config.ripple.maxRadius,
            config.ripple.durationMS,
            config.ripple.radiusExponent
        );
        if (radius < 0.0f) continue;
        
        uint8_t animAlpha = ripple.GetCurrentAlpha(
            nowQPC.QuadPart,
            qpcFrequency.QuadPart,
            config.ripple.durationMS,
            config.ripple.alphaExponent
        );
        uint8_t baseAlpha = static_cast<uint8_t>((config.ripple.colorARGB >> 24) & 0xFF);
        uint8_t a = static_cast<uint8_t>((animAlpha * baseAlpha) / 255);
        if (a == 0) continue;

        uint8_t rr = (config.ripple.colorARGB >> 16) & 0xFF;
        uint8_t gg = (config.ripple.colorARGB >> 8) & 0xFF;
        uint8_t bb = config.ripple.colorARGB & 0xFF;

        float halfT = std::max(0.5f, config.ripple.thickness * 0.5f);
        float inner = std::max(0.0f, radius - halfT);
        float outer = radius + halfT;
        float innerSq = inner * inner;
        float outerSq = outer * outer;

        int left = std::max(0, static_cast<int>(ripple.centerX - outer - 1.0f));
        int top = std::max(0, static_cast<int>(ripple.centerY - outer - 1.0f));
        int right = std::min(static_cast<int>(dibBuffer.width), static_cast<int>(ripple.centerX + outer + 2.0f));
        int bottom = std::min(static_cast<int>(dibBuffer.height), static_cast<int>(ripple.centerY + outer + 2.0f));

        float alpha = a / 255.0f;
        for (int y = top; y < bottom; ++y) {
            for (int x = left; x < right; ++x) {
                float dx = static_cast<float>(x - ripple.centerX);
                float dy = static_cast<float>(y - ripple.centerY);
                float distSq = dx * dx + dy * dy;
                if (distSq < innerSq || distSq > outerSq) {
                    continue;
                }

                uint32_t* pPixel = dibBuffer.pBits + y * dibBuffer.width + x;
                uint32_t dst = *pPixel;
                uint8_t dstA = (dst >> 24) & 0xFF;
                uint8_t dstR = (dst >> 16) & 0xFF;
                uint8_t dstG = (dst >> 8) & 0xFF;
                uint8_t dstB = dst & 0xFF;

                uint8_t outA = static_cast<uint8_t>(std::min(255.0f, dstA + a * (1.0f - dstA / 255.0f)));
                uint8_t outR = static_cast<uint8_t>(rr * alpha + dstR * (1.0f - alpha));
                uint8_t outG = static_cast<uint8_t>(gg * alpha + dstG * (1.0f - alpha));
                uint8_t outB = static_cast<uint8_t>(bb * alpha + dstB * (1.0f - alpha));

                *pPixel = (outA << 24) | (outR << 16) | (outG << 8) | outB;
            }
        }
    }
}

void MouseHighlighter::UpdateLayeredWindowOutput(const RECT& updateRect) {
    if (!hLayeredWindow || !dibBuffer.hdcMem) return;
    
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 0xFF;  // 满强度 (由内容的 alpha 决定最终透明度)
    blend.AlphaFormat = AC_SRC_ALPHA;  // 源图像使用 alpha 通道
    
    // 计算输出的位置和大小
    POINT ptDst = {updateRect.left, updateRect.top};
    SIZE sz = {
        updateRect.right - updateRect.left,
        updateRect.bottom - updateRect.top
    };
    POINT ptSrc = {updateRect.left, updateRect.top};
    
    // 输出分层窗口
    if (!UpdateLayeredWindow(
        hLayeredWindow,
        nullptr,                    // 设备上下文 (nullptr = 屏幕)
        &ptDst,                     // 目标位置
        &sz,                        // 大小
        dibBuffer.hdcMem,           // 源 DC
        &ptSrc,                     // 源起点
        0,                          // 色键 (不使用)
        &blend,                     // 混合函数
        ULW_ALPHA                   // 更新脏矩形 + Alpha 混合
    )) {
        // 更新失败 (可选记录错误)
    }
}

// ============================================================
// 托盘相关
// ============================================================

bool MouseHighlighter::SetupTrayIcon() {
    wmTaskbarCreated = RegisterWindowMessage(L"TaskbarCreated");

    // 从资源加载图标
    HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDR_ICON));

    // 后备方案：如果资源加载失败，使用系统默认图标
    if (!hIcon) {
        hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    }
    if (!hIcon) return false;
    
    // 初始化托盘图标数据
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hLayeredWindow;
    nid.uID = ID_TRAY_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY_ICON;
    nid.hIcon = hIcon;
    wcscpy_s(nid.szTip, sizeof(nid.szTip) / sizeof(wchar_t), APP_NAME);
    
    // 添加到托盘
    if (!Shell_NotifyIcon(NIM_ADD, &nid)) {
        return false;
    }

    // 使用新版托盘图标行为，提升高 DPI 下显示质量
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIcon(NIM_SETVERSION, &nid);
    return true;
}

void MouseHighlighter::RemoveTrayIcon() {
    if (hLayeredWindow && nid.hWnd) {
        Shell_NotifyIcon(NIM_DELETE, &nid);
    }
}

void MouseHighlighter::ShowTrayMenu() {
    // 获取鼠标位置
    POINT pt;
    GetCursorPos(&pt);
    
    // 创建弹出菜单
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    HMENU hHaloColorMenu = CreatePopupMenu();
    HMENU hHaloSizeMenu = CreatePopupMenu();
    HMENU hHaloAlphaMenu = CreatePopupMenu();
    HMENU hHaloQualityMenu = CreatePopupMenu();
    HMENU hRippleColorMenu = CreatePopupMenu();
    HMENU hRippleSizeMenu = CreatePopupMenu();
    HMENU hRippleAlphaMenu = CreatePopupMenu();
    HMENU hRippleSpeedMenu = CreatePopupMenu();
    if (!hHaloColorMenu || !hHaloSizeMenu || !hHaloAlphaMenu || !hHaloQualityMenu || !hRippleColorMenu || !hRippleSizeMenu || !hRippleAlphaMenu || !hRippleSpeedMenu) {
        if (hHaloColorMenu) DestroyMenu(hHaloColorMenu);
        if (hHaloSizeMenu) DestroyMenu(hHaloSizeMenu);
        if (hHaloAlphaMenu) DestroyMenu(hHaloAlphaMenu);
        if (hHaloQualityMenu) DestroyMenu(hHaloQualityMenu);
        if (hRippleColorMenu) DestroyMenu(hRippleColorMenu);
        if (hRippleSizeMenu) DestroyMenu(hRippleSizeMenu);
        if (hRippleAlphaMenu) DestroyMenu(hRippleAlphaMenu);
        if (hRippleSpeedMenu) DestroyMenu(hRippleSpeedMenu);
        DestroyMenu(hMenu);
        return;
    }

    const uint32_t HALO_BLUE = 0x660099FF;
    const uint32_t HALO_YELLOW = 0x66FFFF00;
    const uint32_t RIPPLE_GREEN = 0x3300FF99;
    const uint32_t RIPPLE_BLUE = 0x330099FF;
    const uint32_t RIPPLE_PINK = 0x33FF66CC;

    UINT haloBlueState = (config.halo.colorARGB == HALO_BLUE) ? MF_CHECKED : MF_UNCHECKED;
    UINT haloYellowState = (config.halo.colorARGB == HALO_YELLOW) ? MF_CHECKED : MF_UNCHECKED;
    UINT haloSmallState = (config.halo.radius <= 25.0f) ? MF_CHECKED : MF_UNCHECKED;
    UINT haloMediumState = (config.halo.radius > 25.0f && config.halo.radius <= 37.0f) ? MF_CHECKED : MF_UNCHECKED;
    UINT haloLargeState = (config.halo.radius > 37.0f && config.halo.radius <= 52.0f) ? MF_CHECKED : MF_UNCHECKED;
    UINT haloXLState = (config.halo.radius > 52.0f && config.halo.radius <= 70.0f) ? MF_CHECKED : MF_UNCHECKED;
    UINT haloXXLState = (config.halo.radius > 70.0f) ? MF_CHECKED : MF_UNCHECKED;

    UINT rippleGreenState = (config.ripple.colorARGB == RIPPLE_GREEN) ? MF_CHECKED : MF_UNCHECKED;
    UINT rippleBlueState = (config.ripple.colorARGB == RIPPLE_BLUE) ? MF_CHECKED : MF_UNCHECKED;
    UINT ripplePinkState = (config.ripple.colorARGB == RIPPLE_PINK) ? MF_CHECKED : MF_UNCHECKED;
    UINT rippleSmallState = (config.ripple.maxRadius <= 90.0f) ? MF_CHECKED : MF_UNCHECKED;
    UINT rippleMediumState = (config.ripple.maxRadius > 90.0f && config.ripple.maxRadius <= 140.0f) ? MF_CHECKED : MF_UNCHECKED;
    UINT rippleLargeState = (config.ripple.maxRadius > 140.0f && config.ripple.maxRadius <= 200.0f) ? MF_CHECKED : MF_UNCHECKED;
    UINT rippleXLState = (config.ripple.maxRadius > 200.0f && config.ripple.maxRadius <= 260.0f) ? MF_CHECKED : MF_UNCHECKED;
    UINT rippleXXLState = (config.ripple.maxRadius > 260.0f) ? MF_CHECKED : MF_UNCHECKED;

    uint8_t haloAlpha = static_cast<uint8_t>((config.halo.colorARGB >> 24) & 0xFF);
    uint8_t rippleAlpha = static_cast<uint8_t>((config.ripple.colorARGB >> 24) & 0xFF);
    UINT haloAlphaLowState = (haloAlpha <= 85) ? MF_CHECKED : MF_UNCHECKED;
    UINT haloAlphaMediumState = (haloAlpha > 85 && haloAlpha <= 130) ? MF_CHECKED : MF_UNCHECKED;
    UINT haloAlphaHighState = (haloAlpha > 130) ? MF_CHECKED : MF_UNCHECKED;
    UINT rippleAlphaLowState = (rippleAlpha <= 45) ? MF_CHECKED : MF_UNCHECKED;
    UINT rippleAlphaMediumState = (rippleAlpha > 45 && rippleAlpha <= 80) ? MF_CHECKED : MF_UNCHECKED;
    UINT rippleAlphaHighState = (rippleAlpha > 80) ? MF_CHECKED : MF_UNCHECKED;
    UINT haloQualityNormalState = (config.halo.qualityLevel <= 1) ? MF_CHECKED : MF_UNCHECKED;
    UINT haloQualityHighState = (config.halo.qualityLevel == 2) ? MF_CHECKED : MF_UNCHECKED;
    UINT haloQualityUltraState = (config.halo.qualityLevel >= 3) ? MF_CHECKED : MF_UNCHECKED;
    UINT rippleSpeedFastState = (config.ripple.durationMS <= 380) ? MF_CHECKED : MF_UNCHECKED;
    UINT rippleSpeedMediumState = (config.ripple.durationMS > 380 && config.ripple.durationMS <= 800) ? MF_CHECKED : MF_UNCHECKED;
    UINT rippleSpeedSlowState = (config.ripple.durationMS > 800) ? MF_CHECKED : MF_UNCHECKED;
    UINT rippleEnabledState = config.ripple.enabled ? MF_CHECKED : MF_UNCHECKED;
    UINT autoStartupState = config.system.autoStartup ? MF_CHECKED : MF_UNCHECKED;
    
    // 光晕颜色
    AppendMenu(hHaloColorMenu, MF_STRING | haloBlueState, IDM_COLOR_BLUE, L"蓝色");
    AppendMenu(hHaloColorMenu, MF_STRING | haloYellowState, IDM_COLOR_YELLOW, L"黄色");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hHaloColorMenu, L"光晕颜色");

    // 光晕大小
    AppendMenu(hHaloSizeMenu, MF_STRING | haloSmallState, IDM_HALO_SIZE_SMALL, L"小");
    AppendMenu(hHaloSizeMenu, MF_STRING | haloMediumState, IDM_HALO_SIZE_MEDIUM, L"中");
    AppendMenu(hHaloSizeMenu, MF_STRING | haloLargeState, IDM_HALO_SIZE_LARGE, L"大");
    AppendMenu(hHaloSizeMenu, MF_STRING | haloXLState, IDM_HALO_SIZE_XL, L"超大");
    AppendMenu(hHaloSizeMenu, MF_STRING | haloXXLState, IDM_HALO_SIZE_XXL, L"特大");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hHaloSizeMenu, L"光晕大小");

    // 光晕透明度
    AppendMenu(hHaloAlphaMenu, MF_STRING | haloAlphaLowState, IDM_HALO_ALPHA_LOW, L"低");
    AppendMenu(hHaloAlphaMenu, MF_STRING | haloAlphaMediumState, IDM_HALO_ALPHA_MEDIUM, L"中");
    AppendMenu(hHaloAlphaMenu, MF_STRING | haloAlphaHighState, IDM_HALO_ALPHA_HIGH, L"高");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hHaloAlphaMenu, L"光晕透明度");

    // 光晕画质
    AppendMenu(hHaloQualityMenu, MF_STRING | haloQualityNormalState, IDM_HALO_QUALITY_NORMAL, L"普通");
    AppendMenu(hHaloQualityMenu, MF_STRING | haloQualityHighState, IDM_HALO_QUALITY_HIGH, L"高清");
    AppendMenu(hHaloQualityMenu, MF_STRING | haloQualityUltraState, IDM_HALO_QUALITY_ULTRA, L"超清");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hHaloQualityMenu, L"光晕画质");

    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
    
    // 填充切换选项
    UINT fillState = config.halo.filled ? MF_CHECKED : MF_UNCHECKED;
    AppendMenu(hMenu, MF_STRING | fillState, IDM_TOGGLE_FILL, L"实心圆");

    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);

    // 波纹颜色
    AppendMenu(hRippleColorMenu, MF_STRING | rippleGreenState, IDM_RIPPLE_COLOR_GREEN, L"绿色");
    AppendMenu(hRippleColorMenu, MF_STRING | rippleBlueState, IDM_RIPPLE_COLOR_BLUE, L"蓝色");
    AppendMenu(hRippleColorMenu, MF_STRING | ripplePinkState, IDM_RIPPLE_COLOR_PINK, L"粉色");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hRippleColorMenu, L"波纹颜色");

    // 波纹大小
    AppendMenu(hRippleSizeMenu, MF_STRING | rippleSmallState, IDM_RIPPLE_SIZE_SMALL, L"小");
    AppendMenu(hRippleSizeMenu, MF_STRING | rippleMediumState, IDM_RIPPLE_SIZE_MEDIUM, L"中");
    AppendMenu(hRippleSizeMenu, MF_STRING | rippleLargeState, IDM_RIPPLE_SIZE_LARGE, L"大");
    AppendMenu(hRippleSizeMenu, MF_STRING | rippleXLState, IDM_RIPPLE_SIZE_XL, L"超大");
    AppendMenu(hRippleSizeMenu, MF_STRING | rippleXXLState, IDM_RIPPLE_SIZE_XXL, L"特大");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hRippleSizeMenu, L"波纹大小");

    // 波纹透明度
    AppendMenu(hRippleAlphaMenu, MF_STRING | rippleAlphaLowState, IDM_RIPPLE_ALPHA_LOW, L"低");
    AppendMenu(hRippleAlphaMenu, MF_STRING | rippleAlphaMediumState, IDM_RIPPLE_ALPHA_MEDIUM, L"中");
    AppendMenu(hRippleAlphaMenu, MF_STRING | rippleAlphaHighState, IDM_RIPPLE_ALPHA_HIGH, L"高");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hRippleAlphaMenu, L"波纹透明度");

    // 波纹速度
    AppendMenu(hRippleSpeedMenu, MF_STRING | rippleSpeedSlowState, IDM_RIPPLE_SPEED_SLOW, L"慢");
    AppendMenu(hRippleSpeedMenu, MF_STRING | rippleSpeedMediumState, IDM_RIPPLE_SPEED_MEDIUM, L"中");
    AppendMenu(hRippleSpeedMenu, MF_STRING | rippleSpeedFastState, IDM_RIPPLE_SPEED_FAST, L"快");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hRippleSpeedMenu, L"波纹速度");

    AppendMenu(hMenu, MF_STRING | rippleEnabledState, IDM_RIPPLE_TOGGLE, L"启用波纹");

    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hMenu, MF_STRING | autoStartupState, IDM_AUTO_STARTUP, L"开机自启");

    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
    
    // 退出
    AppendMenu(hMenu, MF_STRING, IDM_EXIT_APP, L"退出");
    
    // 显示菜单
    SetForegroundWindow(hLayeredWindow);
    TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hLayeredWindow, nullptr);
    
    DestroyMenu(hMenu);
}

// ============================================================
// 消息处理
// ============================================================

LRESULT CALLBACK MouseHighlighter::WindowProcStatic(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (!s_pInstance) {
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    
    return s_pInstance->HandleWindowMessage(hWnd, msg, wParam, lParam);
}

LRESULT MouseHighlighter::HandleWindowMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TRAY_ICON: {
            // 兼容 NOTIFYICON_VERSION_4 与旧版本回调参数格式
            UINT trayMsg = static_cast<UINT>(lParam);
            int trayIconId = static_cast<int>(wParam);
            if (nid.uVersion >= NOTIFYICON_VERSION_4) {
                trayMsg = LOWORD(static_cast<DWORD>(lParam));
                trayIconId = HIWORD(static_cast<DWORD>(lParam));
            }
            HandleTrayMessage(trayMsg, trayIconId);
            return 0;
        }
        
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        
        case WM_CLOSE:
            RequestShutdown();
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                RequestShutdown();
                return 0;
            }
            break;
        
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_COLOR_BLUE:
                    config.halo.colorARGB = 0x660099FF;
                    config.Validate();
                    SaveConfigFile();
                    return 0;
                
                case IDM_COLOR_YELLOW:
                    config.halo.colorARGB = 0x66FFFF00;
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_HALO_SIZE_SMALL:
                    config.halo.radius = 20.0f;
                    config.halo.thickness = 1.2f;
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_HALO_SIZE_MEDIUM:
                    config.halo.radius = 30.0f;
                    config.halo.thickness = 1.5f;
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_HALO_SIZE_LARGE:
                    config.halo.radius = 44.0f;
                    config.halo.thickness = 2.0f;
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_HALO_SIZE_XL:
                    config.halo.radius = 60.0f;
                    config.halo.thickness = 2.6f;
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_HALO_SIZE_XXL:
                    config.halo.radius = 80.0f;
                    config.halo.thickness = 3.2f;
                    config.Validate();
                    SaveConfigFile();
                    return 0;
                
                case IDM_TOGGLE_FILL:
                    config.halo.filled = !config.halo.filled;
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_HALO_ALPHA_LOW:
                    config.halo.colorARGB = SetColorAlpha(config.halo.colorARGB, 77);
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_HALO_ALPHA_MEDIUM:
                    config.halo.colorARGB = SetColorAlpha(config.halo.colorARGB, 115);
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_HALO_ALPHA_HIGH:
                    config.halo.colorARGB = SetColorAlpha(config.halo.colorARGB, 153);
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_HALO_QUALITY_NORMAL:
                    config.halo.qualityLevel = 1;
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_HALO_QUALITY_HIGH:
                    config.halo.qualityLevel = 2;
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_HALO_QUALITY_ULTRA:
                    config.halo.qualityLevel = 3;
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_RIPPLE_COLOR_GREEN:
                    config.ripple.colorARGB = 0x3300FF99;
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_RIPPLE_COLOR_BLUE:
                    config.ripple.colorARGB = 0x330099FF;
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_RIPPLE_COLOR_PINK:
                    config.ripple.colorARGB = 0x33FF66CC;
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_RIPPLE_SIZE_SMALL:
                    config.ripple.maxRadius = 80.0f;
                    config.ripple.thickness = 2.0f;
                    config.ripple.durationMS = 200;
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_RIPPLE_SIZE_MEDIUM:
                    config.ripple.maxRadius = 120.0f;
                    config.ripple.thickness = 2.5f;
                    config.ripple.durationMS = 240;
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_RIPPLE_SIZE_LARGE:
                    config.ripple.maxRadius = 170.0f;
                    config.ripple.thickness = 3.0f;
                    config.ripple.durationMS = 300;
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_RIPPLE_SIZE_XL:
                    config.ripple.maxRadius = 240.0f;
                    config.ripple.thickness = 3.2f;
                    config.ripple.durationMS = 360;
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_RIPPLE_SIZE_XXL:
                    config.ripple.maxRadius = 300.0f;
                    config.ripple.thickness = 3.5f;
                    config.ripple.durationMS = 420;
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_RIPPLE_ALPHA_LOW:
                    config.ripple.colorARGB = SetColorAlpha(config.ripple.colorARGB, 38);
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_RIPPLE_ALPHA_MEDIUM:
                    config.ripple.colorARGB = SetColorAlpha(config.ripple.colorARGB, 64);
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_RIPPLE_ALPHA_HIGH:
                    config.ripple.colorARGB = SetColorAlpha(config.ripple.colorARGB, 96);
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_RIPPLE_SPEED_SLOW:
                    config.ripple.durationMS = 1600;
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_RIPPLE_SPEED_MEDIUM:
                    config.ripple.durationMS = 700;
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_RIPPLE_SPEED_FAST:
                    config.ripple.durationMS = 360;
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_RIPPLE_TOGGLE:
                    config.ripple.enabled = !config.ripple.enabled;
                    if (!config.ripple.enabled) {
                        ripplePool.activeCount = 0;
                    }
                    config.Validate();
                    SaveConfigFile();
                    return 0;

                case IDM_AUTO_STARTUP: {
                    bool target = !config.system.autoStartup;
                    if (ApplyAutoStartupSetting(target)) {
                        config.system.autoStartup = target;
                        SaveConfigFile();
                    } else {
                        MessageBoxW(hWnd, L"开机自启设置失败，请检查权限。", L"MouseHighlighter", MB_OK | MB_ICONWARNING);
                    }
                    return 0;
                }
                
                case IDM_EXIT_APP:
                    RequestShutdown();
                    return 0;
            }
            return 0;
        
        default:
            if (msg == wmTaskbarCreated) {
                // 任务栏重创建，重新添加托盘图标
                SetupTrayIcon();
                return 0;
            }
            return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void MouseHighlighter::HandleTrayMessage(UINT msg, int iconID) {
    switch (msg) {
        case WM_CONTEXTMENU:
        case WM_RBUTTONUP:
        case NIN_SELECT:
        case NIN_KEYSELECT:
            ShowTrayMenu();
            break;
        
        case WM_LBUTTONDBLCLK:
            // 双击显示配置窗口 (可选功能)
            break;
    }
}

// ============================================================
// 配置应用
// ============================================================

bool MouseHighlighter::ApplyConfig(const Config& newConfig) {
    config = newConfig;
    config.Validate();
    SaveConfigFile();
    return true;
}

// ============================================================
// 请求退出
// ============================================================

void MouseHighlighter::RequestShutdown() {
    if (hExitEvent) {
        SetEvent(hExitEvent);
    }
}

// ============================================================
// 消息循环
// ============================================================

int MouseHighlighter::Run() {
    MSG msg = {};

    bool prevLeftDown = false;
    bool prevRightDown = false;
    DWORD lastTrimTick = GetTickCount();
    const DWORD tickMs = (config.timing.updateIntervalMS > 0)
        ? static_cast<DWORD>(config.timing.updateIntervalMS)
        : 16;

    // 用于检测窗口尺寸变化
    int lastScreenWidth = dirtyRectTracker.screenWidth;
    int lastScreenHeight = dirtyRectTracker.screenHeight;

    while (true) {
        if (hExitEvent && WaitForSingleObject(hExitEvent, 0) == WAIT_OBJECT_0) {
            break;
        }

// 动态检测分辨率变化并更新窗口位置
        int curScreenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int curScreenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (curScreenWidth != lastScreenWidth || curScreenHeight != lastScreenHeight) {
            int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
            int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
            SetWindowPos(
                hLayeredWindow,
                HWND_TOPMOST,
                screenX,
                screenY,
                curScreenWidth,
                curScreenHeight,
                SWP_SHOWWINDOW | SWP_NOACTIVATE
            );
            dirtyRectTracker.screenWidth = curScreenWidth;
            dirtyRectTracker.screenHeight = curScreenHeight;
            lastScreenWidth = curScreenWidth;
            lastScreenHeight = curScreenHeight;
        }

        DWORD waitResult = MsgWaitForMultipleObjects(
            0,
            nullptr,
            FALSE,
            tickMs,
            QS_ALLINPUT
        );

        if (waitResult == WAIT_TIMEOUT) {
            POINT pt{};
            if (GetCursorPos(&pt)) {
                sharedState.cursorX.store(pt.x, std::memory_order_release);
                sharedState.cursorY.store(pt.y, std::memory_order_release);
            }

            bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            if (config.ripple.enabled && leftDown && !prevLeftDown) {
                sharedState.TryEnqueueClick(pt.x, pt.y, 0);
            }
            prevLeftDown = leftDown;

            bool rightDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
            if (config.ripple.enabled && rightDown && !prevRightDown) {
                sharedState.TryEnqueueClick(pt.x, pt.y, 1);
            }
            prevRightDown = rightDown;

            RenderFrame();

            bool isIdle = (ripplePool.activeCount == 0) && !leftDown && !rightDown;
            TryTrimWorkingSetIfIdle(isIdle, lastTrimTick);

            continue;
        }

        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                return static_cast<int>(msg.wParam);
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}
