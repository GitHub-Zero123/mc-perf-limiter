#pragma once
#include <windows.h>
#include <unordered_map>
#include <cstdint>

// CPU 使用率限制器（针对基岩版 Minecraft.Windows.exe 原生进程）
// 方案1：SetProcessAffinityMask（限制可用 CPU 核心数）
// 方案2：Job Object HARD_CAP（Windows 10/11 支持嵌套 Job，可追加 rate 限制）
// 方案3：SetPriorityClass（进程调度优先级降级，辅助）
class CpuLimiter {
public:
    CpuLimiter();
    ~CpuLimiter();

    // 对指定 PID 应用 CPU 限制（percent: 1-99）
    bool applyLimit(uint32_t pid, uint32_t percent);

    // 移除指定 PID 的 CPU 限制
    void removeLimit(uint32_t pid);

    // 移除所有限制（析构时自动调用）
    void removeAll();

    bool     hasLimit(uint32_t pid) const;
    uint32_t getLimit(uint32_t pid) const;

private:
    struct Entry {
        HANDLE    hProcess      = INVALID_HANDLE_VALUE;
        HANDLE    hJob          = INVALID_HANDLE_VALUE;  // Job Object 句柄
        uint32_t  limitPct      = 0;
        DWORD_PTR origAffinity  = 0;   // 原始 CPU 亲和性掩码
        DWORD     origPriority  = NORMAL_PRIORITY_CLASS;
        bool      affinitySet   = false;
        bool      jobSet        = false;
    };
    std::unordered_map<uint32_t, Entry> entries_;

    // 系统逻辑 CPU 数（初始化时获取）
    DWORD totalCores_ = 0;

    void restoreEntry(Entry& e);
};
