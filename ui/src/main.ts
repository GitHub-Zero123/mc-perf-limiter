/**
 * main.ts — 应用入口
 * 初始化标题栏、进程列表、详情面板，连接 bridge 事件
 */

import './styles/base.css'
import { bridge, type ProcessInfo } from './bridge'
import { themeManager } from './theme'

// ─── 状态 ─────────────────────────────────────────────────────────────────────

let processList: ProcessInfo[] = []
let selectedPid: number | null = null
let isMaximized = false

// ─── DOM 引用 ─────────────────────────────────────────────────────────────────

const elProcessList  = document.getElementById('process-list')!
const elMainContent  = document.getElementById('main-content')!
const elStatusScan   = document.getElementById('status-scan')
const elStatusCount  = document.getElementById('status-count')
const btnMinimize    = document.getElementById('btn-minimize')!
const btnMaximize    = document.getElementById('btn-maximize')!
const btnClose       = document.getElementById('btn-close')!
const btnTheme       = document.getElementById('btn-theme')!
const btnRefresh     = document.getElementById('btn-refresh')!

// ─── 主题下拉菜单 ─────────────────────────────────────────────────────────────

// 动态创建下拉菜单
const themeMenu = document.createElement('div')
themeMenu.className = 'theme-menu'
// 菜单颜色由CSS根据data-theme控制，这里不硬编码颜色
themeMenu.innerHTML = `
  <div class="theme-menu-item" data-theme="dark">
    <svg class="theme-menu-icon" width="12" height="12" viewBox="0 0 16 16">
      <path d="M6 1a7 7 0 100 14c-3.1 0-5.7-2-6.6-4.8A5 5 0 108.7 2.3 7 7 0 006 1z"/>
    </svg>
    <span class="theme-menu-label">深色</span>
    <span class="theme-menu-check" id="check-dark"></span>
  </div>
  <div class="theme-menu-item" data-theme="light">
    <svg class="theme-menu-icon" width="12" height="12" viewBox="0 0 16 16">
      <circle cx="8" cy="8" r="3"/>
      <path d="M8 1v2M8 13v2M1 8h2M13 8h2M3.2 3.2l1.4 1.4M11.4 11.4l1.4 1.4M3.2 12.8l1.4-1.4M11.4 4.6l1.4-1.4" stroke-width="1.5" fill="none"/>
    </svg>
    <span class="theme-menu-label">浅色</span>
    <span class="theme-menu-check" id="check-light"></span>
  </div>
  <div class="theme-menu-sep"></div>
  <div class="theme-menu-item" data-theme="system">
    <svg class="theme-menu-icon" width="12" height="12" viewBox="0 0 16 16" fill="none" stroke-width="1.2">
      <rect x="1" y="2" width="14" height="10" rx="1"/>
      <path d="M5 14h6M8 12v2"/>
    </svg>
    <span class="theme-menu-label">跟随系统</span>
    <span class="theme-menu-check" id="check-system"></span>
  </div>
`
document.body.appendChild(themeMenu)

function updateThemeMenuChecks() {
  const t = themeManager.getPreference()
  document.getElementById('check-dark')!.textContent   = t === 'dark'   ? '✓' : ''
  document.getElementById('check-light')!.textContent  = t === 'light'  ? '✓' : ''
  document.getElementById('check-system')!.textContent = t === 'system' ? '✓' : ''
}

function updateThemeMenuColors() {
  // 根据当前主题确定菜单颜色
  const theme = document.documentElement.getAttribute('data-theme') || 'dark'
  const isDark = theme === 'dark'
  
  // 设置文字颜色
  const labelColor = isDark ? '#e0e0e3' : '#1a1a1e'
  const iconColor = isDark ? '#a0a0a8' : '#56565e'
  
  themeMenu.querySelectorAll<HTMLElement>('.theme-menu-label').forEach(el => {
    el.style.color = labelColor
  })
  
  themeMenu.querySelectorAll<SVGElement>('.theme-menu-icon').forEach(el => {
    el.style.fill = iconColor
    el.style.stroke = iconColor
  })
}

function showThemeMenu() {
  updateThemeMenuChecks()
  updateThemeMenuColors()
  const rect = btnTheme.getBoundingClientRect()
  themeMenu.style.right  = `${document.documentElement.clientWidth - rect.right}px`
  themeMenu.style.top    = `${rect.bottom + 4}px`
  themeMenu.classList.add('open')
}

function hideThemeMenu() {
  themeMenu.classList.remove('open')
}

btnTheme.addEventListener('click', (e) => {
  e.stopPropagation()
  if (themeMenu.classList.contains('open')) {
    hideThemeMenu()
  } else {
    showThemeMenu()
  }
})

themeMenu.querySelectorAll<HTMLElement>('.theme-menu-item').forEach(item => {
  item.addEventListener('click', () => {
    const t = item.dataset.theme as 'dark' | 'light' | 'system'
    themeManager.set(t)
    updateThemeMenuChecks()
    hideThemeMenu()
  })
})

document.addEventListener('click', () => hideThemeMenu())

// ─── 标题栏按钮 ───────────────────────────────────────────────────────────────

btnMinimize.addEventListener('click', () =>
  bridge.send({ cmd: 'windowControl', payload: { action: 'minimize' } }))

btnMaximize.addEventListener('click', () => {
  bridge.send({ cmd: 'windowControl', payload: { action: 'maximizeRestore' } })
  // 状态由后端 windowState 事件同步，不在此处手动切换
})

btnClose.addEventListener('click', () =>
  bridge.send({ cmd: 'windowControl', payload: { action: 'close' } }))

function updateMaximizeIcon() {
  const svg = btnMaximize.querySelector('svg')
  if (!svg) return
  // 注意：isMaximized 表示当前状态，图标应显示"点击后将进入的状态"
  // 当前是最大化 → 显示"还原"图标（两个窗口重叠）
  // 当前未最大化 → 显示"最大化"图标（单个矩形）
  if (isMaximized) {
    // 最大化状态 → 显示"还原"图标（双窗口）
    svg.innerHTML = `
      <rect x="2.5" y="0.5" width="7" height="7" stroke="currentColor" stroke-width="1" fill="none"/>
      <polyline points="2,2 0.5,2 0.5,9.5 8,9.5 8,7.5" stroke="currentColor" stroke-width="1" fill="none"/>
    `
    btnMaximize.title = '还原'
  } else {
    // 普通窗口状态 → 显示"最大化"图标（单矩形）
    svg.innerHTML = `<rect x="0.5" y="0.5" width="9" height="9" stroke="currentColor" fill="none"/>`
    btnMaximize.title = '最大化'
  }
}

// ─── 刷新按钮 ─────────────────────────────────────────────────────────────────

btnRefresh.addEventListener('click', () => {
  setStatus('正在扫描...')
  bridge.send({ cmd: 'getProcessList' })
})

// ─── 状态栏更新 ───────────────────────────────────────────────────────────────

function setStatus(text: string) {
  if (elStatusScan) elStatusScan.textContent = text
}

function setCount(n: number) {
  if (elStatusCount) elStatusCount.textContent = `${n} 个进程`
}

// ─── 进程列表更新 ─────────────────────────────────────────────────────────────

// SVG 图标模板（游戏手柄）
const PROCESS_ICON_SVG = `
  <svg class="process-icon-svg" viewBox="0 0 16 16" fill="none" xmlns="http://www.w3.org/2000/svg">
    <rect x="1" y="4" width="14" height="9" rx="2" stroke="currentColor" stroke-width="1.2"/>
    <line x1="5" y1="8.5" x2="7" y2="8.5" stroke="currentColor" stroke-width="1.2" stroke-linecap="round"/>
    <line x1="6" y1="7.5" x2="6" y2="9.5" stroke="currentColor" stroke-width="1.2" stroke-linecap="round"/>
    <circle cx="10" cy="7.8" r="0.7" fill="currentColor"/>
    <circle cx="11.6" cy="9.2" r="0.7" fill="currentColor"/>
  </svg>`

bridge.on('processList', (list) => {
  processList = list
  setStatus(list.length > 0 ? '扫描完成' : '未发现 Minecraft 进程')
  setCount(list.length)
  patchProcessList()

  if (selectedPid !== null) {
    const proc = list.find(p => p.pid === selectedPid)
    if (proc) {
      updateDetailUsage(proc)
    } else {
      selectedPid = null
      renderNoSelection()
    }
  }
})

/** 创建单个进程列表项 DOM */
function createProcessItem(p: ProcessInfo): HTMLElement {
  const el = document.createElement('div')
  el.className = `process-item${p.pid === selectedPid ? ' active' : ''}`
  el.dataset.pid = String(p.pid)
  // 图标：优先用 exe 图标，无则降级 SVG
  const iconHtml = p.iconBase64
    ? `<img class="process-icon-img" src="${p.iconBase64}" alt="" draggable="false"/>`
    : PROCESS_ICON_SVG
  el.innerHTML = `
    <span class="process-item-icon">${iconHtml}</span>
    <div class="process-item-info">
      <div class="process-item-name" title="${escHtml(p.name)}">${escHtml(p.name)}</div>
      <div class="process-item-pid">PID ${p.pid}</div>
    </div>
    <div class="process-item-right">
      <div class="process-item-usage" data-usage>CPU ${p.cpuUsage.toFixed(1)}%<br>GPU ${p.gpuUsage.toFixed(1)}%</div>
      <div class="process-item-badges" data-badges>
        ${p.cpuLimited ? '<span class="badge badge-cpu">CPU</span>' : ''}
        ${p.gpuLimited ? '<span class="badge badge-gpu">GPU</span>' : ''}
      </div>
    </div>`
  el.addEventListener('click', () => selectProcess(p.pid))
  return el
}

/**
 * 增量更新进程列表 DOM：
 * - 新进程 → 插入对应位置
 * - 已有进程 → 只更新 usage / badge 文字
 * - 消失进程 → 移除 DOM 节点
 */
function patchProcessList() {
  // 空状态
  if (processList.length === 0) {
    elProcessList.innerHTML = `
      <div class="process-empty">
        <svg width="16" height="16" viewBox="0 0 16 16" fill="none" style="opacity:.4">
          <rect x="1" y="4" width="14" height="9" rx="2" stroke="currentColor" stroke-width="1.2"/>
          <line x1="5" y1="8.5" x2="7" y2="8.5" stroke="currentColor" stroke-width="1.2" stroke-linecap="round"/>
          <line x1="6" y1="7.5" x2="6" y2="9.5" stroke="currentColor" stroke-width="1.2" stroke-linecap="round"/>
          <circle cx="10" cy="7.8" r="0.7" fill="currentColor"/>
          <circle cx="11.6" cy="9.2" r="0.7" fill="currentColor"/>
        </svg>
        <span>未发现 Minecraft 进程</span>
      </div>`
    return
  }

  // 移除空状态节点（如果有）
  const emptyEl = elProcessList.querySelector('.process-empty')
  if (emptyEl) elProcessList.innerHTML = ''

  const newPids = new Set(processList.map(p => p.pid))

  // 移除已消失的进程
  elProcessList.querySelectorAll<HTMLElement>('.process-item').forEach(el => {
    const pid = parseInt(el.dataset.pid ?? '0')
    if (!newPids.has(pid)) el.remove()
  })

  // 逐个进程对比更新
  processList.forEach((p, idx) => {
    const existing = elProcessList.querySelector<HTMLElement>(
      `.process-item[data-pid="${p.pid}"]`)

    if (existing) {
      // 只更新 usage 和 badges
      const usageEl = existing.querySelector<HTMLElement>('[data-usage]')
      if (usageEl) usageEl.innerHTML = `CPU ${p.cpuUsage.toFixed(1)}%<br>GPU ${p.gpuUsage.toFixed(1)}%`

      const badgesEl = existing.querySelector<HTMLElement>('[data-badges]')
      if (badgesEl) badgesEl.innerHTML =
        `${p.cpuLimited ? '<span class="badge badge-cpu">CPU</span>' : ''}` +
        `${p.gpuLimited ? '<span class="badge badge-gpu">GPU</span>' : ''}`

      // 同步 active 状态
      existing.classList.toggle('active', p.pid === selectedPid)

      // 确保顺序正确
      const items = elProcessList.querySelectorAll('.process-item')
      if (items[idx] !== existing) elProcessList.insertBefore(existing, items[idx] ?? null)
    } else {
      // 新进程：插入到正确位置
      const items = elProcessList.querySelectorAll('.process-item')
      const newEl = createProcessItem(p)
      elProcessList.insertBefore(newEl, items[idx] ?? null)
    }
  })
}

/** 兼容旧调用 */
function _renderProcessList() {
  // 首次渲染强制全量
  elProcessList.innerHTML = ''
  patchProcessList()
}
// 保留函数引用防止 tree-shaking
void _renderProcessList

// 绑定点击事件（patchProcessList 内对新节点单独绑定，此处仅作兼容保留）
elProcessList.addEventListener('click', (e) => {
  const item = (e.target as Element).closest<HTMLElement>('.process-item')
  if (item) {
    const pid = parseInt(item.dataset.pid ?? '0')
    selectProcess(pid)
  }
})

// ─── 选中进程 ─────────────────────────────────────────────────────────────────

function selectProcess(pid: number) {
  selectedPid = pid
  // 只更新列表项的 active 状态，不重建 DOM（保持增量更新能正常工作）
  elProcessList.querySelectorAll<HTMLElement>('.process-item').forEach(el => {
    el.classList.toggle('active', el.dataset.pid === String(pid))
  })
  const proc = processList.find(p => p.pid === pid)
  if (!proc) return
  renderDetailPanel(proc)
}

function renderNoSelection() {
  elMainContent.innerHTML = `
    <div class="no-selection">
      <div class="no-selection-icon">
        <svg width="40" height="40" viewBox="0 0 40 40" fill="none" style="opacity:.25">
          <rect x="4" y="10" width="32" height="24" rx="4" stroke="currentColor" stroke-width="2"/>
          <path d="M14 22h12M20 18v8" stroke="currentColor" stroke-width="2" stroke-linecap="round"/>
          <circle cx="20" cy="6" r="2" fill="currentColor" opacity=".5"/>
        </svg>
      </div>
      <div class="no-selection-text">请从左侧选择一个 Minecraft 进程</div>
      <div class="no-selection-hint">未检测到进程时，请点击刷新重新扫描</div>
    </div>`
}

// ─── 详情面板 ─────────────────────────────────────────────────────────────────

function renderDetailPanel(proc: ProcessInfo) {
  elMainContent.innerHTML = `
    <div class="detail-panel" id="detail-panel">

      <!-- 进程信息 -->
      <div class="panel-card">
        <div class="panel-card-header">
          <span>进程信息</span>
          <span style="color:var(--accent-success);font-size:10px;font-weight:500;text-transform:none;letter-spacing:0">● 运行中</span>
        </div>
        <div class="panel-card-body">
          <div class="info-grid">
            <span class="info-label">进程名</span>
            <span class="info-value">${escHtml(proc.name)}</span>
            <span class="info-label">PID</span>
            <span class="info-value">${proc.pid}</span>
            <span class="info-label">路径</span>
            <span class="info-value" title="${escHtml(proc.exePath)}">${escHtml(truncate(proc.exePath, 64))}</span>
          </div>
        </div>
      </div>

      <!-- 实时使用率 -->
      <div class="panel-card">
        <div class="panel-card-header">实时使用率</div>
        <div class="panel-card-body">
          <div class="chart-wrap">
            <div class="chart-row">
              <span class="chart-label">CPU</span>
              <div class="chart-bar-bg">
                <div class="chart-bar-fill cpu" id="bar-cpu"
                     style="width:${clamp(proc.cpuUsage)}%"></div>
              </div>
              <span class="chart-value" id="val-cpu">${proc.cpuUsage.toFixed(1)}%</span>
            </div>
            <div class="chart-row">
              <span class="chart-label">GPU</span>
              <div class="chart-bar-bg">
                <div class="chart-bar-fill gpu" id="bar-gpu"
                     style="width:${clamp(proc.gpuUsage)}%"></div>
              </div>
              <span class="chart-value" id="val-gpu">${proc.gpuUsage.toFixed(1)}%</span>
            </div>
          </div>
          <div class="stats-grid">
            <div class="stat-item">
              <span class="stat-label">内存占用</span>
              <span class="stat-value" id="val-mem">${formatBytes(proc.memoryUsage || 0)}</span>
            </div>
            <div class="stat-item">
              <span class="stat-label">IO 读取</span>
              <span class="stat-value" id="val-io-r">${formatSpeed(proc.ioReadBytes || 0)}</span>
            </div>
            <div class="stat-item">
              <span class="stat-label">IO 写入</span>
              <span class="stat-value" id="val-io-w">${formatSpeed(proc.ioWriteBytes || 0)}</span>
            </div>
          </div>
        </div>
      </div>

      <!-- CPU 限制 -->
      <div class="panel-card">
        <div class="panel-card-header">
          <span>CPU 限制</span>
          <label class="toggle" title="启用/禁用 CPU 限制">
            <input type="checkbox" id="toggle-cpu" ${proc.cpuLimited ? 'checked' : ''}>
            <span class="toggle-track"></span>
            <span class="toggle-thumb"></span>
          </label>
        </div>
        <div class="panel-card-body">
          <div class="limit-row">
            <span class="limit-label">限制上限</span>
            <div class="limit-slider-wrap">
              <input type="range" id="slider-cpu" min="1" max="99"
                value="${proc.cpuLimited ? proc.cpuLimitPct : 50}" />
              <span class="limit-value" id="label-cpu">${proc.cpuLimited ? proc.cpuLimitPct : 50}%</span>
            </div>
          </div>
          <button class="apply-btn" id="btn-apply-cpu">应用 CPU 限制</button>
        </div>
      </div>

      <!-- GPU 限制 -->
      <div class="panel-card">
        <div class="panel-card-header">
          <span>GPU 限制</span>
          <label class="toggle" title="启用/禁用 GPU 限制">
            <input type="checkbox" id="toggle-gpu" ${proc.gpuLimited ? 'checked' : ''}>
            <span class="toggle-track"></span>
            <span class="toggle-thumb"></span>
          </label>
        </div>
        <div class="panel-card-body">
          <div class="limit-row">
            <span class="limit-label">限制上限</span>
            <div class="limit-slider-wrap">
              <input type="range" id="slider-gpu" min="1" max="99"
                value="${proc.gpuLimited ? proc.gpuLimitPct : 80}" />
              <span class="limit-value" id="label-gpu">${proc.gpuLimited ? proc.gpuLimitPct : 80}%</span>
            </div>
          </div>
          <button class="apply-btn" id="btn-apply-gpu">应用 GPU 限制</button>
        </div>
      </div>

    </div>`

  bindDetailEvents(proc.pid)
}

function bindDetailEvents(pid: number) {
  const sliderCpu = document.getElementById('slider-cpu') as HTMLInputElement
  const labelCpu  = document.getElementById('label-cpu')!
  sliderCpu?.addEventListener('input', () => { labelCpu.textContent = `${sliderCpu.value}%` })

  const sliderGpu = document.getElementById('slider-gpu') as HTMLInputElement
  const labelGpu  = document.getElementById('label-gpu')!
  sliderGpu?.addEventListener('input', () => { labelGpu.textContent = `${sliderGpu.value}%` })

  document.getElementById('btn-apply-cpu')?.addEventListener('click', () => {
    const enabled = (document.getElementById('toggle-cpu') as HTMLInputElement).checked
    if (enabled) {
      bridge.send({ cmd: 'setLimit', payload: { pid, cpu: parseInt(sliderCpu.value) } })
    } else {
      bridge.send({ cmd: 'removeLimit', payload: { pid } })
    }
  })

  document.getElementById('btn-apply-gpu')?.addEventListener('click', () => {
    const enabled = (document.getElementById('toggle-gpu') as HTMLInputElement).checked
    if (enabled) {
      bridge.send({ cmd: 'setLimit', payload: { pid, gpu: parseInt(sliderGpu.value) } })
    } else {
      bridge.send({ cmd: 'removeLimit', payload: { pid } })
    }
  })
}

// ─── 更新实时使用率 ───────────────────────────────────────────────────────────

function updateDetailUsage(proc: ProcessInfo) {
  // CPU/GPU 进度条
  const barCpu = document.getElementById('bar-cpu')
  const valCpu = document.getElementById('val-cpu')
  const barGpu = document.getElementById('bar-gpu')
  const valGpu = document.getElementById('val-gpu')
  if (barCpu) barCpu.style.width = `${clamp(proc.cpuUsage)}%`
  if (valCpu) valCpu.textContent  = `${proc.cpuUsage.toFixed(1)}%`
  if (barGpu) barGpu.style.width = `${clamp(proc.gpuUsage)}%`
  if (valGpu) valGpu.textContent  = `${proc.gpuUsage.toFixed(1)}%`

  // 内存、IO 统计
  const valMem = document.getElementById('val-mem')
  const valIoR = document.getElementById('val-io-r')
  const valIoW = document.getElementById('val-io-w')
  if (valMem) valMem.textContent = formatBytes(proc.memoryUsage || 0)
  if (valIoR) valIoR.textContent = formatSpeed(proc.ioReadBytes || 0)
  if (valIoW) valIoW.textContent = formatSpeed(proc.ioWriteBytes || 0)
}

// ─── 事件：限制结果 ───────────────────────────────────────────────────────────

bridge.on('limitApplied', (data) => {
  if (!data.cpu_ok || !data.gpu_ok) console.warn('[Bridge] Limit not fully applied:', data)
  bridge.send({ cmd: 'getProcessList' })
})

bridge.on('error', ({ message }) => {
  console.error('[Bridge] Backend error:', message)
  setStatus(`错误: ${message}`)
})

// ─── 事件：主题变化（后端推送）───────────────────────────────────────────────

bridge.on('themeChanged', ({ theme }) => {
  themeManager.applyToDOM(theme)
})

// ─── 事件：窗口状态变化（最大化/还原）────────────────────────────────────────

bridge.on('windowState', ({ maximized }) => {
  isMaximized = maximized
  updateMaximizeIcon()
})

// ─── 工具函数 ─────────────────────────────────────────────────────────────────

function escHtml(s: string): string {
  return s
    .replace(/&/g, '&amp;').replace(/</g, '&lt;')
    .replace(/>/g, '&gt;').replace(/"/g, '&quot;')
}

function truncate(s: string, maxLen: number): string {
  if (s.length <= maxLen) return s
  return '…' + s.slice(s.length - (maxLen - 1))
}

function clamp(v: number): number {
  return Math.min(Math.max(v, 0), 100)
}

/** 格式化字节数为人类可读的单位 */
function formatBytes(bytes: number): string {
  if (bytes === 0) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  const i = Math.floor(Math.log(bytes) / Math.log(1024))
  const value = bytes / Math.pow(1024, i)
  return `${value.toFixed(1)} ${units[i]}`
}

/** 格式化速度为人类可读的单位 */
function formatSpeed(bytesPerSec: number): string {
  if (bytesPerSec === 0) return '0 B/s'
  const units = ['B/s', 'KB/s', 'MB/s', 'GB/s']
  const i = Math.floor(Math.log(bytesPerSec) / Math.log(1024))
  const value = bytesPerSec / Math.pow(1024, i)
  return `${value.toFixed(1)} ${units[i]}`
}

// ─── 侧边栏拖拽 Resize ────────────────────────────────────────────────────────

;(function initSidebarResize() {
  const sidebar  = document.getElementById('sidebar')!
  const resizer  = document.getElementById('sidebar-resizer')!
  const STORAGE_KEY = 'mcperf-sidebar-width'
  const MIN_W = 140, MAX_W = 420

  // 恢复上次宽度
  const saved = localStorage.getItem(STORAGE_KEY)
  if (saved) {
    const w = parseInt(saved)
    if (w >= MIN_W && w <= MAX_W) {
      document.documentElement.style.setProperty('--sidebar-width', `${w}px`)
    }
  }

  let startX = 0, startW = 0

  resizer.addEventListener('mousedown', (e) => {
    e.preventDefault()
    startX = e.clientX
    startW = sidebar.getBoundingClientRect().width
    resizer.classList.add('dragging')
    document.body.style.cursor = 'col-resize'
    document.body.style.userSelect = 'none'

    const onMove = (ev: MouseEvent) => {
      const delta = ev.clientX - startX
      const newW  = Math.min(Math.max(startW + delta, MIN_W), MAX_W)
      document.documentElement.style.setProperty('--sidebar-width', `${newW}px`)
    }

    const onUp = (_ev: MouseEvent) => {
      document.removeEventListener('mousemove', onMove)
      document.removeEventListener('mouseup',   onUp)
      resizer.classList.remove('dragging')
      document.body.style.cursor = ''
      document.body.style.userSelect = ''
      const finalW = Math.min(Math.max(
        sidebar.getBoundingClientRect().width, MIN_W), MAX_W)
      localStorage.setItem(STORAGE_KEY, String(Math.round(finalW)))
    }

    document.addEventListener('mousemove', onMove)
    document.addEventListener('mouseup',   onUp)
  })
})()

// ─── 初始化 ───────────────────────────────────────────────────────────────────

function init() {
  setStatus('正在扫描...')
  // 恢复本地主题偏好并通知后端
  themeManager.restore()
  updateThemeMenuChecks()
  bridge.send({ cmd: 'getProcessList' })
}

init()
