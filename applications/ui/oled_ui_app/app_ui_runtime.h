#ifndef __APP_UI_RUNTIME_H__
#define __APP_UI_RUNTIME_H__

#include <rtthread.h>

#include "ui/oled_ui_core/oled_ui_core.h"

#ifdef __cplusplus
extern "C" {
#endif

extern float AppUI_PageMotionSpeed;
extern float AppUI_TextScrollSpeed;
extern rt_int16_t AppUI_FadeStepMs;
extern rt_int16_t AppUI_FontSize;
extern rt_bool_t AppUI_UseSmoothCurve;

uint8_t app_ui_get_move_style(void);
void app_ui_apply_runtime_to_page(MenuPage *page);
void app_ui_apply_runtime_to_pages(MenuPage **pages, rt_size_t count);

#ifdef __cplusplus
}
#endif

#endif /* __APP_UI_RUNTIME_H__ */
