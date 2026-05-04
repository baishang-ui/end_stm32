#ifndef __APP_UI_PAGES_H__
#define __APP_UI_PAGES_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "ui/oled_ui_core/oled_ui_core.h"

/**
 * @brief 主页菜单项数组。
 */
extern MenuItem MainMenuItems[];

/**
 * @brief 待办列表页面菜单项数组。
 */
extern MenuItem TodoMenuItems[];

/**
 * @brief 无线网络入口页面菜单项数组。
 */
extern MenuItem WiFiMenuItems[];

/**
 * @brief 当前网络信息页面菜单项数组。
 */
extern MenuItem WiFiInfoMenuItems[];

/**
 * @brief Wi-Fi 扫描列表页面菜单项数组。
 */
extern MenuItem WiFiScanMenuItems[];

/**
 * @brief 电脑控制页面菜单项数组。
 */
extern MenuItem PCControlMenuItems[];

/**
 * @brief 快捷键槽位页面菜单项数组。
 */
extern MenuItem PCShortcutMenuItems[];

/**
 * @brief 快捷键预设选择页面菜单项数组。
 */
extern MenuItem ShortcutPresetMenuItems[];

/**
 * @brief 系统状态页面菜单项数组。
 */
extern MenuItem StatusMenuItems[];

/**
 * @brief 系统设置页面菜单项数组。
 */
extern MenuItem SettingsMenuItems[];

/**
 * @brief 主页页面描述对象。
 */
extern MenuPage MainMenuPage;

/**
 * @brief 待办列表页面描述对象。
 */
extern MenuPage TodoMenuPage;

/**
 * @brief 无线网络入口页面描述对象。
 */
extern MenuPage WiFiMenuPage;

/**
 * @brief 当前网络信息页面描述对象。
 */
extern MenuPage WiFiInfoMenuPage;

/**
 * @brief Wi-Fi 扫描列表页面描述对象。
 */
extern MenuPage WiFiScanMenuPage;

/**
 * @brief 电脑控制页面描述对象。
 */
extern MenuPage PCControlMenuPage;

/**
 * @brief 快捷键槽位页面描述对象。
 */
extern MenuPage PCShortcutMenuPage;

/**
 * @brief 第 1 个快捷键槽位的预设选择页面。
 */
extern MenuPage ShortcutPresetPage1;

/**
 * @brief 第 2 个快捷键槽位的预设选择页面。
 */
extern MenuPage ShortcutPresetPage2;

/**
 * @brief 第 3 个快捷键槽位的预设选择页面。
 */
extern MenuPage ShortcutPresetPage3;

/**
 * @brief 第 4 个快捷键槽位的预设选择页面。
 */
extern MenuPage ShortcutPresetPage4;

/**
 * @brief 系统状态页面描述对象。
 */
extern MenuPage StatusMenuPage;

/**
 * @brief 系统设置页面描述对象。
 */
extern MenuPage SettingsMenuPage;

/**
 * @brief 刷新动态页面文本和状态值。
 * @param 无。
 * @return 无。
 */
void app_ui_refresh_pages(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_UI_PAGES_H__ */
