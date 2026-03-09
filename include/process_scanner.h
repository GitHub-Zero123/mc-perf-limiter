#pragma once
#include "ipc_types.h"
#include <windows.h>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <unordered_map>

// Minecraft 进程名白名单（大小写不敏感匹配）
inline const wchar_t* MC_PROCESS_NAMES[] = {
    L"Minecraft.Windows.exe",  // 基岩版 UWP
    L"javaw.exe",              // Java 版（需额外过滤）
    L"java.exe",               // Java 版服务端
    L"bedrock_server.exe",     // 基岩版服务端
    L"MinecraftLauncher.exe",  // 官方启动器
};

class ProcessScanner {
public:
    // 进程列表更新回调
    using UpdateCallback = std::function<void(const std::vector<ipc::ProcessInfo>&)>;

    ProcessScanner();
    ~ProcessScanner();

    // 启动定时扫描（每 intervalMs 毫秒推送一次）
    void start(UpdateCallback cb, uint32_t intervalMs = 1000);
    void stop();

    // 立即扫描一次（同步，返回结果）
    std::vector<ipc::ProcessInfo> scanOnce();

    bool isRunning() const { return running_; }

private:
    std::thread   thread_;
    std::atomic<bool> running_{ false };
    UpdateCallback    callback_;
    uint32_t          intervalMs_ = 1000;

    // PDH 查询句柄（CPU 计数器）
    struct PdhState;
    std::unique_ptr<PdhState> pdh_;

    // 检查进程名是否属于 Minecraft
    static bool isMinecraftProcess(const std::wstring& name);

    // 读取单进程 CPU 使用率（GetProcessTimes 差分）
    struct CpuSample {
        ULONGLONG kernelTime = 0;
        ULONGLONG userTime   = 0;
        ULONGLONG wallTime   = 0;
    };
    std::unordered_map<uint32_t, CpuSample> cpuPrev_;

    double measureCpuUsage(uint32_t pid, HANDLE hProcess);

    // 读取单进程 GPU 使用率（PDH）
    double measureGpuUsage(uint32_t pid);

    // IO 差分状态（字节/秒）
    struct IoSample {
        ULONGLONG readBytes  = 0;
        ULONGLONG writeBytes = 0;
        ULONGLONG wallTime   = 0;
    };
    std::unordered_map<uint32_t, IoSample> ioPrev_;

    // 采集内存使用量（字节）
    static uint64_t measureMemoryUsage(HANDLE hProcess);
    // 采集 IO 速率（字节/秒），返回 {readBytesPerSec, writeBytesPerSec}
    std::pair<uint64_t, uint64_t> measureIoRate(uint32_t pid, HANDLE hProcess);

    // 获取进程完整路径
    static std::string getProcessPath(HANDLE hProcess);

    // 提取 exe 图标为 Base64 PNG 字符串（空表示失败）
    static std::string extractIcon(const std::string& exePath);

    // 图标缓存（pid → base64 PNG）
    std::unordered_map<uint32_t, std::string> iconCache_;

    void workerLoop();
};
