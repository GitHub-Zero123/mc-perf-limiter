#pragma once
#include "window.h"
#include "webview_bridge.h"
#include "process_scanner.h"
#include "cpu_limiter.h"
#include "gpu_limiter.h"
#include "theme_detector.h"
#include "ipc_types.h"
#include <nlohmann/json.hpp>
#include <string>
#include <memory>

// 应用主类 — 协调各子系统
class App {
public:
    App();
    ~App();

    // 初始化并运行（返回退出码）
    int run(HINSTANCE hInstance, int nCmdShow);

private:
    std::unique_ptr<Window>         window_;
    std::unique_ptr<WebViewBridge>  bridge_;
    std::unique_ptr<ProcessScanner> scanner_;
    std::unique_ptr<CpuLimiter>     cpuLimiter_;
    std::unique_ptr<GpuLimiter>     gpuLimiter_;
    std::unique_ptr<ThemeDetector>  themeDetector_;

    std::wstring uiDir_;

    // ── IPC 分发 ──────────────────────────────────────────────
    // 处理来自 JS 的消息
    void onMessage(const std::string& jsonStr);

    // 各命令处理
    void handleGetProcessList();
    void handleSetLimit(const nlohmann::json& payload);
    void handleRemoveLimit(const nlohmann::json& payload);
    void handleSetTheme(const nlohmann::json& payload);
    void handleWindowControl(const nlohmann::json& payload);
    void handleGetSystemTheme();

    // ── 向 JS 发送事件 ────────────────────────────────────────
    void sendEvent(const std::string& event, const nlohmann::json& data);
    void sendError(const std::string& message);

    // ── 子系统回调 ────────────────────────────────────────────
    void onProcessListUpdate(const std::vector<ipc::ProcessInfo>& list);
    void onThemeChange(ipc::Theme effectiveTheme);

    // 初始化 UI 目录路径
    bool resolveUiDir();
};
