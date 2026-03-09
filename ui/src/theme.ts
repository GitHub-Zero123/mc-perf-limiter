/**
 * theme.ts — 主题管理
 * 支持 dark / light / system 三种模式
 * 偏好持久化到 localStorage，启动时恢复并通知后端
 */

import { bridge, type Theme } from './bridge'

// 实际生效的主题
type EffectiveTheme = 'dark' | 'light'

const STORAGE_KEY = 'mcperf-theme'
const VALID_THEMES: Theme[] = ['dark', 'light', 'system']

class ThemeManager {
  private current: Theme = 'system'
  private effective: EffectiveTheme = 'dark'
  private listeners: Array<(t: EffectiveTheme) => void> = []

  constructor() {
    // 监听后端推送的主题变化（系统主题变化时触发）
    bridge.on('themeChanged', ({ theme }) => {
      // 后端返回的是解析后的实际主题
      this.effective = theme as EffectiveTheme
      this.applyToDOM(theme as EffectiveTheme)
      this.listeners.forEach(fn => fn(theme as EffectiveTheme))
    })

    // 读取本地存储的偏好，立即恢复
    const saved = localStorage.getItem(STORAGE_KEY) as Theme | null
    if (saved && VALID_THEMES.includes(saved)) {
      this.current = saved
    }
  }

  /** 启动后通知后端当前偏好（需在 bridge 就绪后调用） */
  restore(): void {
    bridge.send({ cmd: 'setTheme', payload: { theme: this.current } })
  }

  /** 获取当前用户偏好 */
  getPreference(): Theme { return this.current }

  /** 获取当前实际生效主题 */
  getEffective(): EffectiveTheme { return this.effective }

  /** 直接设置主题偏好 */
  set(t: Theme): void {
    this.current = t
    localStorage.setItem(STORAGE_KEY, t)
    bridge.send({ cmd: 'setTheme', payload: { theme: t } })
  }

  /** 订阅主题变化（实际生效主题变化时触发） */
  onChange(fn: (t: EffectiveTheme) => void): () => void {
    this.listeners.push(fn)
    return () => {
      this.listeners = this.listeners.filter(l => l !== fn)
    }
  }

  /** 将实际主题应用到 DOM data-theme 属性 */
  applyToDOM(theme: EffectiveTheme): void {
    document.documentElement.setAttribute('data-theme', theme)
  }
}

export const themeManager = new ThemeManager()
