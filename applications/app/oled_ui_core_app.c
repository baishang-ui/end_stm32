#include <rtthread.h>

#include "ui/oled_ui_core/oled_ui_core.h"
#include "ui/oled_ui_core/oled_ui_pages.h"
#include "data/todo_snapshot.h"

extern bool OLED_UI_ShowFps;
extern bool ColorMode;
extern int16_t OLED_UI_Brightness;

static void oled_ui_core_thread(void *parameter)
{
    rt_tick_t last_input_tick;

    RT_UNUSED(parameter);

    rt_thread_mdelay(80);
    todo_snapshot_init();

    OLED_UI_ShowFps = false;
    ColorMode = DARKMODE;
    OLED_UI_Brightness = 180;

    oled_ui_init(&MainMenuPage);
    OLED_Clear();
    OLED_Update();
    rt_thread_mdelay(30);

    last_input_tick = rt_tick_get();

    while (1)
    {
        if ((rt_tick_get() - last_input_tick) >= rt_tick_from_millisecond(20))
        {
            oled_ui_interrupt_handler();
            last_input_tick = rt_tick_get();
        }

        oled_ui_prepare_pages();
        oled_ui_main_loop();
        rt_thread_mdelay(8);
    }
}

static int oled_ui_core_start(void)
{
    rt_thread_t thread;

    thread = rt_thread_create("uicore", oled_ui_core_thread, RT_NULL, 4096, 18, 10);
    if (thread == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    rt_thread_startup(thread);
    return RT_EOK;
}
INIT_APP_EXPORT(oled_ui_core_start);


