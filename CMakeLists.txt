cmake_minimum_required(VERSION 3.25)

project(MCPerfLimiter VERSION 0.1.0 LANGUAGES CXX)

if(MSVC)
    add_compile_options(/utf-8 /EHsc /W4 /permissive-)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party")
set(RESOURCES_DIR   "${CMAKE_SOURCE_DIR}/resources")
set(UI_DIR          "${CMAKE_SOURCE_DIR}/ui")
set(UI_DIST_DIR     "${CMAKE_SOURCE_DIR}/ui/dist")

# nlohmann/json（header-only）
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE "${THIRD_PARTY_DIR}")

# WebView2 SDK
set(WEBVIEW2_DIR "${THIRD_PARTY_DIR}/webview2")
add_library(webview2 INTERFACE)
target_include_directories(webview2 INTERFACE "${WEBVIEW2_DIR}/include")
target_link_libraries(webview2 INTERFACE
    "${WEBVIEW2_DIR}/lib/x64/WebView2LoaderStatic.lib"
)

# nativefiledialog-extended
add_subdirectory("${THIRD_PARTY_DIR}/nfd" EXCLUDE_FROM_ALL)

file(GLOB_RECURSE MC_PERF_LIMITER_SOURCES "src/*.cpp")

add_executable(MCPerfLimiter WIN32 ${MC_PERF_LIMITER_SOURCES})
target_compile_features(MCPerfLimiter PRIVATE cxx_std_20)

target_include_directories(MCPerfLimiter PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${UI_DIR}"                 # 使 Resource/Resource.hpp 可 #include（ui/Resource/Resource.hpp）
)

target_link_libraries(MCPerfLimiter PRIVATE
    nlohmann_json
    webview2
    nfd
    # Win32 系统库
    user32
    gdi32
    ole32
    oleaut32
    shlwapi
    version
    dwmapi
    comctl32    # SetWindowSubclass
    pdh         # CPU/GPU 性能计数器
    psapi       # GetProcessImageFileName
)

target_compile_definitions(MCPerfLimiter PRIVATE
    MC_PERF_LIMITER_UI_DIR="${UI_DIST_DIR}"
    MC_PERF_LIMITER_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    MC_PERF_LIMITER_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    MC_PERF_LIMITER_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    # 抑制 Windows 头文件 min/max 宏
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    # Unicode
    UNICODE
    _UNICODE
)

# MSVC 链接器支持 /MANIFEST:EMBED + /MANIFESTINPUT 直接嵌入清单
# if(MSVC)
#     target_link_options(MCPerfLimiter PRIVATE
#         "/MANIFEST:EMBED"
#         "/MANIFESTINPUT:${RESOURCES_DIR}/app.manifest"
#     )
# endif()

set_target_properties(MCPerfLimiter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# 构建前运行 generate.py 生成内联资源
# 脚本位置：ui/generate.py
# 扫描目录：ui/dist/
# 输出文件：ui/Resource/Resource.hpp（命名空间 MCPerfLimiter::Resource）
find_program(PYTHON_EXE NAMES python3 python)
if(PYTHON_EXE)
    add_custom_command(
        TARGET MCPerfLimiter PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "[MCPerfLimiter] Generating inline resources..."
        COMMAND ${PYTHON_EXE} "${UI_DIR}/generate.py"
        WORKING_DIRECTORY "${UI_DIR}"
        COMMENT "Running ui/generate.py (ui/dist → ui/Resource/Resource.hpp)"
        VERBATIM
    )
else()
    message(WARNING "[MCPerfLimiter] Python not found — skipping resource generation")
endif()

# 诊断信息
message(STATUS "[MCPerfLimiter] Version:     ${PROJECT_VERSION}")
message(STATUS "[MCPerfLimiter] Build type:  ${CMAKE_BUILD_TYPE}")
message(STATUS "[MCPerfLimiter] UI dist dir: ${UI_DIST_DIR}")
message(STATUS "[MCPerfLimiter] Resources:   ${RESOURCES_DIR}")
message(STATUS "[MCPerfLimiter] Third party: ${THIRD_PARTY_DIR}")
