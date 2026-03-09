#pragma once
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <functional>
#include <string>
#include <string_view>

// WebView2 + IPC 封装
// 负责：初始化 WebView2、加载前端页面、双向消息通道
class WebViewBridge {
public:
    // 收到 JS 消息时的回调：(jsonString) -> void
    using MessageHandler = std::function<void(const std::string&)>;

    WebViewBridge();
    ~WebViewBridge();

    // 异步初始化 WebView2（完成后调用 onReady）
    bool init(HWND hwnd, const std::wstring& uiDir,
              std::function<void()> onReady = nullptr);

    // 向 JS 发送 JSON 消息
    void postMessage(const std::string& jsonStr);

    // 注册 JS → C++ 消息处理器
    void setMessageHandler(MessageHandler handler);

    // 调整 WebView 大小以填满父窗口
    void resize(int width, int height);

    // 设置背景颜色（用于毛玻璃主题切换）
    void setBackgroundColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

    // 是否初始化完成
    bool isReady() const { return ready_; }

    // 获取 WebView2 Controller（用于焦点管理）
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller() const {
        return controller_;
    }

private:
    Microsoft::WRL::ComPtr<ICoreWebView2Environment>  env_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller>   controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2>             webview_;

    MessageHandler msgHandler_;
    bool           ready_  = false;
    HWND           hwnd_   = nullptr;
    std::wstring   uiDir_;   // ui/dist 目录（虚拟主机映射用）

    // 待应用的窗口尺寸（WebView2 就绪前 resize() 会缓存在此）
    int            pendingW_ = 0;
    int            pendingH_ = 0;

    // 注册消息监听
    void setupMessageChannel();

    // 实际设置 WebView2 Bounds（含最大化判断）
    void applyBounds(int width, int height);

#ifdef _DEBUG
    // Debug：注册 NavigationCompleted 监听，失败时 fallback 到虚拟主机
    void setupDebugFallback();
#endif
};
