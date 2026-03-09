#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include "io_limiter.h"
#include <windows.h>

// NtSetInformationProcess 的最小声明（避免依赖 ntdll.h）
typedef LONG NTSTATUS;
#ifndef NT_SUCCESS
#  define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#endif
// ProcessIoPriority = 33
static constexpr int PROCESS_IO_PRIORITY = 33;

typedef NTSTATUS (NTAPI* PFN_NtSetInfo)(HANDLE, int, PVOID, ULONG);
static PFN_NtSetInfo s_NtSetInfo = nullptr;

static bool ensureNtApi() {
    if (s_NtSetInfo) return true;
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll) return false;
    s_NtSetInfo = reinterpret_cast<PFN_NtSetInfo>(
        GetProcAddress(hNtdll, "NtSetInformationProcess"));
    return s_NtSetInfo != nullptr;
}

// ─── 应用低 IO 优先级 ─────────────────────────────────────────────────────────

bool IoLimiter::applyLimit(uint32_t pid) {
    if (!ensureNtApi()) return false;

    HANDLE hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE,
                               static_cast<DWORD>(pid));
    if (!hProc) return false;

    // IoPriorityVeryLow = 0
    ULONG priority = 0;
    NTSTATUS st = s_NtSetInfo(hProc, PROCESS_IO_PRIORITY,
                              &priority, sizeof(priority));
    CloseHandle(hProc);

    if (NT_SUCCESS(st)) {
        limited_[pid] = true;
        return true;
    }
    return false;
}

// ─── 恢复正常 IO 优先级 ───────────────────────────────────────────────────────

void IoLimiter::removeLimit(uint32_t pid) {
    auto it = limited_.find(pid);
    if (it == limited_.end()) return;

    if (ensureNtApi()) {
        HANDLE hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE,
                                   static_cast<DWORD>(pid));
        if (hProc) {
            // IoPriorityNormal = 2
            ULONG priority = 2;
            s_NtSetInfo(hProc, PROCESS_IO_PRIORITY,
                        &priority, sizeof(priority));
            CloseHandle(hProc);
        }
    }
    limited_.erase(it);
}

// ─── 移除所有限制 ─────────────────────────────────────────────────────────────

void IoLimiter::removeAll() {
    for (auto& [pid, _] : limited_) {
        if (ensureNtApi()) {
            HANDLE hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE,
                                       static_cast<DWORD>(pid));
            if (hProc) {
                ULONG priority = 2;
                s_NtSetInfo(hProc, PROCESS_IO_PRIORITY,
                            &priority, sizeof(priority));
                CloseHandle(hProc);
            }
        }
    }
    limited_.clear();
}

// ─── 查询 ─────────────────────────────────────────────────────────────────────

bool IoLimiter::hasLimit(uint32_t pid) const {
    return limited_.count(pid) > 0;
}
