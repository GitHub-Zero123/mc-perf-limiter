#pragma once
#include <windows.h>
#include <cstdint>
#include <unordered_map>

/**
 * IoLimiter — 通过设置进程 IO 优先级来软性限制 IO 占用
 *
 * 原理：
 *   调用 NtSetInformationProcess(ProcessIoPriority) 将进程 IO 优先级
 *   降低到 IoPriorityVeryLow(0) / IoPriorityLow(1)，让系统优先处理
 *   其他进程的 IO 请求，从而降低游戏进程对磁盘的争抢。
 *
 * 注意：
 *   这是软性限制，不能精确限速，但可以有效减少 IO 对系统的影响。
 */
class IoLimiter {
public:
    IoLimiter()  = default;
    ~IoLimiter() { removeAll(); }

    // 对 pid 应用低 IO 优先级
    bool applyLimit(uint32_t pid);

    // 恢复 pid 的 IO 优先级为正常
    void removeLimit(uint32_t pid);

    // 移除所有限制
    void removeAll();

    // 查询是否已限制
    bool hasLimit(uint32_t pid) const;

private:
    std::unordered_map<uint32_t, bool> limited_;
};
