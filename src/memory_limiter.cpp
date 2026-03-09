#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include "memory_limiter.h"
#include <windows.h>

// ─── 应用内存限制 ─────────────────────────────────────────────────────────────

bool MemoryLimiter::applyLimit(uint32_t pid, uint64_t limitBytes) {
    // 先移除旧限制（若已存在）
    removeLimit(pid);

    // 打开目标进程
    HANDLE hProc = OpenProcess(
        PROCESS_ALL_ACCESS, FALSE, static_cast<DWORD>(pid));
    if (!hProc) return false;

    // 创建 Job Object
    HANDLE hJob = CreateJobObjectW(nullptr, nullptr);
    if (!hJob) {
        CloseHandle(hProc);
        return false;
    }

    // 设置内存限制
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    jeli.ProcessMemoryLimit = static_cast<SIZE_T>(limitBytes);

    if (!SetInformationJobObject(hJob,
            JobObjectExtendedLimitInformation,
            &jeli, sizeof(jeli))) {
        CloseHandle(hJob);
        CloseHandle(hProc);
        return false;
    }

    // 将进程加入 Job Object
    if (!AssignProcessToJobObject(hJob, hProc)) {
        CloseHandle(hJob);
        CloseHandle(hProc);
        return false;
    }

    CloseHandle(hProc);
    jobs_[pid] = { hJob, limitBytes };
    return true;
}

// ─── 移除内存限制 ─────────────────────────────────────────────────────────────

void MemoryLimiter::removeLimit(uint32_t pid) {
    auto it = jobs_.find(pid);
    if (it == jobs_.end()) return;

    // 清除 Job Object 上的内存限制（让进程自由运行）
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
    jeli.BasicLimitInformation.LimitFlags = 0; // 清除所有限制标志
    SetInformationJobObject(it->second.hJob,
        JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));

    CloseHandle(it->second.hJob);
    jobs_.erase(it);
}

// ─── 移除所有限制 ─────────────────────────────────────────────────────────────

void MemoryLimiter::removeAll() {
    for (auto& [pid, entry] : jobs_) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
        SetInformationJobObject(entry.hJob,
            JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
        CloseHandle(entry.hJob);
    }
    jobs_.clear();
}

// ─── 查询 ─────────────────────────────────────────────────────────────────────

bool MemoryLimiter::hasLimit(uint32_t pid) const {
    return jobs_.count(pid) > 0;
}

uint64_t MemoryLimiter::getLimit(uint32_t pid) const {
    auto it = jobs_.find(pid);
    return (it != jobs_.end()) ? it->second.limitBytes : 0;
}
