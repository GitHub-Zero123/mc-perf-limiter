#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include "app.h"
#include <windows.h>
#include <nlohmann/json.hpp>
#include <string>
#include <stdexcept>

using json = nlohmann::json;

// ─── 宏：UI 目录（由 CMake 注入）────────────────────────────────────────────

#ifndef MC_PERF_LIMITER_UI_DIR
#  define MC_PERF_LIMITER_UI_DIR ""
#endif

// ─── 辅助：UTF-8 → UTF-16 ────────────────────────────────────────────────────

static std::wstring to_wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0,
                                 s.c_str(), static_cast<int>(s.size()),
                                 nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0,
                        s.c_str(), static_cast<int>(s.size()),
                        &w[0], n);
    return w;
}

// ─── 构造/析构 ────────────────────────────────────────────────────────────────

App::App()
    : window_       (std::make_unique<Window>())
    , bridge_       (std::make_unique<WebViewBridge>())
    , scanner_      (std::make_unique<ProcessScanner>())
    , cpuLimiter_   (std::make_unique<CpuLimiter>())
    , gpuLimiter_   (std::make_unique<GpuLimiter>())
    , themeDetector_(std::make_unique<ThemeDetector>())
{}

App::~App() {
    scanner_->stop();
    cpuLimiter_->removeAll();
    gpuLimiter_->removeAll();
}

// ─── 解析 UI 目录 ─────────────────────────────────────────────────────────────

bool App::resolveUiDir() {
    std::string dir = MC_PERF_LIMITER_UI_DIR;
    if (dir.empty()) return false;
    uiDir_ = to_wide(dir);
    return true;
}

// ─── 运行 ─────────────────────────────────────────────────────────────────────

int App::run(HINSTANCE hInstance, int nCmdShow) {
    resolveUiDir();

    // ── 主题检测器 ────────────────────────────────────────────
    themeDetector_->setCallback(
        [this](ipc::Theme t) { onThemeChange(t); });

    // ── 创建窗口 ──────────────────────────────────────────────
    window_->setControlCallback(
        [this](const std::string& action) {
            handleWindowControl(json{ {"action", action} });
        });

    if (!window_->create(hInstance, nCmdShow)) {
        MessageBoxW(nullptr, L"窗口创建失败", L"MCPerfLimiter", MB_ICONERROR);
        return -1;
    }

    // 初始应用主题到 DWM 标题栏颜色
    ipc::Theme effective = ThemeDetector::resolveTheme(
        themeDetector_->userPreference());
    window_->applyDarkMode(effective == ipc::Theme::Dark);

    // ── WebView2 初始化 ───────────────────────────────────────
    bridge_->setMessageHandler(
        [this](const std::string& msg) { onMessage(msg); });

    bridge_->init(window_->hwnd(), uiDir_,
        [this]() {
            // WebView2 就绪后：推送初始主题
            ipc::Theme t = ThemeDetector::resolveTheme(
                themeDetector_->userPreference());
            sendEvent(ipc::EVT_THEME_CHANGED,
                { {"theme", ipc::theme_to_str(t)} });

            // 启动进程扫描器
            scanner_->start(
                [this](const std::vector<ipc::ProcessInfo>& list) {
                    onProcessListUpdate(list);
                }, 1000);
        });

    // ── 窗口大小变化时 resize WebView ─────────────────────────
    // 通过 WM_SIZE 子类化或直接在 WndProc 中处理
    // 此处通过子类化 hwnd_ 监听 WM_SIZE / WM_SETTINGCHANGE
    HWND hwnd = window_->hwnd();
    SetWindowSubclass(hwnd, [](HWND hWnd, UINT msg, WPARAM wp, LPARAM lp,
                                UINT_PTR /*id*/, DWORD_PTR data) -> LRESULT {
        App* app = reinterpret_cast<App*>(data);
        if (msg == WM_SIZE) {
            int w = LOWORD(lp);
            int ht = HIWORD(lp);
            if (app->bridge_) app->bridge_->resize(w, ht);
        } else if (msg == WM_SETTINGCHANGE) {
            if (app->themeDetector_)
                app->themeDetector_->onSettingChange(wp, lp);
        }
        return DefSubclassProc(hWnd, msg, wp, lp);
    }, 1, reinterpret_cast<DWORD_PTR>(this));

    return Window::runMessageLoop();
}

// ─── 接收 JS 消息 ─────────────────────────────────────────────────────────────

void App::onMessage(const std::string& jsonStr) {
    try {
        json j = json::parse(jsonStr);
        std::string cmd = j.value("cmd", "");
        json payload = j.value("payload", json::object());

        if      (cmd == ipc::CMD_GET_PROCESS_LIST) handleGetProcessList();
        else if (cmd == ipc::CMD_SET_LIMIT)        handleSetLimit(payload);
        else if (cmd == ipc::CMD_REMOVE_LIMIT)     handleRemoveLimit(payload);
        else if (cmd == ipc::CMD_SET_THEME)        handleSetTheme(payload);
        else if (cmd == ipc::CMD_WINDOW_CONTROL)   handleWindowControl(payload);
        else if (cmd == ipc::CMD_GET_SYSTEM_THEME) handleGetSystemTheme();
    } catch (const std::exception& e) {
        sendError(e.what());
    }
}

// ─── 命令处理 ─────────────────────────────────────────────────────────────────

void App::handleGetProcessList() {
    auto list = scanner_->scanOnce();
    onProcessListUpdate(list);
}

void App::handleSetLimit(const json& payload) {
    uint32_t pid        = payload.value("pid", 0u);
    int      cpuPercent = payload.value("cpu", -1);
    int      gpuPercent = payload.value("gpu", -1);

    if (pid == 0) { sendError("Invalid PID"); return; }

    bool cpuOk = true, gpuOk = true;
    if (cpuPercent >= 1 && cpuPercent <= 99)
        cpuOk = cpuLimiter_->applyLimit(pid, static_cast<uint32_t>(cpuPercent));
    if (gpuPercent >= 1 && gpuPercent <= 99)
        gpuOk = gpuLimiter_->applyLimit(pid, static_cast<uint32_t>(gpuPercent));

    sendEvent(ipc::EVT_LIMIT_APPLIED, {
        {"pid",     pid},
        {"cpu_ok",  cpuOk},
        {"gpu_ok",  gpuOk},
        {"cpu_pct", cpuPercent},
        {"gpu_pct", gpuPercent}
    });
}

void App::handleRemoveLimit(const json& payload) {
    uint32_t pid = payload.value("pid", 0u);
    if (pid == 0) { sendError("Invalid PID"); return; }

    cpuLimiter_->removeLimit(pid);
    gpuLimiter_->removeLimit(pid);

    sendEvent(ipc::EVT_LIMIT_REMOVED, { {"pid", pid} });
}

void App::handleSetTheme(const json& payload) {
    std::string themeStr = payload.value("theme", "system");
    ipc::Theme t = ipc::str_to_theme(themeStr);
    themeDetector_->setUserPreference(t);

    // 立即更新 DWM 颜色
    ipc::Theme effective = ThemeDetector::resolveTheme(t);
    window_->applyDarkMode(effective == ipc::Theme::Dark);
}

void App::handleWindowControl(const json& payload) {
    std::string action = payload.value("action", "");
    HWND hwnd = window_->hwnd();

    if      (action == "minimize")        SendMessageW(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    else if (action == "maximizeRestore") {
        if (IsZoomed(hwnd)) SendMessageW(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
        else                SendMessageW(hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
    }
    else if (action == "close")           SendMessageW(hwnd, WM_CLOSE, 0, 0);
}

void App::handleGetSystemTheme() {
    ipc::Theme effective = ThemeDetector::resolveTheme(
        themeDetector_->userPreference());
    sendEvent(ipc::EVT_THEME_CHANGED,
        { {"theme", ipc::theme_to_str(effective)} });
}

// ─── 向 JS 发送事件 ───────────────────────────────────────────────────────────

void App::sendEvent(const std::string& event, const json& data) {
    json msg;
    msg["event"] = event;
    msg["data"]  = data;
    bridge_->postMessage(msg.dump());
}

void App::sendError(const std::string& message) {
    sendEvent(ipc::EVT_ERROR, { {"message", message} });
}

// ─── 子系统回调 ───────────────────────────────────────────────────────────────

void App::onProcessListUpdate(const std::vector<ipc::ProcessInfo>& list) {
    json arr = json::array();
    for (const auto& p : list) {
        arr.push_back({
            {"pid",        p.pid},
            {"name",       p.name},
            {"exePath",    p.exePath},
            {"cpuUsage",   p.cpuUsage},
            {"gpuUsage",   p.gpuUsage},
            {"cpuLimited", cpuLimiter_->hasLimit(p.pid)},
            {"gpuLimited", gpuLimiter_->hasLimit(p.pid)},
            {"cpuLimitPct",cpuLimiter_->getLimit(p.pid)},
            {"gpuLimitPct",gpuLimiter_->getLimit(p.pid)}
        });
    }
    sendEvent(ipc::EVT_PROCESS_LIST, arr);
}

void App::onThemeChange(ipc::Theme effectiveTheme) {
    // 更新 DWM 颜色
    window_->applyDarkMode(effectiveTheme == ipc::Theme::Dark);
    // 通知前端
    sendEvent(ipc::EVT_THEME_CHANGED,
        { {"theme", ipc::theme_to_str(effectiveTheme)} });
}
