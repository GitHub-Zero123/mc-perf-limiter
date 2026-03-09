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
inline constexpr const char* CMD_OPEN_URL         = "openUrl";

// ─── C++ → JS 事件 ─────────────────────────────────────────
inline constexpr const char* EVT_PROCESS_LIST     = "processList";
inline constexpr const char* EVT_LIMIT_APPLIED    = "limitApplied";
inline constexpr const char* EVT_LIMIT_REMOVED    = "limitRemoved";
inline constexpr const char* EVT_STATS_UPDATE     = "statsUpdate";
inline constexpr const char* EVT_THEME_CHANGED    = "themeChanged";
inline constexpr const char* EVT_WINDOW_STATE     = "windowState";
inline constexpr const char* EVT_ERROR            = "error";

// ─── 数据结构 ───────────────────────────────────────────────

enum class Theme {
    Dark,
    Light,
    System
};

inline const char* theme_to_str(Theme t) {
    switch (t) {
        case Theme::Dark:  return "dark";
        case Theme::Light: return "light";
        default:           return "system";
    }
}

inline Theme str_to_theme(const std::string& s) {
    if (s == "dark")  return Theme::Dark;
    if (s == "light") return Theme::Light;
    return Theme::System;
}

struct ProcessInfo {
    uint32_t    pid;
    std::string name;           // 进程名
    std::string exePath;        // 完整路径
    double      cpuUsage;       // CPU 使用率 %
    double      gpuUsage;       // GPU 使用率 %
    uint64_t    memoryUsage;    // 内存使用量（字节）
    uint64_t    ioReadBytes;    // IO 读取字节/秒
    uint64_t    ioWriteBytes;   // IO 写入字节/秒
    uint64_t    netRecvBytes;   // 网络接收字节/秒（暂未实现）
    uint64_t    netSendBytes;   // 网络发送字节/秒（暂未实现）
    bool        cpuLimited;     // 是否已应用 CPU 限制
    bool        gpuLimited;     // 是否已应用 GPU 限制
    bool        memLimited;     // 是否已应用内存限制
    bool        ioLimited;      // 是否已应用 IO 优先级限制
    uint32_t    cpuLimitPct;    // CPU 限制百分比
    uint32_t    gpuLimitPct;    // GPU 限制百分比
    uint64_t    memLimitBytes;  // 内存限制（字节，0 = 未限制）
    std::string iconBase64;     // exe 图标 Base64 PNG（可空）
};

struct LimitRequest {
    uint32_t pid;
    int      cpuPercent;    // -1 表示不限制
    int      gpuPercent;    // -1 表示不限制
    int64_t  memLimitMB;    // -1 表示不限制，0 表示移除，>0 表示限制 MB
    bool     ioLowPriority; // true = 低 IO 优先级，false = 正常
    bool     hasIo;         // 是否携带 IO 设置
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
