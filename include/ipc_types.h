#pragma once
// IPC 消息类型定义 — 前后端通信协议
// JS → C++: { "cmd": "...", "payload": {...} }
// C++ → JS: { "event": "...", "data": {...} }

#include <string>
#include <vector>
#include <cstdint>

namespace ipc {

// ─── JS → C++ 命令 ─────────────────────────────────────────
inline constexpr const char* CMD_GET_PROCESS_LIST = "getProcessList";
inline constexpr const char* CMD_SET_LIMIT        = "setLimit";
inline constexpr const char* CMD_REMOVE_LIMIT     = "removeLimit";
inline constexpr const char* CMD_SET_THEME        = "setTheme";
inline constexpr const char* CMD_WINDOW_CONTROL   = "windowControl";
inline constexpr const char* CMD_GET_SYSTEM_THEME = "getSystemTheme";

// ─── C++ → JS 事件 ─────────────────────────────────────────
inline constexpr const char* EVT_PROCESS_LIST  = "processList";
inline constexpr const char* EVT_LIMIT_APPLIED = "limitApplied";
inline constexpr const char* EVT_LIMIT_REMOVED = "limitRemoved";
inline constexpr const char* EVT_STATS_UPDATE  = "statsUpdate";
inline constexpr const char* EVT_THEME_CHANGED = "themeChanged";
inline constexpr const char* EVT_ERROR         = "error";

// ─── 数据结构 ───────────────────────────────────────────────

enum class Theme {
    Dark,
    Light,
    System
};

inline const char* theme_to_str(Theme t) {
    switch (t) {
        case Theme::Dark:   return "dark";
        case Theme::Light:  return "light";
        default:            return "system";
    }
}

inline Theme str_to_theme(const std::string& s) {
    if (s == "dark")  return Theme::Dark;
    if (s == "light") return Theme::Light;
    return Theme::System;
}

struct ProcessInfo {
    uint32_t    pid;
    std::string name;        // 进程名
    std::string exePath;     // 完整路径
    double      cpuUsage;    // CPU 使用率 %
    double      gpuUsage;    // GPU 使用率 %
    bool        cpuLimited;  // 是否已应用 CPU 限制
    bool        gpuLimited;  // 是否已应用 GPU 限制
    uint32_t    cpuLimitPct; // CPU 限制百分比
    uint32_t    gpuLimitPct; // GPU 限制百分比
};

struct LimitRequest {
    uint32_t pid;
    int      cpuPercent; // -1 表示不限制
    int      gpuPercent; // -1 表示不限制
};

enum class WindowAction {
    Minimize,
    MaximizeRestore,
    Close
};

inline WindowAction str_to_window_action(const std::string& s) {
    if (s == "minimize")        return WindowAction::Minimize;
    if (s == "maximizeRestore") return WindowAction::MaximizeRestore;
    return WindowAction::Close;
}

} // namespace ipc
