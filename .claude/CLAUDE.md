# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目配置

工作目录：`D:\Code\Projects\MouseHighlighter`

## 全局规则继承

本项目继承全局配置 `C:\Users\20749\.claude\CLAUDE.md` 的所有规则。

## 项目描述

MouseAura 是一个极轻量级的 Windows 桌面鼠标高亮工具，使用 C++17 + Win32 API 编写。通过全屏透明叠加层，在鼠标光标周围渲染半透明光晕圆圈和点击波纹动画，方便演示、录屏、教学等场景下突出显示鼠标操作。

## 构建命令

**Visual Studio (推荐)：**
```bash
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

**MinGW：**
```bash
mkdir build && cd build
cmake -G "MinGW Makefiles" ..
cmake --build .
```

编译产物位于 `build/Release/MouseAura.exe` 或 `build/MouseAura.exe`。

## 运行要求

- Windows 10+（Windows 7+ 理论支持）
- CMake 3.15+
- MSVC 2019+ 或 MinGW
- Windows SDK（提供 `dwmapi.h` 等头文件）

双击 `MouseAura.exe` 即可运行。程序启动后会在系统托盘显示图标，ESC 键可退出。

## 代码架构

### 核心类结构

- **MouseHighlighter** (`include/MouseHighlighter.h`, `src/MouseHighlighter.cpp`) - 主应用类，负责：
  - 创建全屏透明分层窗口（`WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST`）
  - 注册全局低级鼠标钩子（`WH_MOUSE_LL`）
  - 启动渲染线程与监控线程
  - 系统托盘图标管理与菜单交互
  - 脏矩形优化渲染

- **SharedMouseState** (`include/SharedState.h`) - 无锁环形队列，用于钩子线程与渲染线程间的线程安全通信

- **Config** (`include/Config.h`, `src/Config.cpp`) - 配置结构体与 INI 文件读写，配置保存在 `%APPDATA%\MouseHighlighter\config.ini`

- **DIBBuffer / DirtyRectTracker / RipplePool** (`include/DataStructures.h`) - DIB 双缓冲、脏矩形追踪、波纹对象池（最多 12 个并发）

### 线程模型

1. **主线程** - 消息循环，处理窗口消息和托盘交互
2. **钩子线程** - 全局鼠标钩子回调，必须快速（< 50μs），通过无锁队列传递事件
3. **渲染线程** - 帧率控制（默认 ~60fps），执行软件光栅化渲染
4. **监控线程** - 性能监控，空闲时回收工作集

### 渲染技术

- 软件光栅化，逐像素 Porter-Duff Alpha 混合
- 超级采样抗锯齿（1x / 2x2 / 3x3 SSAA）
- 脏矩形优化，仅重绘变化区域
- EMA 坐标平滑（可选），减少光标抖动

### 入口点

`src/main.cpp` - 程序入口，初始化 DPI 感知，创建并运行 MouseAura 实例。
