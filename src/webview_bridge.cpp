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
#include <shlwapi.h>   // SHCreateMemStream（Release 内联资源用）
#include <stdexcept>
#include <string>
#include <algorithm>
#include <memory>

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
                                    // 深色: #202024 → RGB(32, 32, 36)
                                    COREWEBVIEW2_COLOR bg{ 255, 32, 32, 36 };
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

                            // ── 注册 NavigationCompleted：页面 JS 加载完毕后调用 onReady ──
                            // 必须在此时调用 onReady，确保 bridge.on() 已注册后再 postMessage
                            // readyFired_ 为成员变量，通过 this 访问，只触发一次
                            webview_->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [this, onReady](ICoreWebView2* /*sender*/,
                                        ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT
                                    {
                                        BOOL success = FALSE;
                                        args->get_IsSuccess(&success);
                                        if (success && onReady && !this->readyFired_) {
                                            this->readyFired_ = true;
                                            this->applyBounds(this->pendingW_, this->pendingH_);
                                            onReady();
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
                            // Release + 内联资源：WebResourceRequested 拦截 + 内存流
                            // 从 resourceMap 提供所有文件，支持 index.html + assets/* 子资源
                            setupInlineResources();
                            webview_->Navigate(L"https://mc-perf-limiter.local/index.html");
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
                            // 注意：onReady() 现在由 NavigationCompleted 回调触发，不在此处调用
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

// ─── 设置 WebView2 背景颜色 ───────────────────────────────────────────────────

void WebViewBridge::setBackgroundColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!controller_) return;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller2> ctrl2;
    if (SUCCEEDED(controller_.As(&ctrl2))) {
        COREWEBVIEW2_COLOR bg{ a, r, g, b };
        ctrl2->put_DefaultBackgroundColor(bg);
    }
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

// ─── Release 内联资源加载 ─────────────────────────────────────────────────────
// 注册 AddWebResourceRequestedFilter + add_WebResourceRequested
// 从内存 resourceMap 提供所有文件（index.html + assets/*）
// 必须在 Navigate 之前调用

#if !defined(_DEBUG)
void WebViewBridge::setupInlineResources() {
#ifdef MC_USE_INLINE_RESOURCES
    // 拦截 https://mc-perf-limiter.local/* 的所有请求
    webview_->AddWebResourceRequestedFilter(
        L"https://mc-perf-limiter.local/*",
        COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);

    webview_->add_WebResourceRequested(
        Callback<ICoreWebView2WebResourceRequestedEventHandler>(
            [this](ICoreWebView2* /*sender*/,
                   ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT
            {
                // 取出请求 URI
                ComPtr<ICoreWebView2WebResourceRequest> req;
                args->get_Request(&req);
                LPWSTR uriRaw = nullptr;
                req->get_Uri(&uriRaw);
                if (!uriRaw) return S_OK;
                std::wstring uri(uriRaw);
                CoTaskMemFree(uriRaw);

                // 解析路径：https://mc-perf-limiter.local/assets/index.js → assets/index.js
                const std::wstring prefix = L"https://mc-perf-limiter.local/";
                if (uri.find(prefix) != 0) return S_OK;

                std::wstring relW = uri.substr(prefix.size());
                // 去查询字符串和 hash
                auto q = relW.find(L'?');
                if (q != std::wstring::npos) relW = relW.substr(0, q);
                auto h = relW.find(L'#');
                if (h != std::wstring::npos) relW = relW.substr(0, h);

                // wstring → UTF-8
                std::string key;
                if (!relW.empty()) {
                    int n = WideCharToMultiByte(CP_UTF8, 0,
                        relW.c_str(), static_cast<int>(relW.size()),
                        nullptr, 0, nullptr, nullptr);
                    if (n > 0) {
                        key.resize(n);
                        WideCharToMultiByte(CP_UTF8, 0,
                            relW.c_str(), static_cast<int>(relW.size()),
                            &key[0], n, nullptr, nullptr);
                    }
                }
                if (key.empty()) key = "index.html";

                // 在 resourceMap 中查找
                auto it = MCPerfLimiter::Resource::resourceMap.find(key);
                if (it == MCPerfLimiter::Resource::resourceMap.end())
                    return S_OK;  // 找不到 → WebView2 显示默认 404

                const auto* ptr = it->second.first;
                size_t       sz  = it->second.second;

                // 推断 Content-Type（按扩展名）
                auto endsWith = [&key](const char* suf) {
                    size_t sl = strlen(suf);
                    return key.size() >= sl &&
                           key.compare(key.size() - sl, sl, suf) == 0;
                };
                std::wstring ct = L"application/octet-stream";
                if      (endsWith(".html")) ct = L"text/html; charset=utf-8";
                else if (endsWith(".js"))   ct = L"text/javascript; charset=utf-8";
                else if (endsWith(".css"))  ct = L"text/css; charset=utf-8";
                else if (endsWith(".png"))  ct = L"image/png";
                else if (endsWith(".svg"))  ct = L"image/svg+xml";
                else if (endsWith(".ico"))  ct = L"image/x-icon";
                else if (endsWith(".woff2"))ct = L"font/woff2";
                else if (endsWith(".woff")) ct = L"font/woff";

                // 创建内存流
                IStream* rawStream = SHCreateMemStream(ptr, static_cast<UINT>(sz));
                if (!rawStream) return S_OK;
                ComPtr<IStream> stream;
                stream.Attach(rawStream);

                // 构造 HTTP 响应
                ComPtr<ICoreWebView2WebResourceResponse> resp;
                env_->CreateWebResourceResponse(
                    stream.Get(), 200, L"OK",
                    (L"Content-Type: " + ct).c_str(),
                    &resp);
                if (resp) args->put_Response(resp.Get());
                return S_OK;
            }
        ).Get(),
        nullptr
    );
#endif  // MC_USE_INLINE_RESOURCES
}
#endif  // !_DEBUG
