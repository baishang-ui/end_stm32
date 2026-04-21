#include <rtthread.h>
#include <string.h>

#include "ui/oled_ui_core/oled_ui_core.h"
#include "ui/oled_ui_app/app_ui_pages.h"
#include "ui/oled_ui_core/oled_graphics.h"
#include "ui/oled_ui_core/oled_hal_input.h"
#include "oled_ui_core_app.h"

extern bool OLED_UI_ShowFps;
extern bool ColorMode;
extern int16_t OLED_UI_Brightness;

#define OLED_BOOT_MAX_STEPS  8

static volatile rt_uint8_t s_oled_ui_ready = 0U;
static volatile rt_uint8_t s_oled_ui_started = 0U;
static char s_boot_labels[OLED_BOOT_MAX_STEPS][32];
static rt_int8_t s_boot_results[OLED_BOOT_MAX_STEPS];
static rt_uint32_t s_boot_step_count = 0U;

static void oled_ui_boot_render(rt_uint32_t active_step, rt_uint32_t total)
{
    rt_uint32_t i;
    int bar_x = 10;
    int bar_y = 54;
    int bar_w = 108;
    int bar_h = 6;
    int fill_w = 0;

    if (total > 0U)
    {
        fill_w = (int)(((bar_w - 2) * active_step) / total);
    }

    OLED_Clear();
    OLED_PrintfMix(28, 6, OLED_12X12_FULL, OLED_6X8_HALF, "系统启动");
    OLED_DrawLine(10, 24, 118, 24);

    for (i = 0; i < s_boot_step_count; i++)
    {
        const char *mark = (s_boot_results[i] == RT_EOK) ? "完成" : ((s_boot_results[i] == 1) ? "进行" : "失败");
        OLED_PrintfMix(10, 28 + (int)i * 8, OLED_8X8_FULL, OLED_6X8_HALF, "%s %s", mark, s_boot_labels[i]);
    }

    OLED_DrawRoundedRectangle(bar_x, bar_y, bar_w, bar_h, 2, OLED_UNFILLED);
    if (fill_w > 0)
    {
        OLED_DrawRoundedRectangle(bar_x + 1, bar_y + 1, fill_w, bar_h - 2, 1, OLED_FILLED);
    }
    OLED_Update();
}

void oled_ui_boot_init(void)
{
    memset(s_boot_labels, 0, sizeof(s_boot_labels));
    memset(s_boot_results, 0, sizeof(s_boot_results));
    s_boot_step_count = 0U;
    s_oled_ui_ready = 0U;
    s_oled_ui_started = 0U;

    OLED_Init();
    OLED_SetColorMode(DARKMODE);
    OLED_Brightness(180);
    oled_ui_boot_render(0U, 1U);
}

void oled_ui_boot_step(rt_uint32_t step, rt_uint32_t total, const char *label, rt_err_t result)
{
    rt_uint32_t index;

    if (step >= OLED_BOOT_MAX_STEPS)
    {
        return;
    }

    index = step;
    if ((label != RT_NULL) && (label[0] != '\0'))
    {
        rt_strncpy(s_boot_labels[index], label, sizeof(s_boot_labels[index]) - 1U);
        s_boot_labels[index][sizeof(s_boot_labels[index]) - 1U] = '\0';
    }
    s_boot_results[index] = (result == RT_EOK) ? RT_EOK : -1;
    if ((index + 1U) > s_boot_step_count)
    {
        s_boot_step_count = index + 1U;
    }

    oled_ui_boot_render(index + 1U, total);
    rt_thread_mdelay(140);
}

void oled_ui_boot_complete(void)
{
    OLED_Clear();
    OLED_PrintfMix(28, 20, OLED_12X12_FULL, OLED_6X8_HALF, "启动完成");
    OLED_DrawLine(26, 40, 102, 40);
    OLED_Update();
    rt_thread_mdelay(220);
}

static void oled_ui_core_thread(void *parameter)
{
    rt_tick_t last_input_tick;

    RT_UNUSED(parameter);

    OLED_UI_ShowFps = false;
    ColorMode = DARKMODE;
    OLED_UI_Brightness = 180;

    oled_ui_init(&MainMenuPage);
    OLED_Clear();
    OLED_Update();
    rt_thread_mdelay(30);

    s_oled_ui_ready = 1U;
    last_input_tick = rt_tick_get();

    while (1)
    {
        if ((rt_tick_get() - last_input_tick) >= rt_tick_from_millisecond(20))
        {
            oled_ui_interrupt_handler();
            last_input_tick = rt_tick_get();
        }

        app_ui_refresh_pages();
        oled_ui_main_loop();
        rt_thread_mdelay(8);
    }
}

int oled_ui_core_start(void)
{
    rt_thread_t thread;

    if (s_oled_ui_started != 0U)
    {
        return RT_EOK;
    }

    thread = rt_thread_create("uicore", oled_ui_core_thread, RT_NULL, 4096, 18, 10);
    if (thread == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    s_oled_ui_started = 1U;
    rt_thread_startup(thread);
    return RT_EOK;
}

rt_err_t oled_ui_core_wait_ready(rt_int32_t timeout_ms)
{
    rt_tick_t start = rt_tick_get();
    rt_tick_t timeout_tick = (timeout_ms < 0) ? 0U : rt_tick_from_millisecond((rt_uint32_t)timeout_ms);

    while (s_oled_ui_ready == 0U)
    {
        if ((timeout_ms >= 0) && ((rt_tick_get() - start) >= timeout_tick))
        {
            return -RT_ETIMEOUT;
        }
        rt_thread_mdelay(10);
    }

    return RT_EOK;
}
