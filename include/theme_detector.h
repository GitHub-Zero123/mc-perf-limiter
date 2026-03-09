#pragma once
#include "ipc_types.h"
#include <windows.h>
#include <functional>

// 系统主题检测器
// 通过注册表读取 AppsUseLightTheme，监听 WM_SETTINGCHANGE 变化
class ThemeDetector {
public:
    using ThemeChangeCallback = std::function<void(ipc::Theme effectiveTheme)>;

    ThemeDetector();
    ~ThemeDetector() = default;

    // 读取当前系统是否为深色模式
    static bool isSystemDark();

    // 根据用户设置解析实际生效主题
    // userPref=System 时返回系统实际值
    static ipc::Theme resolveTheme(ipc::Theme userPref);

    // 注册主题变化回调（供 App 订阅）
    void setCallback(ThemeChangeCallback cb) { callback_ = std::move(cb); }

    // 由 App 在 WM_SETTINGCHANGE 时调用
    void onSettingChange(WPARAM wParam, LPARAM lParam);

    // 当前用户偏好设置
    ipc::Theme userPreference() const { return userPref_; }
    void setUserPreference(ipc::Theme t);

private:
    ipc::Theme       userPref_ = ipc::Theme::System;
    ThemeChangeCallback callback_;

    // 上一次已知的系统主题（避免重复通知）
    bool lastSystemDark_ = false;
};
