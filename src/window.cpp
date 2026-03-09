#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "window.h"
#include <windows.h>
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM
#include <dwmapi.h>
#include <stdexcept>
#include <string>

// 窗口类名
static constexpr const wchar_t* WC_NAME = L"MCPerfLimiterWnd";

// 窗口初始尺寸
static constexpr int INIT_WIDTH  = 960;
static constexpr int INIT_HEIGHT = 640;
static constexpr int MIN_WIDTH   = 720;
static constexpr int MIN_HEIGHT  = 480;

// ─── 构造/析构 ────────────────────────────────────────────────────────────────

Window::Window() = default;

Window::~Window() {
    destroy();
}

void Window::destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

// ─── 注册窗口类 ───────────────────────────────────────────────────────────────

bool Window::registerClass(HINSTANCE hInstance) {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    // WebView2 缩进6px留resize热区，边缝用深色背景填充（与UI #1a1a1d对齐）
    // RGB(26, 26, 29) ≈ #1a1a1d
    wc.hbrBackground = CreateSolidBrush(RGB(26, 26, 29));
    wc.lpszClassName = WC_NAME;
    wc.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(1));
    wc.hIconSm       = wc.hIcon;

    return RegisterClassExW(&wc) != 0 ||
           GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

// ─── 创建窗口 ─────────────────────────────────────────────────────────────────

bool Window::create(HINSTANCE hInstance, int nCmdShow) {
    if (!registerClass(hInstance)) return false;

    // 读取主显示器 DPI（SetProcessDpiAwarenessContext 已在 WinMain 最早设置）
    dpi_ = GetDpiForSystem();

    int w = MulDiv(INIT_WIDTH,  static_cast<int>(dpi_), 96);
    int h = MulDiv(INIT_HEIGHT, static_cast<int>(dpi_), 96);

    // 计算居中位置
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - w) / 2;
    int y = (screenH - h) / 2;

    // 无边框可调整大小窗口
    DWORD style   = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX |
                    WS_MAXIMIZEBOX | WS_CLIPCHILDREN;
    DWORD exStyle = WS_EX_APPWINDOW;

    hwnd_ = CreateWindowExW(
        exStyle, WC_NAME,
        L"MCPerfLimiter",
        style,
        x, y, w, h,
        nullptr, nullptr, hInstance,
        this    // lpCreateParams → 关联 this 指针
    );

    if (!hwnd_) return false;

    // Win11 圆角 + Mica 背景
    applyWin11Effects();

    // 初始深色模式
    applyDarkMode(dark_);

    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);
    return true;
}

// ─── Win11 特效 ───────────────────────────────────────────────────────────────

void Window::applyWin11Effects() {
    // 圆角（Win11 22000+）
    // 使用整数值 2 = DWMWCP_ROUND，避免与 SDK 枚举命名冲突
    DWORD corner = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd_,
        DWMWA_WINDOW_CORNER_PREFERENCE,
        &corner, sizeof(corner));

    // Mica 背景（Win11 22621+）
    // 使用整数值 2 = DWMSBT_MAINWINDOW
    DWORD backdrop = 2; // DWMSBT_MAINWINDOW
    DwmSetWindowAttribute(hwnd_,
        DWMWA_SYSTEMBACKDROP_TYPE,
        &backdrop, sizeof(backdrop));

    // 延伸框架到整个窗口（让 DWM 阴影保留）
    MARGINS margins{ -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hwnd_, &margins);
}

// ─── 深色/浅色模式 ────────────────────────────────────────────────────────────

void Window::applyDarkMode(bool dark) {
    dark_ = dark;
    BOOL val = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd_,
        DWMWA_USE_IMMERSIVE_DARK_MODE,
        &val, sizeof(val));
}

// ─── 缩放辅助 ─────────────────────────────────────────────────────────────────

int Window::scale(int px) const {
    return MulDiv(px, static_cast<int>(dpi_), 96);
}

// ─── 消息循环 ─────────────────────────────────────────────────────────────────

int Window::runMessageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

// ─── WndProc（静态入口）────────────────────────────────────────────────────────

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg,
                                  WPARAM wParam, LPARAM lParam)
{
    Window* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<Window*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) return self->handleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ─── 消息处理 ─────────────────────────────────────────────────────────────────

LRESULT Window::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    // ── 最小窗口尺寸 ──────────────────────────────────────────
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = scale(MIN_WIDTH);
        mmi->ptMinTrackSize.y = scale(MIN_HEIGHT);
        return 0;
    }

    // ── 无边框命中测试 ─────────────────────────────────────────
    // WM_NCCALCSIZE 返回0去掉了所有非客户区，DefWindowProc 无法
    // 自动识别边缘。需手动判断 resize 热区（4边 + 4角）。
    case WM_NCHITTEST: {
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);

        RECT rc{};
        GetWindowRect(hwnd_, &rc);

        // resize 感应宽度（物理像素，与 WebView2 缩进量对齐）
        const int RB = scale(6);

        bool onLeft   = pt.x <  rc.left   + RB;
        bool onRight  = pt.x >= rc.right  - RB;
        bool onTop    = pt.y <  rc.top    + RB;
        bool onBottom = pt.y >= rc.bottom - RB;

        if (IsZoomed(hwnd_)) {
            // 最大化时不允许 resize
        } else {
            if (onLeft  && onTop)    return HTTOPLEFT;
            if (onRight && onTop)    return HTTOPRIGHT;
            if (onLeft  && onBottom) return HTBOTTOMLEFT;
            if (onRight && onBottom) return HTBOTTOMRIGHT;
            if (onLeft)              return HTLEFT;
            if (onRight)             return HTRIGHT;
            if (onTop)               return HTTOP;
            if (onBottom)            return HTBOTTOM;
        }

        // 其余区域交给 WebView2（含 CSS -webkit-app-region:drag）
        return HTCLIENT;
    }

    // ── 激活时阻止系统绘制非客户区 ────────────────────────────
    case WM_NCACTIVATE:
        return DefWindowProcW(hwnd_, msg, wParam, -1);

    // ── 非客户区计算（去掉标准边框）──────────────────────────
    case WM_NCCALCSIZE: {
        if (wParam) {
            if (IsZoomed(hwnd_)) {
                auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                HMONITOR hMon = MonitorFromWindow(hwnd_,
                                    MONITOR_DEFAULTTOPRIMARY);
                MONITORINFO mi{ sizeof(mi) };
                GetMonitorInfoW(hMon, &mi);
                p->rgrc[0] = mi.rcWork;
            }
            return 0;
        }
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }

    // ── DPI 变化（拖到不同密度显示器）────────────────────────
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wParam); // 新 DPI（X/Y 相同）
        // 使用系统推荐的新位置/尺寸
        auto* rc = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd_, nullptr,
                     rc->left, rc->top,
                     rc->right  - rc->left,
                     rc->bottom - rc->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    // ── 系统主题变化（通知 App 层）────────────────────────────
    case WM_SETTINGCHANGE:
        // 不再 PostMessage 以避免递归，直接让 App 订阅
        // App 通过 SetWindowSubclass 或在 WndProc 后处理
        return DefWindowProcW(hwnd_, msg, wParam, lParam);

    // ── 销毁 ───────────────────────────────────────────────────
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }
}

// ─── 无边框命中测试 ──────────────────────────────────────────────────────────
// 注意：已启用 ICoreWebView2Settings9::IsNonClientRegionSupportEnabled
// CSS -webkit-app-region:drag 会由 WebView2 自动上报 HTCAPTION
// 此函数仅用于处理默认的 HTCLIENT 情况，边缘缩放由 DefWindowProc 负责

LRESULT Window::hitTest(POINT /*pt*/) const {
    // WebView2 + IsNonClientRegionSupportEnabled 已接管标题栏拖拽
    // 直接返回 HTCLIENT，让 WebView2 内部处理所有鼠标事件
    return HTCLIENT;
}
