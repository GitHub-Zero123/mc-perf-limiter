cmake_minimum_required(VERSION 3.25)

project(MCPerfLimiter VERSION 0.1.0 LANGUAGES CXX)

if(MSVC)
    add_compile_options(/utf-8 /EHsc /W4 /permissive-)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party")

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
    "${THIRD_PARTY_DIR}"
)

set(WEBVIEW2_DIR "${THIRD_PARTY_DIR}/webview2")
add_library(webview2 INTERFACE)
target_include_directories(webview2 INTERFACE "${WEBVIEW2_DIR}/include")
target_link_libraries(webview2 INTERFACE
    "${WEBVIEW2_DIR}/lib/x64/WebView2LoaderStatic.lib"
)

add_subdirectory("${THIRD_PARTY_DIR}/nfd" EXCLUDE_FROM_ALL)

file(GLOB_RECURSE MC_PERF_LIMITER_SOURCES
    "src/*.cpp"
)

add_executable(MCPerfLimiter WIN32 ${MC_PERF_LIMITER_SOURCES})
target_compile_features(MCPerfLimiter PRIVATE cxx_std_20)

# 包含目录
target_include_directories(MCPerfLimiter PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
)

# 链接依赖
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
)

target_compile_definitions(MCPerfLimiter PRIVATE
    MC_PERF_LIMITER_UI_DIR="${CMAKE_SOURCE_DIR}/ui/dist"
    MC_PERF_LIMITER_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    MC_PERF_LIMITER_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    MC_PERF_LIMITER_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

set_target_properties(MCPerfLimiter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

message(STATUS "[MCPerfLimiter] Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "[MCPerfLimiter] UI dir: ${CMAKE_SOURCE_DIR}/ui/dist")
message(STATUS "[MCPerfLimiter] Third party: ${THIRD_PARTY_DIR}")
