#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include "gpu_limiter.h"
#include <windows.h>
#include <processthreadsapi.h>
#include <algorithm>

// ─── D3DKMT GPU 调度优先级（最小声明，避免依赖 d3dkmthk.h）─────────────────
// D3DKMT_SCHEDULINGPRIORITYCLASS: IDLE=0, BELOW_NORMAL=1, NORMAL=2, ...

typedef struct _GPU_SCHED_REQ {
    HANDLE hProcess;
    int    PriorityClass; // D3DKMT_SCHEDULINGPRIORITYCLASS 枚举值
} GPU_SCHED_REQ;

// 函数指针类型（WINAPI = __stdcall，MSVC 兼容 typedef 形式）
typedef LONG (WINAPI* PFN_SetGpuSched)(const GPU_SCHED_REQ*);

static PFN_SetGpuSched s_SetGpuPriority = nullptr;

static bool ensureD3dkmt() {
    if (s_SetGpuPriority) return true;
    HMODULE hGdi32 = GetModuleHandleW(L"gdi32.dll");
    if (!hGdi32) return false;
    s_SetGpuPriority = reinterpret_cast<PFN_SetGpuSched>(
        GetProcAddress(hGdi32, "D3DKMTSetProcessSchedulingPriorityClass"));
    return s_SetGpuPriority != nullptr;
}

// ─── 构造/析构 ────────────────────────────────────────────────────────────────

GpuLimiter::GpuLimiter() = default;

GpuLimiter::~GpuLimiter() {
    removeAll();
}

// ─── 应用限制 ─────────────────────────────────────────────────────────────────
// 策略：
//   1. 降低 CPU 进程优先级 → 减少 GPU 命令提交速率（核显/独显均有效）
//   2. D3DKMT GPU 调度优先级降级 → 减少 GPU 时间片分配（核显/独显均有效）
//   3. EcoQoS 节流 → 笔记本核显降功耗模式

bool GpuLimiter::applyLimit(uint32_t pid, uint32_t percent) {
    percent = (std::min)(percent, 99u);
    percent = (std::max)(percent, 1u);

    removeLimit(pid);

    HANDLE hProcess = OpenProcess(
        PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION,
        FALSE, pid);
    if (!hProcess) return false;

    GpuEntry entry{};
    entry.hProcess     = hProcess;
    entry.limitPct     = percent;
    entry.origPriority = GetPriorityClass(hProcess);

    // 1. 降低 CPU 优先级（减少帧生成速率，间接降低 GPU 负载）
    DWORD cpuPriority;
    if (percent <= 30)      cpuPriority = IDLE_PRIORITY_CLASS;
    else if (percent <= 60) cpuPriority = BELOW_NORMAL_PRIORITY_CLASS;
    else                    cpuPriority = BELOW_NORMAL_PRIORITY_CLASS;
    SetPriorityClass(hProcess, cpuPriority);

    // 2. D3DKMT GPU 调度优先级（对核显和独显的 GPU 时间片均有效）
    if (ensureD3dkmt()) {
        int gpuClass;
        if (percent <= 25)      gpuClass = 0; // IDLE
        else if (percent <= 60) gpuClass = 1; // BELOW_NORMAL
        else                    gpuClass = 2; // NORMAL（仅靠CPU优先级）

        GPU_SCHED_REQ req{};
        req.hProcess      = hProcess;
        req.PriorityClass = gpuClass;
        LONG st = s_SetGpuPriority(&req);
        entry.gpuPrioritySet  = (st == 0L); // STATUS_SUCCESS = 0
        entry.origGpuPriority = 2;           // NORMAL
    }

    // 3. EcoQoS 节流（Win11 效果显著，老版本无害）
    PROCESS_POWER_THROTTLING_STATE throttle{};
    throttle.Version     = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttle.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    throttle.StateMask   = (percent <= 60)
        ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED
        : 0u;
    SetProcessInformation(hProcess, ProcessPowerThrottling,
                          &throttle, sizeof(throttle));

    entries_[pid] = entry;
    return true;
}

// ─── 恢复 ─────────────────────────────────────────────────────────────────────

void GpuLimiter::restoreEntry(GpuEntry& e) {
    if (!e.hProcess || e.hProcess == INVALID_HANDLE_VALUE) return;

    // 恢复 CPU 优先级
    SetPriorityClass(e.hProcess, e.origPriority);

    // 恢复 GPU 调度优先级
    if (e.gpuPrioritySet && ensureD3dkmt()) {
        GPU_SCHED_REQ req{};
        req.hProcess      = e.hProcess;
        req.PriorityClass = e.origGpuPriority;
        s_SetGpuPriority(&req);
    }

    // 解除 EcoQoS 节流
    PROCESS_POWER_THROTTLING_STATE throttle{};
    throttle.Version     = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttle.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    throttle.StateMask   = 0;
    SetProcessInformation(e.hProcess, ProcessPowerThrottling,
                          &throttle, sizeof(throttle));

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
