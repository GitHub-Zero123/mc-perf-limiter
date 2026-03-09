#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include "theme_detector.h"
#include <windows.h>

// ─── 注册表键路径 ─────────────────────────────────────────────────────────────

static constexpr const wchar_t* REG_KEY =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
static constexpr const wchar_t* REG_VALUE = L"AppsUseLightTheme";

// ─── 构造 ─────────────────────────────────────────────────────────────────────

ThemeDetector::ThemeDetector() {
    lastSystemDark_ = isSystemDark();
}

// ─── 读取系统主题 ─────────────────────────────────────────────────────────────

bool ThemeDetector::isSystemDark() {
    DWORD value = 1; // 默认浅色
    DWORD size  = sizeof(value);
    RegGetValueW(
        HKEY_CURRENT_USER,
        REG_KEY,
        REG_VALUE,
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size);
    // AppsUseLightTheme == 0 → 深色模式
    return value == 0;
}

// ─── 解析实际生效主题 ─────────────────────────────────────────────────────────

ipc::Theme ThemeDetector::resolveTheme(ipc::Theme userPref) {
    if (userPref == ipc::Theme::System) {
        return isSystemDark() ? ipc::Theme::Dark : ipc::Theme::Light;
    }
    return userPref;
}

// ─── 设置用户偏好并触发回调 ───────────────────────────────────────────────────

void ThemeDetector::setUserPreference(ipc::Theme t) {
    userPref_ = t;
    if (callback_) {
        callback_(resolveTheme(t));
    }
}

// ─── 处理系统设置变化 ─────────────────────────────────────────────────────────

void ThemeDetector::onSettingChange(WPARAM /*wParam*/, LPARAM lParam) {
    // WM_SETTINGCHANGE 时 lParam 为变化的区域字符串
    // "ImmersiveColorSet" 表示主题颜色变化
    if (lParam) {
        const wchar_t* section = reinterpret_cast<const wchar_t*>(lParam);
        if (wcscmp(section, L"ImmersiveColorSet") != 0) return;
    }

    bool nowDark = isSystemDark();
    if (nowDark == lastSystemDark_) return; // 无变化
    lastSystemDark_ = nowDark;

    // 仅当用户偏好为 System 时才触发回调
    if (userPref_ == ipc::Theme::System && callback_) {
        callback_(nowDark ? ipc::Theme::Dark : ipc::Theme::Light);
    }
}
