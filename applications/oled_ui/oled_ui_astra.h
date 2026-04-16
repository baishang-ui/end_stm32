/*
 * Astra-style UI launcher for OLED_UI.
 * C adaptation for RT-Thread + SSD1306.
 */

#ifndef __OLED_UI_ASTRA_H__
#define __OLED_UI_ASTRA_H__

#include "oled_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oled_ui_astra_launcher oled_ui_astra_launcher_t;
typedef struct oled_ui_astra_page oled_ui_astra_page_t;
typedef struct oled_ui_astra_item oled_ui_astra_item_t;
typedef struct oled_ui_astra_widget oled_ui_astra_widget_t;

typedef enum
{
    OLED_UI_ASTRA_PAGE_LIST = 0,
    OLED_UI_ASTRA_PAGE_TILE,
    OLED_UI_ASTRA_PAGE_CUSTOM,
} oled_ui_astra_page_type_t;

typedef enum
{
    OLED_UI_ASTRA_ITEM_MENU = 0,
    OLED_UI_ASTRA_ITEM_ACTION,
    OLED_UI_ASTRA_ITEM_BACK,
} oled_ui_astra_item_type_t;

typedef enum
{
    OLED_UI_ASTRA_WIDGET_NONE = 0,
    OLED_UI_ASTRA_WIDGET_CHECKBOX,
    OLED_UI_ASTRA_WIDGET_SLIDER,
} oled_ui_astra_widget_type_t;

typedef struct
{
    float tile_anim_speed;
    float list_anim_speed;
    float camera_anim_speed;
    float selector_x_anim_speed;
    float selector_y_anim_speed;
    float selector_w_anim_speed;
    float selector_h_anim_speed;
    float popup_anim_speed;

    rt_uint8_t menu_loop;
    rt_uint8_t list_unfold;
    rt_uint8_t tile_unfold;
    rt_uint8_t light_mode;

    rt_uint8_t list_line_height;
    rt_uint8_t list_text_margin;
    rt_uint8_t list_bar_width;
    rt_uint8_t list_visible_count;
    rt_uint8_t list_selector_radius;

    rt_uint8_t tile_pic_w;
    rt_uint8_t tile_pic_h;
    rt_uint8_t tile_pic_gap;
    rt_uint8_t tile_pic_top;
    rt_uint8_t tile_text_bottom;
    rt_uint8_t tile_select_margin;
    rt_uint8_t tile_bar_height;
    rt_uint8_t tile_arrow_margin;
    rt_uint8_t tile_arrow_width;

    rt_uint8_t popup_margin;
    rt_uint8_t popup_radius;
} oled_ui_astra_config_t;

typedef void (*oled_ui_astra_action_cb)(oled_ui_astra_launcher_t *launcher,
                                        oled_ui_astra_item_t *item,
                                        void *user);
typedef const char *(*oled_ui_astra_value_cb)(oled_ui_astra_launcher_t *launcher,
                                              oled_ui_astra_item_t *item,
                                              char *buf,
                                              rt_size_t len,
                                              void *user);
typedef void (*oled_ui_astra_icon_cb)(oled_ui_t *ui,
                                      oled_ui_astra_launcher_t *launcher,
                                      oled_ui_astra_item_t *item,
                                      int x,
                                      int y,
                                      int selected,
                                      void *user);
typedef void (*oled_ui_astra_page_render_cb)(oled_ui_t *ui,
                                             oled_ui_astra_launcher_t *launcher,
                                             oled_ui_astra_page_t *page,
                                             void *user);
typedef rt_bool_t (*oled_ui_astra_page_input_cb)(oled_ui_astra_launcher_t *launcher,
                                                 oled_ui_astra_page_t *page,
                                                 rt_int32_t rotate_delta,
                                                 rt_bool_t pressed,
                                                 void *user);
typedef void (*oled_ui_astra_page_enter_cb)(oled_ui_astra_launcher_t *launcher,
                                            oled_ui_astra_page_t *page,
                                            void *user);

struct oled_ui_astra_widget
{
    oled_ui_astra_widget_type_t type;
    union
    {
        struct
        {
            rt_uint8_t *value;
        } checkbox;
        struct
        {
            rt_uint8_t *value;
            rt_uint8_t min;
            rt_uint8_t max;
            rt_uint8_t step;
        } slider;
    } data;
};

struct oled_ui_astra_item
{
    const char *title;
    oled_ui_astra_item_type_t type;
    oled_ui_astra_page_t *target;
    oled_ui_astra_action_cb action;
    void *action_user;
    oled_ui_astra_value_cb value;
    void *value_user;
    oled_ui_astra_icon_cb icon;
    void *icon_user;
    oled_ui_astra_widget_t *widget;
};

struct oled_ui_astra_page
{
    const char *title;
    const char *tag;
    oled_ui_astra_page_type_t type;
    oled_ui_astra_page_t *parent;
    oled_ui_astra_item_t *items;
    rt_uint8_t item_count;
    rt_uint8_t selected;

    rt_uint8_t list_line_height;
    rt_uint8_t list_visible_count;
    rt_uint8_t content_x;
    rt_uint8_t content_y;
    rt_uint8_t content_w;

    oled_ui_astra_page_render_cb render;
    oled_ui_astra_page_input_cb input;
    oled_ui_astra_page_enter_cb on_enter;
    void *user;

    float cam_x;
    float cam_y;
    float cam_x_target;
    float cam_y_target;
    rt_uint8_t boundary_top;
    rt_uint8_t boundary_bottom;
};

struct oled_ui_astra_launcher
{
    oled_ui_t *ui;
    oled_ui_astra_config_t cfg;

    oled_ui_astra_page_t *current;
    oled_ui_astra_page_t *root;

    float selector_x;
    float selector_y;
    float selector_w;
    float selector_h;
    float selector_x_target;
    float selector_y_target;
    float selector_w_target;
    float selector_h_target;
    float selector_text_y;
    float selector_text_y_target;

    rt_uint32_t tick;
    rt_uint32_t transition_ms;
    oled_ui_anim_t transition_anim;

    rt_uint8_t pop_visible;
    rt_tick_t pop_begin_tick;
    rt_uint32_t pop_duration_ms;
    char pop_text[48];
    float pop_y;
    float pop_y_target;
};

void oled_ui_astra_config_default(oled_ui_astra_config_t *cfg);
void oled_ui_astra_page_init(oled_ui_astra_page_t *page,
                             const char *title,
                             const char *tag,
                             oled_ui_astra_page_type_t type);
void oled_ui_astra_launcher_init(oled_ui_astra_launcher_t *launcher,
                                 oled_ui_t *ui,
                                 oled_ui_astra_page_t *root,
                                 const oled_ui_astra_config_t *cfg);

void oled_ui_astra_widget_checkbox_bind(oled_ui_astra_widget_t *widget, rt_uint8_t *value);
void oled_ui_astra_widget_slider_bind(oled_ui_astra_widget_t *widget,
                                      rt_uint8_t *value,
                                      rt_uint8_t min,
                                      rt_uint8_t max,
                                      rt_uint8_t step);

void oled_ui_astra_launcher_rotate(oled_ui_astra_launcher_t *launcher, rt_int32_t delta);
void oled_ui_astra_launcher_press(oled_ui_astra_launcher_t *launcher);
void oled_ui_astra_launcher_back(oled_ui_astra_launcher_t *launcher);
void oled_ui_astra_launcher_switch(oled_ui_astra_launcher_t *launcher,
                                   oled_ui_astra_page_t *next,
                                   oled_ui_anim_t anim,
                                   rt_uint32_t duration_ms);
void oled_ui_astra_launcher_pop(oled_ui_astra_launcher_t *launcher,
                                const char *text,
                                rt_uint32_t duration_ms);
void oled_ui_astra_launcher_update(oled_ui_astra_launcher_t *launcher);

void oled_ui_astra_draw_tile_icon_default(oled_ui_t *ui,
                                          oled_ui_astra_launcher_t *launcher,
                                          oled_ui_astra_item_t *item,
                                          int x,
                                          int y,
                                          int selected,
                                          void *user);

#ifdef __cplusplus
}
#endif

#endif /* __OLED_UI_ASTRA_H__ */
