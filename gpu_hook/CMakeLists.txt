cmake_minimum_required(VERSION 3.25)

project(GpuHook LANGUAGES CXX)

# ── 关键：在本子目录范围内清空 Debug 配置的冲突标志 ──────────────────────────
# gpu_hook.dll 注入到 MC（Release CRT）进程，必须不带 /RTC1（Debug CRT 运行时检查）
# 此设置仅影响本 subdirectory，不影响主程序
if(MSVC)
    # 移除 /RTC1（与 /O2 冲突），保留 /Zi（调试信息可选保留）
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    # 移除 /Od（Debug 不优化），需要 /O2
    string(REPLACE "/Od"   "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

add_library(gpu_hook SHARED gpu_hook.cpp)

target_compile_features(gpu_hook PRIVATE cxx_std_17)

if(MSVC)
    # 始终使用 /MD（Release 动态 CRT），无论主程序 Debug/Release
    set_target_properties(gpu_hook PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
    )

    target_compile_options(gpu_hook PRIVATE
        /utf-8
        /EHsc
        /W3
        /O2       # 始终优化，确保不依赖 Debug CRT 特性
    )
endif()

target_compile_definitions(gpu_hook PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    NDEBUG    # 禁用 assert，确保行为与 Release 一致
)

# 无需额外链接：所有 API 来自 kernel32/user32（windows.h 已覆盖）

# 输出到与主程序相同的 bin/ 目录
set_target_properties(gpu_hook PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    OUTPUT_NAME "gpu_hook"
)
