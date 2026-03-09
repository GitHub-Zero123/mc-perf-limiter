/**
 * theme.ts — 主题管理
 * 支持 dark / light / system 三种模式
 */

import { bridge, type Theme } from './bridge'

type EffectiveTheme = 'dark' | 'light'

class ThemeManager {
  private current: Theme = 'system'
  private effective: EffectiveTheme = 'dark'
  private listeners: Array<(t: EffectiveTheme) => void> = []

  constructor() {
    // 监听后端推送的主题变化
    bridge.on('themeChanged', ({ theme }) => {
      this.effective = theme
      this.applyToDOM(theme)
      this.listeners.forEach(fn => fn(theme))
    })

    // 读取本地存储的偏好
    const saved = localStorage.getItem('theme') as Theme | null
    if (saved && ['dark', 'light', 'system'].includes(saved)) {
      this.current = saved
    }
  }

  /** 获取当前用户偏好 */
  getPreference(): Theme { return this.current }

  /** 获取当前实际生效主题 */
  getEffective(): EffectiveTheme { return this.effective }

  /** 切换到下一个主题 */
  cycle(): void {
    const order: Theme[] = ['dark', 'light', 'system']
    const idx = order.indexOf(this.current)
    this.set(order[(idx + 1) % order.length])
  }

  /** 设置主题偏好 */
  set(t: Theme): void {
    this.current = t
    localStorage.setItem('theme', t)
    bridge.send({ cmd: 'setTheme', payload: { theme: t } })
  }

  /** 订阅主题变化 */
  onChange(fn: (t: EffectiveTheme) => void): () => void {
    this.listeners.push(fn)
    return () => {
      this.listeners = this.listeners.filter(l => l !== fn)
    }
  }

  /** 将主题应用到 DOM */
  private applyToDOM(theme: EffectiveTheme): void {
    document.documentElement.setAttribute('data-theme', theme)
  }

  /** 返回主题图标（用于标题栏按钮） */
  getIcon(theme: Theme = this.current): string {
    if (theme === 'dark')   return '🌙'
    if (theme === 'light')  return '☀️'
    return '🖥️'
  }
}

export const themeManager = new ThemeManager()
