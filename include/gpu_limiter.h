#pragma once
#include <windows.h>
#include <unordered_map>
#include <string>
#include <cstdint>

// GPU 帧率限制器（基于 DLL 注入）
// 工作流程：
//   1. 检测 gpu_hook.dll 是否在同目录（单独运行时禁用）
//   2. 通过 CreateRemoteThread + LoadLibraryW 注入 gpu_hook.dll 到目标进程
//   3. 通过命名管道 \\.\pipe\mcperf_gpu_{ourPid} 发送目标 FPS
//   4. gpu_hook.dll 内 hook wglSwapBuffers / IDXGISwapChain::Present 实现精确帧率限制
//
// 帧率 ← GPU 限制百分比映射：
//   percent=10 → 6  FPS（极限低端）
//   percent=25 → 15 FPS
//   percent=50 → 30 FPS（标准移动端）
//   percent=75 → 45 FPS
//   percent=90 → 54 FPS

class GpuLimiter {
public:
    GpuLimiter();
    ~GpuLimiter();

    // 检测 gpu_hook.dll 是否可用（不可用时 UI 应禁用 GPU 调节）
    static bool isHookAvailable();

    // 返回实际查找 gpu_hook.dll 时使用的路径（用于调试显示）
    static std::wstring getDllSearchPath();

    bool applyLimit(uint32_t pid, uint32_t percent);
    void removeLimit(uint32_t pid);
    void removeAll();

    bool     hasLimit(uint32_t pid) const;
    uint32_t getLimit(uint32_t pid) const;

private:
    struct Entry {
        uint32_t limitPct = 0;
        bool     injected = false;   // DLL 已注入
        // hPipe 已移除：主程序现在是客户端（每次 sendFps 临时连接），不持久持有管道
        HANDLE   hMap     = nullptr; // 命名共享内存句柄（传递主 PID 给 DLL，注入后保持打开）
    };
    std::unordered_map<uint32_t, Entry> entries_;

    // 注入 gpu_hook.dll 到目标进程（需写 entries_[pid].hMap，非静态）
    bool injectDll(uint32_t pid);

    // 向已注入进程发送目标 FPS（0 = 解除限制）
    bool sendFps(uint32_t pid, uint32_t fps);

    // 获取 gpu_hook.dll 的完整路径
    static std::wstring getDllPath();
};
