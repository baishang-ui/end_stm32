#include "app_ui_runtime.h"

float AppUI_PageMotionSpeed = 7.0f;
float AppUI_TextScrollSpeed = 0.20f;
rt_int16_t AppUI_FadeStepMs = 40;
rt_int16_t AppUI_FontSize = OLED_UI_FONT_12;
rt_bool_t AppUI_UseSmoothCurve = RT_TRUE;

/**
 * @brief 将配置字号归一化为 UI 支持的字号。
 * @param font_size 用户配置或运行时修改后的字号。
 * @return UI 支持的最接近字号。
 */
static rt_int16_t app_ui_normalize_font_size(rt_int16_t font_size)
{
    if (font_size <= OLED_UI_FONT_8)
    {
        return OLED_UI_FONT_8;
    }

    if (font_size <= OLED_UI_FONT_12)
    {
        return OLED_UI_FONT_12;
    }

    return OLED_UI_FONT_16;
}

/**
 * @brief 根据运行时开关获取菜单移动动画类型。
 * @param 无。
 * @return 启用平滑曲线返回 EASE_CURVE，否则返回 UNLINEAR。
 */
uint8_t app_ui_get_move_style(void)
{
    return (AppUI_UseSmoothCurve != RT_FALSE) ? EASE_CURVE : UNLINEAR;
}

/**
 * @brief 将当前运行时 UI 参数应用到单个页面。
 * @param page 需要应用参数的页面对象。
 * @return 无。
 */
void app_ui_apply_runtime_to_page(MenuPage *page)
{
    if (page == RT_NULL)
    {
        return;
    }

    AppUI_FontSize = app_ui_normalize_font_size(AppUI_FontSize);

    page->General_MovingSpeed = AppUI_PageMotionSpeed;
    page->General_MoveStyle = app_ui_get_move_style();
    page->General_FontSize = (OLED_Font)AppUI_FontSize;
}

/**
 * @brief 将当前运行时 UI 参数批量应用到页面数组。
 * @param pages 页面指针数组。
 * @param count 页面数量。
 * @return 无。
 */
void app_ui_apply_runtime_to_pages(MenuPage **pages, rt_size_t count)
{
    rt_size_t index;

    if (pages == RT_NULL)
    {
        return;
    }

    for (index = 0; index < count; index++)
    {
        app_ui_apply_runtime_to_page(pages[index]);
    }
}
