#include "app.h"
#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                   LPSTR /*lpCmdLine*/, int nCmdShow)
{
    // 必须在创建任何窗口之前声明 Per-Monitor V2 DPI 感知
    // 这样 WebView2 才能按正确的逻辑 CSS px 渲染，不会模糊
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // 初始化 COM（WebView2 需要）
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return -1;

    App app;
    int exitCode = app.run(hInstance, nCmdShow);

    CoUninitialize();
    return exitCode;
}
