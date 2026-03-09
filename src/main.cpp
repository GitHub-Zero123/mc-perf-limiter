#include "app.h"
#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                   LPSTR /*lpCmdLine*/, int nCmdShow)
{
    // 初始化 COM（WebView2 需要）
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return -1;

    App app;
    int exitCode = app.run(hInstance, nCmdShow);

    CoUninitialize();
    return exitCode;
}
