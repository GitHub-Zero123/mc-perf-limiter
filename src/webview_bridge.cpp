#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include "webview_bridge.h"
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <shlobj.h>
#include <stdexcept>
#include <string>
#include <algorithm>

// Release 模式：引入由 ui/generate.py 生成的内联资源
// 生成命令：python ui/generate.py  （CMake PRE_BUILD 自动调用）
// 生成目标：ui/Resource/Resource.hpp
// 命名空间：MCPerfLimiter::Resource
// CMakeLists.txt 已将 ui/ 加入 include 目录
#if !defined(_DEBUG) && __has_include("Resource/Resource.hpp")
#  include "Resource/Resource.hpp"
#  define MC_USE_INLINE_RESOURCES 1
#endif

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

// ─── 辅助：UTF-8 ↔ UTF-16 ────────────────────────────────────────────────────

static std::wstring utf8_to_wide(const std::string& s) {
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

static std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0,
                                 w.c_str(), static_cast<int>(w.size()),
                                 nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0,
                        w.c_str(), static_cast<int>(w.size()),
                        &s[0], n, nullptr, nullptr);
    return s;
}

// ─── 构造/析构 ────────────────────────────────────────────────────────────────

WebViewBridge::WebViewBridge() = default;

WebViewBridge::~WebViewBridge() {
    if (controller_) {
        controller_->Close();
        controller_.Reset();
    }
    webview_.Reset();
    env_.Reset();
}

// ─── 初始化 ───────────────────────────────────────────────────────────────────

bool WebViewBridge::init(HWND hwnd, const std::wstring& uiDir,
                          std::function<void()> onReady)
{
    hwnd_  = hwnd;
    uiDir_ = uiDir;

    // WebView2 用户数据目录：%APPDATA%\MCPerfLimiter\WebView2
    PWSTR appDataRaw = nullptr;
    SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appDataRaw);
    std::wstring userDataDir =
        std::wstring(appDataRaw) + L"\\MCPerfLimiter\\WebView2";
    CoTaskMemFree(appDataRaw);

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        userDataDir.c_str(),
        nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, hwnd, onReady = std::move(onReady)]
            (HRESULT result, ICoreWebView2Environment* env) -> HRESULT
            {
                if (FAILED(result) || !env) return result;
                env_ = env;

                return env->CreateCoreWebView2Controller(
                    hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, onReady]
                        (HRESULT res, ICoreWebView2Controller* ctrl) -> HRESULT
                        {
                            if (FAILED(res) || !ctrl) return res;
                            controller_ = ctrl;
                            controller_->get_CoreWebView2(&webview_);

                            // ── 深色背景色，防止导航期间白屏闪烁 ────────────
                            {
                                ComPtr<ICoreWebView2Controller2> ctrl2;
                                if (SUCCEEDED(controller_.As(&ctrl2))) {
                                    // COREWEBVIEW2_COLOR: { A, R, G, B }
                                    // #1a1a1d → RGB(26, 26, 29)
                                    COREWEBVIEW2_COLOR bg{ 255, 26, 26, 29 };
                                    ctrl2->put_DefaultBackgroundColor(bg);
                                }
                            }

                            // ── 配置 WebView2 设置 ────────────────────────
                            {
                                ComPtr<ICoreWebView2Settings> settings;
                                webview_->get_Settings(&settings);
                                if (settings) {
#ifdef _DEBUG
                                    settings->put_AreDevToolsEnabled(TRUE);
                                    settings->put_AreDefaultContextMenusEnabled(TRUE);
#else
                                    settings->put_AreDevToolsEnabled(FALSE);
                                    settings->put_AreDefaultContextMenusEnabled(FALSE);
#endif
                                    settings->put_IsStatusBarEnabled(FALSE);

                                    // 启用非客户区支持：让 CSS -webkit-app-region:drag 生效
                                    // 无需 C++ WM_NCHITTEST 判断标题栏区域
                                    // 需要 WebView2 SDK 1.0.2420.47+
                                    ComPtr<ICoreWebView2Settings9> settings9;
                                    if (SUCCEEDED(settings.As(&settings9))) {
                                        settings9->put_IsNonClientRegionSupportEnabled(TRUE);
                                    }
                                }
                            }

                            // ── 设置初始 Bounds ───────────────────────────
                            // 用当前窗口客户区尺寸初始化，
                            // ready 后再刷一次确保最大化等场景正确
                            {
                                RECT rc{};
                                GetClientRect(hwnd_, &rc);
                                if (pendingW_ == 0) pendingW_ = rc.right;
                                if (pendingH_ == 0) pendingH_ = rc.bottom;
                            }
                            applyBounds(pendingW_, pendingH_);
                            controller_->put_IsVisible(TRUE);

                            // ── 注册消息通道 ─────────────────────────────
                            setupMessageChannel();

                            // ── 阻止拖拽文件导致页面跳转到 file:// ───────
                            webview_->add_NavigationStarting(
                                Callback<ICoreWebView2NavigationStartingEventHandler>(
                                    [](ICoreWebView2* /*sender*/,
                                       ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT
                                    {
                                        LPWSTR uri = nullptr;
                                        args->get_Uri(&uri);
                                        if (uri) {
                                            std::wstring url(uri);
                                            CoTaskMemFree(uri);
                                            if (url.find(L"file:///") == 0)
                                                args->put_Cancel(TRUE);
                                        }
                                        return S_OK;
                                    }
                                ).Get(),
                                nullptr
                            );

                            // ── 加载前端页面 ─────────────────────────────
#ifdef _DEBUG
                            // Debug：优先连接 Vite dev server（热更新）
                            // 若 5173 端口不通，NavigationCompleted 会检测到失败并
                            // 自动 fallback 到虚拟主机映射 dist 目录
                            setupDebugFallback();
                            webview_->Navigate(L"http://localhost:5173");
#elif defined(MC_USE_INLINE_RESOURCES)
                            // Release + 内联资源：虚拟主机映射到内存
                            // 此处仍使用 SetVirtualHostNameToFolderMapping 作为 base URL
                            // 实际 index.html 内容通过 NavigateToString 注入
                            {
                                auto it = MCPerfLimiter::Resource::resourceMap
                                              .find("index.html");
                                if (it != MCPerfLimiter::Resource::resourceMap.end()) {
                                    const auto* ptr = it->second.first;
                                    size_t       sz  = it->second.second;
                                    std::string  html(
                                        reinterpret_cast<const char*>(ptr), sz);
                                    std::wstring whtml = utf8_to_wide(html);
                                    webview_->NavigateToString(whtml.c_str());
                                }
                            }
#else
                            // Release 无内联资源：虚拟主机映射 dist 文件夹
                            {
                                ComPtr<ICoreWebView2_3> wv3;
                                if (SUCCEEDED(webview_.As(&wv3))) {
                                    wv3->SetVirtualHostNameToFolderMapping(
                                        L"mc-perf-limiter.local",
                                        uiDir_.c_str(),
                                        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW
                                    );
                                    webview_->Navigate(
                                        L"https://mc-perf-limiter.local/index.html");
                                } else {
                                    // 回退：file:// 方式
                                    std::wstring url = L"file:///" + uiDir_ + L"/index.html";
                                    std::replace(url.begin(), url.end(), L'\\', L'/');
                                    webview_->Navigate(url.c_str());
                                }
                            }
#endif

                            ready_ = true;
                            // ready 后用最新缓存尺寸刷新一次（修复最大化/快速 resize 丢失问题）
                            applyBounds(pendingW_, pendingH_);
                            if (onReady) onReady();
                            return S_OK;
                        }
                    ).Get()
                );
            }
        ).Get()
    );

    return SUCCEEDED(hr);
}

// ─── 消息通道 ─────────────────────────────────────────────────────────────────

void WebViewBridge::setupMessageChannel() {
    webview_->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [this](ICoreWebView2* /*sender*/,
                   ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT
            {
                LPWSTR raw = nullptr;
                args->TryGetWebMessageAsString(&raw);
                if (raw && msgHandler_) {
                    msgHandler_(wide_to_utf8(raw));
                }
                CoTaskMemFree(raw);
                return S_OK;
            }
        ).Get(),
        nullptr
    );
}

// ─── 向 JS 发送消息 ───────────────────────────────────────────────────────────

void WebViewBridge::postMessage(const std::string& jsonStr) {
    if (!webview_ || !ready_) return;
    std::wstring wide = utf8_to_wide(jsonStr);
    webview_->PostWebMessageAsString(wide.c_str());
}

// ─── 注册处理器 ───────────────────────────────────────────────────────────────

void WebViewBridge::setMessageHandler(MessageHandler handler) {
    msgHandler_ = std::move(handler);
}

// ─── 调整大小 ─────────────────────────────────────────────────────────────────

void WebViewBridge::resize(int width, int height) {
    // 缓存最新尺寸（WebView2 未就绪时也更新，ready后会用最新值）
    pendingW_ = width;
    pendingH_ = height;
    if (!controller_) return;
    applyBounds(width, height);
}

void WebViewBridge::applyBounds(int width, int height) {
    if (!controller_) return;
    // 判断是否最大化：最大化时填满全部，否则缩进 resize 边缘
    bool maximized = hwnd_ && IsZoomed(hwnd_);
    RECT rc{};
    if (maximized) {
        // 最大化：WebView2 填满整个客户区
        rc = {0, 0, width, height};
    } else {
        // 窗口模式：四周缩进 6px，让 Win32 resize 热区可见
        constexpr int RB = 6;
        rc = {RB, 0, width - RB, height - RB};
    }
    controller_->put_Bounds(rc);
}

// ─── Debug Fallback：dev server 不通时切换到虚拟主机映射 dist ─────────────────

#ifdef _DEBUG
void WebViewBridge::setupDebugFallback() {
    webview_->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this](ICoreWebView2* /*sender*/,
                   ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT
            {
                BOOL success = FALSE;
                args->get_IsSuccess(&success);
                if (success) return S_OK; // dev server 正常，无需 fallback

                // dev server 不通 → fallback 到虚拟主机映射 dist 目录
                ComPtr<ICoreWebView2_3> wv3;
                if (SUCCEEDED(webview_.As(&wv3)) && !uiDir_.empty()) {
                    wv3->SetVirtualHostNameToFolderMapping(
                        L"mc-perf-limiter.local",
                        uiDir_.c_str(),
                        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW
                    );
                    webview_->Navigate(
                        L"https://mc-perf-limiter.local/index.html");
                }
                return S_OK;
            }
        ).Get(),
        nullptr
    );
}
#endif
