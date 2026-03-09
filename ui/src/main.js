/**
 * main.ts — 应用入口
 * 初始化标题栏、进程列表、详情面板，连接 bridge 事件
 */
import './styles/base.css';
import { bridge } from './bridge';
import { themeManager } from './theme';
// ─── 状态 ─────────────────────────────────────────────────────────────────────
let processList = [];
let selectedPid = null;
let isMaximized = false;
// ─── DOM 引用 ─────────────────────────────────────────────────────────────────
const elProcessList = document.getElementById('process-list');
const elMainContent = document.getElementById('main-content');
const elStatusScan = document.getElementById('status-scan');
const elStatusCount = document.getElementById('status-count');
const btnMinimize = document.getElementById('btn-minimize');
const btnMaximize = document.getElementById('btn-maximize');
const btnClose = document.getElementById('btn-close');
const btnTheme = document.getElementById('btn-theme');
const btnRefresh = document.getElementById('btn-refresh');
// ─── 主题下拉菜单 ─────────────────────────────────────────────────────────────
// 动态创建下拉菜单
const themeMenu = document.createElement('div');
themeMenu.className = 'theme-menu';
themeMenu.innerHTML = `
  <div class="theme-menu-item" data-theme="dark">
    <svg width="12" height="12" viewBox="0 0 16 16" fill="currentColor">
      <path d="M6 1a7 7 0 100 14c-3.1 0-5.7-2-6.6-4.8A5 5 0 108.7 2.3 7 7 0 006 1z"/>
    </svg>
    深色
    <span class="theme-menu-check" id="check-dark"></span>
  </div>
  <div class="theme-menu-item" data-theme="light">
    <svg width="12" height="12" viewBox="0 0 16 16" fill="currentColor">
      <circle cx="8" cy="8" r="3"/>
      <path d="M8 1v2M8 13v2M1 8h2M13 8h2M3.2 3.2l1.4 1.4M11.4 11.4l1.4 1.4M3.2 12.8l1.4-1.4M11.4 4.6l1.4-1.4" stroke="currentColor" stroke-width="1.5" fill="none"/>
    </svg>
    浅色
    <span class="theme-menu-check" id="check-light"></span>
  </div>
  <div class="theme-menu-sep"></div>
  <div class="theme-menu-item" data-theme="system">
    <svg width="12" height="12" viewBox="0 0 16 16" fill="currentColor">
      <rect x="1" y="2" width="14" height="10" rx="1" stroke="currentColor" stroke-width="1.2" fill="none"/>
      <path d="M5 14h6M8 12v2" stroke="currentColor" stroke-width="1.2"/>
    </svg>
    跟随系统
    <span class="theme-menu-check" id="check-system"></span>
  </div>
`;
document.body.appendChild(themeMenu);
function updateThemeMenuChecks() {
    const t = themeManager.getPreference();
    document.getElementById('check-dark').textContent = t === 'dark' ? '✓' : '';
    document.getElementById('check-light').textContent = t === 'light' ? '✓' : '';
    document.getElementById('check-system').textContent = t === 'system' ? '✓' : '';
}
function showThemeMenu() {
    updateThemeMenuChecks();
    const rect = btnTheme.getBoundingClientRect();
    themeMenu.style.right = `${document.documentElement.clientWidth - rect.right}px`;
    themeMenu.style.top = `${rect.bottom + 4}px`;
    themeMenu.classList.add('open');
}
function hideThemeMenu() {
    themeMenu.classList.remove('open');
}
btnTheme.addEventListener('click', (e) => {
    e.stopPropagation();
    if (themeMenu.classList.contains('open')) {
        hideThemeMenu();
    }
    else {
        showThemeMenu();
    }
});
themeMenu.querySelectorAll('.theme-menu-item').forEach(item => {
    item.addEventListener('click', () => {
        const t = item.dataset.theme;
        themeManager.set(t);
        updateThemeMenuChecks();
        hideThemeMenu();
    });
});
document.addEventListener('click', () => hideThemeMenu());
// ─── 标题栏按钮 ───────────────────────────────────────────────────────────────
btnMinimize.addEventListener('click', () => bridge.send({ cmd: 'windowControl', payload: { action: 'minimize' } }));
btnMaximize.addEventListener('click', () => {
    bridge.send({ cmd: 'windowControl', payload: { action: 'maximizeRestore' } });
    isMaximized = !isMaximized;
    updateMaximizeIcon();
});
btnClose.addEventListener('click', () => bridge.send({ cmd: 'windowControl', payload: { action: 'close' } }));
function updateMaximizeIcon() {
    const svg = btnMaximize.querySelector('svg');
    if (!svg)
        return;
    if (isMaximized) {
        svg.innerHTML = `
      <rect x="2.5" y="0.5" width="7" height="7" stroke="currentColor" stroke-width="1" fill="none"/>
      <polyline points="2,2 0.5,2 0.5,9.5 8,9.5 8,7.5" stroke="currentColor" stroke-width="1" fill="none"/>
    `;
        btnMaximize.title = '还原';
    }
    else {
        svg.innerHTML = `<rect x="0.5" y="0.5" width="9" height="9" stroke="currentColor" fill="none"/>`;
        btnMaximize.title = '最大化';
    }
}
// ─── 刷新按钮 ─────────────────────────────────────────────────────────────────
btnRefresh.addEventListener('click', () => {
    setStatus('正在扫描...');
    bridge.send({ cmd: 'getProcessList' });
});
// ─── 状态栏更新 ───────────────────────────────────────────────────────────────
function setStatus(text) {
    if (elStatusScan)
        elStatusScan.textContent = text;
}
function setCount(n) {
    if (elStatusCount)
        elStatusCount.textContent = `${n} 个进程`;
}
// ─── 进程列表更新 ─────────────────────────────────────────────────────────────
bridge.on('processList', (list) => {
    processList = list;
    setStatus(list.length > 0 ? '扫描完成' : '未发现 Minecraft 进程');
    setCount(list.length);
    renderProcessList();
    if (selectedPid !== null) {
        const proc = list.find(p => p.pid === selectedPid);
        if (proc) {
            updateDetailUsage(proc);
        }
        else {
            selectedPid = null;
            renderNoSelection();
        }
    }
});
function renderProcessList() {
    if (processList.length === 0) {
        elProcessList.innerHTML = `
      <div class="process-empty">
        <span>🎮</span>
        <span>未发现 Minecraft 进程</span>
      </div>`;
        return;
    }
    elProcessList.innerHTML = processList.map(p => `
    <div class="process-item ${p.pid === selectedPid ? 'active' : ''}"
         data-pid="${p.pid}">
      <span class="process-item-icon">
        <svg class="process-icon-svg" viewBox="0 0 16 16" fill="none" xmlns="http://www.w3.org/2000/svg">
          <rect x="1" y="4" width="14" height="9" rx="2" stroke="currentColor" stroke-width="1.2"/>
          <line x1="5" y1="8.5" x2="7" y2="8.5" stroke="currentColor" stroke-width="1.2" stroke-linecap="round"/>
          <line x1="6" y1="7.5" x2="6" y2="9.5" stroke="currentColor" stroke-width="1.2" stroke-linecap="round"/>
          <circle cx="10" cy="7.8" r="0.7" fill="currentColor"/>
          <circle cx="11.6" cy="9.2" r="0.7" fill="currentColor"/>
        </svg>
      </span>
      <div class="process-item-info">
        <div class="process-item-name" title="${escHtml(p.name)}">${escHtml(p.name)}</div>
        <div class="process-item-pid">PID ${p.pid}</div>
      </div>
      <div class="process-item-right">
        <div class="process-item-usage">CPU ${p.cpuUsage.toFixed(1)}%<br>GPU ${p.gpuUsage.toFixed(1)}%</div>
        <div class="process-item-badges">
          ${p.cpuLimited ? '<span class="badge badge-cpu">CPU</span>' : ''}
          ${p.gpuLimited ? '<span class="badge badge-gpu">GPU</span>' : ''}
        </div>
      </div>
    </div>
  `).join('');
    elProcessList.querySelectorAll('.process-item').forEach(el => {
        el.addEventListener('click', () => {
            const pid = parseInt(el.dataset.pid ?? '0');
            selectProcess(pid);
        });
    });
}
// ─── 选中进程 ─────────────────────────────────────────────────────────────────
function selectProcess(pid) {
    selectedPid = pid;
    renderProcessList();
    const proc = processList.find(p => p.pid === pid);
    if (!proc)
        return;
    renderDetailPanel(proc);
}
function renderNoSelection() {
    elMainContent.innerHTML = `
    <div class="no-selection">
      <div class="no-selection-icon">🎮</div>
      <div class="no-selection-text">请从左侧选择一个 Minecraft 进程</div>
      <div class="no-selection-hint">未检测到进程时，请点击刷新重新扫描</div>
    </div>`;
}
// ─── 详情面板 ─────────────────────────────────────────────────────────────────
function renderDetailPanel(proc) {
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

    </div>`;
    bindDetailEvents(proc.pid);
}
function bindDetailEvents(pid) {
    const sliderCpu = document.getElementById('slider-cpu');
    const labelCpu = document.getElementById('label-cpu');
    sliderCpu?.addEventListener('input', () => { labelCpu.textContent = `${sliderCpu.value}%`; });
    const sliderGpu = document.getElementById('slider-gpu');
    const labelGpu = document.getElementById('label-gpu');
    sliderGpu?.addEventListener('input', () => { labelGpu.textContent = `${sliderGpu.value}%`; });
    document.getElementById('btn-apply-cpu')?.addEventListener('click', () => {
        const enabled = document.getElementById('toggle-cpu').checked;
        if (enabled) {
            bridge.send({ cmd: 'setLimit', payload: { pid, cpu: parseInt(sliderCpu.value) } });
        }
        else {
            bridge.send({ cmd: 'removeLimit', payload: { pid } });
        }
    });
    document.getElementById('btn-apply-gpu')?.addEventListener('click', () => {
        const enabled = document.getElementById('toggle-gpu').checked;
        if (enabled) {
            bridge.send({ cmd: 'setLimit', payload: { pid, gpu: parseInt(sliderGpu.value) } });
        }
        else {
            bridge.send({ cmd: 'removeLimit', payload: { pid } });
        }
    });
}
// ─── 更新实时使用率 ───────────────────────────────────────────────────────────
function updateDetailUsage(proc) {
    const barCpu = document.getElementById('bar-cpu');
    const valCpu = document.getElementById('val-cpu');
    const barGpu = document.getElementById('bar-gpu');
    const valGpu = document.getElementById('val-gpu');
    if (barCpu)
        barCpu.style.width = `${clamp(proc.cpuUsage)}%`;
    if (valCpu)
        valCpu.textContent = `${proc.cpuUsage.toFixed(1)}%`;
    if (barGpu)
        barGpu.style.width = `${clamp(proc.gpuUsage)}%`;
    if (valGpu)
        valGpu.textContent = `${proc.gpuUsage.toFixed(1)}%`;
}
// ─── 事件：限制结果 ───────────────────────────────────────────────────────────
bridge.on('limitApplied', (data) => {
    if (!data.cpu_ok || !data.gpu_ok)
        console.warn('[Bridge] Limit not fully applied:', data);
    bridge.send({ cmd: 'getProcessList' });
});
bridge.on('error', ({ message }) => {
    console.error('[Bridge] Backend error:', message);
    setStatus(`错误: ${message}`);
});
// ─── 事件：主题变化（后端推送）───────────────────────────────────────────────
bridge.on('themeChanged', ({ theme }) => {
    themeManager.applyToDOM(theme);
});
// ─── 工具函数 ─────────────────────────────────────────────────────────────────
function escHtml(s) {
    return s
        .replace(/&/g, '&amp;').replace(/</g, '&lt;')
        .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}
function truncate(s, maxLen) {
    if (s.length <= maxLen)
        return s;
    return '…' + s.slice(s.length - (maxLen - 1));
}
function clamp(v) {
    return Math.min(Math.max(v, 0), 100);
}
// ─── 初始化 ───────────────────────────────────────────────────────────────────
function init() {
    setStatus('正在扫描...');
    // 恢复本地主题偏好并通知后端
    themeManager.restore();
    updateThemeMenuChecks();
    bridge.send({ cmd: 'getProcessList' });
}
init();
