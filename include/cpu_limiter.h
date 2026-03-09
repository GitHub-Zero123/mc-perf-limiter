#pragma once
#include <windows.h>
#include <unordered_map>
#include <cstdint>

// CPU 使用率限制器
// 优先使用 Job Object HardCap，失败时降级为 SuspendThread 周期方案
class CpuLimiter {
public:
    CpuLimiter();
    ~CpuLimiter();

    // 对指定 PID 应用 CPU 限制（percent: 1-99）
    // 返回：true=成功, false=无法限制
    bool applyLimit(uint32_t pid, uint32_t percent);

    // 移除指定 PID 的 CPU 限制
    void removeLimit(uint32_t pid);

    // 移除所有限制（析构时自动调用）
    void removeAll();

    // 是否已对某 PID 应用限制
    bool hasLimit(uint32_t pid) const;

    // 获取当前限制百分比（未限制返回 0）
    uint32_t getLimit(uint32_t pid) const;

private:
    // ── Job Object 方案 ─────────────────────────────────────
    struct JobEntry {
        HANDLE hJob     = INVALID_HANDLE_VALUE;
        HANDLE hProcess = INVALID_HANDLE_VALUE;
        uint32_t limitPct = 0;
    };
    std::unordered_map<uint32_t, JobEntry> jobs_;

    // ── SuspendThread 降级方案 ──────────────────────────────
    struct SuspendEntry {
        HANDLE    hProcess  = INVALID_HANDLE_VALUE;
        uint32_t  limitPct  = 0;
        bool      active    = false;
    };
    std::unordered_map<uint32_t, SuspendEntry> suspendEntries_;

    // 尝试 Job Object 方案
    bool tryJobObject(uint32_t pid, uint32_t percent);

    // 降级：通过挂起/恢复线程实现近似限制
    bool trySuspendApproach(uint32_t pid, uint32_t percent);

    // 清理 Job 资源
    static void cleanupJob(JobEntry& e);
};
