# MouseHighlighter - 编译指南

本文档提供详细的编译步骤和故障排除方法。

## 前提条件检查清单

- [ ] Windows 7 + SP1 或更高版本
- [ ] Visual Studio 2019 Community/Professional/Enterprise
- [ ] Windows 10 SDK (Build 19041 或更高)
- [ ] CMake 3.15+ 或集成构建工具
- [ ] C++17 兼容编译器

### 验证 Windows SDK 版本

1. 打开 Visual Studio Installer
2. 检查 "Windows 10 SDK (version xxxx)" 是否已安装
3. 如未安装，点击 "Modify" 并勾选 SDK 组件

## 编译方法

### 方法 1: Visual Studio IDE (推荐新手)

#### 步骤 1: 打开项目

1. 启动 Visual Studio 2019+
2. 文件 → 打开 → CMake
3. 选择 `mousehighline` 文件夹中的 `CMakeLists.txt`

Visual Studio 将自动加载 CMake 项目配置。

#### 步骤 2: 配置构建类型

1. 顶部"配置下拉菜单"，选择 `x64-Release`
2. Build → Regenerate CMake Cache (如首次加载)

#### 步骤 3: 编译

1. Build → Build All (Ctrl+Shift+B)
2. 或右键 `CMakeLists.txt` → Build

输出文件位置：
```
mousehighline\build\Release\MouseHighlighter.exe
```

#### 步骤 4: 运行

1. 调试 → 不调试情况下启动 (Ctrl+F5)
   或
2. 从文件资管器直接运行 `build/Release/MouseHighlighter.exe`

### 方法 2: 命令行 (更加可控)

#### 步骤 1: 打开开发者命令提示符

1. 按 Windows 键
2. 搜索 "Developer Command Prompt for VS 2019" (或对应版本)
3. 以管理员身份运行

#### 步骤 2: 导航到项目目录

```bat
cd D:\Code\Project\mousehighline
```

#### 步骤 3: 创建构建目录

```bat
if not exist build mkdir build
cd build
```

#### 步骤 4: 生成 Visual Studio 项目文件

```bat
cmake -G "Visual Studio 17 2022" -A x64 ..
或者mingw+cmake
"D:\Compiler\cmake\bin\cmake.exe" -G "MinGW Makefiles" ..
```

可能的生成器选项：
- `"Visual Studio 16 2019"` - VS2019
- `"Visual Studio 17 2022"` - VS2022
- 或使用 `cmake -G` 查看完整列表

#### 步骤 5: 编译

**使用 CMake:**
```bat
cmake --build . --config Release
```

**使用 MSBuild (速度更快):**
```bat
msbuild MouseHighlighter.sln /p:Configuration=Release /p:Platform=x64
```

**使用 Visual Studio UI:**
```bat
start MouseHighlighter.sln
```
然后在 VS 中按 Ctrl+Shift+B

#### 步骤 6: 运行

```bat
.\Release\MouseHighlighter.exe
```

### 方法 3: 高级 - 使用 Ninja 构建

适合想要最快编译速度的用户。

#### 前提

```bat
# 安装 Ninja
choco install ninja
```

#### 编译

```bat
cd mousehighline
mkdir build && cd build
cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release ..
ninja
```

## 故障排除

### 编译错误

#### 错误 1: "找不到 windows.h"

**症状：**
```
fatal error C1083: Cannot open include file: 'windows.h'
```

**解决方法：**
1. 打开 Visual Studio Installer → Modify
2. 勾选 "Desktop development with C++" 工作负载
3. 确保 "Windows 10 SDK" 已选中
4. 点击 Modify 并等待安装完成

#### 错误 2: "dwmapi.lib 找不到"

**症状：**
```
error LNK1104: cannot open file 'dwmapi.lib'
```

**解决方法：**
1. 确认 CMakeLists.txt 中有 `dwmapi` 库链接
2. 手动验证 SDK 库路径：
   ```
   C:\Program Files (x86)\Windows Kits\10\Lib\<build>\um\x64\
   ```
3. 如路径不同，修改 CMakeLists.txt 中的链接路径

#### 错误 3: C++17 特性未被识别

**症状：**
```
error C4002: too many arguments for macro invocation
error C2440: 'initializing'
```

**解决方法：**
1. 检查 CMakeLists.txt 中 `set(CMAKE_CXX_STANDARD 17)` 是否存在
2. 在 Visual Studio 中进行重新配置：
   ```
   Build → Regenerate CMake Cache
   ```

#### 错误 4: "std::atomic 相关错误"

**症状：**
```
error C2668: 'std::atomic<T>::store': ambiguous call to overloaded function
```

**解决方法：**
- 确保未定义 `_WIN32_WINNT` 为过低版本 (应 ≥ 0x0601)
- CMakeLists.txt 中已指定 C++17 标准

### 运行错误

#### 错误 1: "应用程序无法正常启动"

**症状：**
```
应用程序无法正常启动 (0xc0000135)
```

**解决方法：**
1. 检查 Visual C++ Runtime 是否已安装
   - 下载：https://support.microsoft.com/en-us/help/2977003/the-latest-supported-visual-c-downloads
   - 选择对应 VS 版本的 Runtime
2. 或用 CMake 配置为静态链接：
   - 修改 CMakeLists.txt 中的 MSVC_RUNTIME_LIBRARY

#### 错误 2: "鼠标高亮未显示"

**症状：**
- 运行无错误，但看不到鼠标高亮效果

**检查步骤：**
1. 确认 DWM 已启用：
   ```powershell
   Get-Service dwm | Select Status
   ```
   应输出 "Running"

2. 检查是否在全屏应用 (如游戏) 中
   - 分层窗口在独占模式下被隐藏 (正常行为)
   - 退出全屏游戏后应恢复

3. 查看托盘图标是否存在：
   - 右下角"显示隐藏的图标"
   - 应能看到 MouseHighlighter 图标

4. 检查权限：
   - 以管理员身份运行
   - 右键 → 以管理员身份运行

#### 错误 3: 高 CPU 占用

**症状：**
- 鼠标静止时 CPU 占用 > 5%

**诊断：**
1. 打开任务管理器 → 性能
2. 额外检查是否有其他钩子冲突
3. 查看 config.ini 中 `TargetFPS` 配置

**解决方法：**
1. 降低目标 FPS：
   ```ini
   [Timing]
   TargetFPS=30
   ```
2. 检查系统是否有其他全局钩子工具运行 (截图工具、输入法等)

#### 错误 4: 重复运行时崩溃

**症状：**
- 第一次运行正常，关闭后重新运行崩溃
- 或"钩子已被注册"错误

**解决方法：**
1. 检查是否有残留进程：
   ```powershell
   Get-Process MouseHighlighter -ErrorAction SilentlyContinue
   ```
2. 手动结束进程：
   ```powershell
   Stop-Process -Name MouseHighlighter -Force
   ```
3. 重启应用

### 性能问题

#### 问题 1: 帧率不稳定 (出现卡顿)

**症状：**
- 鼠标移动时间断卡顿

**检查：**
1. CPU 占用是否稳定 (应 ≤ 5%)
2. 磁盘 I/O 是否正常

**解决方法：**
1. 降低渲染目标 FPS：
   ```ini
   TargetFPS=30
   ```
2. 禁用坐标平滑 (仅在需要时启用)：
   ```ini
   [Smoothing]
   Alpha=0.0
   ```

#### 问题 2: 内存占用持续增长

**症状：**
- 运行数小时后内存从 30MB 增长到 100+ MB

**诊断步骤：**
1. 检查 GDI 对象数：
   ```cpp
   // 在监控线程输出中查看
   ```
2. 使用 Performance Monitor (perfmon.exe) 追踪：
   - 打开 → Process → 添加计数器 → GDI Objects

**解决方法：**
- 这通常表示 GDI 资源泄漏 (Developer 问题)
- 报告给开发者并附上内存快照

## 生成发布版本

### 创建可发布的二进制文件

```bash
cd mousehighline
mkdir build_release
cd build_release

# 生成 Release 配置
cmake -G "Visual Studio 16 2019" -A x64 -DCMAKE_BUILD_TYPE=Release ..

# 编译
cmake --build . --config Release

# 输出文件位置
# build_release/Release/MouseHighlighter.exe
```

### 打包应用

```bash
# 创建发布目录
mkdir MouseHighlighter_v1.0.0

# 复制必要文件
copy build_release/Release/MouseHighlighter.exe MouseHighlighter_v1.0.0/
copy README.md MouseHighlighter_v1.0.0/

# 创建压缩包
# (使用 7-Zip、WinRAR 等)
```

## 关键编译标志

### CMake 变量自定义

```bash
# 静态链接 Runtime (推荐发布版)
cmake -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedRelease ..

# 定义 Windows 最低版本
cmake -DWIN32_LEAN_AND_MEAN=ON ..

# 启用链接时优化 (LTO)
cmake -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON ..
```

## 调试技巧

### 启用调试符号

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
```

### 在 Visual Studio 中调试

1. 从 VS 打开 CMake 项目
2. 调试 → 启动调试 (F5)
3. 设置断点，观察变量

### 控制台日志输出

启用监控线程的日志输出 (Config.h 中编译条件)：
```cpp
monitoring.logToFile = true;
```

## 最佳实践

1. **始终编译 Release 版本用于日常使用**
   - Debug 版本性能下降 50-70%

2. **定期清理构建输出**
   ```bash
   rm -r build/
   mkdir build && cd build
   ```

3. **使用 .gitignore 避免检入编译文件**
   - 已提供，无需额外配置

4. **保持 CMake 缓存最新**
   ```bash
   Build → Regenerate CMake Cache
   ```

## 获取帮助

- 检查 README.md 中的"常见问题"
- 查看编译错误的确切行号与文件
- 在 GitHub Issues 报告问题（包括编译环境信息）

## 下一步

编译成功后：
1. [运行应用](#运行错误)
2. 阅读 README.md 了解使用方法
3. 检查 config.ini 的配置选项
4. 右键托盘图标调整设置
