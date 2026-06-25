# MouseAura

一个极轻量级的 Windows 桌面鼠标高亮工具，使用 C++17 + Win32 API 编写。通过全屏透明叠加层，在鼠标光标周围渲染半透明光晕圆圈和点击波纹动画，方便演示、录屏、教学等场景下突出显示鼠标操作。

## 功能特性

- **光晕圆圈** — 跟随鼠标光标，支持自定义颜色、大小、透明度、粗细、填充模式
- **点击波纹** — 鼠标左/右键点击时触发扩散波纹动画，可配置颜色、大小、速度、数量（最多 12 个并发）
- **全屏透明叠加层** — 点击穿透，不影响正常操作
- **多显示器 & DPI 感知** — 自动适配多屏环境和高分辨率显示器
- **系统托盘管理** — 右键托盘图标即可调整所有参数，支持开机自启
- **配置持久化** — 所有设置自动保存到 INI 文件，重启后保留
- **脏矩形优化** — 仅重绘变化区域，CPU 占用极低
- **EMA 坐标平滑** — 可选的光标坐标指数移动平均平滑，减少抖动

## 截图示意

```
  ┌─────────────────────────────┐
  │                             │
  │         ╭───────╮           │
  │        ╱ 光晕圆圈 ╲          │
  │       │   ◉ 鼠标   │         │
  │        ╲         ╱          │
  │         ╰───────╯           │
  │       ╭ ─ ─ ─ ─ ─╮         │
  │      ╭  点击波纹  ╮          │
  │     ╭  (扩散动画)  ╮         │
  │                             │
  └─────────────────────────────┘
  透明叠加层，点击穿透
```

## 项目结构

```
MouseAura/
├── include/
│   ├── MouseHighlighter.h      # 主应用类声明
│   ├── SharedState.h           # 无锁环形队列与线程间通信
│   ├── DataStructures.h        # DIB 缓冲区、波纹状态、脏矩形追踪
│   └── Config.h                # 配置结构体与 INI 读写
├── src/
│   ├── main.cpp                # 程序入口、DPI 感知初始化
│   ├── MouseHighlighter.cpp    # 核心实现：窗口、渲染、托盘、消息循环
│   └── Config.cpp              # INI 文件解析器
├── res/                        # 资源目录（预留）
├── CMakeLists.txt              # CMake 构建脚本
└── README.md                   # 本文件
```

## 快速开始

### 前提条件

- Windows 10+（Windows 7+ 理论支持）
- CMake 3.15+
- MSVC 2019+ 或 MinGW

### 编译

**Visual Studio (推荐)：**

```bash
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

编译产物位于 `build/Release/MouseAura.exe`。

**MinGW：**

```bash
mkdir build && cd build
cmake -G "MinGW Makefiles" ..
cmake --build .
```

编译产物位于 `build/MouseAura.exe`。

### 运行

双击 `MouseAura.exe` 即可。程序启动后会在系统托盘显示图标，ESC 键可退出。

## 托盘菜单

右键托盘图标可调整以下设置：

| 菜单项 | 说明 |
|--------|------|
| 光晕颜色 | 蓝色 / 黄色 |
| 光晕大小 | S / M / L / XL / XXL |
| 光晕透明度 | 低 / 中 / 高 |
| 光晕画质 | 普通 / 高清 / 超清（SSAA 级别） |
| 实心圆 | 切换空心圆环 / 实心圆 |
| 波纹颜色 | 绿 / 蓝 / 粉 |
| 波纹大小 | S / M / L / XL / XXL |
| 波纹透明度 | 低 / 中 / 高 |
| 波纹速度 | 慢 / 中 / 快 |
| 启用波纹 | 开 / 关 |
| 开机自启 | 注册 / 取消 Windows 自启动 |
| 退出 | 关闭程序 |

## 配置文件

配置自动保存在 `%APPDATA%\MouseAura\config.ini`，格式如下：

```ini
[Halo]
ColorARGB=0x660099FF
Radius=30.0
Thickness=1.5

[Ripple]
ColorARGB=0x3300FF99
MaxRadius=120.0
DurationMS=240
Thickness=2.5

[Smoothing]
Alpha=0.25

[Timing]
TargetFPS=60
UpdateIntervalMS=17
```

## 技术要点

| 项目 | 说明 |
|------|------|
| 叠加窗口 | `WS_EX_LAYERED \| WS_EX_TRANSPARENT \| WS_EX_TOPMOST`，全屏覆盖且点击穿透 |
| 渲染方式 | 软件光栅化，逐像素 Porter-Duff Alpha 混合 |
| 抗锯齿 | 超级采样（1x / 2x2 / 3x3 SSAA） |
| 光标追踪 | `GetCursorPos()` 轮询 + 可选 EMA 平滑 |
| 点击检测 | `GetAsyncKeyState()` 异步按键状态查询 |
| 帧率控制 | `MsgWaitForMultipleObjects` 超时驱动，默认 ~60fps |
| 内存管理 | 空闲时 `EmptyWorkingSet()` 回收工作集 |

## 性能

- CPU：静止 < 1%，移动时 2-5%，多波纹并发 < 9%
- 内存：< 30 MB，24 小时运行无增长
- 渲染延迟：< 10ms (P95)

## 常见问题

**Q: 窗口不显示？**
确保 DWM 合成已启用（Windows 10/11 默认开启）。全屏独占应用下叠加层会被隐藏。

**Q: 鼠标有延迟？**
降低波纹并发数或检查是否有其他全局钩子程序冲突。

**Q: 找不到 `dwmapi.h`？**
安装 Windows SDK，可通过 Visual Studio Installer 添加。

## License

MIT
