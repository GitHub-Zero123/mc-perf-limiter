#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
// GDI+ 需要在 windows.h 之后、且不能有 WIN32_LEAN_AND_MEAN 省掉 objidl.h
// 所以先包含完整 windows 再 undef 限制
#include <windows.h>
#include <objidl.h>
// 关闭 GDI+ 最低版本要求，允许使用完整 API
#ifndef GDIPVER
#  define GDIPVER 0x0110
#endif
#include <gdiplus.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <pdh.h>
#include <shellapi.h>
#include <shlobj.h>
#include "process_scanner.h"
#include <string>
#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")

using namespace Gdiplus;

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

// ─── GDI+ 辅助：提取 exe 图标为 Base64 PNG ───────────────────────────────────

static ULONG_PTR s_gdiplusToken = 0;

static void ensureGdiplus() {
    if (s_gdiplusToken) return;
    Gdiplus::GdiplusStartupInput si;
    Gdiplus::GdiplusStartup(&s_gdiplusToken, &si, nullptr);
}

// 获取 IStream CLSID for PNG
static bool getPngClsid(CLSID& clsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (!size) return false;
    auto* buf = reinterpret_cast<Gdiplus::ImageCodecInfo*>(new BYTE[size]);
    Gdiplus::GetImageEncoders(num, size, buf);
    bool found = false;
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(buf[i].MimeType, L"image/png") == 0) {
            clsid = buf[i].Clsid;
            found = true;
            break;
        }
    }
    delete[] reinterpret_cast<BYTE*>(buf);
    return found;
}

// Base64 编码
static std::string base64Encode(const std::vector<BYTE>& data) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        BYTE b0 = data[i];
        BYTE b1 = (i + 1 < data.size()) ? data[i + 1] : 0;
        BYTE b2 = (i + 2 < data.size()) ? data[i + 2] : 0;
        out += tbl[b0 >> 2];
        out += tbl[((b0 & 3) << 4) | (b1 >> 4)];
        out += (i + 1 < data.size()) ? tbl[((b1 & 15) << 2) | (b2 >> 6)] : '=';
        out += (i + 2 < data.size()) ? tbl[b2 & 63] : '=';
    }
    return out;
}

// 从 HICON 生成 16×16 PNG Base64 字符串
static std::string iconToBase64Png(HICON hIcon) {
    ensureGdiplus();

    // 创建 16×16 内存 DC
    HDC hdc = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(hdc);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = 16;
    bmi.bmiHeader.biHeight      = -16; // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP hOld = static_cast<HBITMAP>(SelectObject(memDC, hBmp));

    // 填充透明背景
    RECT r{ 0, 0, 16, 16 };
    FillRect(memDC, &r, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    DrawIconEx(memDC, 0, 0, hIcon, 16, 16, 0, nullptr, DI_NORMAL);
    SelectObject(memDC, hOld);

    // GDI+ bitmap → IStream → PNG bytes
    Gdiplus::Bitmap bmp(hBmp, nullptr);
    CLSID pngClsid;
    std::string result;
    if (getPngClsid(pngClsid)) {
        IStream* stream = nullptr;
        if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) {
            if (bmp.Save(stream, &pngClsid) == Gdiplus::Ok) {
                LARGE_INTEGER li{}; ULARGE_INTEGER uli{};
                stream->Seek(li, STREAM_SEEK_END, &uli);
                DWORD sz = static_cast<DWORD>(uli.QuadPart);
                std::vector<BYTE> buf(sz);
                stream->Seek(li, STREAM_SEEK_SET, nullptr);
                ULONG read = 0;
                stream->Read(buf.data(), sz, &read);
                result = "data:image/png;base64," + base64Encode(buf);
            }
            stream->Release();
        }
    }

    DeleteObject(hBmp);
    DeleteDC(memDC);
    ReleaseDC(nullptr, hdc);
    return result;
}

// 从 exe 路径提取图标
std::string ProcessScanner::extractIcon(const std::string& exePath) {
    if (exePath.empty()) return {};
    // UTF-8 → wide
    int n = MultiByteToWideChar(CP_UTF8, 0, exePath.c_str(),
                                 static_cast<int>(exePath.size()), nullptr, 0);
    std::wstring wpath(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, exePath.c_str(),
                         static_cast<int>(exePath.size()), &wpath[0], n);

    SHFILEINFOW sfi{};
    UINT flags = SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES;
    DWORD_PTR ok = SHGetFileInfoW(wpath.c_str(), FILE_ATTRIBUTE_NORMAL,
                                   &sfi, sizeof(sfi), flags);
    if (!ok || !sfi.hIcon) return {};

    std::string b64 = iconToBase64Png(sfi.hIcon);
    DestroyIcon(sfi.hIcon);
    return b64;
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

// ─── 内存使用量 ───────────────────────────────────────────────────────────────

uint64_t ProcessScanner::measureMemoryUsage(HANDLE hProcess) {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(hProcess,
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return 0;
    // PrivateUsage = 进程独占物理内存（Working Set 的私有部分）
    return static_cast<uint64_t>(pmc.PrivateUsage);
}

// ─── IO 速率（差分法，字节/秒）────────────────────────────────────────────────

std::pair<uint64_t, uint64_t> ProcessScanner::measureIoRate(
        uint32_t pid, HANDLE hProcess) {
    IO_COUNTERS ioc{};
    if (!GetProcessIoCounters(hProcess, &ioc))
        return {0, 0};

    FILETIME ftNow{};
    GetSystemTimeAsFileTime(&ftNow);
    auto toULL = [](const FILETIME& ft) -> ULONGLONG {
        return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    };
    ULONGLONG wallNow = toULL(ftNow);

    auto it = ioPrev_.find(pid);
    if (it == ioPrev_.end()) {
        ioPrev_[pid] = { ioc.ReadTransferCount, ioc.WriteTransferCount, wallNow };
        return {0, 0};
    }

    auto& prev = it->second;
    ULONGLONG dWall  = wallNow - prev.wallTime;
    if (dWall == 0) return {0, 0};

    // dWall 单位为 100ns，转换为秒
    double secs = static_cast<double>(dWall) / 10000000.0;

    uint64_t readPS  = static_cast<uint64_t>(
        (ioc.ReadTransferCount  - prev.readBytes)  / secs);
    uint64_t writePS = static_cast<uint64_t>(
        (ioc.WriteTransferCount - prev.writeBytes) / secs);

    prev = { ioc.ReadTransferCount, ioc.WriteTransferCount, wallNow };
    return {readPS, writePS};
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
        info.pid        = pid;
        info.name       = wide_to_utf8(name);
        info.exePath    = getProcessPath(hProc);
        info.cpuUsage   = measureCpuUsage(pid, hProc);
        info.gpuUsage   = measureGpuUsage(pid);
        info.memoryUsage = measureMemoryUsage(hProc);
        auto [ioR, ioW] = measureIoRate(pid, hProc);
        info.ioReadBytes  = ioR;
        info.ioWriteBytes = ioW;
        // 网络流量：暂未实现，置零
        info.netRecvBytes = 0;
        info.netSendBytes = 0;

        // 图标：首次出现时提取并缓存，避免每次扫描重复解码
        auto iconIt = iconCache_.find(pid);
        if (iconIt == iconCache_.end()) {
            iconCache_[pid] = extractIcon(info.exePath);
        }
        info.iconBase64 = iconCache_[pid];
        // 限制状态由 App 层填充（持有 Limiter 对象）

        result.push_back(std::move(info));
        CloseHandle(hProc);

    } while (Process32NextW(hSnap, &pe));

    CloseHandle(hSnap);

    // 清理已消失进程的历史样本 & 图标缓存
    std::vector<uint32_t> foundPids;
    for (auto& p : result) foundPids.push_back(p.pid);

    auto cleanMap = [&foundPids](auto& m) {
        for (auto it = m.begin(); it != m.end(); ) {
            bool found = false;
            for (auto pid : foundPids)
                if (pid == it->first) { found = true; break; }
            it = found ? std::next(it) : m.erase(it);
        }
    };
    cleanMap(cpuPrev_);
    cleanMap(ioPrev_);
    cleanMap(iconCache_);

    return result;
}
