#pragma once
#include <windows.h>
#include <unordered_map>
#include <string>
#include <cstdint>

// GPU 使用率限制器
// 方案1（简单）：ProcessPowerThrottling 降低调度优先级
// 方案2（进阶）：PDH 监控 GPU 占用，动态调整进程优先级
class GpuLimiter {
public:
    GpuLimiter();
    ~GpuLimiter();

    // 对指定 PID 应用 GPU 限制（percent: 1-99）
    bool applyLimit(uint32_t pid, uint32_t percent);

    // 移除限制
    void removeLimit(uint32_t pid);
    void removeAll();

    bool     hasLimit(uint32_t pid) const;
    uint32_t getLimit(uint32_t pid) const;

private:
    struct GpuEntry {
        HANDLE   hProcess       = INVALID_HANDLE_VALUE;
        uint32_t limitPct       = 0;
        DWORD    origPriority   = NORMAL_PRIORITY_CLASS;
        int      origGpuPriority = 2;   // D3DKMT_SCHEDULINGPRIORITYCLASS_NORMAL
        bool     gpuPrioritySet = false;
    };
    std::unordered_map<uint32_t, GpuEntry> entries_;

    // 恢复原始设置
    void restoreEntry(GpuEntry& e);
};
