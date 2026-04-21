#ifndef __OLED_UI_CORE_APP_H__
#define __OLED_UI_CORE_APP_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

int oled_ui_core_start(void);
rt_err_t oled_ui_core_wait_ready(rt_int32_t timeout_ms);
void oled_ui_boot_init(void);
void oled_ui_boot_set_plan(rt_uint32_t total, const char * const *labels);
void oled_ui_boot_step(rt_uint32_t step, rt_uint32_t total, const char *label, rt_err_t result);
void oled_ui_boot_complete(void);

#ifdef __cplusplus
}
#endif

#endif
