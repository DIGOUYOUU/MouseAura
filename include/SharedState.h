#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
#include <windows.h>

// 对齐至缓存行 (64 字节) 避免伪共享
#pragma pack(push, 8)

/**
 * @brief 点击事件结构 - 无锁环形队列中的单个元素
 */
struct ClickEvent {
    int32_t x, y;           // 屏幕坐标
    uint32_t timestamp;     // GetTickCount64() 毫秒戳
    uint8_t button;         // 0=Left, 1=Right, 2=Middle
};

/**
 * @brief 钩子线程与渲染线程间的共享状态
 * 
 * - 钩子线程写入：cursorX, cursorY, isDirty, clickQueue
 * - 渲染线程读取：同上
 * - 所有操作无锁，仅原子变量
 */
struct alignas(64) SharedMouseState {
    // 鼠标实时坐标 - 钩子线程写，渲染线程读
    std::atomic<int32_t> cursorX{0};
    std::atomic<int32_t> cursorY{0};
    
    // 脏标记 - 通知渲染线程需要更新
    std::atomic<bool> isDirty{false};
    
    // 点击事件环形队列
    static constexpr size_t MAX_CLICK_EVENTS = 8;
    std::array<ClickEvent, MAX_CLICK_EVENTS> clickQueue{};
    
    // 环形队列指针 - 只能单调递增
    // 注意: 不追回溯，无需 CAS；仅需 load/store 序列化
    std::atomic<uint8_t> clickHead{0};  // 写指针 (钩子线程写)
    std::atomic<uint8_t> clickTail{0};  // 读指针 (渲染线程读)
    
    /**
     * @brief 钩子线程调用 - 尝试入队一个点击事件
     * @return true 成功入队，false 队列满
     */
    bool TryEnqueueClick(int32_t x, int32_t y, uint8_t button) noexcept {
        uint8_t head = clickHead.load(std::memory_order_relaxed);
        uint8_t tail = clickTail.load(std::memory_order_acquire);
        
        // 检查队列是否满
        uint8_t nextHead = (head + 1) % MAX_CLICK_EVENTS;
        if (nextHead == tail) {
            return false;  // 队列满，丢弃
        }
        
        // 写入事件
        clickQueue[head].x = x;
        clickQueue[head].y = y;
        clickQueue[head].timestamp = GetTickCount();
        clickQueue[head].button = button;
        
        // 推进写指针
        clickHead.store(nextHead, std::memory_order_release);
        isDirty.store(true, std::memory_order_release);
        return true;
    }
    
    /**
     * @brief 渲染线程调用 - 尝试出队一个点击事件
     * @return true 成功出队到 event，false 队列空
     */
    bool TryDequeueClick(ClickEvent& event) noexcept {
        uint8_t head = clickHead.load(std::memory_order_acquire);
        uint8_t tail = clickTail.load(std::memory_order_relaxed);
        
        if (tail == head) {
            return false;  // 队列空
        }
        
        // 读取事件
        event = clickQueue[tail];
        
        // 推进读指针
        clickTail.store((tail + 1) % MAX_CLICK_EVENTS, std::memory_order_release);
        return true;
    }
};

#pragma pack(pop)

// 编译期检查
static_assert(sizeof(SharedMouseState) <= 256, "SharedMouseState should not exceed 256 bytes");
static_assert(alignof(SharedMouseState) == 64, "SharedMouseState must be cache-line aligned");
