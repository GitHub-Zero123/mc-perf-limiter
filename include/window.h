#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <functional>
#include <string>

// Win11 DWM 常量（SDK 较旧时可能缺失，仅补充宏，不重复定义枚举）
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#  define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#  define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#  define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
// DWMWCP_ROUND = 2, DWMSBT_MAINWINDOW = 2 — 直接以整数传递，避免与新版 SDK 枚举冲突

// 标题栏高度（px，逻辑像素）
inline constexpr int TITLEBAR_HEIGHT = 32;

class Window {
public:
    // 窗口控制回调（由 App 设置）
    using ControlCallback = std::function<void(const std::string& action)>;

    Window();
    ~Window();

    bool create(HINSTANCE hInstance, int nCmdShow);
    void destroy();

    HWND hwnd() const { return hwnd_; }

    // 应用深色/浅色模式到标题栏（DWM 非客户区颜色）
    void applyDarkMode(bool dark);

    // 设置窗口控制回调
    void setControlCallback(ControlCallback cb) { controlCb_ = std::move(cb); }

    // 消息循环
    static int runMessageLoop();

private:
    HWND hwnd_  = nullptr;
    bool dark_  = true;
    ControlCallback controlCb_;
    UINT dpi_   = 96;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                     WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // 无边框窗口命中测试
    LRESULT hitTest(POINT pt) const;

    // 注册窗口类
    static bool registerClass(HINSTANCE hInstance);

    // Win11 特效
    void applyWin11Effects();

    // 缩放辅助
    int scale(int px) const;
};
