#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include "process_scanner.h"
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <pdh.h>
#include <string>
#include <algorithm>
#include <chrono>
#include <thread>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "psapi.lib")

// ─── PDH 状态（GPU 查询）────────────────────────────────────────────────────

struct ProcessScanner::PdhState {
    PDH_HQUERY   query   = nullptr;
    PDH_HCOUNTER counter = nullptr;
    bool         valid   = false;

    PdhState() {
        if (PdhOpenQuery(nullptr, 0, &query) != ERROR_SUCCESS) return;
        // GPU Engine 利用率（所有 engtype_3D 引擎）
        WCHAR path[] = L"\\GPU Engine(*)\\Utilization Percentage";
        if (PdhAddEnglishCounterW(query, path, 0, &counter) != ERROR_SUCCESS) {
            PdhCloseQuery(query);
            query = nullptr;
            return;
        }
        PdhCollectQueryData(query); // 初始采样
        valid = true;
    }

    ~PdhState() {
        if (query) PdhCloseQuery(query);
    }
};

// ─── 辅助：宽字符大小写不敏感比较 ────────────────────────────────────────────

static bool wstr_iequal(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

static std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0,
                                 w.c_str(), static_cast<int>(w.size()),
                                 nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0,
                        w.c_str(), static_cast<int>(w.size()),
                        &s[0], n, nullptr, nullptr);
    return s;
}

// ─── 构造/析构 ────────────────────────────────────────────────────────────────

ProcessScanner::ProcessScanner()
    : pdh_(std::make_unique<PdhState>())
{}

ProcessScanner::~ProcessScanner() {
    stop();
}

// ─── 启动/停止 ────────────────────────────────────────────────────────────────

void ProcessScanner::start(UpdateCallback cb, uint32_t intervalMs) {
    if (running_) return;
    callback_    = std::move(cb);
    intervalMs_  = intervalMs;
    running_     = true;
    thread_      = std::thread(&ProcessScanner::workerLoop, this);
}

void ProcessScanner::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

// ─── 工作线程 ─────────────────────────────────────────────────────────────────

void ProcessScanner::workerLoop() {
    while (running_) {
        auto result = scanOnce();
        if (callback_) callback_(result);
        std::this_thread::sleep_for(
            std::chrono::milliseconds(intervalMs_));
    }
}

// ─── 判断是否为 Minecraft 进程 ────────────────────────────────────────────────

bool ProcessScanner::isMinecraftProcess(const std::wstring& name) {
    for (auto* mc : MC_PROCESS_NAMES) {
        if (wstr_iequal(name, mc)) return true;
    }
    return false;
}

// ─── 获取进程路径 ─────────────────────────────────────────────────────────────

std::string ProcessScanner::getProcessPath(HANDLE hProcess) {
    WCHAR buf[MAX_PATH]{};
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(hProcess, 0, buf, &size))
        return wide_to_utf8(buf);
    return {};
}

// ─── CPU 使用率测量（差分法）─────────────────────────────────────────────────

double ProcessScanner::measureCpuUsage(uint32_t pid, HANDLE hProcess) {
    FILETIME ftCreate, ftExit, ftKernel, ftUser;
    if (!GetProcessTimes(hProcess, &ftCreate, &ftExit, &ftKernel, &ftUser))
        return 0.0;

    auto toULL = [](const FILETIME& ft) -> ULONGLONG {
        return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) |
                ft.dwLowDateTime;
    };

    ULONGLONG kernelNow = toULL(ftKernel);
    ULONGLONG userNow   = toULL(ftUser);

    SYSTEMTIME st{};
    FILETIME ftNow;
    GetSystemTimeAsFileTime(&ftNow);
    ULONGLONG wallNow = toULL(ftNow);

    auto it = cpuPrev_.find(pid);
    if (it == cpuPrev_.end()) {
        cpuPrev_[pid] = { kernelNow, userNow, wallNow };
        return 0.0;
    }

    auto& prev = it->second;
    ULONGLONG dKernel = kernelNow - prev.kernelTime;
    ULONGLONG dUser   = userNow   - prev.userTime;
    ULONGLONG dWall   = wallNow   - prev.wallTime;

    prev = { kernelNow, userNow, wallNow };

    if (dWall == 0) return 0.0;

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    double cores = static_cast<double>(si.dwNumberOfProcessors);

    double usage = static_cast<double>(dKernel + dUser) /
                   static_cast<double>(dWall) * 100.0 / cores;
    return (std::min)(usage, 100.0);
}

// ─── GPU 使用率（PDH）────────────────────────────────────────────────────────

double ProcessScanner::measureGpuUsage(uint32_t pid) {
    if (!pdh_ || !pdh_->valid) return 0.0;

    // 刷新 PDH 采样
    if (PdhCollectQueryData(pdh_->query) != ERROR_SUCCESS) return 0.0;

    // 读取所有实例，累加属于该 PID 的值
    DWORD  bufSize = 0, itemCount = 0;
    PDH_FMT_COUNTERVALUE_ITEM_W* items = nullptr;

    PdhGetFormattedCounterArrayW(pdh_->counter,
        PDH_FMT_DOUBLE, &bufSize, &itemCount, nullptr);
    if (bufSize == 0) return 0.0;

    items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(
                new BYTE[bufSize]);

    PDH_STATUS st = PdhGetFormattedCounterArrayW(
        pdh_->counter, PDH_FMT_DOUBLE,
        &bufSize, &itemCount, items);

    double total = 0.0;
    if (st == ERROR_SUCCESS) {
        std::wstring pidStr = std::to_wstring(pid);
        for (DWORD i = 0; i < itemCount; ++i) {
            // 实例名格式：<luid>_<engtype>_<pid>_<engindex>
            // 检索含有该 pid 的实例
            std::wstring inst = items[i].szName;
            // 在实例名中查找 "pid_<pid>_"
            std::wstring needle = L"pid_" + pidStr + L"_";
            if (inst.find(needle) != std::wstring::npos) {
                total += items[i].FmtValue.doubleValue;
            }
        }
    }
    delete[] reinterpret_cast<BYTE*>(items);

    return (std::min)(total, 100.0);
}

// ─── 扫描一次 ─────────────────────────────────────────────────────────────────

std::vector<ipc::ProcessInfo> ProcessScanner::scanOnce() {
    std::vector<ipc::ProcessInfo> result;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return result;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);

    if (!Process32FirstW(hSnap, &pe)) {
        CloseHandle(hSnap);
        return result;
    }

    do {
        std::wstring name = pe.szExeFile;
        if (!isMinecraftProcess(name)) continue;

        uint32_t pid = pe.th32ProcessID;

        HANDLE hProc = OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
            FALSE, pid);
        if (!hProc) continue;

        ipc::ProcessInfo info{};
        info.pid      = pid;
        info.name     = wide_to_utf8(name);
        info.exePath  = getProcessPath(hProc);
        info.cpuUsage = measureCpuUsage(pid, hProc);
        info.gpuUsage = measureGpuUsage(pid);
        // 限制状态由 App 层填充（持有 Limiter 对象）

        result.push_back(std::move(info));
        CloseHandle(hProc);

    } while (Process32NextW(hSnap, &pe));

    CloseHandle(hSnap);

    // 清理已消失进程的历史 CPU 样本
    std::vector<uint32_t> foundPids;
    for (auto& p : result) foundPids.push_back(p.pid);
    for (auto it = cpuPrev_.begin(); it != cpuPrev_.end(); ) {
        bool found = false;
        for (auto pid : foundPids)
            if (pid == it->first) { found = true; break; }
        it = found ? std::next(it) : cpuPrev_.erase(it);
    }

    return result;
}
