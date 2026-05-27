#include "Config.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>

// ============================================================
// INI 加载
// ============================================================

bool Config::LoadFromINI(const wchar_t* path) noexcept {
    FILE* file = nullptr;
    
    // 尝试以 UTF-8 编码打开文件
    errno_t err = _wfopen_s(&file, path, L"r, ccs=UTF-8");
    if (err != 0 || !file) {
        // 文件不存在或打开失败，使用默认配置
        *this = GetDefaultConfig();
        return false;
    }
    
    wchar_t lineBuf[512] = {};
    wchar_t section[64] = {};  // 当前段落
    
    while (fgetws(lineBuf, sizeof(lineBuf) / sizeof(wchar_t), file)) {
        // 移除行尾换行符
        wchar_t* p = wcschr(lineBuf, L'\n');
        if (p) *p = L'\0';
        p = wcschr(lineBuf, L'\r');
        if (p) *p = L'\0';
        
        // 跳过空行和注释
        if (lineBuf[0] == L'\0' || lineBuf[0] == L';') {
            continue;
        }
        
        // 检查段落标记 [Section]
        if (lineBuf[0] == L'[') {
            wchar_t* end = wcschr(lineBuf, L']');
            if (end) {
                *end = L'\0';
                wcscpy_s(section, sizeof(section) / sizeof(wchar_t), lineBuf + 1);
            }
            continue;
        }
        
        // 解析 key=value
        wchar_t* eq = wcschr(lineBuf, L'=');
        if (!eq) continue;
        
        *eq = L'\0';
        wchar_t* key = lineBuf;
        wchar_t* value = eq + 1;
        
        // 移除前后空格
        while (*key == L' ' || *key == L'\t') key++;
        while (*value == L' ' || *value == L'\t') value++;
        
        // 根据段落与键值加载配置
        if (wcscmp(section, L"Halo") == 0) {
            if (wcscmp(key, L"ColorARGB") == 0) {
                halo.colorARGB = (uint32_t)wcstoul(value, nullptr, 0);
            } else if (wcscmp(key, L"Radius") == 0) {
                halo.radius = (float)wcstod(value, nullptr);
            } else if (wcscmp(key, L"Thickness") == 0) {
                halo.thickness = (float)wcstod(value, nullptr);
            } else if (wcscmp(key, L"DrawSteps") == 0) {
                halo.drawSteps = (uint16_t)wcstoul(value, nullptr, 10);
            } else if (wcscmp(key, L"Filled") == 0) {
                halo.filled = (_wcsicmp(value, L"true") == 0 || wcscmp(value, L"1") == 0);
            } else if (wcscmp(key, L"QualityLevel") == 0) {
                halo.qualityLevel = (uint8_t)wcstoul(value, nullptr, 10);
            }
        } else if (wcscmp(section, L"Ripple") == 0) {
            if (wcscmp(key, L"Enabled") == 0) {
                ripple.enabled = (_wcsicmp(value, L"true") == 0 || wcscmp(value, L"1") == 0);
            } else if (wcscmp(key, L"ColorARGB") == 0) {
                ripple.colorARGB = (uint32_t)wcstoul(value, nullptr, 0);
            } else if (wcscmp(key, L"MaxRadius") == 0) {
                ripple.maxRadius = (float)wcstod(value, nullptr);
            } else if (wcscmp(key, L"DurationMS") == 0) {
                ripple.durationMS = (uint32_t)wcstoul(value, nullptr, 10);
            } else if (wcscmp(key, L"Thickness") == 0) {
                ripple.thickness = (float)wcstod(value, nullptr);
            } else if (wcscmp(key, L"MaxConcurrent") == 0) {
                ripple.maxConcurrent = (uint8_t)wcstoul(value, nullptr, 10);
            } else if (wcscmp(key, L"AlphaExponent") == 0) {
                ripple.alphaExponent = (float)wcstod(value, nullptr);
            } else if (wcscmp(key, L"RadiusExponent") == 0) {
                ripple.radiusExponent = (float)wcstod(value, nullptr);
            }
        } else if (wcscmp(section, L"Smoothing") == 0) {
            if (wcscmp(key, L"Alpha") == 0) {
                smoothing.alpha = (float)wcstod(value, nullptr);
            }
        } else if (wcscmp(section, L"Timing") == 0) {
            if (wcscmp(key, L"TargetFPS") == 0) {
                timing.targetFPS = (uint32_t)wcstoul(value, nullptr, 10);
            }
        } else if (wcscmp(section, L"System") == 0) {
            if (wcscmp(key, L"EnableTrayIcon") == 0) {
                system.enableTrayIcon = (_wcsicmp(value, L"true") == 0 || wcscmp(value, L"1") == 0);
            } else if (wcscmp(key, L"AutoStartup") == 0) {
                system.autoStartup = (_wcsicmp(value, L"true") == 0 || wcscmp(value, L"1") == 0);
            }
        }
    }
    
    fclose(file);
    
    // 验证并钳制参数
    Validate();
    
    return true;
}

// ============================================================
// INI 保存
// ============================================================

bool Config::SaveToINI(const wchar_t* path) const noexcept {
    FILE* file = nullptr;
    
    // 以 UTF-8 编码创建或覆盖文件
    errno_t err = _wfopen_s(&file, path, L"w, ccs=UTF-8");
    if (err != 0 || !file) {
        return false;
    }
    
    // 写入 BOM（UTF-8 with BOM）
    // fwprintf 自动处理
    
    // 写 Halo 段
    fwprintf_s(file, L"[Halo]\n");
    fwprintf_s(file, L"ColorARGB=0x%X\n", halo.colorARGB);
    fwprintf_s(file, L"Radius=%g\n", halo.radius);
    fwprintf_s(file, L"Thickness=%g\n", halo.thickness);
    fwprintf_s(file, L"DrawSteps=%u\n", halo.drawSteps);
    fwprintf_s(file, L"Filled=%s\n", halo.filled ? L"true" : L"false");
    fwprintf_s(file, L"QualityLevel=%u\n", halo.qualityLevel);
    fwprintf_s(file, L"\n");
    
    // 写 Ripple 段
    fwprintf_s(file, L"[Ripple]\n");
    fwprintf_s(file, L"Enabled=%s\n", ripple.enabled ? L"true" : L"false");
    fwprintf_s(file, L"ColorARGB=0x%X\n", ripple.colorARGB);
    fwprintf_s(file, L"MaxRadius=%g\n", ripple.maxRadius);
    fwprintf_s(file, L"DurationMS=%u\n", ripple.durationMS);
    fwprintf_s(file, L"Thickness=%g\n", ripple.thickness);
    fwprintf_s(file, L"MaxConcurrent=%u\n", ripple.maxConcurrent);
    fwprintf_s(file, L"AlphaExponent=%g\n", ripple.alphaExponent);
    fwprintf_s(file, L"RadiusExponent=%g\n", ripple.radiusExponent);
    fwprintf_s(file, L"\n");
    
    // 写 Smoothing 段
    fwprintf_s(file, L"[Smoothing]\n");
    fwprintf_s(file, L"Alpha=%g\n", smoothing.alpha);
    fwprintf_s(file, L"MinUpdateDistPx=%u\n", smoothing.minUpdateDistPx);
    fwprintf_s(file, L"\n");
    
    // 写 Timing 段
    fwprintf_s(file, L"[Timing]\n");
    fwprintf_s(file, L"TargetFPS=%u\n", timing.targetFPS);
    fwprintf_s(file, L"UpdateIntervalMS=%u\n", timing.updateIntervalMS);
    fwprintf_s(file, L"\n");
    
    // 写 Monitoring 段
    fwprintf_s(file, L"[Monitoring]\n");
    fwprintf_s(file, L"CheckIntervalMS=%u\n", monitoring.checkIntervalMS);
    fwprintf_s(file, L"MemoryThresholdMB=%u\n", monitoring.memoryThresholdMB);
    fwprintf_s(file, L"GDIHandleThreshold=%u\n", monitoring.gdiHandleThreshold);
    fwprintf_s(file, L"\n");
    
    // 写 System 段
    fwprintf_s(file, L"[System]\n");
    fwprintf_s(file, L"EnableTrayIcon=%s\n", system.enableTrayIcon ? L"true" : L"false");
    fwprintf_s(file, L"AutoStartup=%s\n", system.autoStartup ? L"true" : L"false");
    fwprintf_s(file, L"\n");
    
    fclose(file);
    return true;
}
