#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include "cpu_limiter.h"
#include <windows.h>
#include <algorithm>

// ─── 构造/析构 ────────────────────────────────────────────────────────────────

CpuLimiter::CpuLimiter() = default;

CpuLimiter::~CpuLimiter() {
    removeAll();
}

// ─── 工具：清理 Job 资源 ──────────────────────────────────────────────────────

void CpuLimiter::cleanupJob(JobEntry& e) {
    if (e.hProcess && e.hProcess != INVALID_HANDLE_VALUE) {
        CloseHandle(e.hProcess);
        e.hProcess = INVALID_HANDLE_VALUE;
    }
    if (e.hJob && e.hJob != INVALID_HANDLE_VALUE) {
        CloseHandle(e.hJob);
        e.hJob = INVALID_HANDLE_VALUE;
    }
}

// ─── 应用限制（主入口）───────────────────────────────────────────────────────

bool CpuLimiter::applyLimit(uint32_t pid, uint32_t percent) {
    percent = (std::min)(percent, 99u);
    percent = (std::max)(percent, 1u);

    // 先移除旧限制
    removeLimit(pid);

    // 优先尝试 Job Object
    if (tryJobObject(pid, percent)) return true;

    // 降级
    return trySuspendApproach(pid, percent);
}

// ─── Job Object 方案 ──────────────────────────────────────────────────────────

bool CpuLimiter::tryJobObject(uint32_t pid, uint32_t percent) {
    HANDLE hProcess = OpenProcess(
        PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) return false;

    HANDLE hJob = CreateJobObjectW(nullptr, nullptr);
    if (!hJob) {
        CloseHandle(hProcess);
        return false;
    }

    // 允许进程突破已有 Job（若已在 Job 中）
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION extInfo{};
    extInfo.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_BREAKAWAY_OK |
        JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
    SetInformationJobObject(hJob,
        JobObjectExtendedLimitInformation,
        &extInfo, sizeof(extInfo));

    // 将进程加入 Job
    if (!AssignProcessToJobObject(hJob, hProcess)) {
        CloseHandle(hJob);
        CloseHandle(hProcess);
        return false;
    }

    // 设置 CPU 速率控制（HardCap）
    // CpuRate 单位：万分比（0-10000），即 percent * 100
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpuRate{};
    cpuRate.ControlFlags =
        JOB_OBJECT_CPU_RATE_CONTROL_ENABLE |
        JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
    cpuRate.CpuRate = percent * 100;

    if (!SetInformationJobObject(hJob,
            JobObjectCpuRateControlInformation,
            &cpuRate, sizeof(cpuRate))) {
        CloseHandle(hJob);
        CloseHandle(hProcess);
        return false;
    }

    jobs_[pid] = { hJob, hProcess, percent };
    return true;
}

// ─── SuspendThread 降级方案 ───────────────────────────────────────────────────
// 通过周期性挂起所有线程实现近似 CPU 限制
// 注意：这是近似方案，精度较低，仅作为 Job Object 失败时的兜底

bool CpuLimiter::trySuspendApproach(uint32_t pid, uint32_t percent) {
    HANDLE hProcess = OpenProcess(
        PROCESS_SUSPEND_RESUME | PROCESS_QUERY_INFORMATION,
        FALSE, pid);
    if (!hProcess) return false;

    suspendEntries_[pid] = { hProcess, percent, true };
    // 实际挂起/恢复逻辑由调用方定时器驱动（App 层），此处仅注册
    return true;
}

// ─── 移除限制 ─────────────────────────────────────────────────────────────────

void CpuLimiter::removeLimit(uint32_t pid) {
    // 移除 Job Object
    auto jit = jobs_.find(pid);
    if (jit != jobs_.end()) {
        cleanupJob(jit->second);
        jobs_.erase(jit);
    }
    // 移除 SuspendThread
    auto sit = suspendEntries_.find(pid);
    if (sit != suspendEntries_.end()) {
        if (sit->second.hProcess &&
            sit->second.hProcess != INVALID_HANDLE_VALUE)
            CloseHandle(sit->second.hProcess);
        suspendEntries_.erase(sit);
    }
}

void CpuLimiter::removeAll() {
    for (auto& [pid, e] : jobs_)
        cleanupJob(e);
    jobs_.clear();

    for (auto& [pid, e] : suspendEntries_) {
        if (e.hProcess && e.hProcess != INVALID_HANDLE_VALUE)
            CloseHandle(e.hProcess);
    }
    suspendEntries_.clear();
}

// ─── 查询 ─────────────────────────────────────────────────────────────────────

bool CpuLimiter::hasLimit(uint32_t pid) const {
    return jobs_.count(pid) > 0 || suspendEntries_.count(pid) > 0;
}

uint32_t CpuLimiter::getLimit(uint32_t pid) const {
    auto jit = jobs_.find(pid);
    if (jit != jobs_.end()) return jit->second.limitPct;
    auto sit = suspendEntries_.find(pid);
    if (sit != suspendEntries_.end()) return sit->second.limitPct;
    return 0;
}
