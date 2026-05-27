#pragma once

#include <windows.h>
#include <memory>
#include <cstdint>
#include "SharedState.h"
#include "DataStructures.h"
#include "Config.h"

/**
 * @brief 鼠标高亮工具应用主类
 * 
 * 职责：
 * 1. 创建并管理全屏透明分层窗口
 * 2. 注册全局低级鼠标钩子（WH_MOUSE_LL）
 * 3. 启动渲染线程与监控线程
 * 4. 处理退出信号与资源清理
 */
class MouseHighlighter {
public:
    /**
     * @brief 构造函数 - 初始化默认值
     */
    MouseHighlighter() = default;
    
    /**
     * @brief 析构函数 - 清理资源 (若未主动调用 Cleanup)
     */
    ~MouseHighlighter();
    
    /**
     * @brief 初始化应用
     * 
     * 执行步骤：
     * 1. 加载配置文件 (config.ini)
     * 2. 创建透明分层窗口
     * 3. 初始化 DIB 双缓冲
     * 4. 注册全局鼠标钩子
     * 5. 启动渲染线程 & 监控线程
     * 6. 创建系统托盘图标
     * 
     * @return true 初始化成功，false 失败
     */
    bool Initialize();
    
    /**
     * @brief 运行消息循环 (阻塞至退出信号)
     * 
     * @return 消息循环的退出代码
     */
    int Run();
    
    /**
     * @brief 请求干净退出
     * 
     * 这会设置 exitEvent，触发消息循环结束、线程停止、资源清理
     */
    void RequestShutdown();
    
    /**
     * @brief 手动清理所有资源
     * 
     * 可在 WM_DESTROY 或程序结束时调用
     * 设计为幂等的（可多次调用）
     */
    void Cleanup();
    
    /**
     * @brief 获取窗口句柄
     */
    HWND GetHWND() const { return hLayeredWindow; }
    
    /**
     * @brief 获取配置对象 (const)
     */
    const Config& GetConfig() const { return config; }
    
    /**
     * @brief 获取配置对象 (mutable)
     */
    Config& GetMutableConfig() { return config; }
    
    /**
     * @brief 应用新配置 (例如颜色变更、圆形大小变更)
     * 
     * @param newConfig 新的配置
     * @return true 成功应用，false 失败
     */
    bool ApplyConfig(const Config& newConfig);
    
    // 禁用拷贝与移动
    MouseHighlighter(const MouseHighlighter&) = delete;
    MouseHighlighter& operator=(const MouseHighlighter&) = delete;
    MouseHighlighter(MouseHighlighter&&) = delete;
    MouseHighlighter& operator=(MouseHighlighter&&) = delete;
    
private:
    // ===== 窗口 & 显示相关 =====
    HWND hLayeredWindow = nullptr;      // 透明分层窗口
    HINSTANCE hInstance = nullptr;      // 应用实例句柄
    
    // ===== 配置 =====
    Config config;
    wchar_t configFilePath[MAX_PATH] = {};  // config.ini 路径
    
    // ===== 图形渲染相关 =====
    DIBBuffer dibBuffer;                // DIB 双缓冲
    DirtyRectTracker dirtyRectTracker;  // 脏矩形追踪
    LARGE_INTEGER qpcFrequency{};       // QueryPerformanceCounter 频率
    
    // ===== 共享状态 =====
    SharedMouseState sharedState;       // 钩子 <-> 渲染线程间的共享状态
    RipplePool ripplePool;              // 波纹对象池
    
    // ===== 线程管理 =====
    HANDLE hRenderThread = nullptr;     // 渲染节拍线程
    HANDLE hMonitorThread = nullptr;    // 性能监控线程
    HANDLE hExitEvent = nullptr;        // 应用退出信号
    
    // ===== 钩子 =====
    HHOOK hMouseHook = nullptr;         // 全局鼠标钩子
    
    // ===== 托盘相关 =====
    NOTIFYICONDATA nid{};               // 托盘图标数据
    UINT wmTaskbarCreated = 0;          // 任务栏重创建消息 ID
    
    // ===== 平滑坐标 (EMA) =====
    float smoothedX = 0.0f;
    float smoothedY = 0.0f;
    
    // ===== 窗口类名 =====
    static constexpr wchar_t WINDOW_CLASS_NAME[] = L"MouseHighlightWindow";
    static constexpr wchar_t APP_NAME[] = L"MouseHighlighter";
    
    // ===== 内部方法 =====
    
    /**
     * @brief 创建透明分层窗口
     */
    bool CreateLayeredWindow();
    
    /**
     * @brief 销毁窗口与相关资源
     */
    void DestroyLayeredWindow();
    
    /**
     * @brief 初始化 DIB 缓冲
     */
    bool InitializeDIBBuffer();
    
    /**
     * @brief 注册全局鼠标钩子
     */
    bool RegisterMouseHook();
    
    /**
     * @brief 卸载鼠标钩子
     */
    bool UnregisterMouseHook();
    
    /**
     * @brief 启动渲染线程
     */
    bool StartRenderThread();
    
    /**
     * @brief 启动监控线程
     */
    bool StartMonitorThread();
    
    /**
     * @brief 等待所有线程结束
     */
    void WaitForAllThreads();
    
    /**
     * @brief 加载配置文件
     */
    bool LoadConfigFile();
    
    /**
     * @brief 保存配置文件
     */
    bool SaveConfigFile();
    
    /**
     * @brief 更新 EMA 平滑坐标
     */
    void UpdateSmoothedCursor();
    
    /**
     * @brief 执行单次渲染帧
     */
    void RenderFrame();
    
    /**
     * @brief 绘制鼠标圆圈光晕到 DIB
     */
    void DrawHalo(int centerX, int centerY);
    
    /**
     * @brief 绘制所有活跃波纹到 DIB
     */
    void DrawRipples();
    
    /**
     * @brief 更新分层窗口 (输出脏矩形)
     */
    void UpdateLayeredWindowOutput(const RECT& updateRect);
    
    /**
     * @brief 创建系统托盘图标与菜单
     */
    bool SetupTrayIcon();
    
    /**
     * @brief 销毁托盘图标
     */
    void RemoveTrayIcon();
    
    /**
     * @brief 显示托盘右键菜单
     */
    void ShowTrayMenu();
    
    // ===== 消息处理 =====
    
    /**
     * @brief 窗口消息处理函数
     */
    LRESULT HandleWindowMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    
    /**
     * @brief 处理托盘消息
     */
    void HandleTrayMessage(UINT msg, int iconID);
    
    // ===== 全局钩子回调 (静态) =====
    
    /**
     * @brief 全局鼠标钩子回调 (低级)
     * 
     * 由 Windows 直接调用，必须快 (< 50μs)
     */
    static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);
    
    // ===== 线程函数 (静态) =====
    
    /**
     * @brief 渲染节拍线程
     */
    static DWORD WINAPI RenderTickThreadProc(LPVOID lpParam);
    
    /**
     * @brief 监控线程
     */
    static DWORD WINAPI MonitorThreadProc(LPVOID lpParam);
    
    // ===== 窗口过程 (静态) =====
    
    /**
     * @brief 窗口过程回调
     */
    static LRESULT CALLBACK WindowProcStatic(HWND hWnd, UINT msg,
                                             WPARAM wParam, LPARAM lParam);
    
    // ===== 全局实例指针 (用于静态回调) =====
    
    static MouseHighlighter* s_pInstance;
};
