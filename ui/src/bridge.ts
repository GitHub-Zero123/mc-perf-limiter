/**
 * bridge.ts — 与 C++ 后端通信层
 * 封装 window.chrome.webview API，提供类型安全的 IPC 接口
 */

// ─── 消息类型定义（与 ipc_types.h 保持一致）─────────────────────────────────

export interface ProcessInfo {
  pid:          number
  name:         string
  exePath:      string
  cpuUsage:     number    // CPU 使用率 %
  gpuUsage:     number    // GPU 使用率 %
  memoryUsage:  number    // 内存使用量（字节）
  ioReadBytes:  number    // IO 读取字节/秒
  ioWriteBytes: number    // IO 写入字节/秒
  netRecvBytes: number    // 网络接收字节/秒
  netSendBytes: number    // 网络发送字节/秒
  cpuLimited:   boolean
  gpuLimited:   boolean
  cpuLimitPct:  number
  gpuLimitPct:  number
  iconBase64?:  string    // exe 图标 Base64 PNG（可选）
}

export type Theme = 'dark' | 'light' | 'system'

// JS → C++ 命令
export type Command =
  | { cmd: 'getProcessList' }
  | { cmd: 'setLimit';    payload: { pid: number; cpu?: number; gpu?: number } }
  | { cmd: 'removeLimit'; payload: { pid: number } }
  | { cmd: 'setTheme';    payload: { theme: Theme } }
  | { cmd: 'windowControl'; payload: { action: 'minimize' | 'maximizeRestore' | 'close' } }
  | { cmd: 'getSystemTheme' }

// C++ → JS 事件
export interface EventMap {
  processList:  ProcessInfo[]
  limitApplied: { pid: number; cpu_ok: boolean; gpu_ok: boolean; cpu_pct: number; gpu_pct: number }
  limitRemoved: { pid: number }
  statsUpdate:  { pid: number; cpu: number; gpu: number }
  themeChanged: { theme: 'dark' | 'light' }
  windowState:  { maximized: boolean }
  error:        { message: string }
}

type EventHandler<K extends keyof EventMap> = (data: EventMap[K]) => void
type AnyHandler = (data: unknown) => void

// ─── Bridge 类 ────────────────────────────────────────────────────────────────

class Bridge {
  private handlers = new Map<string, AnyHandler[]>()
  private isWebView2 = false

  constructor() {
    // 检测是否在 WebView2 环境中
    this.isWebView2 = !!(window as any).chrome?.webview

    if (this.isWebView2) {
      ;(window as any).chrome.webview.addEventListener(
        'message',
        (ev: MessageEvent) => {
          try {
            const msg = typeof ev.data === 'string'
              ? JSON.parse(ev.data)
              : ev.data
            this.dispatch(msg.event, msg.data)
          } catch (e) {
            console.error('[Bridge] Failed to parse message:', ev.data, e)
          }
        }
      )
    } else {
      // 开发模式：注入模拟数据
      console.info('[Bridge] Running in browser dev mode — using mock data')
      this.startMockMode()
    }
  }

  // ── 发送命令 ──────────────────────────────────────────────────────────────
  send(cmd: Command): void {
    const json = JSON.stringify(cmd)
    if (this.isWebView2) {
      ;(window as any).chrome.webview.postMessage(json)
    } else {
      console.log('[Bridge] send:', json)
    }
  }

  // ── 订阅事件 ──────────────────────────────────────────────────────────────
  on<K extends keyof EventMap>(event: K, handler: EventHandler<K>): () => void {
    const list = this.handlers.get(event) ?? []
    list.push(handler as AnyHandler)
    this.handlers.set(event, list)
    // 返回取消订阅函数
    return () => {
      const updated = (this.handlers.get(event) ?? [])
        .filter(h => h !== handler)
      this.handlers.set(event, updated)
    }
  }

  // ── 内部分发 ──────────────────────────────────────────────────────────────
  private dispatch(event: string, data: unknown): void {
    const list = this.handlers.get(event)
    if (!list) return
    for (const h of list) h(data)
  }

  // ── Mock 模式（浏览器开发调试）────────────────────────────────────────────
  private startMockMode(): void {
    const mockProcesses: ProcessInfo[] = [
      {
        pid: 1234, name: 'Minecraft.Windows.exe', exePath: 'C:\\...\\Minecraft.Windows.exe',
        cpuUsage: 45.2, gpuUsage: 12.1,
        memoryUsage: 1024 * 1024 * 512, // 512MB
        ioReadBytes: 1024 * 100, ioWriteBytes: 1024 * 50,
        netRecvBytes: 1024 * 20, netSendBytes: 1024 * 10,
        cpuLimited: false, gpuLimited: false, cpuLimitPct: 0, gpuLimitPct: 0
      },
      {
        pid: 5678, name: 'javaw.exe', exePath: 'C:\\...\\javaw.exe',
        cpuUsage: 22.7, gpuUsage: 5.3,
        memoryUsage: 1024 * 1024 * 256, // 256MB
        ioReadBytes: 1024 * 30, ioWriteBytes: 1024 * 15,
        netRecvBytes: 1024 * 5, netSendBytes: 1024 * 2,
        cpuLimited: true, gpuLimited: false, cpuLimitPct: 40, gpuLimitPct: 0
      },
    ]

    setTimeout(() => {
      this.dispatch('processList', mockProcesses)
      this.dispatch('themeChanged', { theme: 'dark' })
    }, 500)

    // 模拟定时刷新
    setInterval(() => {
      const updated = mockProcesses.map(p => ({
        ...p,
        cpuUsage: Math.random() * 80 + 5,
        gpuUsage: Math.random() * 30,
        memoryUsage: Math.floor(Math.random() * 1024 * 1024 * 500 + 100 * 1024 * 1024),
        ioReadBytes: Math.floor(Math.random() * 1024 * 200),
        ioWriteBytes: Math.floor(Math.random() * 1024 * 100),
        netRecvBytes: Math.floor(Math.random() * 1024 * 50),
        netSendBytes: Math.floor(Math.random() * 1024 * 30),
      }))
      this.dispatch('processList', updated)
    }, 2000)
  }
}

// 单例导出
export const bridge = new Bridge()
