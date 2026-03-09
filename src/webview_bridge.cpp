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
#include <shlobj.h>   // SHGetKnownFolderPath / FOLDERID_RoamingAppData
#include <stdexcept>
#include <string>

// Release 模式：引入由 ui/generate.py 生成的内联资源
// 生成命令：python ui/generate.py  （CMake PRE_BUILD 自动调用）
// 生成目标：ui/Resource/Resource.hpp
// 命名空间：MCPerfLimiter::Resource
// map key：相对 ui/dist/ 的路径，正斜杠（如 "index.html"）
// CMakeLists.txt 已将 ui/ 加入 include 目录，故以下路径可解析
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

// ─── 内部：根据 uiDir 构建 file:/// URL ───────────────────────────────────────

static std::wstring make_file_url(const std::wstring& uiDir,
                                   const std::wstring& file) {
    std::wstring url = L"file:///";
    url += uiDir;
    url += L"/";
    url += file;
    for (auto& c : url)
        if (c == L'\\') c = L'/';
    return url;
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
    hwnd_ = hwnd;

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
            [this, hwnd, uiDir, onReady = std::move(onReady)]
            (HRESULT result, ICoreWebView2Environment* env) -> HRESULT
            {
                if (FAILED(result) || !env) return result;
                env_ = env;

                return env->CreateCoreWebView2Controller(
                    hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, uiDir, onReady]
                        (HRESULT res, ICoreWebView2Controller* ctrl) -> HRESULT
                        {
                            if (FAILED(res) || !ctrl) return res;
                            controller_ = ctrl;
                            controller_->get_CoreWebView2(&webview_);

                            // 配置 WebView 设置
                            ComPtr<ICoreWebView2Settings> settings;
                            webview_->get_Settings(&settings);
                            if (settings) {
#ifdef _DEBUG
                                settings->put_AreDevToolsEnabled(TRUE);
#else
                                settings->put_AreDevToolsEnabled(FALSE);
#endif
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                            }

                            // 填满父窗口
                            RECT rc{};
                            GetClientRect(hwnd_, &rc);
                            controller_->put_Bounds(rc);
                            controller_->put_IsVisible(TRUE);

                            // 注册消息通道
                            setupMessageChannel();

                            // ── 加载前端页面 ─────────────────────────────
#ifdef MC_USE_INLINE_RESOURCES
                            // Release + 资源 hpp 已生成：从内存加载
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
                                } else {
                                    // 兜底
                                    webview_->Navigate(
                                        make_file_url(uiDir, L"index.html").c_str());
                                }
                            }
#else
                            // Debug 或资源 hpp 未生成：从文件路径加载
                            webview_->Navigate(
                                make_file_url(uiDir, L"index.html").c_str());
#endif

                            ready_ = true;
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
    webview_->PostWebMessageAsJson(wide.c_str());
}

// ─── 注册处理器 ───────────────────────────────────────────────────────────────

void WebViewBridge::setMessageHandler(MessageHandler handler) {
    msgHandler_ = std::move(handler);
}

// ─── 调整大小 ─────────────────────────────────────────────────────────────────

void WebViewBridge::resize(int width, int height) {
    if (!controller_) return;
    RECT rc{ 0, 0, width, height };
    controller_->put_Bounds(rc);
}
