/**
 * bridge.ts — 与 C++ 后端通信层
 * 封装 window.chrome.webview API，提供类型安全的 IPC 接口
 */
// ─── Bridge 类 ────────────────────────────────────────────────────────────────
class Bridge {
    constructor() {
        Object.defineProperty(this, "handlers", {
            enumerable: true,
            configurable: true,
            writable: true,
            value: new Map()
        });
        Object.defineProperty(this, "isWebView2", {
            enumerable: true,
            configurable: true,
            writable: true,
            value: false
        });
        // 检测是否在 WebView2 环境中
        this.isWebView2 = !!window.chrome?.webview;
        if (this.isWebView2) {
            ;
            window.chrome.webview.addEventListener('message', (ev) => {
                try {
                    const msg = typeof ev.data === 'string'
                        ? JSON.parse(ev.data)
                        : ev.data;
                    this.dispatch(msg.event, msg.data);
                }
                catch (e) {
                    console.error('[Bridge] Failed to parse message:', ev.data, e);
                }
            });
        }
        else {
            // 开发模式：注入模拟数据
            console.info('[Bridge] Running in browser dev mode — using mock data');
            this.startMockMode();
        }
    }
    // ── 发送命令 ──────────────────────────────────────────────────────────────
    send(cmd) {
        const json = JSON.stringify(cmd);
        if (this.isWebView2) {
            ;
            window.chrome.webview.postMessage(json);
        }
        else {
            console.log('[Bridge] send:', json);
        }
    }
    // ── 订阅事件 ──────────────────────────────────────────────────────────────
    on(event, handler) {
        const list = this.handlers.get(event) ?? [];
        list.push(handler);
        this.handlers.set(event, list);
        // 返回取消订阅函数
        return () => {
            const updated = (this.handlers.get(event) ?? [])
                .filter(h => h !== handler);
            this.handlers.set(event, updated);
        };
    }
    // ── 内部分发 ──────────────────────────────────────────────────────────────
    dispatch(event, data) {
        const list = this.handlers.get(event);
        if (!list)
            return;
        for (const h of list)
            h(data);
    }
    // ── Mock 模式（浏览器开发调试）────────────────────────────────────────────
    startMockMode() {
        const mockProcesses = [
            {
                pid: 1234, name: 'Minecraft.Windows.exe', exePath: 'C:\\...\\Minecraft.Windows.exe',
                cpuUsage: 45.2, gpuUsage: 12.1,
                memoryUsage: 1024 * 1024 * 512, // 512MB
                ioReadBytes: 1024 * 100, ioWriteBytes: 1024 * 50,
                cpuLimited: false, gpuLimited: false, memLimited: false, ioLimited: false,
                cpuLimitPct: 0, gpuLimitPct: 0, memLimitBytes: 0
            },
            {
                pid: 5678, name: 'javaw.exe', exePath: 'C:\\...\\javaw.exe',
                cpuUsage: 22.7, gpuUsage: 5.3,
                memoryUsage: 1024 * 1024 * 256, // 256MB
                ioReadBytes: 1024 * 30, ioWriteBytes: 1024 * 15,
                cpuLimited: true, gpuLimited: false, memLimited: false, ioLimited: false,
                cpuLimitPct: 40, gpuLimitPct: 0, memLimitBytes: 0
            },
        ];
        setTimeout(() => {
            this.dispatch('processList', mockProcesses);
            this.dispatch('themeChanged', { theme: 'dark' });
        }, 500);
        // 模拟定时刷新
        setInterval(() => {
            const updated = mockProcesses.map(p => ({
                ...p,
                cpuUsage: Math.random() * 80 + 5,
                gpuUsage: Math.random() * 30,
                memoryUsage: Math.floor(Math.random() * 1024 * 1024 * 500 + 100 * 1024 * 1024),
                ioReadBytes: Math.floor(Math.random() * 1024 * 200),
                ioWriteBytes: Math.floor(Math.random() * 1024 * 100),
            }));
            this.dispatch('processList', updated);
        }, 2000);
    }
}
// 单例导出
export const bridge = new Bridge();
