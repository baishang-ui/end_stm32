#include "app_ui_runtime.h"

float AppUI_PageMotionSpeed = 7.0f;
float AppUI_TextScrollSpeed = 0.20f;
rt_int16_t AppUI_FadeStepMs = 40;
rt_bool_t AppUI_UseSmoothCurve = RT_TRUE;

uint8_t app_ui_get_move_style(void)
{
    return (AppUI_UseSmoothCurve != RT_FALSE) ? PID_CURVE : UNLINEAR;
}

void app_ui_apply_runtime_to_page(MenuPage *page)
{
    if (page == RT_NULL)
    {
        return;
    }

    page->General_MovingSpeed = AppUI_PageMotionSpeed;
    page->General_MoveStyle = app_ui_get_move_style();
}

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
