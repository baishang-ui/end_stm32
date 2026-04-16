/*
 * SSD1306 OLED demo with EC11 encoder menu.
 *
 * OLED wiring:
 *   SCK  -> PA5
 *   MOSI -> PA7
 *   CS   -> PA4
 *   DC   -> PB0
 *   RST  -> PB1
 *
 * EC11 wiring:
 *   A    -> PB12
 *   B    -> PB13
 *   SW   -> PB14
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <drv_gpio.h>
#include <rthw.h>
#include <stdio.h>

#include "esp01s_at.h"
#include "todo_snapshot.h"
#include "oled_ui/oled_ui.h"
#include "oled_ui/oled_ui_astra.h"

#define DBG_TAG    "oled.demo"
#define DBG_LVL    DBG_INFO
#include <rtdbg.h>

#define OLED_SPI_BUS          "spi1"
#define OLED_DEV_NAME         "oled0"
#define OLED_CS_PIN           GET_PIN(A, 4)
#define OLED_DC_PIN           GET_PIN(B, 0)
#define OLED_RST_PIN          GET_PIN(B, 1)

#define ENC_PIN_A             GET_PIN(B, 12)
#define ENC_PIN_B             GET_PIN(B, 13)
#define ENC_PIN_SW            GET_PIN(B, 14)
#define BACK_KEY_PIN          GET_PIN(A, 0)

#define UI_REFRESH_MS         6
#define ANIM_DURATION_MS      650
#define SELECT_ANIM_MS        220
#define BRIGHTNESS_STEP       10

#define STR_MAIN_MENU         "\xE4\xB8\xBB\xE8\x8F\x9C\xE5\x8D\x95"
#define STR_TODO              "TODO"
#define STR_STATUS            "\xE7\x8A\xB6\xE6\x80\x81"
#define STR_WIFI              "WIFI"
#define STR_SETTINGS          "\xE8\xAE\xBE\xE7\xBD\xAE"
#define STR_ABOUT             "\xE5\x85\xB3\xE4\xBA\x8E"
#define STR_BRIGHTNESS        "\xE4\xBA\xAE\xE5\xBA\xA6"
#define STR_ANIM              "\xE5\x8A\xA8\xE7\x94\xBB"
#define STR_PC                "PC"
#define STR_VOLUME            "VOL"
#define STR_PC_BRIGHT         "BRT"
#define STR_BACK              "\xE8\xBF\x94\xE5\x9B\x9E"

#define WIFI_SCAN_MAX_APS     8U
#define ARRAY_SIZE(a)         ((rt_uint8_t)(sizeof(a) / sizeof((a)[0])))

typedef enum
{
    WIFI_JOB_NONE = 0,
    WIFI_JOB_SCAN,
    WIFI_JOB_CONNECT,
} wifi_job_type_t;

typedef enum
{
    ENC_EVT_NONE = 0,
    ENC_EVT_LEFT,
    ENC_EVT_RIGHT,
    ENC_EVT_PRESS,
} enc_event_t;

typedef struct
{
    rt_int32_t value_q8;
    rt_int32_t start_q8;
    rt_int32_t target_q8;
    rt_tick_t start_tick;
    rt_uint16_t duration_ms;
} ui_anim_value_t;

typedef struct
{
    rt_base_t pin_a;
    rt_base_t pin_b;
    rt_base_t pin_sw;
    rt_int8_t acc;
    rt_uint8_t prev_ab;
    volatile rt_int32_t step_delta;
    volatile rt_uint8_t press_pending;
    rt_tick_t ab_last_tick;
    rt_tick_t step_last_tick;
    rt_tick_t sw_last_tick;
    rt_tick_t ab_debounce_ticks;
    rt_tick_t step_lock_ticks;
    rt_tick_t sw_debounce_ticks;
} encoder_t;

typedef struct
{
    rt_base_t pin;
    rt_uint8_t prev_level;
    rt_tick_t last_tick;
    rt_tick_t debounce_ticks;
} back_key_t;

typedef struct
{
    encoder_t *enc;
    rt_uint8_t channel;
} encoder_irq_ctx_t;

typedef struct ui_page ui_page_t;
typedef struct ui_item ui_item_t;

typedef enum
{
    UI_ITEM_SUBPAGE = 0,
    UI_ITEM_ACTION,
    UI_ITEM_BACK,
} ui_item_kind_t;

typedef enum
{
    UI_PAGE_TILES = 0,
    UI_PAGE_LIST,
    UI_PAGE_CUSTOM,
} ui_page_style_t;

typedef const char *(*ui_item_value_fn_t)(char *buf, rt_size_t len);
typedef void (*ui_item_action_fn_t)(void);
typedef void (*ui_page_render_fn_t)(oled_ui_t *ui, ui_page_t *page);
typedef rt_bool_t (*ui_page_input_fn_t)(ui_page_t *page, rt_int32_t delta, rt_bool_t pressed);
typedef void (*ui_page_enter_fn_t)(ui_page_t *page);
typedef void (*ui_tile_icon_draw_fn_t)(oled_ui_t *ui, rt_uint8_t icon_id, int x, int y, int color);

struct ui_item
{
    const char *label;
    ui_item_kind_t kind;
    ui_page_t *target;
    ui_item_action_fn_t action;
    ui_item_value_fn_t value_fn;
    rt_uint8_t icon_id;
    const char *prefix;
};

struct ui_page
{
    const char *title;
    const char *tag;
    ui_page_style_t style;
    ui_page_t *parent;

    ui_item_t *items;
    rt_uint8_t item_count;
    rt_uint8_t selected;
    ui_anim_value_t select_anim;

    rt_uint8_t list_visible;
    rt_uint8_t list_item_h;
    rt_uint8_t list_x;
    rt_uint8_t list_y;
    rt_uint8_t list_w;

    ui_tile_icon_draw_fn_t tile_icon_draw;
    ui_page_render_fn_t render_custom;
    ui_page_input_fn_t input_custom;
    ui_page_enter_fn_t on_enter;
};

typedef struct
{
    oled_ui_t *oled;
    ui_page_t *current;
    rt_uint32_t tick;
} ui_runtime_t;

typedef struct
{
    rt_int32_t total;
    rt_uint32_t cw_count;
    rt_uint32_t ccw_count;
    rt_uint32_t press_count;
    rt_uint32_t fps;
    char last_dir;
    rt_uint8_t brightness;
    rt_uint8_t pc_volume;
    rt_uint8_t pc_brightness;
    rt_uint8_t anim_idx;
} demo_state_t;

typedef struct
{
    wifi_job_type_t type;
    char ssid[33];
    rt_uint8_t ecn;
} wifi_job_ctx_t;

typedef struct
{
    ui_page_t *old_page;
    ui_item_t *old_item;
} astra_item_user_t;

typedef struct
{
    ui_page_t *old_page;
    oled_ui_astra_page_t page;
    oled_ui_astra_item_t *items;
    astra_item_user_t *item_users;
} astra_page_bridge_t;

static void render_list_page(oled_ui_t *ui, ui_page_t *page);
static void render_tiles_page(oled_ui_t *ui, ui_page_t *page);

static void render_todo_page(oled_ui_t *ui, ui_page_t *page);
static void render_status_page(oled_ui_t *ui, ui_page_t *page);
static void render_wifi_page(oled_ui_t *ui, ui_page_t *page);
static void render_wifi_scan_page(oled_ui_t *ui, ui_page_t *page);
static void render_brightness_page(oled_ui_t *ui, ui_page_t *page);
static void render_anim_page(oled_ui_t *ui, ui_page_t *page);
static void render_pc_volume_page(oled_ui_t *ui, ui_page_t *page);
static void render_pc_brightness_page(oled_ui_t *ui, ui_page_t *page);
static void render_about_page(oled_ui_t *ui, ui_page_t *page);

static rt_bool_t input_todo_page(ui_page_t *page, rt_int32_t delta, rt_bool_t pressed);
static rt_bool_t input_status_page(ui_page_t *page, rt_int32_t delta, rt_bool_t pressed);
static rt_bool_t input_wifi_page(ui_page_t *page, rt_int32_t delta, rt_bool_t pressed);
static rt_bool_t input_wifi_scan_page(ui_page_t *page, rt_int32_t delta, rt_bool_t pressed);
static rt_bool_t input_brightness_page(ui_page_t *page, rt_int32_t delta, rt_bool_t pressed);
static rt_bool_t input_anim_page(ui_page_t *page, rt_int32_t delta, rt_bool_t pressed);
static rt_bool_t input_pc_volume_page(ui_page_t *page, rt_int32_t delta, rt_bool_t pressed);
static rt_bool_t input_pc_brightness_page(ui_page_t *page, rt_int32_t delta, rt_bool_t pressed);
static rt_bool_t input_about_page(ui_page_t *page, rt_int32_t delta, rt_bool_t pressed);

static void ui_refresh(void);
static void ui_switch_page(ui_page_t *next, oled_ui_anim_t anim, rt_uint32_t duration_ms);
static void ui_back(void);
static rt_err_t ui_astra_init_bridge(void);
static void ui_astra_sync_current(void);
static void page_shift_selected(ui_page_t *page, rt_uint8_t count, rt_int32_t delta);
static void start_wifi_job(wifi_job_type_t type, const char *ssid, rt_uint8_t ecn);
static rt_err_t back_key_init(back_key_t *key, rt_base_t pin);
static rt_uint8_t back_key_take_press(back_key_t *key);

static oled_ui_t g_oled;
static encoder_t g_encoder;
static back_key_t g_back_key;
static encoder_irq_ctx_t g_irq_ctx_a;
static encoder_irq_ctx_t g_irq_ctx_b;
static encoder_irq_ctx_t g_irq_ctx_sw;
static ui_runtime_t g_ui;
static demo_state_t g_demo =
{
    .total = 0,
    .cw_count = 0,
    .ccw_count = 0,
    .press_count = 0,
    .fps = 0,
    .last_dir = '-',
    .brightness = 60,
    .pc_volume = 50,
    .pc_brightness = 50,
    .anim_idx = 0,
};

static volatile rt_uint8_t g_wifi_job_busy;
static char g_pc_status[16] = "OFF";

static ui_page_t g_page_main;
static ui_page_t g_page_todo;
static ui_page_t g_page_status;
static ui_page_t g_page_wifi;
static ui_page_t g_page_wifi_scan;
static ui_page_t g_page_settings;
static ui_page_t g_page_brightness;
static ui_page_t g_page_anim;
static ui_page_t g_page_pc;
static ui_page_t g_page_pc_volume;
static ui_page_t g_page_pc_brightness;
static ui_page_t g_page_about;

static oled_ui_astra_launcher_t g_astra_launcher;
static oled_ui_astra_config_t g_astra_cfg;
static astra_page_bridge_t *g_astra_bridges;
static rt_uint8_t g_astra_bridge_count;
static rt_uint8_t g_astra_ready;

static const char *value_brightness(char *buf, rt_size_t len);
static const char *value_anim(char *buf, rt_size_t len);
static const char *value_pc_volume(char *buf, rt_size_t len);
static const char *value_pc_brightness(char *buf, rt_size_t len);

static ui_item_t g_main_items[] =
{
    { STR_TODO,     UI_ITEM_SUBPAGE, &g_page_todo,            RT_NULL, RT_NULL,             0U, RT_NULL },
    { STR_WIFI,     UI_ITEM_SUBPAGE, &g_page_wifi,            RT_NULL, RT_NULL,             2U, RT_NULL },
    { STR_SETTINGS, UI_ITEM_SUBPAGE, &g_page_settings,        RT_NULL, RT_NULL,             3U, RT_NULL },
};

static ui_item_t g_settings_items[] =
{
    { STR_BRIGHTNESS, UI_ITEM_SUBPAGE, &g_page_brightness,    RT_NULL, value_brightness,    0U, RT_NULL },
};

static ui_item_t g_pc_items[] =
{
    { STR_VOLUME,    UI_ITEM_SUBPAGE, &g_page_pc_volume,      RT_NULL, value_pc_volume,      0U, RT_NULL },
    { STR_PC_BRIGHT, UI_ITEM_SUBPAGE, &g_page_pc_brightness,  RT_NULL, value_pc_brightness,  0U, RT_NULL },
    { STR_BACK,      UI_ITEM_BACK,    &g_page_settings,       RT_NULL, RT_NULL,              0U, RT_NULL },
};

static ui_page_t *g_all_pages[] =
{
    &g_page_main,
    &g_page_todo,
    &g_page_wifi,
    &g_page_wifi_scan,
    &g_page_settings,
    &g_page_brightness,
};

static rt_uint8_t brightness_to_contrast(rt_uint8_t brightness)
{
    return (rt_uint8_t)((brightness * 255U) / 100U);
}

static void apply_brightness(void)
{
    oled_ui_set_contrast(&g_oled, brightness_to_contrast(g_demo.brightness));
}

static void pc_status_set(const char *status)
{
    if (status == RT_NULL)
    {
        g_pc_status[0] = '\0';
        return;
    }

    rt_strncpy(g_pc_status, status, sizeof(g_pc_status) - 1);
    g_pc_status[sizeof(g_pc_status) - 1] = '\0';
}

static const char *anim_name(rt_uint8_t idx)
{
    static const char *names[] =
    {
        "LEFT",
        "UP",
        "RIGHT",
        "DOWN",
    };

    return names[idx % 4U];
}

static oled_ui_anim_t anim_forward(void)
{
    static const oled_ui_anim_t table[] =
    {
        OLED_UI_ANIM_PUSH_LEFT,
        OLED_UI_ANIM_PUSH_UP,
        OLED_UI_ANIM_PUSH_RIGHT,
        OLED_UI_ANIM_PUSH_DOWN,
    };

    return table[g_demo.anim_idx % 4U];
}

static oled_ui_anim_t anim_backward(void)
{
    switch (anim_forward())
    {
    case OLED_UI_ANIM_PUSH_LEFT:
        return OLED_UI_ANIM_PUSH_RIGHT;
    case OLED_UI_ANIM_PUSH_RIGHT:
        return OLED_UI_ANIM_PUSH_LEFT;
    case OLED_UI_ANIM_PUSH_UP:
        return OLED_UI_ANIM_PUSH_DOWN;
    case OLED_UI_ANIM_PUSH_DOWN:
    default:
        return OLED_UI_ANIM_PUSH_UP;
    }
}

static rt_uint32_t ui_bezier_axis(rt_uint32_t t, rt_uint32_t p1, rt_uint32_t p2)
{
    rt_uint32_t inv = 1000U - t;
    rt_uint64_t num;
    rt_uint64_t den = 1000ULL * 1000ULL * 1000ULL;

    num = 3ULL * inv * inv * t * p1;
    num += 3ULL * inv * t * t * p2;
    num += 1ULL * t * t * t * 1000ULL;

    return (rt_uint32_t)((num + (den / 2ULL)) / den);
}

static rt_uint32_t ui_bezier_ease(rt_uint32_t progress)
{
    rt_uint32_t low = 0U;
    rt_uint32_t high = 1000U;
    rt_uint32_t mid = 0U;
    rt_uint32_t i;

    if (progress >= 1000U)
    {
        return 1000U;
    }

    for (i = 0U; i < 12U; i++)
    {
        rt_uint32_t x;

        mid = (low + high) / 2U;
        x = ui_bezier_axis(mid, 250U, 250U);
        if (x < progress)
        {
            low = mid;
        }
        else
        {
            high = mid;
        }
    }

    return ui_bezier_axis((low + high) / 2U, 100U, 1000U);
}

static void ui_anim_init(ui_anim_value_t *anim, rt_int32_t value_q8)
{
    if (anim == RT_NULL)
    {
        return;
    }

    anim->value_q8 = value_q8;
    anim->start_q8 = value_q8;
    anim->target_q8 = value_q8;
    anim->start_tick = rt_tick_get();
    anim->duration_ms = SELECT_ANIM_MS;
}

static void ui_anim_set_target(ui_anim_value_t *anim, rt_int32_t target_q8, rt_uint16_t duration_ms)
{
    if (anim == RT_NULL)
    {
        return;
    }

    if ((anim->target_q8 == target_q8) && (anim->value_q8 == target_q8))
    {
        return;
    }

    anim->start_q8 = anim->value_q8;
    anim->target_q8 = target_q8;
    anim->start_tick = rt_tick_get();
    anim->duration_ms = (duration_ms == 0U) ? 1U : duration_ms;
}

static void ui_anim_update(ui_anim_value_t *anim)
{
    rt_uint32_t elapsed_ms;
    rt_uint32_t progress;
    rt_uint32_t eased;
    rt_int32_t delta;

    if (anim == RT_NULL)
    {
        return;
    }

    if (anim->value_q8 == anim->target_q8)
    {
        return;
    }

    elapsed_ms = (rt_uint32_t)(((rt_tick_get() - anim->start_tick) * 1000U) / RT_TICK_PER_SECOND);
    if (elapsed_ms >= anim->duration_ms)
    {
        anim->value_q8 = anim->target_q8;
        return;
    }

    progress = (elapsed_ms * 1000U) / anim->duration_ms;
    eased = ui_bezier_ease(progress);
    delta = anim->target_q8 - anim->start_q8;
    anim->value_q8 = anim->start_q8 + (rt_int32_t)(((rt_int64_t)delta * eased) / 1000);
}

static void draw_page_head(oled_ui_t *ui, const char *label)
{
    oled_ui_draw_text(ui, 2, 0, label, 1, 1);
    oled_ui_draw_line(ui, 0, 9, OLED_UI_WIDTH - 1, 9, 1);
}

static rt_uint8_t pulse_level(rt_uint32_t tick)
{
    rt_uint32_t phase = (tick / 4U) % 4U;

    return (rt_uint8_t)((phase == 1U || phase == 2U) ? 1U : 0U);
}

static void draw_selected_fill(oled_ui_t *ui, int x, int y, int w, int h, int radius, rt_uint8_t expand)
{
    int fill_x = x + 1 - (int)expand;
    int fill_y = y + 1 - (int)expand;
    int fill_w = w - 2 + (int)expand * 2;
    int fill_h = h - 2 + (int)expand * 2;
    int fill_radius = radius + (int)expand;

    oled_ui_fill_round_rect(ui, fill_x, fill_y, fill_w, fill_h, fill_radius, 1);
}

static void draw_ascii_text_center(oled_ui_t *ui, int x, int y, int w, const char *text, int color)
{
    int text_w;
    int draw_x;

    if (text == RT_NULL)
    {
        return;
    }

    text_w = (int)rt_strlen(text) * 6;
    draw_x = x + (w - text_w) / 2;
    if (draw_x < x)
    {
        draw_x = x;
    }

    oled_ui_draw_text(ui, draw_x, y, text, 1, color);
}

static void draw_ascii_text_center_scale(oled_ui_t *ui, int x, int y, int w, const char *text, int scale, int color)
{
    int text_w;
    int draw_x;

    if ((text == RT_NULL) || (scale <= 0))
    {
        return;
    }

    text_w = (int)rt_strlen(text) * 6 * scale;
    draw_x = x + (w - text_w) / 2;
    if (draw_x < x)
    {
        draw_x = x;
    }

    oled_ui_draw_text(ui, draw_x, y, text, scale, color);
}

static void draw_utf8_text_center(oled_ui_t *ui, int x, int y, int w, const char *text, int color)
{
    int text_w;
    int draw_x;

    if (text == RT_NULL)
    {
        return;
    }

    text_w = oled_ui_measure_utf8_width(text, 1);
    draw_x = x + (w - text_w) / 2;
    if (draw_x < x)
    {
        draw_x = x;
    }

    oled_ui_draw_utf8_text(ui, draw_x, y, text, 1, color);
}

static void draw_ref_sidebar(oled_ui_t *ui, const char *tag)
{
    oled_ui_draw_line(ui, 92, 10, 92, 63, 1);
    if ((tag != RT_NULL) && (tag[0] != '\0'))
    {
        oled_ui_draw_text(ui, 96, 14, tag, 1, 1);
    }
}

static void draw_ref_list_shell(oled_ui_t *ui, const char *tag, const char *title)
{
    draw_ref_sidebar(ui, tag);
    oled_ui_draw_utf8_marquee(ui, 2, 12, 88, title, 1, 1, g_ui.tick * 3U);
}

static void draw_ascii_button(oled_ui_t *ui, int x, int y, int w, int h, const char *text, rt_uint8_t selected)
{
    RT_UNUSED(h);
    draw_ascii_text_center(ui, x, y + 1, w, text, 1);
    if (selected)
    {
        oled_ui_draw_line(ui, x + 2, y + 9, x + w - 3, y + 9, 1);
    }
}

static void draw_button_pair(oled_ui_t *ui,
                             int x0,
                             int y,
                             int w0,
                             const char *text0,
                             int x1,
                             int w1,
                             const char *text1,
                             rt_int32_t anim_q8,
                             rt_uint8_t selected)
{
    int h = 10;
    int hx = x0 + (int)(((x1 - x0) * anim_q8) / 256);
    int hw = w0 + (int)(((w1 - w0) * anim_q8) / 256);

    draw_ascii_text_center(ui, x0, y, w0, text0, 1);
    draw_ascii_text_center(ui, x1, y, w1, text1, 1);
    oled_ui_draw_line(ui, hx + 2, y + h, hx + hw - 3, y + h, 1);
    if (selected == 0U)
    {
        oled_ui_draw_line(ui, x0 + 2, y + h + 1, x0 + w0 - 3, y + h + 1, 1);
    }
    else
    {
        oled_ui_draw_line(ui, x1 + 2, y + h + 1, x1 + w1 - 3, y + h + 1, 1);
    }
}

static void draw_tile_icon(oled_ui_t *ui, rt_uint8_t icon_id, int x, int y, int color)
{
    switch (icon_id)
    {
    case 0U:
        oled_ui_draw_round_rect(ui, x + 2, y + 2, 18, 14, 3, color);
        oled_ui_draw_line(ui, x + 5, y + 7, x + 17, y + 7, color);
        oled_ui_draw_line(ui, x + 6, y + 10, x + 16, y + 10, color);
        oled_ui_draw_line(ui, x + 11, y + 16, x + 11, y + 24, color);
        break;

    case 1U:
        oled_ui_draw_line(ui, x + 10, y + 2, x + 2, y + 10, color);
        oled_ui_draw_line(ui, x + 10, y + 2, x + 18, y + 10, color);
        oled_ui_draw_line(ui, x + 2, y + 10, x + 18, y + 10, color);
        oled_ui_draw_line(ui, x + 5, y + 10, x + 3, y + 20, color);
        oled_ui_draw_line(ui, x + 15, y + 10, x + 17, y + 20, color);
        oled_ui_draw_line(ui, x + 7, y + 18, x + 13, y + 18, color);
        break;

    case 2U:
        oled_ui_draw_round_rect(ui, x + 2, y + 3, 16, 10, 3, color);
        oled_ui_draw_line(ui, x + 6, y + 15, x + 14, y + 15, color);
        oled_ui_draw_line(ui, x + 4, y + 19, x + 16, y + 19, color);
        break;

    case 3U:
        oled_ui_draw_round_rect(ui, x + 4, y + 2, 12, 20, 5, color);
        oled_ui_draw_line(ui, x + 10, y + 7, x + 10, y + 13, color);
        oled_ui_draw_pixel(ui, x + 10, y + 17, color);
        break;

    case 4U:
    default:
        oled_ui_draw_round_rect(ui, x + 5, y + 2, 10, 20, 5, color);
        oled_ui_draw_text(ui, x + 8, y + 7, "i", 1, color);
        oled_ui_draw_pixel(ui, x + 10, y + 17, color);
        break;
    }
}

static void page_set_selected(ui_page_t *page, rt_uint8_t selected)
{
    if (page == RT_NULL)
    {
        return;
    }

    page->selected = selected;
    ui_anim_set_target(&page->select_anim, ((rt_int32_t)selected) << 8, SELECT_ANIM_MS);
}

static void page_shift_selected(ui_page_t *page, rt_uint8_t count, rt_int32_t delta)
{
    rt_int32_t index;

    if ((page == RT_NULL) || (count == 0U) || (delta == 0))
    {
        return;
    }

    index = (rt_int32_t)page->selected + delta;
    while (index < 0)
    {
        index += count;
    }
    while (index >= count)
    {
        index -= count;
    }

    page_set_selected(page, (rt_uint8_t)index);
}

static const char *item_prefix(const ui_item_t *item)
{
    if (item == RT_NULL)
    {
        return " ";
    }

    if ((item->prefix != RT_NULL) && (item->prefix[0] != '\0'))
    {
        return item->prefix;
    }

    if (item->kind == UI_ITEM_BACK)
    {
        return "<";
    }
    if (item->kind == UI_ITEM_SUBPAGE)
    {
        return ">";
    }
    if (item->kind == UI_ITEM_ACTION)
    {
        return "*";
    }

    return " ";
}

static void render_list_page(oled_ui_t *ui, ui_page_t *page)
{
    rt_uint8_t count = page->item_count;
    rt_int32_t anim_q8;
    rt_int32_t max_top_q8;
    rt_int32_t top_anim_q8;
    int y0;
    int x;
    int w;
    int item_h;
    rt_uint8_t visible;
    rt_uint8_t i;

    draw_page_head(ui, page->tag);
    draw_ref_list_shell(ui, page->tag, page->title);

    if ((count == 0U) || (page->items == RT_NULL))
    {
        oled_ui_draw_text(ui, 34, 34, "EMPTY", 1, 1);
        return;
    }

    if (page->selected >= count)
    {
        page->selected = (rt_uint8_t)(count - 1U);
        ui_anim_init(&page->select_anim, ((rt_int32_t)page->selected) << 8);
    }

    x = (page->list_x > 0U) ? page->list_x : 30;
    y0 = (page->list_y > 0U) ? page->list_y : 20;
    w = (page->list_w > 0U) ? page->list_w : 96;
    item_h = (page->list_item_h > 0U) ? page->list_item_h : 11;
    visible = (page->list_visible > 0U) ? page->list_visible : 4;

    anim_q8 = page->select_anim.value_q8;
    max_top_q8 = (count > visible) ? (((rt_int32_t)count - visible) << 8) : 0;
    top_anim_q8 = anim_q8 - (((rt_int32_t)visible - 1) << 7);
    if (top_anim_q8 < 0)
    {
        top_anim_q8 = 0;
    }
    if (top_anim_q8 > max_top_q8)
    {
        top_anim_q8 = max_top_q8;
    }

    {
        int hy = y0 + (int)(((anim_q8 - top_anim_q8) * item_h) / 256);
        oled_ui_fill_rect(ui, x, hy, w, item_h, 1);
    }

    for (i = 0U; i < count; i++)
    {
        int y = y0 + (int)(((((rt_int32_t)i) << 8) - top_anim_q8) * item_h / 256);
        rt_int32_t diff = ((((rt_int32_t)i) << 8) - anim_q8);
        rt_uint8_t focused = (rt_uint8_t)((diff > -128) && (diff < 128));
        int text_color = focused ? 0 : 1;
        const ui_item_t *item = &page->items[i];
        const char *prefix = item_prefix(item);
        char value_buf[20];
        const char *value_text = RT_NULL;
        int value_x = x + w - 3;
        int label_x = x + 6;
        int label_w;

        if ((y < (y0 - item_h)) || (y > (y0 + item_h * visible)))
        {
            continue;
        }

        if (item->value_fn != RT_NULL)
        {
            value_text = item->value_fn(value_buf, sizeof(value_buf));
        }

        if ((value_text != RT_NULL) && (value_text[0] != '\0'))
        {
            int value_w = oled_ui_measure_utf8_width(value_text, 1);
            value_x = x + w - value_w - 3;
            oled_ui_draw_utf8_text(ui, value_x, y + 1, value_text, 1, text_color);
        }

        if ((prefix != RT_NULL) && (prefix[0] != '\0'))
        {
            oled_ui_draw_text(ui, x + 2, y + 1, prefix, 1, text_color);
            label_x = x + 12;
        }

        label_w = value_x - label_x - 2;
        if (label_w < 10)
        {
            label_w = 10;
        }

        oled_ui_draw_utf8_marquee(ui,
                                  label_x,
                                  y + 1,
                                  label_w,
                                  item->label,
                                  1,
                                  text_color,
                                  focused ? (g_ui.tick * 3U) : 0U);
    }
}

static void render_tiles_page(oled_ui_t *ui, ui_page_t *page)
{
    rt_uint8_t count = page->item_count;
    rt_int32_t anim_q8;
    rt_uint8_t i;
    const int tile_w = 36;
    const int tile_h = 26;
    const int tile_y = 14;
    const int tile_pitch = 44;
    const int center_x = (OLED_UI_WIDTH - tile_w) / 2;

    if ((count == 0U) || (page->items == RT_NULL))
    {
        return;
    }

    if (page->selected >= count)
    {
        page->selected = (rt_uint8_t)(count - 1U);
        ui_anim_init(&page->select_anim, ((rt_int32_t)page->selected) << 8);
    }

    anim_q8 = page->select_anim.value_q8;

    for (i = 0U; i < count; i++)
    {
        rt_int32_t diff_q8 = (((rt_int32_t)i) << 8) - anim_q8;
        int x = center_x + (int)((diff_q8 * tile_pitch) / 256);
        rt_uint8_t focused = (rt_uint8_t)((diff_q8 > -128) && (diff_q8 < 128));
        int icon_color = 1;

        if ((x < -tile_w) || (x > OLED_UI_WIDTH))
        {
            continue;
        }

        if (page->tile_icon_draw != RT_NULL)
        {
            page->tile_icon_draw(ui, page->items[i].icon_id, x + 7, tile_y + 1, icon_color);
        }

        if (focused)
        {
            oled_ui_draw_line(ui, x + 6, tile_y + tile_h + 2, x + tile_w - 7, tile_y + tile_h + 2, 1);
            if (pulse_level(g_ui.tick))
            {
                oled_ui_draw_line(ui, x + 10, tile_y + tile_h + 3, x + tile_w - 11, tile_y + tile_h + 3, 1);
            }
        }
    }

    draw_utf8_text_center(ui, 0, 48, OLED_UI_WIDTH, page->items[page->selected].label, 1);

    {
        int track_x = 12;
        int track_y = 58;
        int track_w = 104;
        int knob_w = 16;
        int knob_x = track_x;

        oled_ui_draw_line(ui, track_x, track_y + 2, track_x + track_w, track_y + 2, 1);
        if (count > 1U)
        {
            knob_x += (int)((anim_q8 * (track_w - knob_w)) / (((rt_int32_t)count - 1) << 8));
        }
        oled_ui_fill_round_rect(ui, knob_x, track_y, knob_w, 5, 2, 1);
    }
}

static oled_ui_astra_page_type_t astra_map_page_type(ui_page_style_t style)
{
    if (style == UI_PAGE_TILES)
    {
        return OLED_UI_ASTRA_PAGE_TILE;
    }
    if (style == UI_PAGE_LIST)
    {
        return OLED_UI_ASTRA_PAGE_LIST;
    }
    return OLED_UI_ASTRA_PAGE_CUSTOM;
}

static astra_page_bridge_t *ui_astra_bridge_from_old(ui_page_t *page)
{
    rt_uint8_t i;

    if ((page == RT_NULL) || (g_astra_bridges == RT_NULL))
    {
        return RT_NULL;
    }

    for (i = 0U; i < g_astra_bridge_count; i++)
    {
        if (g_astra_bridges[i].old_page == page)
        {
            return &g_astra_bridges[i];
        }
    }

    return RT_NULL;
}

static astra_page_bridge_t *ui_astra_bridge_from_page(oled_ui_astra_page_t *page)
{
    rt_uint8_t i;

    if ((page == RT_NULL) || (g_astra_bridges == RT_NULL))
    {
        return RT_NULL;
    }

    for (i = 0U; i < g_astra_bridge_count; i++)
    {
        if (&g_astra_bridges[i].page == page)
        {
            return &g_astra_bridges[i];
        }
    }

    return RT_NULL;
}

static const char *astra_old_value_cb(oled_ui_astra_launcher_t *launcher,
                                      oled_ui_astra_item_t *item,
                                      char *buf,
                                      rt_size_t len,
                                      void *user)
{
    ui_item_t *old_item = (ui_item_t *)user;

    RT_UNUSED(launcher);
    RT_UNUSED(item);

    if ((old_item == RT_NULL) || (old_item->value_fn == RT_NULL))
    {
        return RT_NULL;
    }

    return old_item->value_fn(buf, len);
}

static void astra_old_action_cb(oled_ui_astra_launcher_t *launcher,
                                oled_ui_astra_item_t *item,
                                void *user)
{
    ui_item_t *old_item = (ui_item_t *)user;

    RT_UNUSED(launcher);
    RT_UNUSED(item);

    if ((old_item != RT_NULL) && (old_item->action != RT_NULL))
    {
        old_item->action();
    }
}

static void astra_old_icon_cb(oled_ui_t *ui,
                              oled_ui_astra_launcher_t *launcher,
                              oled_ui_astra_item_t *item,
                              int x,
                              int y,
                              int selected,
                              void *user)
{
    astra_item_user_t *ctx = (astra_item_user_t *)user;

    RT_UNUSED(launcher);
    RT_UNUSED(item);
    RT_UNUSED(selected);

    if ((ctx == RT_NULL) || (ctx->old_page == RT_NULL) || (ctx->old_item == RT_NULL))
    {
        return;
    }

    if (ctx->old_page->tile_icon_draw != RT_NULL)
    {
        ctx->old_page->tile_icon_draw(ui, ctx->old_item->icon_id, x + 7, y + 1, 1);
    }
}

static void astra_old_page_render_cb(oled_ui_t *ui,
                                     oled_ui_astra_launcher_t *launcher,
                                     oled_ui_astra_page_t *page,
                                     void *user)
{
    ui_page_t *old_page = (ui_page_t *)user;

    RT_UNUSED(launcher);

    if (old_page == RT_NULL)
    {
        return;
    }

    old_page->selected = page->selected;
    g_ui.current = old_page;

    if (old_page->style == UI_PAGE_TILES)
    {
        render_tiles_page(ui, old_page);
    }
    else if (old_page->style == UI_PAGE_LIST)
    {
        render_list_page(ui, old_page);
    }
    else if (old_page->render_custom != RT_NULL)
    {
        old_page->render_custom(ui, old_page);
    }
}

static rt_bool_t astra_old_page_input_cb(oled_ui_astra_launcher_t *launcher,
                                         oled_ui_astra_page_t *page,
                                         rt_int32_t rotate_delta,
                                         rt_bool_t pressed,
                                         void *user)
{
    ui_page_t *old_page = (ui_page_t *)user;
    rt_bool_t handled;

    RT_UNUSED(launcher);

    if ((old_page == RT_NULL) || (old_page->input_custom == RT_NULL))
    {
        return RT_FALSE;
    }

    old_page->selected = page->selected;
    handled = old_page->input_custom(old_page, rotate_delta, pressed);
    page->selected = old_page->selected;
    return handled;
}

static void astra_old_page_enter_cb(oled_ui_astra_launcher_t *launcher,
                                    oled_ui_astra_page_t *page,
                                    void *user)
{
    ui_page_t *old_page = (ui_page_t *)user;

    RT_UNUSED(launcher);
    RT_UNUSED(page);

    if ((old_page != RT_NULL) && (old_page->on_enter != RT_NULL))
    {
        old_page->on_enter(old_page);
    }
}

static void ui_astra_sync_current(void)
{
    astra_page_bridge_t *bridge;

    if (g_astra_ready == 0U)
    {
        return;
    }

    bridge = ui_astra_bridge_from_page(g_astra_launcher.current);
    if ((bridge != RT_NULL) && (bridge->old_page != RT_NULL))
    {
        bridge->old_page->selected = bridge->page.selected;
        g_ui.current = bridge->old_page;
    }
}

static rt_err_t ui_astra_init_bridge(void)
{
    rt_uint8_t i;

    g_astra_ready = 0U;
    if (g_astra_bridges != RT_NULL)
    {
        g_astra_ready = 1U;
        return RT_EOK;
    }

    g_astra_bridge_count = ARRAY_SIZE(g_all_pages);
    g_astra_bridges = (astra_page_bridge_t *)rt_calloc(g_astra_bridge_count, sizeof(*g_astra_bridges));
    if (g_astra_bridges == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    for (i = 0U; i < g_astra_bridge_count; i++)
    {
        astra_page_bridge_t *bridge = &g_astra_bridges[i];
        ui_page_t *old_page = g_all_pages[i];
        rt_uint8_t j;

        bridge->old_page = old_page;
        oled_ui_astra_page_init(&bridge->page, old_page->title, old_page->tag, astra_map_page_type(old_page->style));
        bridge->page.user = old_page;
        bridge->page.selected = old_page->selected;

        if (old_page->style == UI_PAGE_LIST)
        {
            bridge->page.content_x = old_page->list_x;
            bridge->page.content_y = old_page->list_y;
            bridge->page.content_w = old_page->list_w;
            bridge->page.list_line_height = old_page->list_item_h;
            bridge->page.list_visible_count = old_page->list_visible;
        }
        else if (old_page->style == UI_PAGE_TILES)
        {
            bridge->page.content_y = 14U;
        }

        if ((old_page->style == UI_PAGE_CUSTOM) || (old_page->render_custom != RT_NULL))
        {
            bridge->page.render = astra_old_page_render_cb;
        }
        if (old_page->input_custom != RT_NULL)
        {
            bridge->page.input = astra_old_page_input_cb;
        }
        if (old_page->on_enter != RT_NULL)
        {
            bridge->page.on_enter = astra_old_page_enter_cb;
        }

        bridge->page.item_count = old_page->item_count;
        if ((old_page->item_count > 0U) && (old_page->items != RT_NULL))
        {
            bridge->items = (oled_ui_astra_item_t *)rt_calloc(old_page->item_count, sizeof(*bridge->items));
            bridge->item_users = (astra_item_user_t *)rt_calloc(old_page->item_count, sizeof(*bridge->item_users));
            if ((bridge->items == RT_NULL) || (bridge->item_users == RT_NULL))
            {
                return -RT_ENOMEM;
            }

            bridge->page.items = bridge->items;
            for (j = 0U; j < old_page->item_count; j++)
            {
                oled_ui_astra_item_t *new_item = &bridge->items[j];
                ui_item_t *old_item = &old_page->items[j];

                bridge->item_users[j].old_page = old_page;
                bridge->item_users[j].old_item = old_item;

                new_item->title = old_item->label;
                if (old_item->kind == UI_ITEM_SUBPAGE)
                {
                    new_item->type = OLED_UI_ASTRA_ITEM_MENU;
                }
                else if (old_item->kind == UI_ITEM_BACK)
                {
                    new_item->type = OLED_UI_ASTRA_ITEM_BACK;
                }
                else
                {
                    new_item->type = OLED_UI_ASTRA_ITEM_ACTION;
                }

                if (old_item->action != RT_NULL)
                {
                    new_item->action = astra_old_action_cb;
                    new_item->action_user = old_item;
                }
                if (old_item->value_fn != RT_NULL)
                {
                    new_item->value = astra_old_value_cb;
                    new_item->value_user = old_item;
                }
                if (old_page->style == UI_PAGE_TILES)
                {
                    new_item->icon = astra_old_icon_cb;
                    new_item->icon_user = &bridge->item_users[j];
                }
            }
        }
    }

    for (i = 0U; i < g_astra_bridge_count; i++)
    {
        astra_page_bridge_t *bridge = &g_astra_bridges[i];
        ui_page_t *old_page = bridge->old_page;
        rt_uint8_t j;

        if (old_page->parent != RT_NULL)
        {
            astra_page_bridge_t *parent_bridge = ui_astra_bridge_from_old(old_page->parent);
            if (parent_bridge != RT_NULL)
            {
                bridge->page.parent = &parent_bridge->page;
            }
        }

        for (j = 0U; j < bridge->page.item_count; j++)
        {
            ui_item_t *old_item = &old_page->items[j];
            if (old_item->target != RT_NULL)
            {
                astra_page_bridge_t *target_bridge = ui_astra_bridge_from_old(old_item->target);
                if (target_bridge != RT_NULL)
                {
                    bridge->items[j].target = &target_bridge->page;
                }
            }
        }
    }

    oled_ui_astra_config_default(&g_astra_cfg);
    g_astra_cfg.list_visible_count = 3U;
    g_astra_cfg.list_line_height = 16U;
    g_astra_cfg.list_text_margin = 3U;
    g_astra_cfg.tile_pic_w = 36U;
    g_astra_cfg.tile_pic_h = 26U;
    g_astra_cfg.tile_pic_top = 14U;
    g_astra_cfg.tile_text_bottom = 48U;
    g_astra_cfg.tile_bar_height = 5U;
    g_astra_cfg.list_selector_radius = 3U;
    g_astra_cfg.light_mode = 0U;

    {
        astra_page_bridge_t *root_bridge = ui_astra_bridge_from_old(&g_page_main);
        if (root_bridge == RT_NULL)
        {
            return -RT_ERROR;
        }

        oled_ui_astra_launcher_init(&g_astra_launcher, &g_oled, &root_bridge->page, &g_astra_cfg);
        g_astra_launcher.transition_ms = ANIM_DURATION_MS;
    }

    g_astra_ready = 1U;
    ui_astra_sync_current();
    return RT_EOK;
}

static void render_page_dispatch(oled_ui_t *ui, void *user)
{
    ui_page_t *page = (ui_page_t *)user;

    if (page == RT_NULL)
    {
        return;
    }

    if (page->style == UI_PAGE_TILES)
    {
        render_tiles_page(ui, page);
    }
    else if (page->style == UI_PAGE_LIST)
    {
        render_list_page(ui, page);
    }
    else if (page->render_custom != RT_NULL)
    {
        page->render_custom(ui, page);
    }
}

static void ui_refresh(void)
{
    oled_ui_page_t page;

    if (g_astra_ready != 0U)
    {
        oled_ui_astra_launcher_update(&g_astra_launcher);
        ui_astra_sync_current();
        return;
    }

    if ((g_ui.current == RT_NULL) || (g_ui.oled == RT_NULL))
    {
        return;
    }

    page.render = render_page_dispatch;
    page.user = g_ui.current;
    oled_ui_show_page(g_ui.oled, RT_NULL, &page, OLED_UI_ANIM_NONE, 0);
}

static void ui_switch_page(ui_page_t *next, oled_ui_anim_t anim, rt_uint32_t duration_ms)
{
    oled_ui_page_t from_page;
    oled_ui_page_t to_page;

    if ((g_astra_ready != 0U) && (next != RT_NULL))
    {
        astra_page_bridge_t *next_bridge = ui_astra_bridge_from_old(next);

        if (next_bridge != RT_NULL)
        {
            oled_ui_astra_launcher_switch(&g_astra_launcher,
                                          &next_bridge->page,
                                          anim,
                                          duration_ms);
            ui_astra_sync_current();
            return;
        }
    }

    if ((next == RT_NULL) || (g_ui.oled == RT_NULL))
    {
        return;
    }

    if (next->on_enter != RT_NULL)
    {
        next->on_enter(next);
    }

    to_page.render = render_page_dispatch;
    to_page.user = next;

    if (g_ui.current == RT_NULL)
    {
        oled_ui_show_page(g_ui.oled, RT_NULL, &to_page, OLED_UI_ANIM_NONE, 0);
        g_ui.current = next;
        return;
    }

    if ((g_ui.current == next) || (anim == OLED_UI_ANIM_NONE) || (duration_ms == 0U))
    {
        oled_ui_show_page(g_ui.oled, RT_NULL, &to_page, OLED_UI_ANIM_NONE, 0);
        g_ui.current = next;
        return;
    }

    from_page.render = render_page_dispatch;
    from_page.user = g_ui.current;
    oled_ui_show_page(g_ui.oled, &from_page, &to_page, anim, duration_ms);
    g_ui.current = next;
}

static void ui_back(void)
{
    if (g_astra_ready != 0U)
    {
        oled_ui_astra_launcher_back(&g_astra_launcher);
        ui_astra_sync_current();
        return;
    }

    if ((g_ui.current != RT_NULL) && (g_ui.current->parent != RT_NULL))
    {
        ui_switch_page(g_ui.current->parent, anim_backward(), ANIM_DURATION_MS);
    }
}

static void ui_activate_item(const ui_item_t *item)
{
    if (item == RT_NULL)
    {
        return;
    }

    if (item->kind == UI_ITEM_SUBPAGE)
    {
        if (item->target != RT_NULL)
        {
            ui_switch_page(item->target, anim_forward(), ANIM_DURATION_MS);
        }
        return;
    }

    if (item->kind == UI_ITEM_BACK)
    {
        if (item->target != RT_NULL)
        {
            ui_switch_page(item->target, anim_backward(), ANIM_DURATION_MS);
        }
        else
        {
            ui_back();
        }
        return;
    }

    if ((item->kind == UI_ITEM_ACTION) && (item->action != RT_NULL))
    {
        item->action();
    }
}

static void ui_handle_rotate(rt_int32_t delta)
{
    ui_page_t *page = g_ui.current;
    ui_page_t *before = g_ui.current;

    if (g_astra_ready != 0U)
    {
        oled_ui_astra_launcher_rotate(&g_astra_launcher, delta);
        ui_astra_sync_current();
        ui_refresh();
        return;
    }

    if ((page == RT_NULL) || (delta == 0))
    {
        return;
    }

    if ((page->input_custom != RT_NULL) && page->input_custom(page, delta, RT_FALSE))
    {
        if (g_ui.current == before)
        {
            ui_refresh();
        }
        return;
    }

    if ((page->items == RT_NULL) || (page->item_count == 0U))
    {
        return;
    }

    page_shift_selected(page, page->item_count, delta);
    ui_refresh();
}

static void ui_handle_press(void)
{
    ui_page_t *page = g_ui.current;
    ui_page_t *before = g_ui.current;

    if (g_astra_ready != 0U)
    {
        oled_ui_astra_launcher_press(&g_astra_launcher);
        ui_astra_sync_current();
        ui_refresh();
        return;
    }

    if (page == RT_NULL)
    {
        return;
    }

    if ((page->input_custom != RT_NULL) && page->input_custom(page, 0, RT_TRUE))
    {
        if (g_ui.current == before)
        {
            ui_refresh();
        }
        return;
    }

    if ((page->items == RT_NULL) || (page->item_count == 0U) || (page->selected >= page->item_count))
    {
        return;
    }

    ui_activate_item(&page->items[page->selected]);
    if (g_ui.current == before)
    {
        ui_refresh();
    }
}

static void update_ui_animation_states(void)
{
    rt_size_t i;

    for (i = 0; i < sizeof(g_all_pages) / sizeof(g_all_pages[0]); i++)
    {
        ui_anim_update(&g_all_pages[i]->select_anim);
    }
}

static const char *value_brightness(char *buf, rt_size_t len)
{
    rt_snprintf(buf, len, "%u%%", (unsigned int)g_demo.brightness);
    return buf;
}

static const char *value_anim(char *buf, rt_size_t len)
{
    rt_snprintf(buf, len, "%s", anim_name(g_demo.anim_idx));
    return buf;
}

static const char *value_pc_volume(char *buf, rt_size_t len)
{
    rt_snprintf(buf, len, "%u%%", (unsigned int)g_demo.pc_volume);
    return buf;
}

static const char *value_pc_brightness(char *buf, rt_size_t len)
{
    rt_snprintf(buf, len, "%u%%", (unsigned int)g_demo.pc_brightness);
    return buf;
}

static void render_todo_page(oled_ui_t *ui, ui_page_t *page)
{
    todo_snapshot_t snapshot;
    rt_uint8_t count;
    rt_int32_t anim_q8;
    rt_int32_t max_top_q8;
    rt_int32_t top_anim_q8;
    rt_uint8_t i;
    char line[20];
    const int x = 2;
    const int y0 = 18;
    const int w = 88;
    const int item_h = 14;
    const rt_uint8_t visible = 3U;

    todo_snapshot_get(&snapshot);
    count = (rt_uint8_t)snapshot.count;

    if (count == 0U)
    {
        page->selected = 0U;
        ui_anim_init(&page->select_anim, 0);
    }
    else if (page->selected >= count)
    {
        page_set_selected(page, (rt_uint8_t)(count - 1U));
    }

    draw_page_head(ui, "TODO");
    draw_ref_list_shell(ui, "TD", STR_TODO);
    oled_ui_draw_text(ui, 96, 28, "ROT", 1, 1);
    oled_ui_draw_text(ui, 96, 38, "SEL", 1, 1);
    oled_ui_draw_text(ui, 96, 48, "PA0", 1, 1);

    if (count == 0U)
    {
        oled_ui_draw_text(ui, 4, 34, "WAITING SNAPSHOT", 1, 1);
        return;
    }

    rt_snprintf(line, sizeof(line), "%u/%u",
                (unsigned int)snapshot.completed_count,
                (unsigned int)count);
    oled_ui_draw_text(ui, 4, 13, line, 1, 1);
    rt_snprintf(line, sizeof(line), "%u", (unsigned int)(page->selected + 1U));
    oled_ui_draw_text(ui, 78, 13, line, 1, 1);

    anim_q8 = page->select_anim.value_q8;
    max_top_q8 = (count > visible) ? (((rt_int32_t)count - visible) << 8) : 0;
    top_anim_q8 = anim_q8 - (((rt_int32_t)visible - 1) << 7);
    if (top_anim_q8 < 0)
    {
        top_anim_q8 = 0;
    }
    if (top_anim_q8 > max_top_q8)
    {
        top_anim_q8 = max_top_q8;
    }

    draw_selected_fill(ui,
                       x + 1,
                       y0 + (int)(((anim_q8 - top_anim_q8) * item_h) / 256),
                       w - 2,
                       item_h,
                       4,
                       pulse_level(g_ui.tick));

    for (i = 0U; i < count; i++)
    {
        int y = y0 + (int)(((((rt_int32_t)i) << 8) - top_anim_q8) * item_h / 256);
        rt_int32_t diff = ((((rt_int32_t)i) << 8) - anim_q8);
        rt_uint8_t focused = (rt_uint8_t)((diff > -128) && (diff < 128));
        int text_color = focused ? 0 : 1;
        const char *status = snapshot.items[i].completed ? "[x]" : "[ ]";
        const char *title = snapshot.items[i].title[0] ? snapshot.items[i].title : snapshot.items[i].id;

        if ((y < (y0 - item_h)) || (y > (y0 + item_h * visible)))
        {
            continue;
        }

        oled_ui_draw_utf8_marquee(ui,
                                  6,
                                  y + 3,
                                  58,
                                  title,
                                  1,
                                  text_color,
                                  focused ? (g_ui.tick * 3U) : 0U);
        oled_ui_draw_text(ui, 66, y + 3, status, 1, text_color);
    }
}

static rt_bool_t input_todo_page(ui_page_t *page, rt_int32_t delta, rt_bool_t pressed)
{
    todo_snapshot_t snapshot;
    rt_uint8_t count;

    todo_snapshot_get(&snapshot);
    count = (rt_uint8_t)snapshot.count;

    if ((delta != 0) && (count > 0U))
    {
        page_shift_selected(page, count, delta);
    }

    RT_UNUSED(pressed);

    return RT_TRUE;
}

static void render_status_page(oled_ui_t *ui, ui_page_t *page)
{
    char line[24];

    RT_UNUSED(page);

    draw_page_head(ui, "ST");
    draw_ref_list_shell(ui, "ST", STR_STATUS);

    rt_snprintf(line, sizeof(line), "R:%lu  L:%lu",
                (unsigned long)g_demo.cw_count,
                (unsigned long)g_demo.ccw_count);
    oled_ui_draw_text(ui, 24, 30, line, 1, 1);
    rt_snprintf(line, sizeof(line), "P:%lu  D:%c",
                (unsigned long)g_demo.press_count,
                g_demo.last_dir);
    oled_ui_draw_text(ui, 24, 40, line, 1, 1);
    oled_ui_draw_text(ui, 24, 52, "PRESS TO BACK", 1, 1);
}

static rt_bool_t input_status_page(ui_page_t *page, rt_int32_t delta, rt_bool_t pressed)
{
    RT_UNUSED(page);
    RT_UNUSED(delta);

    if (pressed)
    {
        ui_back();
    }
    return RT_TRUE;
}

static void render_wifi_page(oled_ui_t *ui, ui_page_t *page)
{
    esp01s_status_t wifi;
    char line[32];

    RT_UNUSED(page);

    esp01s_get_status(&wifi);

    draw_page_head(ui, "WF");
    draw_ref_list_shell(ui, "WF", STR_WIFI);
    oled_ui_draw_text(ui, 96, 24, "OK", 1, 1);
    oled_ui_draw_text(ui, 96, 34, "SCAN", 1, 1);
    oled_ui_draw_text(ui, 96, 46, "PA0", 1, 1);

    rt_snprintf(line, sizeof(line), "B%lu", (unsigned long)wifi.baud_rate);
    oled_ui_draw_text(ui, 4, 24, line, 1, 1);
    rt_snprintf(line, sizeof(line), "R%d", wifi.rssi);
    oled_ui_draw_text(ui, 34, 24, line, 1, 1);
    oled_ui_draw_text(ui, 64, 24, wifi.connected ? "UP" : "DOWN", 1, 1);

    oled_ui_draw_text(ui, 4, 36, "SSID", 1, 1);
    oled_ui_draw_utf8_marquee(ui, 4, 46, 86, wifi.ssid[0] ? wifi.ssid : "-", 1, 1, g_ui.tick * 3U);
    oled_ui_draw_text(ui, 4, 56, "IP", 1, 1);
    oled_ui_draw_utf8_marquee(ui, 20, 56, 70, wifi.ip[0] ? wifi.ip : "-", 1, 1, g_ui.tick * 3U);
}

static rt_bool_t input_wifi_page(ui_page_t *page, rt_int32_t delta, rt_bool_t pressed)
{
    RT_UNUSED(page);
    RT_UNUSED(delta);

    if (pressed)
    {
        page_set_selected(&g_page_wifi_scan, 0U);
        ui_switch_page(&g_page_wifi_scan, anim_forward(), ANIM_DURATION_MS);
        start_wifi_job(WIFI_JOB_SCAN, RT_NULL, 0U);
    }

    return RT_TRUE;
}

static void render_wifi_scan_page(oled_ui_t *ui, ui_page_t *page)
{
    esp01s_status_t wifi;
    esp01s_ap_info_t aps[WIFI_SCAN_MAX_APS];
    rt_size_t count;
    rt_uint8_t total_items;
    rt_int32_t max_top_q8;
    rt_int32_t top_anim_q8;
    rt_int32_t anim_q8;
    rt_uint8_t i;
    char line[24];
    const int x = 2;
    const int y0 = 18;
    const int w = 88;
    const int item_h = 14;
    const rt_uint8_t visible = 3U;

    esp01s_get_status(&wifi);
    count = esp01s_get_scan_list(aps, WIFI_SCAN_MAX_APS);
    total_items = (rt_uint8_t)(count + 1U);

    if (page->selected >= total_items)
    {
        page_set_selected(page, (rt_uint8_t)(total_items - 1U));
    }

    draw_page_head(ui, "SC");
    draw_ref_list_shell(ui, "WF", wifi.scan_busy ? "SCAN BUSY" : "SCAN LIST");
    oled_ui_draw_text(ui, 96, 24, "OK", 1, 1);
    oled_ui_draw_text(ui, 96, 34, "JOIN", 1, 1);
    oled_ui_draw_text(ui, 96, 46, "PA0", 1, 1);

    anim_q8 = page->select_anim.value_q8;
    max_top_q8 = (total_items > visible) ? (((rt_int32_t)total_items - visible) << 8) : 0;
    top_anim_q8 = anim_q8 - (((rt_int32_t)visible - 1) << 7);
    if (top_anim_q8 < 0)
    {
        top_anim_q8 = 0;
    }
    if (top_anim_q8 > max_top_q8)
    {
        top_anim_q8 = max_top_q8;
    }

    draw_selected_fill(ui,
                       x + 1,
                       y0 + (int)(((anim_q8 - top_anim_q8) * item_h) / 256),
                       w - 2,
                       item_h,
                       4,
                       pulse_level(g_ui.tick));

    for (i = 0U; i < total_items; i++)
    {
        int y = y0 + (int)(((((rt_int32_t)i) << 8) - top_anim_q8) * item_h / 256);
        rt_int32_t diff = ((((rt_int32_t)i) << 8) - anim_q8);
        rt_uint8_t focused = (rt_uint8_t)((diff > -128) && (diff < 128));
        int text_color = focused ? 0 : 1;
        const char *text;
        rt_int16_t rssi = 0;
        rt_uint8_t locked = 0;

        if ((y < (y0 - item_h)) || (y > (y0 + item_h * visible)))
        {
            continue;
        }

        if (i == 0U)
        {
            text = "REFRESH";
        }
        else
        {
            text = aps[i - 1U].ssid;
            rssi = aps[i - 1U].rssi;
            locked = (rt_uint8_t)(aps[i - 1U].ecn != 0U);
        }

        oled_ui_draw_utf8_marquee(ui, 6, y + 3, 58, text, 1, text_color, focused ? (g_ui.tick * 3U) : 0U);
        if (i != 0U)
        {
            rt_snprintf(line, sizeof(line), "%c%d", locked ? 'L' : 'O', rssi);
            oled_ui_draw_text(ui, 66, y + 3, line, 1, text_color);
        }
    }
}

static rt_bool_t input_wifi_scan_page(ui_page_t *page, rt_int32_t delta, rt_bool_t pressed)
{
    esp01s_ap_info_t aps[WIFI_SCAN_MAX_APS];
    rt_size_t count = esp01s_get_scan_list(aps, WIFI_SCAN_MAX_APS);
    rt_uint8_t total_items = (rt_uint8_t)(count + 1U);

    if (delta != 0)
    {
        page_shift_selected(page, total_items, delta);
    }

    if (pressed)
    {
        if (page->selected == 0U)
        {
            start_wifi_job(WIFI_JOB_SCAN, RT_NULL, 0U);
        }
        else if ((page->selected - 1U) < count)
        {
            esp01s_ap_info_t *ap = &aps[page->selected - 1U];
            start_wifi_job(WIFI_JOB_CONNECT, ap->ssid, ap->ecn);
            ui_switch_page(&g_page_wifi, anim_backward(), ANIM_DURATION_MS);
        }
    }

    return RT_TRUE;
}

static void draw_pc_slider_page(oled_ui_t *ui, const char *title, rt_uint8_t value, const char *subtext)
{
    char line[24];
    const char *status = g_pc_status;

    draw_page_head(ui, "PC");
    draw_ref_list_shell(ui, "PC", title);

    rt_snprintf(line, sizeof(line), "%u%%", value);
    draw_ascii_text_center_scale(ui, 24, 26, 102, line, 2, 1);

    if (subtext != RT_NULL)
    {
        oled_ui_draw_text(ui, 24, 48, subtext, 1, 1);
    }

    rt_snprintf(line, sizeof(line), "S:%s", (status != RT_NULL) ? status : "N/A");
    oled_ui_draw_text(ui, 52, 48, line, 1, 1);
    oled_ui_draw_progress(ui, 24, 58, 80, 6, value, 100, 1);
    draw_ascii_button(ui, 106, 55, 20, 8, "BACK", 1);
}

static void render_pc_volume_page(oled_ui_t *ui, ui_page_t *page)
{
    RT_UNUSED(page);
    draw_pc_slider_page(ui, STR_VOLUME, g_demo.pc_volume, "USB");
}

static rt_bool_t input_pc_volume_page(ui_page_t *page, rt_int32_t delta, rt_bool_t pressed)
{
    RT_UNUSED(page);

    if (delta > 0)
    {
        if (g_demo.pc_volume < 100U)
        {
            g_demo.pc_volume++;
            pc_status_set("LOCAL");
        }
    }
    else if (delta < 0)
    {
        if (g_demo.pc_volume > 0U)
        {
            g_demo.pc_volume--;
            pc_status_set("LOCAL");
        }
    }

    if (pressed)
    {
        ui_back();
    }
    return RT_TRUE;
}

static void render_pc_brightness_page(oled_ui_t *ui, ui_page_t *page)
{
    RT_UNUSED(page);
    draw_pc_slider_page(ui, STR_PC_BRIGHT, g_demo.pc_brightness, "USB");
}

static rt_bool_t input_pc_brightness_page(ui_page_t *page, rt_int32_t delta, rt_bool_t pressed)
{
    RT_UNUSED(page);

    if (delta > 0)
    {
        if (g_demo.pc_brightness < 100U)
        {
            g_demo.pc_brightness++;
            pc_status_set("LOCAL");
        }
    }
    else if (delta < 0)
    {
        if (g_demo.pc_brightness > 0U)
        {
            g_demo.pc_brightness--;
            pc_status_set("LOCAL");
        }
    }

    if (pressed)
    {
        ui_back();
    }
    return RT_TRUE;
}

static void render_brightness_page(oled_ui_t *ui, ui_page_t *page)
{
    char line[24];

    RT_UNUSED(page);

    draw_page_head(ui, "BR");
    draw_ref_list_shell(ui, "SE", STR_BRIGHTNESS);
    oled_ui_draw_text(ui, 96, 24, "ROT", 1, 1);
    oled_ui_draw_text(ui, 96, 34, "+/-", 1, 1);
    oled_ui_draw_text(ui, 96, 46, "PA0", 1, 1);

    rt_snprintf(line, sizeof(line), "%u%%", (unsigned int)g_demo.brightness);
    draw_ascii_text_center_scale(ui, 2, 24, 88, line, 2, 1);
    oled_ui_draw_text(ui, 4, 49, "L/R ADJUST", 1, 1);
    oled_ui_draw_progress(ui, 2, 58, 88, 6, g_demo.brightness, 100, 1);
}

static rt_bool_t input_brightness_page(ui_page_t *page, rt_int32_t delta, rt_bool_t pressed)
{
    RT_UNUSED(page);

    if (delta > 0)
    {
        if (g_demo.brightness < (100U - BRIGHTNESS_STEP))
        {
            g_demo.brightness = (rt_uint8_t)(g_demo.brightness + BRIGHTNESS_STEP);
        }
        else
        {
            g_demo.brightness = 100U;
        }
        apply_brightness();
    }
    else if (delta < 0)
    {
        if (g_demo.brightness > BRIGHTNESS_STEP)
        {
            g_demo.brightness = (rt_uint8_t)(g_demo.brightness - BRIGHTNESS_STEP);
        }
        else
        {
            g_demo.brightness = 0U;
        }
        apply_brightness();
    }

    RT_UNUSED(pressed);

    return RT_TRUE;
}

static void render_anim_page(oled_ui_t *ui, ui_page_t *page)
{
    char line[24];

    draw_page_head(ui, "AN");
    draw_ref_list_shell(ui, "SE", STR_ANIM);

    rt_snprintf(line, sizeof(line), "MODE:%s", anim_name(g_demo.anim_idx));
    draw_ascii_text_center(ui, 24, 32, 102, line, 1);
    rt_snprintf(line, sizeof(line), "STYLE REF");
    draw_ascii_text_center(ui, 24, 42, 102, line, 1);
    draw_button_pair(ui,
                     58,
                     55,
                     24,
                     "NEXT",
                     90,
                     24,
                     "BACK",
                     page->select_anim.value_q8,
                     page->selected);
}

static rt_bool_t input_anim_page(ui_page_t *page, rt_int32_t delta, rt_bool_t pressed)
{
    if (delta != 0)
    {
        page_shift_selected(page, 2U, delta);
    }

    if (pressed)
    {
        if (page->selected == 0U)
        {
            g_demo.anim_idx = (rt_uint8_t)((g_demo.anim_idx + 1U) % 4U);
        }
        else
        {
            ui_back();
        }
    }

    return RT_TRUE;
}

static void render_about_page(oled_ui_t *ui, ui_page_t *page)
{
    RT_UNUSED(page);

    draw_page_head(ui, "IN");
    draw_ref_list_shell(ui, "IN", STR_ABOUT);
    oled_ui_draw_text(ui, 24, 30, "SSD1306 SPI1", 1, 1);
    oled_ui_draw_text(ui, 24, 40, "EC11 PB12-14", 1, 1);
    oled_ui_draw_text(ui, 24, 50, "PA5/PA7 PB0/1", 1, 1);
}

static rt_bool_t input_about_page(ui_page_t *page, rt_int32_t delta, rt_bool_t pressed)
{
    RT_UNUSED(page);
    RT_UNUSED(delta);

    if (pressed)
    {
        ui_back();
    }
    return RT_TRUE;
}

static void ui_init_pages(void)
{
    rt_size_t i;

    g_page_main.title = STR_MAIN_MENU;
    g_page_main.tag = "MN";
    g_page_main.style = UI_PAGE_TILES;
    g_page_main.parent = RT_NULL;
    g_page_main.items = g_main_items;
    g_page_main.item_count = ARRAY_SIZE(g_main_items);
    g_page_main.selected = 0U;
    g_page_main.tile_icon_draw = draw_tile_icon;

    g_page_todo.title = STR_TODO;
    g_page_todo.tag = "TD";
    g_page_todo.style = UI_PAGE_CUSTOM;
    g_page_todo.parent = &g_page_main;
    g_page_todo.selected = 0U;
    g_page_todo.render_custom = render_todo_page;
    g_page_todo.input_custom = input_todo_page;

    g_page_wifi.title = STR_WIFI;
    g_page_wifi.tag = "WF";
    g_page_wifi.style = UI_PAGE_CUSTOM;
    g_page_wifi.parent = &g_page_main;
    g_page_wifi.selected = 0U;
    g_page_wifi.render_custom = render_wifi_page;
    g_page_wifi.input_custom = input_wifi_page;

    g_page_wifi_scan.title = "SCAN";
    g_page_wifi_scan.tag = "WF";
    g_page_wifi_scan.style = UI_PAGE_CUSTOM;
    g_page_wifi_scan.parent = &g_page_wifi;
    g_page_wifi_scan.selected = 0U;
    g_page_wifi_scan.render_custom = render_wifi_scan_page;
    g_page_wifi_scan.input_custom = input_wifi_scan_page;

    g_page_settings.title = STR_SETTINGS;
    g_page_settings.tag = "SE";
    g_page_settings.style = UI_PAGE_LIST;
    g_page_settings.parent = &g_page_main;
    g_page_settings.items = g_settings_items;
    g_page_settings.item_count = ARRAY_SIZE(g_settings_items);
    g_page_settings.selected = 0U;
    g_page_settings.render_custom = render_list_page;
    g_page_settings.list_visible = 1U;
    g_page_settings.list_item_h = 16U;
    g_page_settings.list_x = 2U;
    g_page_settings.list_y = 18U;
    g_page_settings.list_w = 88U;

    g_page_brightness.title = STR_BRIGHTNESS;
    g_page_brightness.tag = "SE";
    g_page_brightness.style = UI_PAGE_CUSTOM;
    g_page_brightness.parent = &g_page_settings;
    g_page_brightness.render_custom = render_brightness_page;
    g_page_brightness.input_custom = input_brightness_page;

    for (i = 0; i < sizeof(g_all_pages) / sizeof(g_all_pages[0]); i++)
    {
        ui_anim_init(&g_all_pages[i]->select_anim, ((rt_int32_t)g_all_pages[i]->selected) << 8);
    }
}

static void wifi_job_thread(void *parameter)
{
    wifi_job_ctx_t *ctx = (wifi_job_ctx_t *)parameter;
    rt_err_t ret = -RT_ERROR;
    char ip[32];

    if (ctx != RT_NULL)
    {
        if (ctx->type == WIFI_JOB_SCAN)
        {
            ret = esp01s_scan_aps();
        }
        else if (ctx->type == WIFI_JOB_CONNECT)
        {
            ret = esp01s_join_saved_ap(ctx->ssid, ctx->ecn);
            if (ret == RT_EOK)
            {
                esp01s_get_ip(ip, sizeof(ip));
            }
        }
    }

    LOG_I("wifi job done: type=%d ret=%d", (ctx != RT_NULL) ? ctx->type : 0, ret);
    g_wifi_job_busy = 0U;
    if (ctx != RT_NULL)
    {
        rt_free(ctx);
    }
}

static void start_wifi_job(wifi_job_type_t type, const char *ssid, rt_uint8_t ecn)
{
    rt_thread_t thread;
    wifi_job_ctx_t *ctx;

    if (g_wifi_job_busy != 0U)
    {
        return;
    }

    ctx = (wifi_job_ctx_t *)rt_calloc(1, sizeof(*ctx));
    if (ctx == RT_NULL)
    {
        return;
    }

    ctx->type = type;
    ctx->ecn = ecn;
    if (ssid != RT_NULL)
    {
        rt_strncpy(ctx->ssid, ssid, sizeof(ctx->ssid) - 1);
        ctx->ssid[sizeof(ctx->ssid) - 1] = '\0';
    }

    thread = rt_thread_create("wifijob", wifi_job_thread, ctx, 3072, 17, 10);
    if (thread == RT_NULL)
    {
        rt_free(ctx);
        return;
    }

    g_wifi_job_busy = 1U;
    rt_thread_startup(thread);
}

static void encoder_handle_ab_irq(encoder_t *enc)
{
    static const rt_int8_t qdec[16] =
    {
         0, -1,  1,  0,
         1,  0,  0, -1,
        -1,  0,  0,  1,
         0,  1, -1,  0
    };
    rt_tick_t now = rt_tick_get();
    rt_uint8_t a;
    rt_uint8_t b;
    rt_uint8_t curr_ab;
    rt_uint8_t idx;

    if ((now - enc->ab_last_tick) < enc->ab_debounce_ticks)
    {
        return;
    }

    a = (rt_uint8_t)(rt_pin_read(enc->pin_a) ? 1U : 0U);
    b = (rt_uint8_t)(rt_pin_read(enc->pin_b) ? 1U : 0U);
    curr_ab = (rt_uint8_t)((a << 1) | b);

    if (curr_ab == enc->prev_ab)
    {
        return;
    }

    idx = (rt_uint8_t)((enc->prev_ab << 2) | curr_ab);
    enc->acc += qdec[idx];
    enc->prev_ab = curr_ab;
    enc->ab_last_tick = now;

    if (enc->acc >= 4)
    {
        if ((now - enc->step_last_tick) < enc->step_lock_ticks)
        {
            enc->acc = 0;
            return;
        }
        enc->acc = 0;
        enc->step_last_tick = now;
        if (enc->step_delta < 32)
        {
            enc->step_delta++;
        }
    }
    else if (enc->acc <= -4)
    {
        if ((now - enc->step_last_tick) < enc->step_lock_ticks)
        {
            enc->acc = 0;
            return;
        }
        enc->acc = 0;
        enc->step_last_tick = now;
        if (enc->step_delta > -32)
        {
            enc->step_delta--;
        }
    }
}

static void encoder_handle_sw_irq(encoder_t *enc)
{
    rt_tick_t now = rt_tick_get();

    if (rt_pin_read(enc->pin_sw) != PIN_LOW)
    {
        return;
    }

    if ((now - enc->sw_last_tick) < enc->sw_debounce_ticks)
    {
        return;
    }

    enc->sw_last_tick = now;
    enc->press_pending = 1U;
}

static void encoder_irq_handler(void *args)
{
    encoder_irq_ctx_t *ctx = (encoder_irq_ctx_t *)args;

    if ((ctx == RT_NULL) || (ctx->enc == RT_NULL))
    {
        return;
    }

    if (ctx->channel == 2U)
    {
        encoder_handle_sw_irq(ctx->enc);
    }
    else
    {
        encoder_handle_ab_irq(ctx->enc);
    }
}

static rt_err_t encoder_init(encoder_t *enc, rt_base_t pin_a, rt_base_t pin_b, rt_base_t pin_sw)
{
    rt_uint8_t a;
    rt_uint8_t b;
    rt_err_t ret;

    enc->pin_a = pin_a;
    enc->pin_b = pin_b;
    enc->pin_sw = pin_sw;

    rt_pin_mode(pin_a, PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(pin_b, PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(pin_sw, PIN_MODE_INPUT_PULLUP);

    a = (rt_uint8_t)(rt_pin_read(pin_a) ? 1U : 0U);
    b = (rt_uint8_t)(rt_pin_read(pin_b) ? 1U : 0U);
    enc->prev_ab = (rt_uint8_t)((a << 1) | b);
    enc->acc = 0;
    enc->step_delta = 0;
    enc->press_pending = 0U;
    enc->ab_last_tick = 0;
    enc->step_last_tick = 0;
    enc->sw_last_tick = 0;
    enc->ab_debounce_ticks = rt_tick_from_millisecond(1);
    enc->step_lock_ticks = rt_tick_from_millisecond(2);
    enc->sw_debounce_ticks = rt_tick_from_millisecond(25);

    g_irq_ctx_a.enc = enc;
    g_irq_ctx_a.channel = 0U;
    g_irq_ctx_b.enc = enc;
    g_irq_ctx_b.channel = 1U;
    g_irq_ctx_sw.enc = enc;
    g_irq_ctx_sw.channel = 2U;

    ret = rt_pin_attach_irq(pin_a, PIN_IRQ_MODE_RISING_FALLING, encoder_irq_handler, &g_irq_ctx_a);
    if (ret != RT_EOK)
    {
        return ret;
    }

    ret = rt_pin_attach_irq(pin_b, PIN_IRQ_MODE_RISING_FALLING, encoder_irq_handler, &g_irq_ctx_b);
    if (ret != RT_EOK)
    {
        rt_pin_detach_irq(pin_a);
        return ret;
    }

    ret = rt_pin_attach_irq(pin_sw, PIN_IRQ_MODE_FALLING, encoder_irq_handler, &g_irq_ctx_sw);
    if (ret != RT_EOK)
    {
        rt_pin_detach_irq(pin_a);
        rt_pin_detach_irq(pin_b);
        return ret;
    }

    ret = rt_pin_irq_enable(pin_a, PIN_IRQ_ENABLE);
    if (ret != RT_EOK)
    {
        rt_pin_detach_irq(pin_a);
        rt_pin_detach_irq(pin_b);
        rt_pin_detach_irq(pin_sw);
        return ret;
    }

    ret = rt_pin_irq_enable(pin_b, PIN_IRQ_ENABLE);
    if (ret != RT_EOK)
    {
        rt_pin_irq_enable(pin_a, PIN_IRQ_DISABLE);
        rt_pin_detach_irq(pin_a);
        rt_pin_detach_irq(pin_b);
        rt_pin_detach_irq(pin_sw);
        return ret;
    }

    ret = rt_pin_irq_enable(pin_sw, PIN_IRQ_ENABLE);
    if (ret != RT_EOK)
    {
        rt_pin_irq_enable(pin_a, PIN_IRQ_DISABLE);
        rt_pin_irq_enable(pin_b, PIN_IRQ_DISABLE);
        rt_pin_detach_irq(pin_a);
        rt_pin_detach_irq(pin_b);
        rt_pin_detach_irq(pin_sw);
        return ret;
    }

    return RT_EOK;
}

static rt_int32_t encoder_take_delta(encoder_t *enc)
{
    rt_base_t level;
    rt_int32_t delta;

    level = rt_hw_interrupt_disable();
    delta = enc->step_delta;
    enc->step_delta = 0;
    rt_hw_interrupt_enable(level);

    return delta;
}

static rt_uint8_t encoder_take_press(encoder_t *enc)
{
    rt_base_t level;
    rt_uint8_t pressed;

    level = rt_hw_interrupt_disable();
    pressed = enc->press_pending;
    enc->press_pending = 0U;
    rt_hw_interrupt_enable(level);

    return pressed;
}

static rt_err_t back_key_init(back_key_t *key, rt_base_t pin)
{
    if (key == RT_NULL)
    {
        return -RT_EINVAL;
    }

    key->pin = pin;
    rt_pin_mode(pin, PIN_MODE_INPUT_PULLUP);
    key->prev_level = (rt_uint8_t)(rt_pin_read(pin) ? 1U : 0U);
    key->last_tick = 0;
    key->debounce_ticks = rt_tick_from_millisecond(25);
    return RT_EOK;
}

static rt_uint8_t back_key_take_press(back_key_t *key)
{
    rt_uint8_t level;
    rt_tick_t now;

    if (key == RT_NULL)
    {
        return 0U;
    }

    level = (rt_uint8_t)(rt_pin_read(key->pin) ? 1U : 0U);
    now = rt_tick_get();

    if (level != key->prev_level)
    {
        if ((now - key->last_tick) >= key->debounce_ticks)
        {
            key->last_tick = now;
            key->prev_level = level;
            if (level == 0U)
            {
                return 1U;
            }
        }
    }

    return 0U;
}

static void handle_encoder_event(enc_event_t evt)
{
    if (evt == ENC_EVT_RIGHT)
    {
        g_demo.cw_count++;
        g_demo.total++;
        g_demo.last_dir = 'R';
        ui_handle_rotate(1);
    }
    else if (evt == ENC_EVT_LEFT)
    {
        g_demo.ccw_count++;
        g_demo.total--;
        g_demo.last_dir = 'L';
        ui_handle_rotate(-1);
    }
    else if (evt == ENC_EVT_PRESS)
    {
        g_demo.press_count++;
        g_demo.last_dir = 'P';
        ui_handle_press();
    }
}

static void oled_demo_thread(void *parameter)
{
    rt_tick_t last_refresh;
    rt_tick_t last_fps_tick;
    rt_uint32_t last_present_count;

    RT_UNUSED(parameter);

    if (oled_ui_init(&g_oled, OLED_SPI_BUS, OLED_DEV_NAME, OLED_CS_PIN, OLED_DC_PIN, OLED_RST_PIN) != RT_EOK)
    {
        LOG_E("oled init failed");
        return;
    }

    if (encoder_init(&g_encoder, ENC_PIN_A, ENC_PIN_B, ENC_PIN_SW) != RT_EOK)
    {
        LOG_E("encoder init failed");
        return;
    }
    if (back_key_init(&g_back_key, BACK_KEY_PIN) != RT_EOK)
    {
        LOG_E("back key init failed");
        return;
    }

    g_ui.oled = &g_oled;
    g_ui.current = RT_NULL;
    g_ui.tick = 0U;

    ui_init_pages();
    if (ui_astra_init_bridge() != RT_EOK)
    {
        LOG_W("astra bridge init failed, fallback to legacy ui");
        g_astra_ready = 0U;
    }
    apply_brightness();
    oled_ui_clear(&g_oled, 0);
    oled_ui_present(&g_oled);
    rt_thread_mdelay(40);

    pc_status_set("WAIT");
    ui_switch_page(&g_page_main, OLED_UI_ANIM_NONE, 0U);
    ui_refresh();
    last_refresh = rt_tick_get();
    last_fps_tick = last_refresh;
    last_present_count = oled_ui_get_present_count(&g_oled);
    LOG_I("oled init ok");

    while (1)
    {
        rt_int32_t delta = encoder_take_delta(&g_encoder);

        while (delta > 0)
        {
            handle_encoder_event(ENC_EVT_RIGHT);
            delta--;
        }

        while (delta < 0)
        {
            handle_encoder_event(ENC_EVT_LEFT);
            delta++;
        }

        if (encoder_take_press(&g_encoder) != 0U)
        {
            handle_encoder_event(ENC_EVT_PRESS);
        }

        if (back_key_take_press(&g_back_key) != 0U)
        {
            ui_back();
            ui_refresh();
        }

        if ((rt_tick_get() - last_refresh) >= rt_tick_from_millisecond(UI_REFRESH_MS))
        {
            g_ui.tick++;
            update_ui_animation_states();
            ui_refresh();
            last_refresh = rt_tick_get();
        }

        if ((rt_tick_get() - last_fps_tick) >= rt_tick_from_millisecond(1000))
        {
            rt_tick_t now = rt_tick_get();
            rt_tick_t elapsed_tick = now - last_fps_tick;
            rt_uint32_t elapsed_ms;
            rt_uint32_t present_count = oled_ui_get_present_count(&g_oled);
            rt_uint32_t delta_present = present_count - last_present_count;

            elapsed_ms = (rt_uint32_t)((elapsed_tick * 1000U) / RT_TICK_PER_SECOND);
            if (elapsed_ms == 0U)
            {
                elapsed_ms = 1U;
            }

            g_demo.fps = (delta_present * 1000U + (elapsed_ms / 2U)) / elapsed_ms;
            last_present_count = present_count;
            last_fps_tick = now;
        }

        rt_thread_mdelay(1);
    }
}

static int oled_demo_start(void)
{
    rt_thread_t thread;

    thread = rt_thread_create("oledui", oled_demo_thread, RT_NULL, 4096, 18, 20);
    if (thread == RT_NULL)
    {
        return -RT_ERROR;
    }

    rt_thread_startup(thread);
    return RT_EOK;
}
INIT_APP_EXPORT(oled_demo_start);
