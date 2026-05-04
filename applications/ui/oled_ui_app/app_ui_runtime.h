#ifndef __APP_UI_RUNTIME_H__
#define __APP_UI_RUNTIME_H__

#include <rtthread.h>

#include "ui/oled_ui_core/oled_ui_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 页面切换动画速度倍率。
 */
extern float AppUI_PageMotionSpeed;

/**
 * @brief 长文本横向滚动速度倍率。
 */
extern float AppUI_TextScrollSpeed;

/**
 * @brief 淡入淡出动画单步时间，单位毫秒。
 */
extern rt_int16_t AppUI_FadeStepMs;

/**
 * @brief 当前 UI 字号配置。
 */
extern rt_int16_t AppUI_FontSize;

/**
 * @brief 是否使用平滑 ease 曲线动画。
 */
extern rt_bool_t AppUI_UseSmoothCurve;

/**
 * @brief 根据运行时配置获取菜单移动动画类型。
 * @param 无。
 * @return UNLINEAR 或 EASE_CURVE。
 */
uint8_t app_ui_get_move_style(void);

/**
 * @brief 将运行时 UI 参数应用到一个页面。
 * @param page 需要更新的页面对象。
 * @return 无。
 */
void app_ui_apply_runtime_to_page(MenuPage *page);

/**
 * @brief 将运行时 UI 参数批量应用到多个页面。
 * @param pages 页面指针数组。
 * @param count pages 中的页面数量。
 * @return 无。
 */
void app_ui_apply_runtime_to_pages(MenuPage **pages, rt_size_t count);

#ifdef __cplusplus
}
#endif

#endif /* __APP_UI_RUNTIME_H__ */
