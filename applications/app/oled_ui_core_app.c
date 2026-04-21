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

#define OLED_BOOT_MAX_STEPS        16U
#define OLED_BOOT_TITLE_X          4
#define OLED_BOOT_TITLE_Y          2
#define OLED_BOOT_LIST_X           6
#define OLED_BOOT_LIST_Y           16
#define OLED_BOOT_LIST_W           116
#define OLED_BOOT_LIST_H           36
#define OLED_BOOT_ROW_H            12
#define OLED_BOOT_VISIBLE_ROWS     3U
#define OLED_BOOT_MARKER_X         9
#define OLED_BOOT_TEXT_X           18
#define OLED_BOOT_COUNTER_Y        4
#define OLED_BOOT_COUNTER_MARGIN   6
#define OLED_BOOT_BAR_X            14
#define OLED_BOOT_BAR_Y            56
#define OLED_BOOT_BAR_W            100
#define OLED_BOOT_BAR_H            5
#define OLED_BOOT_ANIM_FRAMES      6
#define OLED_BOOT_ANIM_DELAY_MS    18U
#define OLED_BOOT_SETTLE_DELAY_MS  40U
#define OLED_BOOT_FINISH_DELAY_MS  180U

static volatile rt_uint8_t s_oled_ui_ready = 0U;
static volatile rt_uint8_t s_oled_ui_started = 0U;
static char s_boot_labels[OLED_BOOT_MAX_STEPS][32];
static rt_uint32_t s_boot_step_count = 0U;
static rt_uint32_t s_boot_plan_total = 0U;
static rt_uint32_t s_boot_last_active_step = 0U;

static int oled_ui_boot_lerp_int(int from, int to, int numerator, int denominator)
{
    if (denominator <= 0)
    {
        return to;
    }

    return from + (((to - from) * numerator) / denominator);
}

static rt_uint32_t oled_ui_boot_clamp_total(rt_uint32_t total)
{
    if (total > OLED_BOOT_MAX_STEPS)
    {
        return OLED_BOOT_MAX_STEPS;
    }

    return total;
}

static rt_uint32_t oled_ui_boot_get_total(rt_uint32_t total)
{
    total = oled_ui_boot_clamp_total(total);
    if (total != 0U)
    {
        return total;
    }

    if (s_boot_plan_total != 0U)
    {
        return s_boot_plan_total;
    }

    if (s_boot_step_count != 0U)
    {
        return s_boot_step_count;
    }

    return 1U;
}

static const char *oled_ui_boot_get_label_by_index(rt_uint32_t index)
{
    if ((index < OLED_BOOT_MAX_STEPS) && (s_boot_labels[index][0] != '\0'))
    {
        return s_boot_labels[index];
    }

    return "...";
}

static rt_uint32_t oled_ui_boot_get_visible_rows(rt_uint32_t total)
{
    if (total == 0U)
    {
        return 0U;
    }

    if (total < OLED_BOOT_VISIBLE_ROWS)
    {
        return total;
    }

    return OLED_BOOT_VISIBLE_ROWS;
}

static rt_uint32_t oled_ui_boot_get_active_index(rt_uint32_t active_step, rt_uint32_t total)
{
    if (total == 0U)
    {
        return 0U;
    }

    if (active_step == 0U)
    {
        return 0U;
    }

    if (active_step >= total)
    {
        return total - 1U;
    }

    return active_step - 1U;
}

static rt_uint32_t oled_ui_boot_get_first_index(rt_uint32_t active_index, rt_uint32_t total)
{
    rt_uint32_t visible_rows = oled_ui_boot_get_visible_rows(total);
    rt_uint32_t max_first;

    if ((visible_rows == 0U) || (visible_rows >= total))
    {
        return 0U;
    }

    max_first = total - visible_rows;
    if (active_index < (visible_rows - 1U))
    {
        return 0U;
    }

    active_index -= (visible_rows - 1U);
    if (active_index > max_first)
    {
        return max_first;
    }

    return active_index;
}

static rt_uint32_t oled_ui_boot_get_cursor_row(rt_uint32_t active_index, rt_uint32_t total)
{
    rt_uint32_t visible_rows = oled_ui_boot_get_visible_rows(total);

    if (visible_rows == 0U)
    {
        return 0U;
    }

    if (active_index < visible_rows)
    {
        return active_index;
    }

    return visible_rows - 1U;
}

static void oled_ui_boot_draw_counter(rt_uint32_t active_step, rt_uint32_t total)
{
    char counter_text[16];
    int text_width;
    int text_x;

    if (total == 0U)
    {
        total = 1U;
    }

    rt_snprintf(counter_text,
                sizeof(counter_text),
                "%lu/%lu",
                (unsigned long)active_step,
                (unsigned long)total);
    text_width = (int)rt_strlen(counter_text) * OLED_6X8_HALF;
    text_x = OLED_WIDTH - OLED_BOOT_COUNTER_MARGIN - text_width;
    if (text_x < OLED_BOOT_COUNTER_MARGIN)
    {
        text_x = OLED_BOOT_COUNTER_MARGIN;
    }

    OLED_Printf(text_x, OLED_BOOT_COUNTER_Y, OLED_6X8_HALF, "%s", counter_text);
}

static void oled_ui_boot_draw_header(const char *title, rt_uint32_t active_step, rt_uint32_t total)
{
    OLED_PrintfMixArea(0,
                       0,
                       48,
                       14,
                       OLED_BOOT_TITLE_X,
                       OLED_BOOT_TITLE_Y,
                       OLED_12X12_FULL,
                       OLED_6X8_HALF,
                       "%s",
                       title);
    oled_ui_boot_draw_counter(active_step, total);
}

static void oled_ui_boot_draw_list(rt_uint32_t active_step,
                                   rt_uint32_t total,
                                   int numerator,
                                   int denominator)
{
    rt_uint32_t prev_active_index;
    rt_uint32_t next_active_index;
    rt_uint32_t row_index;
    int scroll_prev;
    int scroll_next;
    int scroll_pixels;
    int highlight_prev_y;
    int highlight_next_y;
    int highlight_y;

    if (total == 0U)
    {
        return;
    }

    prev_active_index = oled_ui_boot_get_active_index(s_boot_last_active_step, total);
    next_active_index = oled_ui_boot_get_active_index(active_step, total);
    scroll_prev = (int)(oled_ui_boot_get_first_index(prev_active_index, total) * OLED_BOOT_ROW_H);
    scroll_next = (int)(oled_ui_boot_get_first_index(next_active_index, total) * OLED_BOOT_ROW_H);
    scroll_pixels = oled_ui_boot_lerp_int(scroll_prev, scroll_next, numerator, denominator);
    highlight_prev_y = OLED_BOOT_LIST_Y +
                       (int)(oled_ui_boot_get_cursor_row(prev_active_index, total) * OLED_BOOT_ROW_H);
    highlight_next_y = OLED_BOOT_LIST_Y +
                       (int)(oled_ui_boot_get_cursor_row(next_active_index, total) * OLED_BOOT_ROW_H);
    highlight_y = oled_ui_boot_lerp_int(highlight_prev_y, highlight_next_y, numerator, denominator);

    for (row_index = 0U; row_index < total; row_index++)
    {
        char row_text[48];
        int row_y = OLED_BOOT_LIST_Y + ((int)row_index * OLED_BOOT_ROW_H) - scroll_pixels;
        int marker_y = row_y + 4;

        if ((row_y <= (OLED_BOOT_LIST_Y - OLED_BOOT_ROW_H)) ||
            (row_y >= (OLED_BOOT_LIST_Y + OLED_BOOT_LIST_H)))
        {
            continue;
        }

        if ((active_step > 0U) && (row_index <= next_active_index))
        {
            OLED_DrawRectangle(OLED_BOOT_MARKER_X, marker_y, 4, 4, OLED_FILLED);
        }
        else
        {
            OLED_DrawRectangle(OLED_BOOT_MARKER_X, marker_y, 4, 4, OLED_UNFILLED);
        }

        rt_snprintf(row_text,
                    sizeof(row_text),
                    "%02lu %s",
                    (unsigned long)(row_index + 1U),
                    oled_ui_boot_get_label_by_index(row_index));
        OLED_PrintfMixArea(OLED_BOOT_LIST_X,
                           OLED_BOOT_LIST_Y,
                           OLED_BOOT_LIST_W,
                           OLED_BOOT_LIST_H,
                           OLED_BOOT_TEXT_X,
                           row_y,
                           OLED_12X12_FULL,
                           OLED_6X8_HALF,
                           "%s",
                           row_text);
    }

    OLED_ReverseArea(OLED_BOOT_LIST_X,
                     highlight_y - 1,
                     OLED_BOOT_LIST_W,
                     OLED_BOOT_ROW_H);
}

static void oled_ui_boot_draw_progress(int fill_width)
{
    OLED_DrawRoundedRectangle(OLED_BOOT_BAR_X,
                              OLED_BOOT_BAR_Y,
                              OLED_BOOT_BAR_W,
                              OLED_BOOT_BAR_H,
                              2,
                              OLED_UNFILLED);
    if (fill_width > 0)
    {
        OLED_DrawRoundedRectangle(OLED_BOOT_BAR_X + 1,
                                  OLED_BOOT_BAR_Y + 1,
                                  fill_width,
                                  OLED_BOOT_BAR_H - 2,
                                  1,
                                  OLED_FILLED);
    }
}

static void oled_ui_boot_render(rt_uint32_t active_step,
                                rt_uint32_t total,
                                const char *title,
                                int numerator,
                                int denominator)
{
    int fill_prev = 0;
    int fill_next = 0;
    int fill_width;

    total = oled_ui_boot_get_total(total);
    if (active_step > total)
    {
        active_step = total;
    }

    if (total > 0U)
    {
        fill_prev = (int)(((OLED_BOOT_BAR_W - 2) * s_boot_last_active_step) / total);
        fill_next = (int)(((OLED_BOOT_BAR_W - 2) * active_step) / total);
    }

    fill_width = oled_ui_boot_lerp_int(fill_prev, fill_next, numerator, denominator);

    OLED_Clear();
    oled_ui_boot_draw_header(title, active_step, total);
    oled_ui_boot_draw_list(active_step, total, numerator, denominator);
    oled_ui_boot_draw_progress(fill_width);
    OLED_Update();
}

void oled_ui_boot_init(void)
{
    memset(s_boot_labels, 0, sizeof(s_boot_labels));
    s_boot_step_count = 0U;
    s_boot_plan_total = 0U;
    s_boot_last_active_step = 0U;
    s_oled_ui_ready = 0U;
    s_oled_ui_started = 0U;

    OLED_Init();
    OLED_SetColorMode(DARKMODE);
    OLED_Brightness(180);
    oled_ui_boot_render(0U, 1U, "启动", 1, 1);
}

void oled_ui_boot_set_plan(rt_uint32_t total, const char * const *labels)
{
    rt_uint32_t index;

    memset(s_boot_labels, 0, sizeof(s_boot_labels));
    s_boot_step_count = 0U;
    s_boot_plan_total = 0U;
    s_boot_last_active_step = 0U;

    total = oled_ui_boot_clamp_total(total);
    s_boot_plan_total = total;
    s_boot_step_count = total;

    if (labels != RT_NULL)
    {
        for (index = 0U; index < total; index++)
        {
            if ((labels[index] != RT_NULL) && (labels[index][0] != '\0'))
            {
                rt_strncpy(s_boot_labels[index], labels[index], sizeof(s_boot_labels[index]) - 1U);
                s_boot_labels[index][sizeof(s_boot_labels[index]) - 1U] = '\0';
            }
        }
    }

    oled_ui_boot_render(0U, s_boot_plan_total, "启动", 1, 1);
}

void oled_ui_boot_step(rt_uint32_t step, rt_uint32_t total, const char *label, rt_err_t result)
{
    rt_uint32_t index;
    rt_uint32_t active_step;
    int frame;

    RT_UNUSED(result);

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
    if ((index + 1U) > s_boot_step_count)
    {
        s_boot_step_count = index + 1U;
    }

    if (total != 0U)
    {
        s_boot_plan_total = oled_ui_boot_clamp_total(total);
    }

    active_step = index + 1U;
    if (active_step <= s_boot_last_active_step)
    {
        oled_ui_boot_render(active_step, total, "启动", 1, 1);
        s_boot_last_active_step = active_step;
        rt_thread_mdelay(80);
        return;
    }

    for (frame = 1; frame <= OLED_BOOT_ANIM_FRAMES; frame++)
    {
        oled_ui_boot_render(active_step, total, "启动", frame, OLED_BOOT_ANIM_FRAMES);
        rt_thread_mdelay(OLED_BOOT_ANIM_DELAY_MS);
    }

    s_boot_last_active_step = active_step;
    rt_thread_mdelay(OLED_BOOT_SETTLE_DELAY_MS);
}

void oled_ui_boot_complete(void)
{
    rt_uint32_t total = oled_ui_boot_get_total(0U);

    oled_ui_boot_render(total, total, "完成", 1, 1);
    rt_thread_mdelay(OLED_BOOT_FINISH_DELAY_MS);
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
