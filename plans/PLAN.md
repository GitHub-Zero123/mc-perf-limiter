# MCPerfLimiter 开发计划

> **项目定位**：面向 Minecraft 开发者的性能限制工具，模拟低性能环境测试。
> **技术栈**：C++17 后端 + WebView2 前端（HTML/CSS/JS），CMake 构建，Windows-only。

---

## 架构总览

```
┌─────────────────────────────────────────────────────────┐
│                    MCPerfLimiter.exe                    │
│                                                         │
│  ┌──────────────┐        ┌─────────────────────────┐   │
│  │  C++ Backend │◄──────►│  WebView2 Frontend      │   │
│  │              │  IPC   │  (HTML/CSS/JS)           │   │
│  │ - Win32 窗口  │  JSON  │ - 自制标题栏             │   │
│  │ - 进程扫描    │        │ - 控制面板               │   │
│  │ - CPU 限制    │        │ - 实时性能图表           │   │
│  │ - GPU 限制    │        │ - 主题切换               │   │
│  │ - 系统主题检测│        │                         │   │
│  └──────────────┘        └─────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

---

## 目录结构规划

```
mc-perf-limiter/
├── CMakeLists.txt
├── CMakePresets.json
├── plans/
│   └── PLAN.md                  ← 本文件
├── include/
│   ├── app.h                    ← 应用主类声明
│   ├── window.h                 ← 自定义 Win32 窗口
│   ├── webview_bridge.h         ← WebView2 封装 + IPC
│   ├── process_scanner.h        ← 进程扫描（Minecraft）
│   ├── cpu_limiter.h            ← CPU 使用率限制
│   ├── gpu_limiter.h            ← GPU 使用率限制
│   ├── theme_detector.h         ← 系统主题检测
│   └── ipc_types.h              ← IPC 消息类型定义（nlohmann/json）
├── src/
│   ├── main.cpp                 ← 入口，App 初始化
│   ├── app.cpp
│   ├── window.cpp               ← 无边框窗口 + Win11 圆角 + Mica/Acrylic
│   ├── webview_bridge.cpp
│   ├── process_scanner.cpp
│   ├── cpu_limiter.cpp
│   ├── gpu_limiter.cpp
│   └── theme_detector.cpp
├── ui/                          ← 前端工程（Vite + Vanilla TS）
│   ├── package.json
│   ├── vite.config.ts
│   ├── index.html
│   ├── src/
│   │   ├── main.ts
│   │   ├── bridge.ts            ← 与 C++ 后端通信层
│   │   ├── theme.ts             ← 主题管理
│   │   ├── components/
│   │   │   ├── TitleBar.ts      ← 自制标题栏组件
│   │   │   ├── ProcessList.ts   ← 进程列表
│   │   │   ├── LimiterPanel.ts  ← CPU/GPU 限制控制面板
│   │   │   └── StatsChart.ts    ← 实时性能图表
│   │   └── styles/
│   │       ├── base.css
│   │       ├── theme-dark.css
│   │       ├── theme-light.css
│   │       └── components.css
│   └── dist/                    ← Vite 构建输出（C++ 加载此目录）
├── third_party/
│   ├── nlohmann/json.hpp
│   ├── nfd/                     ← native file dialog
│   └── webview2/                ← WebView2 SDK
└── scripts/
    └── setup.py                 ← 环境初始化脚本
```

---

## 阶段划分

### Phase 0 — 项目脚手架（基础工程）
**目标**：能编译并弹出一个最小化的 WebView2 窗口。

| 任务 | 文件 | 说明 |
|------|------|------|
| 补全 CMakeLists.txt | `CMakeLists.txt` | 添加资源文件、manifest、链接 pdh.lib |
| 创建 Win32 无边框窗口 | `src/window.cpp` | `WS_POPUP` + DWM 扩展 + `WM_NCHITTEST` |
| Win11 圆角支持 | `src/window.cpp` | `DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE)` |
| Mica/Acrylic 背景 | `src/window.cpp` | `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE)` |
| WebView2 初始化 | `src/webview_bridge.cpp` | 加载 `ui/dist/index.html` |
| 应用入口 | `src/main.cpp` | 消息循环 |
| 应用清单 | `resources/app.manifest` | 声明 DPI-aware、Win11 兼容 |

### Phase 1 — IPC 通信层
**目标**：前后端双向消息通道。

| 任务 | 文件 | 说明 |
|------|------|------|
| 定义消息协议 | `include/ipc_types.h` | JSON 消息格式规范 |
| C++ → JS 推送 | `src/webview_bridge.cpp` | `PostWebMessageAsJson()` |
| JS → C++ 调用 | `src/webview_bridge.cpp` | `WebMessageReceived` 事件处理 |
| 前端 bridge 模块 | `ui/src/bridge.ts` | 封装 `window.chrome.webview` API |

**IPC 消息格式**：
```jsonc
// JS → C++  (请求)
{ "cmd": "setLimit", "payload": { "pid": 1234, "cpu": 30, "gpu": 50 } }
{ "cmd": "getProcessList" }
{ "cmd": "setTheme", "payload": { "theme": "dark" } }
{ "cmd": "windowControl", "payload": { "action": "minimize" } }

// C++ → JS  (响应/推送)
{ "event": "processList",  "data": [ { "pid": 1234, "name": "Minecraft.Windows.exe", "cpu": 45.2, "gpu": 12.1 } ] }
{ "event": "limitApplied", "data": { "pid": 1234, "success": true } }
{ "event": "themeChanged", "data": { "theme": "dark" } }
{ "event": "statsUpdate",  "data": { "pid": 1234, "cpu": 28.3, "gpu": 49.7 } }
```

### Phase 2 — 进程扫描
**目标**：列出含 Minecraft 名称的进程并实时刷新。

| 任务 | 文件 | API |
|------|------|-----|
| 枚举所有进程 | `src/process_scanner.cpp` | `CreateToolhelp32Snapshot` / `EnumProcesses` |
| 过滤 Minecraft 进程 | `src/process_scanner.cpp` | 名称匹配：`Minecraft`, `javaw`, `bedrock_server` 等 |
| 读取 CPU/GPU 使用率 | `src/process_scanner.cpp` | PDH (`pdh.h`) 或 `GetProcessTimes` |
| 定时推送进程列表 | `src/process_scanner.cpp` | 每 1s 推送 `processList` 事件 |

**Minecraft 进程识别白名单**：
- `Minecraft.Windows.exe` (UWP/基岩版)
- `javaw.exe` (Java 版，需过滤非 MC 进程)
- `java.exe`
- `bedrock_server.exe`
- `MinecraftLauncher.exe`

### Phase 3 — CPU 限制器
**目标**：通过周期性挂起/恢复目标线程实现 CPU 限制。

| 任务 | 文件 | 说明 |
|------|------|------|
| Job Object 限制（优先） | `src/cpu_limiter.cpp` | `SetInformationJobObject(JobObjectCpuRateControlInformation)` |
| 挂起/恢复限制（备选） | `src/cpu_limiter.cpp` | `SuspendThread` / `ResumeThread` 周期占比控制 |
| 限制应用/取消接口 | `include/cpu_limiter.h` | `applyLimit(pid, percent)` / `removeLimit(pid)` |
| 限制状态持久化 | `src/app.cpp` | 内存中维护限制表 |

**Job Object 方案（推荐）**：
```cpp
JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpuRate{};
cpuRate.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE
                     | JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
cpuRate.CpuRate = targetPercent * 100;  // 以 0.01% 为单位
SetInformationJobObject(hJob, JobObjectCpuRateControlInformation, &cpuRate, sizeof(cpuRate));
```

### Phase 4 — GPU 限制器
**目标**：限制目标进程的 GPU 使用率。

| 任务 | 文件 | 方案 |
|------|------|------|
| DirectX DXGI 帧率限制 | `src/gpu_limiter.cpp` | 通过注入帧睡眠降低 GPU 负载（DLL 注入） |
| Windows GPU 调度优先级 | `src/gpu_limiter.cpp` | `SetProcessInformation(ProcessPowerThrottling)` |
| GPU 占用监控 | `src/gpu_limiter.cpp` | PDH 计数器 `GPU Engine\Utilization Percentage` |

**注意**：GPU 限制比 CPU 限制复杂，实现分层：
1. **简单版**：通过 `SetPriorityClass` + `ProcessPowerThrottling` 降低 GPU 调度
2. **进阶版**：DXGI 帧率上限（需 DLL 注入）

### Phase 5 — 前端 UI
**目标**：完整 GUI，VSCode 风格，自制标题栏。

#### 5.1 项目初始化
- 使用 **Vite + TypeScript + Vanilla**（无框架，轻量）
- CSS 变量实现主题切换

#### 5.2 自制标题栏
```
┌─────────────────────────────────────────────────────────────┐
│  🎮 MCPerfLimiter v0.1.0      [主题切换🌙]  [─] [□] [✕]  │
└─────────────────────────────────────────────────────────────┘
```
- `-webkit-app-region: drag` 实现拖拽
- 最小化/最大化/关闭按钮发送 `windowControl` IPC 消息
- 双击标题栏最大化/还原

#### 5.3 主界面布局（VSCode 风格）
```
┌──────────────────────────────────────────────────────────────┐
│ [标题栏]                                              [─][□][✕]│
├──────────────┬───────────────────────────────────────────────┤
│  侧边栏       │  主内容区                                      │
│              │                                               │
│  📋 进程列表  │  ┌─── 进程信息 ──────────────────────────┐   │
│  > MC.Win.exe│  │ 名称: Minecraft.Windows.exe           │   │
│  > javaw.exe │  │ PID: 1234    状态: 运行中              │   │
│              │  └────────────────────────────────────────┘   │
│  ⚙️ 设置      │                                               │
│              │  ┌─── CPU 限制 ──────────────────────────┐   │
│              │  │ [████░░░░░░] 40%  当前: 45.2%         │   │
│              │  │ [启用限制 ●]                           │   │
│              │  └────────────────────────────────────────┘   │
│              │                                               │
│              │  ┌─── GPU 限制 ──────────────────────────┐   │
│              │  │ [████████░░] 80%  当前: 12.1%         │   │
│              │  │ [启用限制 ○]                           │   │
│              │  └────────────────────────────────────────┘   │
│              │                                               │
│              │  ┌─── 实时图表 ──────────────────────────┐   │
│              │  │  CPU ▁▂▄▆▃▂▅▄▃▂▁▃▄▅▆▄▂▁▃           │   │
│              │  │  GPU ▁▁▁▂▂▁▁▂▁▁▁▁▁▁▁▁▁▁▁           │   │
│              │  └────────────────────────────────────────┘   │
└──────────────┴───────────────────────────────────────────────┘
```

#### 5.4 主题配色
| 变量 | 暗色 | 亮色 |
|------|------|------|
| `--bg-primary` | `#1e1e1e` | `#ffffff` |
| `--bg-secondary` | `#252526` | `#f3f3f3` |
| `--bg-sidebar` | `#333333` | `#dddddd` |
| `--accent` | `#007acc` | `#005fb8` |
| `--text-primary` | `#cccccc` | `#1f1f1f` |
| `--text-secondary` | `#858585` | `#616161` |
| `--border` | `#474747` | `#e5e5e5` |
| `--titlebar-bg` | `#323233` | `#dddddd` |

#### 5.5 Win11 圆角 + Mica
- C++ 层：`DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_ROUND`
- C++ 层：`DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_MAINWINDOW`（Mica 效果）
- 窗口边框：移除标准边框，DWM 阴影保留

### Phase 6 — 系统主题检测
**目标**：跟随系统自动切换深色/浅色主题。

| 任务 | 文件 | API |
|------|------|-----|
| 读取系统主题 | `src/theme_detector.cpp` | 注册表 `AppsUseLightTheme` |
| 监听主题变化 | `src/theme_detector.cpp` | `WM_SETTINGCHANGE` 消息 |
| 推送主题变化 | `src/theme_detector.cpp` | IPC `themeChanged` 事件 |

```cpp
// 读取系统主题
DWORD value = 0;
RegGetValueW(HKEY_CURRENT_USER,
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
    L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
bool isDark = (value == 0);
```

---

## 技术关键点 & 注意事项

### 无边框窗口拖拽区域
C++ 处理 `WM_NCHITTEST`，将标题栏区域返回 `HTCAPTION`，按钮区域返回 `HTCLIENT`（由 JS 处理点击）。

### WebView2 数据目录
```cpp
// 使用 %APPDATA%/MCPerfLimiter 作为 WebView2 用户数据目录
auto userDataDir = std::filesystem::path(getenv("APPDATA")) / "MCPerfLimiter";
```

### Job Object 权限
部分 Minecraft 进程（UWP/沙盒）可能已在 Job Object 中，需要：
- 尝试 `AssignProcessToJobObject`
- 失败时降级为 `SuspendThread` 方案
- 需要 SeDebugPrivilege 或以管理员身份运行

### 管理员权限
应用需要以管理员身份运行（`app.manifest` 中设置 `requireAdministrator`）。

---

## 构建产物

| 文件 | 说明 |
|------|------|
| `build/.../bin/MCPerfLimiter.exe` | 主程序 |
| `ui/dist/` | 前端静态文件（运行时需要） |

---

## 实现顺序（推荐）

```
Phase 0 → Phase 1 → Phase 5（UI骨架）→ Phase 2 → Phase 3 → Phase 4 → Phase 6
```

先把窗口和通信层打通，然后做 UI 外壳，再接入业务逻辑。

---

## 依赖清单

| 库 | 版本 | 用途 |
|----|------|------|
| WebView2 SDK | 已集成 | 前端渲染引擎 |
| nlohmann/json | 已集成 | IPC 消息序列化 |
| nativefiledialog-extended | 已集成 | 文件对话框（可选） |
| PDH (Windows) | 系统自带 | CPU/GPU 性能计数器 |
| DWM API (Windows) | 系统自带 | 窗口美化、圆角、Mica |
| Vite + TypeScript | npm | 前端构建 |
