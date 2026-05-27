cmake_minimum_required(VERSION 3.15)
project(MouseHighlighter CXX)

# ============================================================
# 设置 C++ 标准
# ============================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Zi /W4 /MTd")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /W4 /MT")

# ============================================================
# 源文件
# ============================================================
set(SOURCES
    src/main.cpp
    src/MouseHighlighter.cpp
    src/Config.cpp
)

set(HEADERS
    include/MouseHighlighter.h
    include/SharedState.h
    include/DataStructures.h
    include/Config.h
)

# ============================================================
# 可执行文件
# ============================================================
add_executable(MouseHighlighter WIN32 ${SOURCES} ${HEADERS})

# ============================================================
# 包含目录
# ============================================================
target_include_directories(MouseHighlighter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ============================================================
# 链接库
# ============================================================
target_link_libraries(MouseHighlighter
    user32
    gdi32
    kernel32
    shell32
    dwmapi
    psapi
)

target_compile_definitions(MouseHighlighter PRIVATE UNICODE _UNICODE)

# ============================================================
# 编译选项
# ============================================================
if(MSVC)
    # Visual Studio 编译选项
    target_compile_options(MouseHighlighter PRIVATE
        /Zc:inline          # 移除不使用的代码
        /Zc:wchar_t         # wchar_t 是内置类型
        /Zc:forScope        # for 循环作用域
        /Zc:throwingNew     # 抛出异常的 new
        /Zc:referenceBinding # const 引用绑定
    )
endif()

# ============================================================
# 运行时
# ============================================================
if(MSVC)
    set_property(TARGET MouseHighlighter PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded$<$<CONFIG:Debug>:Debug>)
endif()

# ============================================================
# 优化选项 (Release 模式)
# ============================================================
if(CMAKE_BUILD_TYPE MATCHES Release)
    target_compile_options(MouseHighlighter PRIVATE /Ox)
endif()
