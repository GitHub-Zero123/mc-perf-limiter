#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include "gpu_limiter.h"
#include <windows.h>
#include <processthreadsapi.h>  // PROCESS_POWER_THROTTLING_STATE
#include <algorithm>

// ─── 构造/析构 ────────────────────────────────────────────────────────────────

GpuLimiter::GpuLimiter() = default;

GpuLimiter::~GpuLimiter() {
    removeAll();
}

// ─── 应用限制 ─────────────────────────────────────────────────────────────────

bool GpuLimiter::applyLimit(uint32_t pid, uint32_t percent) {
    percent = (std::min)(percent, 99u);
    percent = (std::max)(percent, 1u);

    removeLimit(pid);

    HANDLE hProcess = OpenProcess(
        PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION,
        FALSE, pid);
    if (!hProcess) return false;

    GpuEntry entry{};
    entry.hProcess    = hProcess;
    entry.limitPct    = percent;
    entry.origPriority = GetPriorityClass(hProcess);

    if (!applyPowerThrottling(pid, percent, entry)) {
        CloseHandle(hProcess);
        return false;
    }

    entries_[pid] = entry;
    return true;
}

// ─── ProcessPowerThrottling ───────────────────────────────────────────────────

bool GpuLimiter::applyPowerThrottling(uint32_t /*pid*/, uint32_t percent,
                                       GpuEntry& entry)
{
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version     = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;

    if (percent <= 70) {
        // 启用 EcoQoS
        state.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        DWORD newPriority = (percent <= 30)
            ? IDLE_PRIORITY_CLASS
            : BELOW_NORMAL_PRIORITY_CLASS;
        SetPriorityClass(entry.hProcess, newPriority);
    } else {
        // 解除节流，仅降低优先级
        state.StateMask = 0;
        SetPriorityClass(entry.hProcess, BELOW_NORMAL_PRIORITY_CLASS);
    }

    BOOL ok = SetProcessInformation(
        entry.hProcess,
        ProcessPowerThrottling,
        &state,
        static_cast<DWORD>(sizeof(state)));

    return ok != FALSE;
}

// ─── 恢复 ─────────────────────────────────────────────────────────────────────

void GpuLimiter::restoreEntry(GpuEntry& e) {
    if (!e.hProcess || e.hProcess == INVALID_HANDLE_VALUE) return;

    SetPriorityClass(e.hProcess, e.origPriority);

    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version     = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask   = 0;
    SetProcessInformation(e.hProcess,
        ProcessPowerThrottling,
        &state,
        static_cast<DWORD>(sizeof(state)));

    CloseHandle(e.hProcess);
    e.hProcess = INVALID_HANDLE_VALUE;
}

// ─── 移除限制 ─────────────────────────────────────────────────────────────────

void GpuLimiter::removeLimit(uint32_t pid) {
    auto it = entries_.find(pid);
    if (it == entries_.end()) return;
    restoreEntry(it->second);
    entries_.erase(it);
}

void GpuLimiter::removeAll() {
    for (auto& [pid, e] : entries_)
        restoreEntry(e);
    entries_.clear();
}

// ─── 查询 ─────────────────────────────────────────────────────────────────────

bool GpuLimiter::hasLimit(uint32_t pid) const {
    return entries_.count(pid) > 0;
}

uint32_t GpuLimiter::getLimit(uint32_t pid) const {
    auto it = entries_.find(pid);
    return (it != entries_.end()) ? it->second.limitPct : 0u;
}
