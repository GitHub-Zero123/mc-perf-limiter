#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include "gpu_limiter.h"
#include <windows.h>
#include <string>
#include <vector>

// ─── 工具：获取当前 exe 所在目录 ─────────────────────────────────────────────

static std::wstring getExeDir() {
    // 使用足够大的缓冲区，避免路径超过 MAX_PATH(260) 时截断
    wchar_t path[4096]{};
    DWORD len = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (len == 0) return L".";
    std::wstring s(path, len);
    auto pos = s.rfind(L'\\');
    return (pos != std::wstring::npos) ? s.substr(0, pos) : L".";
}

// ─── 百分比 → 目标 FPS 映射 ──────────────────────────────────────────────────
// 模拟移动端 GPU 性能档位：
//   percent=10  → 6  FPS（极低端）
//   percent=25  → 15 FPS（低端移动）
//   percent=50  → 30 FPS（中端移动）
//   percent=75  → 45 FPS（高端移动）
//   percent=90  → 54 FPS（接近 60fps 轻微限制）

static uint32_t percentToFps(uint32_t percent) {
    // 以 60 FPS 为基准（PC 上 MCBE 默认帧率上限）
    // 映射：percent% → 60 * (percent/100) FPS，最低 1 FPS
    uint32_t fps = static_cast<uint32_t>(60.0 * percent / 100.0 + 0.5);
    if (fps < 1)  fps = 1;
    if (fps > 60) fps = 60;
    return fps;
}

// ─── 构造/析构 ────────────────────────────────────────────────────────────────

GpuLimiter::GpuLimiter() = default;

GpuLimiter::~GpuLimiter() {
    removeAll();
}

// ─── 静态：检测 gpu_hook.dll 是否存在 ────────────────────────────────────────

bool GpuLimiter::isHookAvailable() {
    std::wstring dllPath = getDllPath();
    return GetFileAttributesW(dllPath.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// 返回所有被搜索的路径（用于调试显示，用换行分隔）
std::wstring GpuLimiter::getDllSearchPath() {
    static const wchar_t kDllName[] = L"gpu_hook.dll";
    std::wstring result;

    // ① exe 同目录
    std::wstring exeDir  = getExeDir();
    result += exeDir + L"\\" + kDllName;

    // ② 当前工作目录
    wchar_t cwd[4096]{};
    GetCurrentDirectoryW(static_cast<DWORD>(std::size(cwd)), cwd);
    std::wstring cwdPath = std::wstring(cwd) + L"\\" + kDllName;
    if (cwdPath != result)
        result += L"\n" + cwdPath;

    return result;
}

std::wstring GpuLimiter::getDllPath() {
    static const wchar_t kDllName[] = L"gpu_hook.dll";

    // ① exe 同目录
    std::wstring exeDir  = getExeDir();
    std::wstring exePath = exeDir + L"\\" + kDllName;
    OutputDebugStringW((L"[GpuLimiter] Checking exe dir: " + exePath).c_str());
    if (GetFileAttributesW(exePath.c_str()) != INVALID_FILE_ATTRIBUTES)
        return exePath;

    // ② 当前工作目录
    wchar_t cwd[4096]{};
    GetCurrentDirectoryW(static_cast<DWORD>(std::size(cwd)), cwd);
    std::wstring cwdPath = std::wstring(cwd) + L"\\" + kDllName;
    OutputDebugStringW((L"[GpuLimiter] Checking cwd: " + cwdPath).c_str());
    if (GetFileAttributesW(cwdPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        return cwdPath;

    // ③ 相对路径（兜底，适合直接双击运行的情形）
    OutputDebugStringW(L"[GpuLimiter] Checking relative: .\\gpu_hook.dll");
    if (GetFileAttributesW(kDllName) != INVALID_FILE_ATTRIBUTES)
        return kDllName;

    // 所有位置都找不到，返回 exe 目录路径让 isHookAvailable 返回 false
    OutputDebugStringW((L"[GpuLimiter] dll not found, searched: " + exePath).c_str());
    return exePath;
}

// ─── 注入 gpu_hook.dll ────────────────────────────────────────────────────────
// 注入前创建命名共享内存 "Local\mcperf_pid_{targetPid}"，写入主进程 PID（DWORD）
// DLL 在 DLL_PROCESS_ATTACH 中读取该共享内存，获得主进程 PID 后构造管道名
// 非静态方法：需要写入 entries_[pid].hMap

bool GpuLimiter::injectDll(uint32_t pid) {  // NOLINT（非静态，访问 entries_）
    std::wstring dllPath = getDllPath();
    OutputDebugStringW((L"[GpuLimiter] injectDll pid=" + std::to_wstring(pid) +
                        L" dllPath=" + dllPath).c_str());

    // ── 1. 创建命名共享内存，让 DLL 知道主进程 PID ──────────────────────────
    wchar_t mapName[64];
    swprintf_s(mapName, L"Local\\mcperf_pid_%u", pid);

    DWORD  myPid   = GetCurrentProcessId();
    HANDLE hMap    = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr,
        PAGE_READWRITE, 0, sizeof(DWORD), mapName);
    if (!hMap) {
        OutputDebugStringW((L"[GpuLimiter] CreateFileMappingW failed err=" +
                            std::to_wstring(GetLastError())).c_str());
        return false;
    }

    DWORD* pData = static_cast<DWORD*>(
        MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, sizeof(DWORD)));
    if (pData) {
        *pData = myPid;
        UnmapViewOfFile(pData);
        OutputDebugStringW((L"[GpuLimiter] shared mem created, myPid=" +
                            std::to_wstring(myPid)).c_str());
    }

    // ── 2. 打开目标进程 ──────────────────────────────────────────────────────
    HANDLE hProcess = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!hProcess) {
        DWORD err = GetLastError();
        OutputDebugStringW((L"[GpuLimiter] OpenProcess failed err=" +
                            std::to_wstring(err)).c_str());
        CloseHandle(hMap);
        return false;
    }
    OutputDebugStringW(L"[GpuLimiter] OpenProcess OK");

    // ── 3. 在目标进程分配内存写入 DLL 路径 ──────────────────────────────────
    size_t pathBytes = (dllPath.size() + 1) * sizeof(wchar_t);
    LPVOID remoteMem = VirtualAllocEx(hProcess, nullptr, pathBytes,
                                       MEM_COMMIT | MEM_RESERVE,
                                       PAGE_READWRITE);
    if (!remoteMem) {
        OutputDebugStringW((L"[GpuLimiter] VirtualAllocEx failed err=" +
                            std::to_wstring(GetLastError())).c_str());
        CloseHandle(hProcess);
        CloseHandle(hMap);
        return false;
    }

    if (!WriteProcessMemory(hProcess, remoteMem,
                             dllPath.c_str(), pathBytes, nullptr)) {
        OutputDebugStringW((L"[GpuLimiter] WriteProcessMemory failed err=" +
                            std::to_wstring(GetLastError())).c_str());
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        CloseHandle(hMap);
        return false;
    }

    // ── 4. CreateRemoteThread(LoadLibraryW) 完成注入 ────────────────────────
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC loadLib   = GetProcAddress(hKernel32, "LoadLibraryW");
    OutputDebugStringW((L"[GpuLimiter] LoadLibraryW addr=" +
                        std::to_wstring(reinterpret_cast<uintptr_t>(loadLib))).c_str());

    HANDLE hThread = CreateRemoteThread(
        hProcess, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLib),
        remoteMem, 0, nullptr);

    if (!hThread) {
        DWORD err = GetLastError();
        OutputDebugStringW((L"[GpuLimiter] CreateRemoteThread failed err=" +
                            std::to_wstring(err)).c_str());
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        CloseHandle(hMap);
        return false;
    }
    OutputDebugStringW(L"[GpuLimiter] CreateRemoteThread OK, waiting...");

    // 等待注入完成（最多 5 秒）
    DWORD waitRet = WaitForSingleObject(hThread, 5000);
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    OutputDebugStringW((L"[GpuLimiter] inject wait ret=" + std::to_wstring(waitRet) +
                        L" exitCode(hModule)=" + std::to_wstring(exitCode)).c_str());
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    // exitCode == 0 表示 LoadLibraryW 返回 NULL（加载失败）
    if (exitCode == 0) {
        OutputDebugStringW(L"[GpuLimiter] LoadLibraryW returned NULL - DLL load failed in target process!");
        CloseHandle(hMap);
        return false;
    }

    // hMap 保持打开，DLL 才能 OpenFileMappingW 读取 PID
    auto it = entries_.find(pid);
    if (it != entries_.end()) {
        it->second.hMap = hMap;
    } else {
        CloseHandle(hMap);
    }
    OutputDebugStringW(L"[GpuLimiter] injectDll SUCCESS");
    return true;
}

// ─── 向注入的 DLL 发送目标 FPS ────────────────────────────────────────────────
// 架构：DLL 作为服务端（CreateNamedPipe），主程序作为客户端（CreateFile + WriteFile）
// 原因：PIPE_NOWAIT 服务端在客户端未连接时 WriteFile 失败（ERROR_PIPE_LISTENING）
//       改为客户端模式，主程序可以在 DLL 就绪后随时连接并发送

bool GpuLimiter::sendFps(uint32_t pid, uint32_t fps) {
    // 管道名：DLL 创建，主程序连接（以目标进程 PID 区分）
    wchar_t pipeName[64];
    swprintf_s(pipeName, L"\\\\.\\pipe\\mcperf_gpu_%u", pid);

    OutputDebugStringW((L"[GpuLimiter] sendFps pid=" + std::to_wstring(pid) +
                        L" fps=" + std::to_wstring(fps) +
                        L" pipe=" + pipeName).c_str());

    // 尝试连接 DLL 服务端管道（最多重试 20 次，共等待约 2 秒）
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    for (int retry = 0; retry < 20; ++retry) {
        hPipe = CreateFileW(pipeName, GENERIC_WRITE, 0, nullptr,
                            OPEN_EXISTING, 0, nullptr);
        if (hPipe != INVALID_HANDLE_VALUE) break;

        DWORD err = GetLastError();
        if (err == ERROR_PIPE_BUSY) {
            // 等待管道可用
            WaitNamedPipeW(pipeName, 200);
        } else {
            Sleep(100);
        }
    }

    if (hPipe == INVALID_HANDLE_VALUE) {
        OutputDebugStringW((L"[GpuLimiter] sendFps: cannot connect to pipe, err=" +
                            std::to_wstring(GetLastError())).c_str());
        return false;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(hPipe, &fps, sizeof(fps), &written, nullptr);
    CloseHandle(hPipe);

    OutputDebugStringW((L"[GpuLimiter] sendFps WriteFile ok=" + std::to_wstring(ok) +
                        L" written=" + std::to_wstring(written)).c_str());
    return ok && written == sizeof(fps);
}

// ─── 应用限制 ─────────────────────────────────────────────────────────────────

bool GpuLimiter::applyLimit(uint32_t pid, uint32_t percent) {
    if (percent < 1u)  percent = 1u;
    if (percent > 99u) percent = 99u;

    if (!isHookAvailable()) return false;

    removeLimit(pid);

    Entry& entry = entries_[pid];
    entry.limitPct = percent;

    // 注入 DLL（如果还未注入）
    // 注意：注入是幂等的，LoadLibrary 对已加载模块会直接返回句柄
    entry.injected = injectDll(pid);

    if (entry.injected) {
        // DLL initThreadProc Sleep(1000) 后安装 hook 并启动管道服务端
        // sendFps 内部最多重试 20 次 × 100ms = 2 秒
        // 注意：applyLimit 由后台线程调用，可以在此 Sleep
        Sleep(200);
        // 直接发送百分比（DLL 端用 g_limitPct 存储算力百分比）
        sendFps(pid, percent);
    }

    return entry.injected;
}

// ─── 移除限制 ─────────────────────────────────────────────────────────────────

void GpuLimiter::removeLimit(uint32_t pid) {
    auto it = entries_.find(pid);
    if (it == entries_.end()) return;

    Entry& entry = it->second;

    // 发送 FPS=0 解除帧率限制
    if (entry.injected) {
        sendFps(pid, 0);
        Sleep(50);
    }

    // 关闭共享内存句柄（DLL 已读取完 PID，可以释放）
    if (entry.hMap) {
        CloseHandle(entry.hMap);
        entry.hMap = nullptr;
    }

    entries_.erase(it);
}

void GpuLimiter::removeAll() {
    // 收集 pids 避免迭代时修改
    std::vector<uint32_t> pids;
    pids.reserve(entries_.size());
    for (auto& [pid, _] : entries_) pids.push_back(pid);
    for (auto pid : pids) removeLimit(pid);
}

// ─── 查询 ─────────────────────────────────────────────────────────────────────

bool GpuLimiter::hasLimit(uint32_t pid) const {
    return entries_.count(pid) > 0;
}

uint32_t GpuLimiter::getLimit(uint32_t pid) const {
    auto it = entries_.find(pid);
    return (it != entries_.end()) ? it->second.limitPct : 0u;
}
