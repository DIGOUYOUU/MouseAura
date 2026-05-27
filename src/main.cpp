#include "MouseHighlighter.h"
#include <iostream>

static void EnableBestEffortDpiAwareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) {
        return;
    }

    using SetDpiAwarenessContextFn = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
    auto setDpiAwarenessContext = reinterpret_cast<SetDpiAwarenessContextFn>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext")
    );
    if (setDpiAwarenessContext) {
        setDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}

/**
 * @brief 应用程序主入口
 * 
 * 创建全局唯一的 MouseHighlighter 实例，初始化后进入消息循环
 */
int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    // 提升高 DPI 下托盘菜单/图标清晰度（动态调用，兼容老系统）
    EnableBestEffortDpiAwareness();
    
    // 创建应用实例
    MouseHighlighter app;
    
    // 初始化
    if (!app.Initialize()) {
        MessageBoxW(nullptr,
                   L"初始化失败。可能的原因：\n"
                   L"1. DWM 未启用\n"
                   L"2. 权限不足\n"
                   L"3. 系统资源耗尽",
                   L"鼠标高亮工具 - 错误",
                   MB_OK | MB_ICONERROR);
        return 1;
    }
    
    // 进入消息循环 (阻塞直到 WM_QUIT)
    int exitCode = app.Run();
    
    // 清理 (可选，析构函数也会调用)
    app.Cleanup();
    
    return exitCode;
}

/**
 * @brief ANSI 入口点 (Windows 兼容性)
 * 
 * 这样可以使用 WinMain 而不仅限 wWinMain
 */
int main() {
    wchar_t emptyCmdLine[] = L"";
    return wWinMain(GetModuleHandle(nullptr), nullptr, emptyCmdLine, SW_HIDE);
}
