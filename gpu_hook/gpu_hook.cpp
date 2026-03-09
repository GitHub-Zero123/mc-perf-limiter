// gpu_hook.cpp — 注入到 Minecraft 进程的 GPU 算力模拟 DLL
//
// 目的：模拟低端移动端 GPU 算力（不是限制帧率，而是让渲染线程"消耗"更多 GPU 时间）
// 方法：在 wglSwapBuffers hook 中，每帧结束后用 QPC 忙等（busy-wait）额外占用 CPU/GPU
//        【不使用 Sleep】— Sleep 会让线程进入内核等待状态，触发 MC 看门狗崩溃
//        percent=50 → 忙等 frameTime * 1.0 → GPU 有效算力降低 50%
//        percent=30 → 忙等 frameTime * 2.33 → GPU 有效算力降低 70%
//
// hook 方式：5字节相对 JMP + 临时恢复模式（无 trampoline 截断问题）
// 通信：DLL=管道服务端，主程序=客户端，管道名 \\.\pipe\mcperf_gpu_{targetPid}
// 构建：x64 DLL，/MD 动态运行时

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#include <cstdarg>

// ─── 日志工具（仅 Debug 构建启用，Release 编译为空操作）───────────────────────

#ifdef _DEBUG
#  define GPU_HOOK_LOG 1
#endif

#if GPU_HOOK_LOG
static FILE* g_logFile = nullptr;

static void logInit() {
    if (g_logFile) return;
    wchar_t tmpPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmpPath);
    wchar_t logPath[MAX_PATH]{};
    swprintf_s(logPath, L"%sgpu_hook.log", tmpPath);
    _wfopen_s(&g_logFile, logPath, L"a");
}

static void logWrite(const char* fmt, ...) {
    if (!g_logFile) return;
    char buf[512]{};
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    fprintf(g_logFile, "%s\n", buf);
    fflush(g_logFile);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}

static void logClose() {
    if (g_logFile) { fclose(g_logFile); g_logFile = nullptr; }
}
#else
static void logInit()  {}
static void logWrite(const char* /*fmt*/, ...) {}
static void logClose() {}
#endif

// ─── 全局状态（纯 POD，避免静态构造器崩溃）────────────────────────────────────

// g_limitPct：GPU 算力百分比（0=不限制，50=降至50%算力）
// 实现：每帧结束后 QPC 忙等 renderTime*(100-pct)/pct 微秒
static volatile LONG  g_limitPct  = 0;    // 0~99，0=不限制
static volatile BOOL  g_running   = TRUE;
static volatile LONG  g_frameCount = 0;

static void* g_hookTarget   = nullptr;
static BYTE  g_origBytes5[5] = {};
static BOOL  g_hookInstalled = FALSE;
static CRITICAL_SECTION g_cs;
static BOOL  g_csInited = FALSE;

// ─── 帧时间估算 + 忙等 ────────────────────────────────────────────────────────
// 注意：不使用 Sleep()！Sleep 会让渲染线程进入内核等待，触发 MC 看门狗崩溃。
// 改用 QPC 自旋忙等（busy-wait），渲染线程保持"运行中"状态，不被看门狗检测到挂起。
// 代价：忙等期间 CPU 占用率会升高，但这是必要的权衡。
static LONGLONG g_qpfFreq      = 0;   // QueryPerformanceFrequency

// 忙等指定微秒数（不进入内核等待，避免触发看门狗）
// 最大等待 50ms（安全上限，超过会触发 MC 看门狗；调用方已过滤）
static void busyWaitUs(LONGLONG waitUs) {
    if (waitUs <= 0 || waitUs > 50000LL) return;  // 安全上限 50ms
    LARGE_INTEGER start, now;
    QueryPerformanceCounter(&start);
    LONGLONG targetTicks = start.QuadPart + waitUs * g_qpfFreq / 1000000LL;
    do {
        QueryPerformanceCounter(&now);
    } while (now.QuadPart < targetTicks);
}

// g_lastRenderQpc: 上一帧 wglSwapBuffers 被调用时的时间戳（不含忙等）
// 与 g_lastFrameQpc 区分：g_lastFrameQpc 在帧末（含忙等后），g_lastRenderQpc 在帧初
static LONGLONG g_lastRenderQpc = 0;

static void throttleGpu(int pct) {
    // pct = GPU 算力百分比（30 = 保留 30%，忙等占用剩余 70%）
    //
    // 关键：必须用"纯渲染时间"来计算忙等，而不能用"上一帧总耗时"
    // 否则忙等时间会被计入下一帧的"渲染时间"，导致正反馈爆炸
    //
    // 时间线：[renderA]--swap--[busyWait]--[renderB]--swap--...
    //         ^                            ^
    //         g_lastRenderQpc              此处采样 = renderB 时长
    //
    // renderB 时长 = now - g_lastRenderQpc - busyWait(A)
    // 但我们无法减去 busyWait(A)，所以改为：
    // 在忙等开始前记录时间戳，下一帧用该时间戳计算纯渲染时间

    LARGE_INTEGER swapTime;
    QueryPerformanceCounter(&swapTime);  // wglSwapBuffers 刚返回时的时间戳

    if (g_lastRenderQpc > 0 && g_qpfFreq > 0) {
        // elapsed = 从上一帧忙等结束到本帧 swap 结束（= 本帧纯渲染时间）
        LONGLONG elapsed = swapTime.QuadPart - g_lastRenderQpc;
        double renderUs = (double)elapsed * 1000000.0 / (double)g_qpfFreq;

        // 合理性过滤：渲染时间应在 0.1ms ~ 100ms（10~10000fps 对应范围）
        if (renderUs > 100.0 && renderUs < 100000.0) {
            double waitUs = renderUs * (100.0 - pct) / (double)pct;
            // 忙等上限 50ms，防止触发 MC 看门狗
            if (waitUs > 100.0 && waitUs < 50000.0) {
                busyWaitUs((LONGLONG)waitUs);
            }
        }
    }

    // 在忙等结束后记录时间戳，供下一帧计算"纯渲染时间"用
    // 注意：这里在 busyWaitUs 之后执行，所以下一帧的 elapsed 不含本次忙等
    QueryPerformanceCounter(&swapTime);
    g_lastRenderQpc = swapTime.QuadPart;
}

// ─── 5字节相对 JMP hook ───────────────────────────────────────────────────────

typedef BOOL (WINAPI* PFN_wglSwapBuffers)(HDC hdc);

static void writeJmp5(void* from, void* to) {
    BYTE jmp[5];
    jmp[0] = 0xE9;
    LONGLONG rel = (LONGLONG)to - (LONGLONG)from - 5;
    *(INT32*)&jmp[1] = (INT32)rel;
    DWORD old;
    VirtualProtect(from, 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy(from, jmp, 5);
    VirtualProtect(from, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), from, 5);
}

static void restoreOrig(void* target) {
    DWORD old;
    VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy(target, g_origBytes5, 5);
    VirtualProtect(target, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, 5);
}

static BOOL WINAPI hook_wglSwapBuffers(HDC hdc) {
    // 临时恢复 → 调用真实函数 → 重新写 hook
    EnterCriticalSection(&g_cs);
    restoreOrig(g_hookTarget);
    BOOL ret = ((PFN_wglSwapBuffers)g_hookTarget)(hdc);
    writeJmp5(g_hookTarget, (void*)hook_wglSwapBuffers);
    LeaveCriticalSection(&g_cs);

    // GPU 算力模拟：帧结束后忙等，降低 GPU 有效算力比例
    int pct = (int)InterlockedCompareExchange(&g_limitPct, 0, 0);
    LONG fc = InterlockedIncrement(&g_frameCount);
    if (fc % 300 == 0) {
        logWrite("[gpu_hook] frame %ld limitPct=%d", fc, pct);
    }
    if (pct > 0 && pct < 100) {
        throttleGpu(pct);
    } else {
        // pct=0 时仍更新帧时间基准（供下次限制时使用）
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        g_lastRenderQpc = now.QuadPart;
    }

    return ret;
}

static bool installHook() {
    HMODULE hOgl = GetModuleHandleW(L"opengl32.dll");
    logWrite("[gpu_hook] opengl32.dll: %p", hOgl);
    if (!hOgl) return false;

    void* fn = (void*)GetProcAddress(hOgl, "wglSwapBuffers");
    logWrite("[gpu_hook] wglSwapBuffers: %p", fn);
    if (!fn) return false;

    LONGLONG rel = (LONGLONG)hook_wglSwapBuffers - (LONGLONG)fn - 5;
    logWrite("[gpu_hook] rel32=%lld", rel);
    if (rel > 0x7FFFFFFF || rel < (LONGLONG)0xFFFFFFFF80000000LL) {
        logWrite("[gpu_hook] rel32 out of range!");
        return false;
    }

    g_hookTarget = fn;
    memcpy(g_origBytes5, fn, 5);
    logWrite("[gpu_hook] orig5: %02X %02X %02X %02X %02X",
             g_origBytes5[0], g_origBytes5[1], g_origBytes5[2],
             g_origBytes5[3], g_origBytes5[4]);

    writeJmp5(fn, (void*)hook_wglSwapBuffers);
    g_hookInstalled = TRUE;
    logWrite("[gpu_hook] hook installed OK");
    return true;
}

// ─── 命名管道服务端线程 ───────────────────────────────────────────────────────

static DWORD WINAPI pipeThreadProc(LPVOID) {
    DWORD myPid = GetCurrentProcessId();
    wchar_t pipeName[64];
    swprintf_s(pipeName, 64, L"\\\\.\\pipe\\mcperf_gpu_%u", myPid);
    logWrite("[gpu_hook] pipe: '%S'", pipeName);

    while (g_running) {
        HANDLE hPipe = CreateNamedPipeW(pipeName,
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            16, 16, 0, nullptr);
        if (hPipe == INVALID_HANDLE_VALUE) { Sleep(500); continue; }

        BOOL ok2 = ConnectNamedPipe(hPipe, nullptr);
        DWORD ce = GetLastError();
        if (!ok2 && ce != ERROR_PIPE_CONNECTED) {
            CloseHandle(hPipe); continue;
        }

        UINT32 fps = 0; DWORD rd = 0;
        if (ReadFile(hPipe, &fps, 4, &rd, nullptr) && rd == 4) {
            logWrite("[gpu_hook] recv fps=%u (used as pct)", fps);
            // fps 值实际上被当作算力百分比
            InterlockedExchange(&g_limitPct, (LONG)fps);
        }
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }
    return 0;
}

// ─── 初始化线程 ───────────────────────────────────────────────────────────────

static DWORD WINAPI initThreadProc(LPVOID) {
    logWrite("[gpu_hook] init thread PID=%u", GetCurrentProcessId());
    Sleep(1000);  // 等待 MC OpenGL 初始化

    bool ok = installHook();
    logWrite("[gpu_hook] hook: %s", ok ? "OK" : "FAILED");

    CreateThread(nullptr, 0, pipeThreadProc, nullptr, 0, nullptr);
    return 0;
}

// ─── DLL 入口点 ───────────────────────────────────────────────────────────────

BOOL APIENTRY DllMain(HMODULE /*hMod*/, DWORD reason, LPVOID /*res*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        // 日志初始化必须最先执行
        logInit();
        logWrite("[gpu_hook] DLL_PROCESS_ATTACH pid=%u", GetCurrentProcessId());

        // QPC 频率（用于帧时间估算）
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        g_qpfFreq = freq.QuadPart;

        // CriticalSection 初始化
        InitializeCriticalSection(&g_cs);
        g_csInited = TRUE;

        CreateThread(nullptr, 0, initThreadProc, nullptr, 0, nullptr);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        g_running = FALSE;
        if (g_hookInstalled && g_hookTarget && g_csInited) {
            EnterCriticalSection(&g_cs);
            restoreOrig(g_hookTarget);
            LeaveCriticalSection(&g_cs);
        }
        if (g_csInited) DeleteCriticalSection(&g_cs);
        logClose();
    }
    return TRUE;
}
