#pragma once

#include <algorithm>
#include <cstdint>
#include <array>

/**
 * @brief 应用全局配置
 * 
 * 从 INI 文件加载，可通过托盘菜单修改
 */
struct Config {
    // ========== 光晕圆形参数 ==========
    struct Halo {
        uint32_t colorARGB = 0x660099FF;  // Alpha(102/255≈0.4) + 蓝紫色 ARGB
        float radius = 30.0f;              // 半径 (像素)
        float thickness = 1.5f;            // 线条厚度
        uint16_t drawSteps = 128;          // 多边形近似圆的段数
        bool filled = false;               // 是否填充 (true=实心圆, false=空心圆)
        uint8_t qualityLevel = 2;          // 光晕画质: 1=普通, 2=高清, 3=超清
    } halo;
    
    // ========== 波纹动画参数 ==========
    struct Ripple {
        bool enabled = true;              // 是否启用波纹效果
        uint32_t colorARGB = 0x3300FF99;  // Alpha(51/255≈0.2) + 青绿色
        float maxRadius = 120.0f;         // 最大扩散半径
        uint32_t durationMS = 240;        // 动画持续时间 (毫秒)
        float thickness = 2.5f;           // 波纹线条厚度
        uint8_t maxConcurrent = 12;       // 最多并发波纹数
        float alphaExponent = 3.0f;       // 衰减阶次: alpha = (1-t)^n
        float radiusExponent = 1.0f;      // 扩展阶次: 线性 (1.0)
    } ripple;
    
    // ========== 坐标平滑参数 ==========
    struct Smoothing {
        float alpha = 0.0f;               // EMA 系数 (0.0=不平滑, 推荐 0.25)
        uint32_t minUpdateDistPx = 1;     // 最小更新距离 (像素)
    } smoothing;
    
    // ========== 渲染节拍参数 ==========
    struct Timing {
        uint32_t targetFPS = 60;          // 目标帧率
        uint32_t updateIntervalMS = 17;   // 更新间隔 (1000/60 ≈ 16.67ms, 上舍入)
        uint32_t maxFrameTimeMS = 20;     // 硬超时告警阈值
    } timing;
    
    // ========== 性能监控参数 ==========
    struct Monitoring {
        uint32_t checkIntervalMS = 5000;   // 监控检查间隔
        uint32_t memoryThresholdMB = 100;  // 内存告警阈值
        uint32_t gdiHandleThreshold = 3000; // GDI 对象告警
        bool logToFile = false;             // 是否输出调试日志
    } monitoring;
    
    // ========== 系统集成参数 ==========
    struct System {
        bool enableTrayIcon = true;         // 启用托盘图标
        bool autoStartup = false;           // 开机自启 (Windows 注册表)
        uint32_t memoryCheckIntervalMS = 5000;  // 内存检查间隔
    } system;
    
    // ========== 方法 ==========
    
    /**
     * @brief 从 INI 文件加载配置
     * @param path 文件路径 (宽字符)
     * @return true 成功，false 失败或文件不存在
     */
    bool LoadFromINI(const wchar_t* path) noexcept;
    
    /**
     * @brief 保存配置到 INI 文件
     * @param path 文件路径 (宽字符)
     * @return true 成功，false 失败
     */
    bool SaveToINI(const wchar_t* path) const noexcept;
    
    /**
     * @brief 验证所有参数的有效范围，必要时钳制
     */
    void Validate() noexcept {
        // 光晕
        halo.radius = std::min(100.0f, std::max(10.0f, halo.radius));
        halo.thickness = std::min(5.0f, std::max(0.5f, halo.thickness));
        halo.drawSteps = std::min<uint16_t>(256, std::max<uint16_t>(32, halo.drawSteps));
        halo.qualityLevel = static_cast<uint8_t>(std::min(3u, std::max(1u, static_cast<unsigned>(halo.qualityLevel))));
        
        // 波纹
        ripple.maxRadius = std::min(320.0f, std::max(50.0f, ripple.maxRadius));
        ripple.durationMS = std::min(3000u, std::max(100u, ripple.durationMS));
        ripple.thickness = std::min(5.0f, std::max(0.5f, ripple.thickness));
        ripple.maxConcurrent = std::min(32u, std::max(4u, (uint32_t)ripple.maxConcurrent));
        
        // 平滑
        smoothing.alpha = std::min(0.5f, std::max(0.0f, smoothing.alpha));
        
        // 定时
        timing.targetFPS = std::min(120u, std::max(30u, timing.targetFPS));
        timing.updateIntervalMS = std::max(8u, (uint32_t)(1000 / timing.targetFPS));
    }
};

// 获取默认配置
inline Config GetDefaultConfig() {
    Config cfg;
    cfg.Validate();
    return cfg;
}
