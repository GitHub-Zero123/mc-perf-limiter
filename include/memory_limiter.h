#pragma once
#include <windows.h>
#include <cstdint>
#include <unordered_map>

/**
 * MemoryLimiter — 通过 Job Object 限制进程最大工作集（内存使用量）
 *
 * 原理：
 *   将目标进程加入一个 Job Object，设置 JobObjectExtendedLimitInformation
 *   中的 ProcessMemoryLimit 字段。当进程超过限制时，OS 会强制 trim 工作集。
 *
 * 注意：
 *   - 一个进程只能属于一个 Job Object（Win8+ 可嵌套，但兼容性问题多）
 *   - 若进程已属于其他 Job Object（如任务管理器），AssignProcess 会失败
 */
class MemoryLimiter {
public:
    MemoryLimiter()  = default;
    ~MemoryLimiter() { removeAll(); }

    // 对 pid 设置内存限制（limitBytes = 最大工作集字节数）
    // 返回 true 表示成功
    bool applyLimit(uint32_t pid, uint64_t limitBytes);

    // 移除 pid 的内存限制（关闭 Job Object）
    void removeLimit(uint32_t pid);

    // 移除所有限制
    void removeAll();

    // 查询是否已限制
    bool hasLimit(uint32_t pid) const;

    // 获取当前限制字节数（0 = 未限制）
    uint64_t getLimit(uint32_t pid) const;

private:
    struct JobEntry {
        HANDLE  hJob   = nullptr;
        uint64_t limitBytes = 0;
    };
    std::unordered_map<uint32_t, JobEntry> jobs_;
};
