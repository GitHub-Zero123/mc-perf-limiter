#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include "cpu_limiter.h"
#include <windows.h>

// ─── 构造/析构 ────────────────────────────────────────────────────────────────

CpuLimiter::CpuLimiter() {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    DWORD n = si.dwNumberOfProcessors;
    totalCores_ = (n < 64u) ? n : 64u;
    if (totalCores_ == 0) totalCores_ = 1;
}

CpuLimiter::~CpuLimiter() {
    removeAll();
}

// ─── 应用限制 ─────────────────────────────────────────────────────────────────
// 策略：SetProcessAffinityMask 限制可用 CPU 核心数
//
// 这是对基岩版（原生 C++/UWP 进程）最有效且不会导致卡死的方案：
//   - percent=50%：分配系统核心数的一半
//   - percent=25%：分配系统核心数的四分之一（至少保留 1 个核心）
//   - 不使用 HARD_CAP（与 UWP 沙盒 Job 嵌套会导致过度限制）
//   - 不使用 BELOW_NORMAL 优先级（叠加会导致使用率接近 0）
//
// 注意：亲和性限制后，进程在允许的核心上仍可跑满，这是正常的。
//       任务管理器显示的 CPU% 是相对全局 CPU 计算的，
//       限制到 2 核（16核系统）显示约 12.5% 的实际可用上限。

bool CpuLimiter::applyLimit(uint32_t pid, uint32_t percent) {
    if (percent < 1u)  percent = 1u;
    if (percent > 99u) percent = 99u;

    removeLimit(pid);

    // 尝试以完整权限打开，失败则以较低权限重试
    HANDLE hProcess = OpenProcess(
        PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION,
        FALSE, pid);
    if (!hProcess) return false;

    // 获取系统和进程当前亲和性掩码
    DWORD_PTR procAff = 0, sysAff = 0;
    if (!GetProcessAffinityMask(hProcess, &procAff, &sysAff) || sysAff == 0) {
        CloseHandle(hProcess);
        return false;
    }

    // 统计系统可用核心数
    DWORD avail = 0;
    {
        DWORD_PTR tmp = sysAff;
        while (tmp) { if (tmp & 1) avail++; tmp >>= 1; }
    }
    if (avail == 0) avail = totalCores_;

    // 计算目标核心数：至少 1 个
    DWORD raw = static_cast<DWORD>(
        static_cast<uint64_t>(avail) * percent / 100u);
    DWORD target = (raw < 1u) ? 1u : (raw > avail ? avail : raw);

    // 从低位依次选取 target 个可用核心
    DWORD_PTR newAff = 0;
    DWORD cnt = 0;
    for (int bit = 0; bit < 64 && cnt < target; ++bit) {
        if (sysAff & (DWORD_PTR(1) << bit)) {
            newAff |= (DWORD_PTR(1) << bit);
            ++cnt;
        }
    }

    if (!SetProcessAffinityMask(hProcess, newAff)) {
        CloseHandle(hProcess);
        return false;
    }

    Entry e{};
    e.hProcess     = hProcess;
    e.limitPct     = percent;
    e.origAffinity = procAff;
    e.affinitySet  = true;
    e.origPriority = GetPriorityClass(hProcess);
    // 不修改进程优先级，避免与亲和性叠加过度限制

    entries_[pid] = e;
    return true;
}

// ─── 恢复 ─────────────────────────────────────────────────────────────────────

void CpuLimiter::restoreEntry(Entry& e) {
    if (!e.hProcess || e.hProcess == INVALID_HANDLE_VALUE) return;

    // 恢复原始亲和性掩码
    if (e.affinitySet && e.origAffinity != 0)
        SetProcessAffinityMask(e.hProcess, e.origAffinity);

    // 关闭 Job（如有）
    if (e.hJob && e.hJob != INVALID_HANDLE_VALUE) {
        CloseHandle(e.hJob);
        e.hJob = INVALID_HANDLE_VALUE;
    }

    CloseHandle(e.hProcess);
    e.hProcess = INVALID_HANDLE_VALUE;
}

// ─── 移除限制 ─────────────────────────────────────────────────────────────────

void CpuLimiter::removeLimit(uint32_t pid) {
    auto it = entries_.find(pid);
    if (it == entries_.end()) return;
    restoreEntry(it->second);
    entries_.erase(it);
}

void CpuLimiter::removeAll() {
    for (auto& [pid, e] : entries_)
        restoreEntry(e);
    entries_.clear();
}

// ─── 查询 ─────────────────────────────────────────────────────────────────────

bool CpuLimiter::hasLimit(uint32_t pid) const {
    return entries_.count(pid) > 0;
}

uint32_t CpuLimiter::getLimit(uint32_t pid) const {
    auto it = entries_.find(pid);
    return (it != entries_.end()) ? it->second.limitPct : 0u;
}
