/**
 * theme.ts — 主题管理
 * 支持 dark / light / system 三种模式
 * 偏好持久化到 localStorage，启动时恢复并通知后端
 */
import { bridge } from './bridge';
const STORAGE_KEY = 'mcperf-theme';
const VALID_THEMES = ['dark', 'light', 'system'];
class ThemeManager {
    constructor() {
        Object.defineProperty(this, "current", {
            enumerable: true,
            configurable: true,
            writable: true,
            value: 'system'
        });
        Object.defineProperty(this, "effective", {
            enumerable: true,
            configurable: true,
            writable: true,
            value: 'dark'
        });
        Object.defineProperty(this, "listeners", {
            enumerable: true,
            configurable: true,
            writable: true,
            value: []
        });
        // 监听后端推送的主题变化（系统主题变化时触发）
        bridge.on('themeChanged', ({ theme }) => {
            // 后端返回的是解析后的实际主题
            this.effective = theme;
            this.applyToDOM(theme);
            this.listeners.forEach(fn => fn(theme));
        });
        // 读取本地存储的偏好，立即恢复
        const saved = localStorage.getItem(STORAGE_KEY);
        if (saved && VALID_THEMES.includes(saved)) {
            this.current = saved;
        }
    }
    /** 启动后通知后端当前偏好（需在 bridge 就绪后调用） */
    restore() {
        bridge.send({ cmd: 'setTheme', payload: { theme: this.current } });
    }
    /** 获取当前用户偏好 */
    getPreference() { return this.current; }
    /** 获取当前实际生效主题 */
    getEffective() { return this.effective; }
    /** 直接设置主题偏好 */
    set(t) {
        this.current = t;
        localStorage.setItem(STORAGE_KEY, t);
        bridge.send({ cmd: 'setTheme', payload: { theme: t } });
    }
    /** 订阅主题变化（实际生效主题变化时触发） */
    onChange(fn) {
        this.listeners.push(fn);
        return () => {
            this.listeners = this.listeners.filter(l => l !== fn);
        };
    }
    /** 将实际主题应用到 DOM data-theme 属性 */
    applyToDOM(theme) {
        document.documentElement.setAttribute('data-theme', theme);
    }
}
export const themeManager = new ThemeManager();
