#pragma once

#include <cstdint>
#include <wingdi.h>
#include <array>
#include <cmath>

/**
 * @brief DIB (Device Independent Bitmap) 双缓冲管理
 * 
 * 维护 ARGB32 位图，用于 UpdateLayeredWindow 输出
 */
class DIBBuffer {
public:
    DIBBuffer() = default;

    // 内存 DC & 位图
    HDC hdcMem = nullptr;
    HBITMAP hbmDIB = nullptr;
    uint32_t* pBits = nullptr;
    
    uint32_t width = 0;
    uint32_t height = 0;
    
    /**
     * @brief 创建或重建 DIB 缓冲
     * @param w 宽度 (像素)
     * @param h 高度 (像素)
     * @param hdcScreen 参考设备上下文，通常来自 GetDC(nullptr)
     * @return true 成功，false 失败
     */
    bool Create(uint32_t w, uint32_t h, HDC hdcScreen) noexcept {
        Release();
        
        width = w;
        height = h;
        
        // 创建兼容 DC
        hdcMem = CreateCompatibleDC(hdcScreen);
        if (!hdcMem) {
            return false;
        }
        
        // 创建 ARGB32 DIB
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = static_cast<LONG>(w);
        bmi.bmiHeader.biHeight = -static_cast<LONG>(h);  // 负数: 顶部对齐
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;  // ARGB
        bmi.bmiHeader.biCompression = BI_RGB;
        
        hbmDIB = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS,
                                  reinterpret_cast<void**>(&pBits), nullptr, 0);
        if (!hbmDIB || !pBits) {
            DeleteDC(hdcMem);
            hdcMem = nullptr;
            return false;
        }
        
        // 选择 DIB 到 DC
        HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmDIB);
        if (hbmOld) {
            // 如果旧位图非空，需跟踪以便后续删除 (此处可忽略，因为创建时通常为空)
        }
        
        return true;
    }
    
    /**
     * @brief 释放所有资源
     */
    void Release() noexcept {
        if (hbmDIB) {
            DeleteObject(hbmDIB);
            hbmDIB = nullptr;
        }
        if (hdcMem) {
            DeleteDC(hdcMem);
            hdcMem = nullptr;
        }
        pBits = nullptr;
        width = height = 0;
    }
    
    /**
     * @brief 清除矩形区域为透明 (所有像素 alpha = 0)
     */
    void ClearRect(const RECT& rc) noexcept {
        if (!pBits || width == 0 || height == 0) return;
        
        int left = std::max(0L, rc.left);
        int top = std::max(0L, rc.top);
        int right = std::min((LONG)width, rc.right);
        int bottom = std::min((LONG)height, rc.bottom);
        
        for (int y = top; y < bottom; ++y) {
            for (int x = left; x < right; ++x) {
                pBits[y * width + x] = 0;  // ARGB: alpha=0 (完全透明)
            }
        }
    }
    
    /**
     * @brief 获取指针到特定像素
     */
    inline uint32_t* GetPixel(int x, int y) noexcept {
        if (x < 0 || x >= (int)width || y < 0 || y >= (int)height) {
            return nullptr;
        }
        return &pBits[y * width + x];
    }
    
    ~DIBBuffer() {
        Release();
    }
    
    // 禁用拷贝
    DIBBuffer(const DIBBuffer&) = delete;
    DIBBuffer& operator=(const DIBBuffer&) = delete;
};

/**
 * @brief 单个波纹动画状态
 */
struct RippleState {
    int32_t centerX = 0;
    int32_t centerY = 0;
    uint64_t startTime = 0;  // QueryPerformanceCounter() 返回值
    
    /**
     * @brief 计算当前半径
     * @param nowQPC 当前 QueryPerformanceCounter() 返回值
     * @param freq QueryPerformanceFrequency() 返回值
     * @return 半径 (像素)，如果波纹已死亡返回 -1.0f
     */
    float GetCurrentRadius(uint64_t nowQPC,
                           uint64_t freq,
                           float maxRadius,
                           uint32_t durationMS,
                           float radiusExponent = 1.0f) const noexcept {
        if (freq == 0) return -1.0f;
        
        uint64_t elapsedMS = (nowQPC - startTime) * 1000 / freq;
        if (elapsedMS > durationMS) {
            return -1.0f;  // 标记已死亡
        }

        float t = elapsedMS / static_cast<float>(durationMS);
        float exp = std::max(0.1f, radiusExponent);
        return maxRadius * std::pow(t, exp);
    }
    
    /**
     * @brief 计算当前 Alpha 值 (0-255)
     * 使用立方衰减: alpha = (1 - t)^3
     */
    uint8_t GetCurrentAlpha(uint64_t nowQPC,
                            uint64_t freq,
                            uint32_t durationMS,
                            float alphaExponent = 3.0f) const noexcept {
        if (freq == 0) return 0;
        
        uint64_t elapsedMS = (nowQPC - startTime) * 1000 / freq;
        if (elapsedMS > durationMS) {
            return 0;
        }
        
        float t = elapsedMS / static_cast<float>(durationMS);
        float exp = std::max(0.1f, alphaExponent);
        float alpha_norm = std::pow((1.0f - t), exp);
        return static_cast<uint8_t>(255.0f * alpha_norm);
    }
    
    /**
     * @brief 检查波纹是否已鼓励 (可回收)
     */
    bool IsAlive(uint64_t nowQPC, uint64_t freq, uint32_t durationMS) const noexcept {
        if (freq == 0) return false;
        uint64_t elapsedMS = (nowQPC - startTime) * 1000 / freq;
        return elapsedMS <= durationMS;
    }
};

/**
 * @brief 波纹对象池
 */
struct RipplePool {
    static constexpr size_t MAX_RIPPLES = 12;
    
    std::array<RippleState, MAX_RIPPLES> ripples{};
    uint8_t activeCount = 0;  // 当前活跃波纹数 (0 到 MAX_RIPPLES)
    
    /**
     * @brief 添加新波纹
     * @return true 成功，false 池已满
     */
    bool AddRipple(int32_t x, int32_t y, uint64_t nowQPC) noexcept {
        if (activeCount >= MAX_RIPPLES) {
            return false;
        }
        
        ripples[activeCount].centerX = x;
        ripples[activeCount].centerY = y;
        ripples[activeCount].startTime = nowQPC;
        activeCount++;
        
        return true;
    }
    
    /**
     * @brief 清理并压缩死亡波纹，更新 activeCount
     */
    void CompactDeadRipples(uint64_t nowQPC, uint64_t freq, uint32_t durationMS) noexcept {
        uint8_t writeIdx = 0;
        for (uint8_t i = 0; i < activeCount; ++i) {
            if (ripples[i].IsAlive(nowQPC, freq, durationMS)) {
                ripples[writeIdx++] = ripples[i];
            }
        }
        activeCount = writeIdx;
    }
};

/**
 * @brief 脏矩形追踪器
 * 
 * 记录"上一帧圆圈的包围盒 + 当前帧圆圈的包围盒"，
 * 计算联合区域以确定必须重绘的区域
 */
class DirtyRectTracker {
public:
    RECT currentDirty{0, 0, 0, 0};  // 当前帧脏区
    RECT prevDirty{0, 0, 0, 0};    // 上一帧脏区
    
    uint32_t screenWidth = 1920;
    uint32_t screenHeight = 1080;
    
    /**
     * @brief 记下一个圆形的包围盒到脏区
     */
    void UnionCircle(int x, int y, float radius) noexcept {
        int l = std::max(0, static_cast<int>(x - radius - 2.0f));
        int t = std::max(0, static_cast<int>(y - radius - 2.0f));
        int r = std::min(static_cast<int>(screenWidth), 
                        static_cast<int>(x + radius + 2.0f));
        int b = std::min(static_cast<int>(screenHeight),
                        static_cast<int>(y + radius + 2.0f));
        
        if (l >= r || t >= b) return;  // 无效矩形
        
        if (currentDirty.left == 0 && currentDirty.right == 0) {
            currentDirty = {l, t, r, b};
        } else {
            currentDirty.left = std::min(currentDirty.left, (LONG)l);
            currentDirty.top = std::min(currentDirty.top, (LONG)t);
            currentDirty.right = std::max(currentDirty.right, (LONG)r);
            currentDirty.bottom = std::max(currentDirty.bottom, (LONG)b);
        }
    }
    
    /**
     * @brief 获取需要更新的矩形 (当前 + 前一帧的并集)
     */
    RECT GetUpdateRect() const noexcept {
        RECT result = currentDirty;
        result.left = std::min(result.left, prevDirty.left);
        result.top = std::min(result.top, prevDirty.top);
        result.right = std::max(result.right, prevDirty.right);
        result.bottom = std::max(result.bottom, prevDirty.bottom);
        return result;
    }
    
    /**
     * @brief 循环开始 - 交换脏区，准备新一帧
     */
    void BeginFrame() noexcept {
        prevDirty = currentDirty;
        currentDirty = {0, 0, 0, 0};
    }
    
    /**
     * @brief 检查此帧是否有脏内容
     */
    bool HasDirtyRects() const noexcept {
        return !IsRectEmpty(&currentDirty);
    }
};
