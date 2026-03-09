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

// ─── DOM 引用 ─────────────────────────────────────────────────────────────────

const elProcessList  = document.getElementById('process-list')!
const elMainContent  = document.getElementById('main-content')!
const btnMinimize    = document.getElementById('btn-minimize')!
const btnMaximize    = document.getElementById('btn-maximize')!
const btnClose       = document.getElementById('btn-close')!
const btnTheme       = document.getElementById('btn-theme')!
const btnRefresh     = document.getElementById('btn-refresh')!

// ─── 标题栏按钮 ───────────────────────────────────────────────────────────────

btnMinimize.addEventListener('click', () =>
  bridge.send({ cmd: 'windowControl', payload: { action: 'minimize' } }))

btnMaximize.addEventListener('click', () =>
  bridge.send({ cmd: 'windowControl', payload: { action: 'maximizeRestore' } }))

btnClose.addEventListener('click', () =>
  bridge.send({ cmd: 'windowControl', payload: { action: 'close' } }))

btnTheme.addEventListener('click', () => {
  themeManager.cycle()
  updateThemeIcon()
})

function updateThemeIcon() {
  const iconEl = document.getElementById('icon-theme')
  if (!iconEl) return
  const t = themeManager.getPreference()
  // 替换为 emoji 文本（简洁）
  btnTheme.title = `主题: ${t === 'dark' ? '深色' : t === 'light' ? '浅色' : '跟随系统'} (点击切换)`
}

// ─── 刷新按钮 ─────────────────────────────────────────────────────────────────

btnRefresh.addEventListener('click', () => {
  bridge.send({ cmd: 'getProcessList' })
})

// ─── 进程列表更新 ─────────────────────────────────────────────────────────────

bridge.on('processList', (list) => {
  processList = list
  renderProcessList()

  // 如果当前选中进程仍存在，更新详情面板中的实时数据
  if (selectedPid !== null) {
    const proc = list.find(p => p.pid === selectedPid)
    if (proc) {
      updateDetailUsage(proc)
    } else {
      // 进程已消失
      selectedPid = null
      renderNoSelection()
    }
  }
})

function renderProcessList() {
  if (processList.length === 0) {
    elProcessList.innerHTML =
      '<div class="process-empty">未发现 Minecraft 进程</div>'
    return
  }

  elProcessList.innerHTML = processList.map(p => `
    <div class="process-item ${p.pid === selectedPid ? 'active' : ''}"
         data-pid="${p.pid}">
      <span class="process-item-icon">🎮</span>
      <div class="process-item-info">
        <div class="process-item-name" title="${escHtml(p.name)}">${escHtml(p.name)}</div>
        <div class="process-item-pid">PID ${p.pid}</div>
      </div>
      <div class="process-item-right">
        <div class="process-item-usage">
          CPU ${p.cpuUsage.toFixed(1)}%<br>
          GPU ${p.gpuUsage.toFixed(1)}%
        </div>
        <div class="process-item-badges">
          ${p.cpuLimited ? '<span class="badge badge-cpu">CPU</span>' : ''}
          ${p.gpuLimited ? '<span class="badge badge-gpu">GPU</span>' : ''}
        </div>
      </div>
    </div>
  `).join('')

  // 绑定点击
  elProcessList.querySelectorAll<HTMLElement>('.process-item').forEach(el => {
    el.addEventListener('click', () => {
      const pid = parseInt(el.dataset.pid ?? '0')
      selectProcess(pid)
    })
  })
}

// ─── 选中进程 ─────────────────────────────────────────────────────────────────

function selectProcess(pid: number) {
  selectedPid = pid
  renderProcessList() // 更新高亮

  const proc = processList.find(p => p.pid === pid)
  if (!proc) return
  renderDetailPanel(proc)
}

function renderNoSelection() {
  elMainContent.innerHTML = `
    <div class="no-selection">
      <div class="no-selection-icon">🎮</div>
      <div class="no-selection-text">请从左侧选择一个 Minecraft 进程</div>
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
          <span style="color:var(--accent-success);font-size:11px">● 运行中</span>
        </div>
        <div class="panel-card-body">
          <div class="info-grid">
            <span class="info-label">进程名</span>
            <span class="info-value">${escHtml(proc.name)}</span>
            <span class="info-label">PID</span>
            <span class="info-value">${proc.pid}</span>
            <span class="info-label">路径</span>
            <span class="info-value" title="${escHtml(proc.exePath)}">${escHtml(truncate(proc.exePath, 60))}</span>
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
                     style="width:${proc.cpuUsage.toFixed(1)}%"></div>
              </div>
              <span class="chart-value" id="val-cpu">${proc.cpuUsage.toFixed(1)}%</span>
            </div>
            <div class="chart-row">
              <span class="chart-label">GPU</span>
              <div class="chart-bar-bg">
                <div class="chart-bar-fill gpu" id="bar-gpu"
                     style="width:${proc.gpuUsage.toFixed(1)}%"></div>
              </div>
              <span class="chart-value" id="val-gpu">${proc.gpuUsage.toFixed(1)}%</span>
            </div>
          </div>
        </div>
      </div>

      <!-- CPU 限制 -->
      <div class="panel-card">
        <div class="panel-card-header">
          <span>CPU 限制</span>
          <label class="toggle">
            <input type="checkbox" id="toggle-cpu"
              ${proc.cpuLimited ? 'checked' : ''}>
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
              <span class="limit-value" id="label-cpu">
                ${proc.cpuLimited ? proc.cpuLimitPct : 50}%
              </span>
            </div>
          </div>
          <button class="apply-btn" id="btn-apply-cpu">应用 CPU 限制</button>
        </div>
      </div>

      <!-- GPU 限制 -->
      <div class="panel-card">
        <div class="panel-card-header">
          <span>GPU 限制</span>
          <label class="toggle">
            <input type="checkbox" id="toggle-gpu"
              ${proc.gpuLimited ? 'checked' : ''}>
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
              <span class="limit-value" id="label-gpu">
                ${proc.gpuLimited ? proc.gpuLimitPct : 80}%
              </span>
            </div>
          </div>
          <button class="apply-btn" id="btn-apply-gpu">应用 GPU 限制</button>
        </div>
      </div>
    </div>`

  bindDetailEvents(proc.pid)
}

function bindDetailEvents(pid: number) {
  // CPU 滑块联动
  const sliderCpu = document.getElementById('slider-cpu') as HTMLInputElement
  const labelCpu  = document.getElementById('label-cpu')!
  sliderCpu?.addEventListener('input', () => {
    labelCpu.textContent = `${sliderCpu.value}%`
  })

  // GPU 滑块联动
  const sliderGpu = document.getElementById('slider-gpu') as HTMLInputElement
  const labelGpu  = document.getElementById('label-gpu')!
  sliderGpu?.addEventListener('input', () => {
    labelGpu.textContent = `${sliderGpu.value}%`
  })

  // 应用 CPU 限制
  document.getElementById('btn-apply-cpu')?.addEventListener('click', () => {
    const enabled = (document.getElementById('toggle-cpu') as HTMLInputElement).checked
    if (enabled) {
      bridge.send({
        cmd: 'setLimit',
        payload: { pid, cpu: parseInt(sliderCpu.value) }
      })
    } else {
      bridge.send({ cmd: 'removeLimit', payload: { pid } })
    }
  })

  // 应用 GPU 限制
  document.getElementById('btn-apply-gpu')?.addEventListener('click', () => {
    const enabled = (document.getElementById('toggle-gpu') as HTMLInputElement).checked
    if (enabled) {
      bridge.send({
        cmd: 'setLimit',
        payload: { pid, gpu: parseInt(sliderGpu.value) }
      })
    } else {
      bridge.send({ cmd: 'removeLimit', payload: { pid } })
    }
  })
}

// ─── 更新实时使用率（不重建 DOM）─────────────────────────────────────────────

function updateDetailUsage(proc: ProcessInfo) {
  const barCpu = document.getElementById('bar-cpu')
  const valCpu = document.getElementById('val-cpu')
  const barGpu = document.getElementById('bar-gpu')
  const valGpu = document.getElementById('val-gpu')

  if (barCpu) barCpu.style.width = `${Math.min(proc.cpuUsage, 100).toFixed(1)}%`
  if (valCpu) valCpu.textContent  = `${proc.cpuUsage.toFixed(1)}%`
  if (barGpu) barGpu.style.width = `${Math.min(proc.gpuUsage, 100).toFixed(1)}%`
  if (valGpu) valGpu.textContent  = `${proc.gpuUsage.toFixed(1)}%`
}

// ─── 限制结果反馈 ─────────────────────────────────────────────────────────────

bridge.on('limitApplied', (data) => {
  if (!data.cpu_ok || !data.gpu_ok) {
    console.warn('[Bridge] Limit not fully applied:', data)
  }
  // 刷新进程列表以更新 badge
  bridge.send({ cmd: 'getProcessList' })
})

bridge.on('error', ({ message }) => {
  console.error('[Bridge] Backend error:', message)
})

// ─── 工具函数 ─────────────────────────────────────────────────────────────────

function escHtml(s: string): string {
  return s
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
}

function truncate(s: string, maxLen: number): string {
  if (s.length <= maxLen) return s
  return '...' + s.slice(s.length - (maxLen - 3))
}

// ─── 初始化 ───────────────────────────────────────────────────────────────────

function init() {
  // 初始主题图标
  updateThemeIcon()

  // 请求进程列表
  bridge.send({ cmd: 'getProcessList' })
  bridge.send({ cmd: 'getSystemTheme' })
}

init()
