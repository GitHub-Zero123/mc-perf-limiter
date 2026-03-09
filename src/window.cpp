#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include "window.h"
#include <windows.h>
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM
#include <dwmapi.h>
#include <shellapi.h>  // APPBARDATA / SHAppBarMessage
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
    // WebView2 缩进6px留resize热区，边缝背景色与内容区对齐
    // 深色: #202024 = RGB(32, 32, 36)  浅色: #f3f3f3 = RGB(243, 243, 243)
    // 初始用深色，applyDarkMode() 会动态切换
    wc.hbrBackground = CreateSolidBrush(RGB(32, 32, 36));
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

    // 标准窗口样式（带原生边框和标题栏）
    // 后续通过 DwmExtendFrameIntoClientArea 隐藏原生标题栏内容
    DWORD style   = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    DWORD exStyle = 0;

    hwnd_ = CreateWindowExW(
        exStyle, WC_NAME,
        L"MCPerfLimiter",
        style,
        x, y, w, h,
        nullptr, nullptr, hInstance,
        this    // lpCreateParams → 关联 this 指针
    );

    if (!hwnd_) return false;

    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);

    // Win11 圆角 + 无边框扩展（必须在 ShowWindow 之后）
    applyWin11Effects();

    // 初始深色模式
    applyDarkMode(dark_);

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

    // 不在这里设置 Mica，由 applyMicaEffect 控制
    // 默认禁用系统背景效果
    DWORD backdrop = 1; // DWMSBT_NONE
    DwmSetWindowAttribute(hwnd_,
        DWMWA_SYSTEMBACKDROP_TYPE,
        &backdrop, sizeof(backdrop));

    // 延伸框架到整个窗口（隐藏原生标题栏，保留 Win11 圆角和阴影）
    // 这个设置始终保持，不会在主题切换时改变
    MARGINS margins{ -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hwnd_, &margins);
}

// ─── 毛玻璃效果（Win11 Mica）──────────────────────────────────────────────────

void Window::applyMicaEffect(bool enable, bool dark) {
    // 设置深色/浅色标题栏
    BOOL useDark = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd_,
        DWMWA_USE_IMMERSIVE_DARK_MODE,
        &useDark, sizeof(useDark));

    if (enable) {
        // 启用 Mica 毛玻璃效果
        // DWMSBT_MAINWINDOW = 2（标准 Mica，类似系统设置）
        // DWMSBT_TABBEDWINDOW = 4（Mica Alt，更透明，类似终端）
        DWORD backdrop = 4; // DWMSBT_TABBEDWINDOW
        DwmSetWindowAttribute(hwnd_,
            DWMWA_SYSTEMBACKDROP_TYPE,
            &backdrop, sizeof(backdrop));

        // 隐藏原生标题栏文字：设置标题颜色为透明
        // DWMWA_CAPTION_COLOR = 35
        COLORREF captionColor = 0xFFFFFFFE; // 特殊值表示使用默认/透明
        DwmSetWindowAttribute(hwnd_, 35, &captionColor, sizeof(captionColor));

        // 窗口背景设为空/透明画刷，让 Mica 效果显示
        HBRUSH hBrush = reinterpret_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
        SetClassLongPtrW(hwnd_, GCLP_HBRBACKGROUND,
                         reinterpret_cast<LONG_PTR>(hBrush));
    } else {
        // 禁用 Mica，恢复不透明背景
        DWORD backdrop = 1; // DWMSBT_NONE
        DwmSetWindowAttribute(hwnd_,
            DWMWA_SYSTEMBACKDROP_TYPE,
            &backdrop, sizeof(backdrop));

        // 恢复实色背景
        COLORREF bgColor = dark ? RGB(32, 32, 36) : RGB(243, 243, 243);
        HBRUSH hBrush = CreateSolidBrush(bgColor);
        HBRUSH hOld = reinterpret_cast<HBRUSH>(
            SetClassLongPtrW(hwnd_, GCLP_HBRBACKGROUND,
                             reinterpret_cast<LONG_PTR>(hBrush)));
        if (hOld && hOld != reinterpret_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH))) {
            DeleteObject(hOld);
        }
    }

    // 强制重新计算非客户区并重绘
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

// ─── 深色/浅色模式 ────────────────────────────────────────────────────────────

void Window::applyDarkMode(bool dark) {
    dark_ = dark;
    BOOL val = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd_,
        DWMWA_USE_IMMERSIVE_DARK_MODE,
        &val, sizeof(val));

    // 每次主题切换都重新延伸帧到整个客户区，防止原生标题栏重新出现
    MARGINS margins{ -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hwnd_, &margins);

    // 同步更新窗口背景刷颜色（与 WebView2 缩进边缝 + 内容区对齐）
    // 深色: #202024 = RGB(32,32,36)  浅色: #f3f3f3 = RGB(243,243,243)
    COLORREF bgColor = dark ? RGB(32, 32, 36) : RGB(243, 243, 243);
    HBRUSH hBrush = CreateSolidBrush(bgColor);
    HBRUSH hOld = reinterpret_cast<HBRUSH>(
        SetClassLongPtrW(hwnd_, GCLP_HBRBACKGROUND,
                         reinterpret_cast<LONG_PTR>(hBrush)));
    if (hOld && hOld != reinterpret_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH))) {
        DeleteObject(hOld);
    }

    // 强制重新计算非客户区，确保 WM_NCCALCSIZE 重新移除标题栏高度
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    InvalidateRect(hwnd_, nullptr, TRUE);
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

    // ── 非客户区命中测试 ─────────────────────────────────────────
    // WM_NCCALCSIZE 移除了原生边框，DefWindowProc 无法判断左/右/底 resize 区域
    // 必须手动在 HTCLIENT 之前插入边框检测；顶部由 WebView2 drag-region 处理，不能拦截
    case WM_NCHITTEST: {
        // 最大化状态下不需要边框 resize
        if (IsZoomed(hwnd_)) return DefWindowProcW(hwnd_, msg, wParam, lParam);

        RECT rc{};
        GetWindowRect(hwnd_, &rc);
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

        // 边框感应宽度（物理像素）
        constexpr int BORDER = 8;

        bool onLeft   = mx <  rc.left   + BORDER;
        bool onRight  = mx >  rc.right  - BORDER;
        bool onBottom = my >  rc.bottom - BORDER;

        // 四角（仅底部相关）
        if (onBottom && onLeft)  return HTBOTTOMLEFT;
        if (onBottom && onRight) return HTBOTTOMRIGHT;
        // 单边
        if (onLeft)              return HTLEFT;
        if (onRight)             return HTRIGHT;
        if (onBottom)            return HTBOTTOM;

        // 其余交给 DefWindowProc（含顶部 HTTOP、拖拽区域等）
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }

    // ── 激活时阻止系统绘制非客户区 ────────────────────────────
    case WM_NCACTIVATE:
        return DefWindowProcW(hwnd_, msg, wParam, -1);

    // ── 非客户区计算（去掉原生标题栏，保留边框和圆角）──────────
    // 参考 Verum 项目实现：WS_OVERLAPPEDWINDOW + DwmExtendFrameIntoClientArea
    case WM_NCCALCSIZE: {
        if (wParam) {
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            auto& rect = params->rgrc[0];

            // 最大化时需要补回系统预留的边框宽度，防止遮住任务栏
            if (IsZoomed(hwnd_)) {
                int frame_x = GetSystemMetrics(SM_CXFRAME) +
                              GetSystemMetrics(SM_CXPADDEDBORDER);
                int frame_y = GetSystemMetrics(SM_CYFRAME) +
                              GetSystemMetrics(SM_CXPADDEDBORDER);
                rect.left   += frame_x;
                rect.right  -= frame_x;
                rect.top    += frame_y;
                rect.bottom -= frame_y;
            } else {
                // 普通窗口：仅移除顶部标题栏，保留 1px 给 DWM 圆角
                rect.top += 1;
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
