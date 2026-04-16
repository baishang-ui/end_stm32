/*
 * Astra-style UI launcher for OLED_UI.
 * C adaptation for RT-Thread + SSD1306.
 */

#include "oled_ui_astra.h"

#include <string.h>

#define ASTRA_TEXT_SCALE            1
#define ASTRA_TILE_STRIDE           44
#define ASTRA_POPUP_MAX_MS          10000U
#define ASTRA_MIN_DURATION_MS       1U
#define ASTRA_LERP_EPSILON          0.01f

typedef struct
{
    oled_ui_astra_launcher_t *launcher;
    oled_ui_astra_page_t *page;
} astra_render_ctx_t;

static rt_bool_t astra_is_valid(const oled_ui_astra_launcher_t *launcher)
{
    return (launcher != RT_NULL) &&
           (launcher->ui != RT_NULL) &&
           (launcher->current != RT_NULL);
}

static int astra_clampi(int v, int min_v, int max_v)
{
    if (v < min_v)
    {
        return min_v;
    }
    if (v > max_v)
    {
        return max_v;
    }
    return v;
}

static float astra_absf(float v)
{
    return (v >= 0.0f) ? v : -v;
}

static float astra_lerp_to(float current, float target, float speed, rt_uint32_t dt_ms)
{
    float k;

    if (dt_ms == 0U)
    {
        return current;
    }

    k = speed * ((float)dt_ms / 1000.0f);
    if (k > 1.0f)
    {
        k = 1.0f;
    }
    if (k < 0.02f)
    {
        k = 0.02f;
    }

    current += (target - current) * k;
    if (astra_absf(target - current) < ASTRA_LERP_EPSILON)
    {
        current = target;
    }

    return current;
}

static rt_uint8_t astra_page_line_h(const oled_ui_astra_launcher_t *launcher,
                                    const oled_ui_astra_page_t *page)
{
    if (page->list_line_height != 0U)
    {
        return page->list_line_height;
    }
    return launcher->cfg.list_line_height;
}

static rt_uint8_t astra_page_visible(const oled_ui_astra_launcher_t *launcher,
                                     const oled_ui_astra_page_t *page)
{
    if (page->list_visible_count != 0U)
    {
        return page->list_visible_count;
    }
    return launcher->cfg.list_visible_count;
}

static void astra_list_update_targets(oled_ui_astra_launcher_t *launcher, oled_ui_astra_page_t *page)
{
    int line_h = astra_page_line_h(launcher, page);
    int visible = astra_page_visible(launcher, page);
    int content_y = (page->content_y != 0U) ? page->content_y : 14;
    int content_x = (page->content_x != 0U) ? page->content_x : 0;
    int content_w = (page->content_w != 0U) ? page->content_w : OLED_UI_WIDTH;
    int y_target;
    int max_cam;
    int selected_y;

    if (page->item_count == 0U)
    {
        page->cam_y_target = 0.0f;
        launcher->selector_x_target = (float)content_x;
        launcher->selector_y_target = (float)content_y;
        launcher->selector_w_target = (float)content_w;
        launcher->selector_h_target = (float)line_h;
        launcher->selector_text_y_target = (float)content_y;
        return;
    }

    selected_y = (int)page->selected * line_h;
    y_target = selected_y - ((visible - 1) * line_h) / 2;
    max_cam = (int)page->item_count * line_h - visible * line_h;
    if (max_cam < 0)
    {
        max_cam = 0;
    }
    y_target = astra_clampi(y_target, 0, max_cam);

    page->cam_y_target = (float)y_target;
    launcher->selector_x_target = (float)content_x;
    launcher->selector_y_target = (float)(content_y + selected_y - y_target);
    launcher->selector_w_target = (float)content_w;
    launcher->selector_h_target = (float)line_h;
    launcher->selector_text_y_target = launcher->selector_y_target;
}

static void astra_tile_update_targets(oled_ui_astra_launcher_t *launcher, oled_ui_astra_page_t *page)
{
    int stride = ASTRA_TILE_STRIDE;
    int center_x = OLED_UI_WIDTH / 2;
    int box_w = launcher->cfg.tile_pic_w + launcher->cfg.tile_select_margin * 2U;
    int box_h = launcher->cfg.tile_pic_h + launcher->cfg.tile_select_margin * 2U;
    int box_y = launcher->cfg.tile_pic_top - launcher->cfg.tile_select_margin;
    int box_x;

    page->cam_x_target = (float)((int)page->selected * stride);
    box_x = center_x - (box_w / 2);

    launcher->selector_x_target = (float)box_x;
    launcher->selector_y_target = (float)box_y;
    launcher->selector_w_target = (float)box_w;
    launcher->selector_h_target = (float)box_h;
    launcher->selector_text_y_target = (float)launcher->cfg.tile_text_bottom;
}

static void astra_update_targets(oled_ui_astra_launcher_t *launcher, oled_ui_astra_page_t *page)
{
    if ((launcher == RT_NULL) || (page == RT_NULL))
    {
        return;
    }

    switch (page->type)
    {
    case OLED_UI_ASTRA_PAGE_TILE:
        astra_tile_update_targets(launcher, page);
        break;
    case OLED_UI_ASTRA_PAGE_LIST:
    case OLED_UI_ASTRA_PAGE_CUSTOM:
    default:
        astra_list_update_targets(launcher, page);
        break;
    }
}

static rt_uint8_t astra_step_index(const oled_ui_astra_launcher_t *launcher,
                                   rt_uint8_t selected,
                                   rt_uint8_t count,
                                   rt_int32_t delta)
{
    int idx;

    if (count == 0U)
    {
        return 0U;
    }

    idx = (int)selected + (int)delta;
    if (launcher->cfg.menu_loop)
    {
        while (idx < 0)
        {
            idx += count;
        }
        while (idx >= count)
        {
            idx -= count;
        }
    }
    else
    {
        idx = astra_clampi(idx, 0, count - 1);
    }

    return (rt_uint8_t)idx;
}

static void astra_draw_selector(oled_ui_t *ui, oled_ui_astra_launcher_t *launcher)
{
    int x = (int)(launcher->selector_x + 0.5f);
    int y = (int)(launcher->selector_y + 0.5f);
    int w = (int)(launcher->selector_w + 0.5f);
    int h = (int)(launcher->selector_h + 0.5f);
    int r = launcher->cfg.list_selector_radius;

    if ((w <= 2) || (h <= 2))
    {
        return;
    }

    if (launcher->cfg.light_mode)
    {
        oled_ui_fill_round_rect(ui, x, y, w, h, r, 1);
    }
    else
    {
        oled_ui_draw_round_rect(ui, x, y, w, h, r, 1);
    }
}

static void astra_draw_tile_bar(oled_ui_t *ui,
                                oled_ui_astra_launcher_t *launcher,
                                oled_ui_astra_page_t *page,
                                int y)
{
    int i;
    int count = page->item_count;
    int gap = 3;
    int dot_w = 6;
    int selected_w = 16;
    int total_w = (count > 0) ? ((count - 1) * gap + (count - 1) * dot_w + selected_w) : 0;
    int x = (OLED_UI_WIDTH - total_w) / 2;

    for (i = 0; i < count; i++)
    {
        if (i == (int)page->selected)
        {
            oled_ui_fill_round_rect(ui, x, y, selected_w, launcher->cfg.tile_bar_height, 2, 1);
            x += selected_w + gap;
        }
        else
        {
            oled_ui_fill_round_rect(ui, x, y, dot_w, launcher->cfg.tile_bar_height, 1, 1);
            x += dot_w + gap;
        }
    }
}

static void astra_draw_item_widget(oled_ui_t *ui,
                                   oled_ui_astra_launcher_t *launcher,
                                   const oled_ui_astra_item_t *item,
                                   int x,
                                   int y,
                                   int w,
                                   int selected)
{
    int right_x;
    rt_uint8_t invert = selected ? 1U : 0U;

    if ((item == RT_NULL) || (item->widget == RT_NULL))
    {
        return;
    }

    right_x = x + w - 4;
    switch (item->widget->type)
    {
    case OLED_UI_ASTRA_WIDGET_CHECKBOX:
        if (item->widget->data.checkbox.value != RT_NULL)
        {
            oled_ui_draw_rect(ui, right_x - 10, y + 3, 8, 8, invert ? 0 : 1);
            if (*(item->widget->data.checkbox.value))
            {
                oled_ui_draw_line(ui, right_x - 9, y + 7, right_x - 6, y + 10, invert ? 0 : 1);
                oled_ui_draw_line(ui, right_x - 6, y + 10, right_x - 3, y + 4, invert ? 0 : 1);
            }
        }
        break;

    case OLED_UI_ASTRA_WIDGET_SLIDER:
        if (item->widget->data.slider.value != RT_NULL)
        {
            int bar_w = 26;
            int bar_h = 6;
            int bar_x = right_x - bar_w - 2;
            int bar_y = y + 4;
            int min_v = item->widget->data.slider.min;
            int max_v = item->widget->data.slider.max;
            int val = *(item->widget->data.slider.value);
            int fill;

            if (max_v <= min_v)
            {
                max_v = min_v + 1;
            }
            val = astra_clampi(val, min_v, max_v);
            fill = ((bar_w - 2) * (val - min_v)) / (max_v - min_v);
            oled_ui_draw_round_rect(ui, bar_x, bar_y, bar_w, bar_h, 3, invert ? 0 : 1);
            if (fill > 0)
            {
                oled_ui_fill_round_rect(ui, bar_x + 1, bar_y + 1, fill, bar_h - 2, 2, invert ? 0 : 1);
            }
        }
        break;

    case OLED_UI_ASTRA_WIDGET_NONE:
    default:
        break;
    }
}

static void astra_render_list(oled_ui_t *ui, oled_ui_astra_launcher_t *launcher, oled_ui_astra_page_t *page)
{
    int i;
    int line_h = astra_page_line_h(launcher, page);
    int content_x = (page->content_x != 0U) ? page->content_x : 0;
    int content_y = (page->content_y != 0U) ? page->content_y : 14;
    int content_w = (page->content_w != 0U) ? page->content_w : OLED_UI_WIDTH;
    int text_x = content_x + launcher->cfg.list_text_margin;
    int base_y = content_y - (int)(page->cam_y + 0.5f);
    rt_uint32_t phase = launcher->tick * 3U;

    astra_draw_selector(ui, launcher);
    oled_ui_draw_utf8_text(ui, 0, 0, page->title, ASTRA_TEXT_SCALE, 1);

    for (i = 0; i < page->item_count; i++)
    {
        int y = base_y + i * line_h;
        int in_view = !((y + line_h) < content_y || (y > (content_y + OLED_UI_HEIGHT)));
        int selected = (i == page->selected);
        char value_buf[24];
        const char *value_text = RT_NULL;

        if (!in_view)
        {
            continue;
        }

        if (selected && (page->items[i].title != RT_NULL))
        {
            oled_ui_draw_utf8_marquee(ui,
                                      text_x,
                                      y + 3,
                                      content_w - launcher->cfg.list_text_margin * 2,
                                      page->items[i].title,
                                      ASTRA_TEXT_SCALE,
                                      launcher->cfg.light_mode ? 0 : 1,
                                      phase);
        }
        else
        {
            oled_ui_draw_utf8_text(ui,
                                   text_x,
                                   y + 3,
                                   (page->items[i].title != RT_NULL) ? page->items[i].title : "",
                                   ASTRA_TEXT_SCALE,
                                   launcher->cfg.light_mode && selected ? 0 : 1);
        }

        if (page->items[i].value != RT_NULL)
        {
            value_buf[0] = '\0';
            value_text = page->items[i].value(launcher, &page->items[i], value_buf, sizeof(value_buf), page->items[i].value_user);
            if ((value_text != RT_NULL) && (value_text[0] != '\0'))
            {
                int value_w = oled_ui_measure_utf8_width(value_text, ASTRA_TEXT_SCALE);
                int value_x = content_x + content_w - value_w - 6;
                oled_ui_draw_utf8_text(ui,
                                       value_x,
                                       y + 3,
                                       value_text,
                                       ASTRA_TEXT_SCALE,
                                       launcher->cfg.light_mode && selected ? 0 : 1);
            }
        }

        astra_draw_item_widget(ui, launcher, &page->items[i], content_x, y, content_w, selected);
    }

    if (page->item_count > 0U)
    {
        int bar_x = content_x + content_w - launcher->cfg.list_bar_width;
        int bar_h = (astra_page_visible(launcher, page) * line_h * (OLED_UI_HEIGHT - content_y - 1)) /
                    ((page->item_count * line_h) > 0 ? (page->item_count * line_h) : 1);
        int max_bar_y = OLED_UI_HEIGHT - 1 - bar_h;
        int bar_y = content_y;

        if (bar_h < 8)
        {
            bar_h = 8;
        }
        if (bar_h > (OLED_UI_HEIGHT - content_y - 1))
        {
            bar_h = OLED_UI_HEIGHT - content_y - 1;
        }

        if (page->item_count > astra_page_visible(launcher, page))
        {
            int max_cam = (page->item_count - astra_page_visible(launcher, page)) * line_h;
            if (max_cam < 1)
            {
                max_cam = 1;
            }
            bar_y = content_y + ((int)page->cam_y * (max_bar_y - content_y)) / max_cam;
        }

        oled_ui_fill_round_rect(ui, bar_x, content_y, launcher->cfg.list_bar_width, OLED_UI_HEIGHT - content_y, 1, 0);
        oled_ui_fill_round_rect(ui, bar_x, bar_y, launcher->cfg.list_bar_width, bar_h, 1, 1);
    }
}

static void astra_render_tile(oled_ui_t *ui, oled_ui_astra_launcher_t *launcher, oled_ui_astra_page_t *page)
{
    int i;
    int center_x = OLED_UI_WIDTH / 2;
    int base_y = launcher->cfg.tile_pic_top;
    int stride = ASTRA_TILE_STRIDE;
    int text_y = launcher->cfg.tile_text_bottom;
    int text_w = 112;
    int text_x = (OLED_UI_WIDTH - text_w) / 2;

    oled_ui_draw_utf8_text(ui, 0, 0, page->title, ASTRA_TEXT_SCALE, 1);
    astra_draw_selector(ui, launcher);

    for (i = 0; i < page->item_count; i++)
    {
        int x = center_x - (launcher->cfg.tile_pic_w / 2) + i * stride - (int)(page->cam_x + 0.5f);
        int selected = (i == page->selected);
        int color = launcher->cfg.light_mode && selected ? 0 : 1;

        if ((x + launcher->cfg.tile_pic_w) < -2 || x > (OLED_UI_WIDTH + 2))
        {
            continue;
        }

        if (page->items[i].icon != RT_NULL)
        {
            page->items[i].icon(ui, launcher, &page->items[i], x, base_y, selected, page->items[i].icon_user);
        }
        else
        {
            oled_ui_astra_draw_tile_icon_default(ui, launcher, &page->items[i], x, base_y, selected, page->items[i].icon_user);
        }

        oled_ui_draw_round_rect(ui, x, base_y, launcher->cfg.tile_pic_w, launcher->cfg.tile_pic_h, 4, color);
    }

    if ((page->item_count > 0U) && (page->items[page->selected].title != RT_NULL))
    {
        oled_ui_draw_utf8_marquee(ui,
                                  text_x,
                                  text_y,
                                  text_w,
                                  page->items[page->selected].title,
                                  ASTRA_TEXT_SCALE,
                                  launcher->cfg.light_mode ? 0 : 1,
                                  launcher->tick * 3U);
    }

    astra_draw_tile_bar(ui, launcher, page, OLED_UI_HEIGHT - launcher->cfg.tile_bar_height - 1);
}

static void astra_render_popup(oled_ui_t *ui, oled_ui_astra_launcher_t *launcher)
{
    int h = 18;
    int y = (int)(launcher->pop_y + 0.5f);
    int x = launcher->cfg.popup_margin;
    int w = OLED_UI_WIDTH - launcher->cfg.popup_margin * 2;
    int radius = launcher->cfg.popup_radius;

    if (launcher->pop_visible == 0U)
    {
        return;
    }

    oled_ui_fill_round_rect(ui, x, y, w, h, radius, 1);
    oled_ui_draw_utf8_text(ui, x + 6, y + 5, launcher->pop_text, ASTRA_TEXT_SCALE, 0);
}

static void astra_render_page_internal(oled_ui_t *ui,
                                       oled_ui_astra_launcher_t *launcher,
                                       oled_ui_astra_page_t *page)
{
    if ((ui == RT_NULL) || (launcher == RT_NULL) || (page == RT_NULL))
    {
        return;
    }

    if (page->render != RT_NULL)
    {
        page->render(ui, launcher, page, page->user);
        astra_render_popup(ui, launcher);
        return;
    }

    switch (page->type)
    {
    case OLED_UI_ASTRA_PAGE_TILE:
        astra_render_tile(ui, launcher, page);
        break;
    case OLED_UI_ASTRA_PAGE_LIST:
    case OLED_UI_ASTRA_PAGE_CUSTOM:
    default:
        astra_render_list(ui, launcher, page);
        break;
    }

    astra_render_popup(ui, launcher);
}

static void astra_page_render_bridge(oled_ui_t *ui, void *user)
{
    astra_render_ctx_t *ctx = (astra_render_ctx_t *)user;

    if ((ctx == RT_NULL) || (ctx->launcher == RT_NULL) || (ctx->page == RT_NULL))
    {
        return;
    }

    astra_render_page_internal(ui, ctx->launcher, ctx->page);
}

static void astra_page_apply_enter(oled_ui_astra_launcher_t *launcher, oled_ui_astra_page_t *page)
{
    if ((launcher == RT_NULL) || (page == RT_NULL))
    {
        return;
    }

    if (page->selected >= page->item_count && page->item_count > 0U)
    {
        page->selected = page->item_count - 1U;
    }

    astra_update_targets(launcher, page);
    page->cam_x = page->cam_x_target;
    page->cam_y = page->cam_y_target;
    launcher->selector_x = launcher->selector_x_target;
    launcher->selector_y = launcher->selector_y_target;
    launcher->selector_w = launcher->selector_w_target;
    launcher->selector_h = launcher->selector_h_target;
    launcher->selector_text_y = launcher->selector_text_y_target;

    if (page->on_enter != RT_NULL)
    {
        page->on_enter(launcher, page, page->user);
    }
}

void oled_ui_astra_config_default(oled_ui_astra_config_t *cfg)
{
    if (cfg == RT_NULL)
    {
        return;
    }

    cfg->tile_anim_speed = 14.0f;
    cfg->list_anim_speed = 15.0f;
    cfg->camera_anim_speed = 16.0f;
    cfg->selector_x_anim_speed = 18.0f;
    cfg->selector_y_anim_speed = 18.0f;
    cfg->selector_w_anim_speed = 16.0f;
    cfg->selector_h_anim_speed = 16.0f;
    cfg->popup_anim_speed = 18.0f;

    cfg->menu_loop = 1U;
    cfg->list_unfold = 1U;
    cfg->tile_unfold = 1U;
    cfg->light_mode = 1U;

    cfg->list_line_height = 14U;
    cfg->list_text_margin = 6U;
    cfg->list_bar_width = 3U;
    cfg->list_visible_count = 3U;
    cfg->list_selector_radius = 4U;

    cfg->tile_pic_w = 28U;
    cfg->tile_pic_h = 28U;
    cfg->tile_pic_gap = 12U;
    cfg->tile_pic_top = 16U;
    cfg->tile_text_bottom = 50U;
    cfg->tile_select_margin = 3U;
    cfg->tile_bar_height = 3U;
    cfg->tile_arrow_margin = 3U;
    cfg->tile_arrow_width = 4U;

    cfg->popup_margin = 8U;
    cfg->popup_radius = 4U;
}

void oled_ui_astra_page_init(oled_ui_astra_page_t *page,
                             const char *title,
                             const char *tag,
                             oled_ui_astra_page_type_t type)
{
    if (page == RT_NULL)
    {
        return;
    }

    memset(page, 0, sizeof(*page));
    page->title = title;
    page->tag = tag;
    page->type = type;
    page->list_line_height = 0U;
    page->list_visible_count = 0U;
    page->content_x = 0U;
    page->content_y = 14U;
    page->content_w = OLED_UI_WIDTH;
}

void oled_ui_astra_launcher_init(oled_ui_astra_launcher_t *launcher,
                                 oled_ui_t *ui,
                                 oled_ui_astra_page_t *root,
                                 const oled_ui_astra_config_t *cfg)
{
    if ((launcher == RT_NULL) || (ui == RT_NULL) || (root == RT_NULL))
    {
        return;
    }

    memset(launcher, 0, sizeof(*launcher));
    launcher->ui = ui;
    launcher->root = root;
    launcher->current = root;
    launcher->tick = 0U;
    launcher->transition_ms = 280U;
    launcher->transition_anim = OLED_UI_ANIM_PUSH_LEFT;
    launcher->pop_duration_ms = 900U;
    launcher->pop_visible = 0U;
    launcher->pop_y = OLED_UI_HEIGHT;
    launcher->pop_y_target = OLED_UI_HEIGHT;

    if (cfg != RT_NULL)
    {
        launcher->cfg = *cfg;
    }
    else
    {
        oled_ui_astra_config_default(&launcher->cfg);
    }

    astra_page_apply_enter(launcher, root);
}

void oled_ui_astra_widget_checkbox_bind(oled_ui_astra_widget_t *widget, rt_uint8_t *value)
{
    if (widget == RT_NULL)
    {
        return;
    }

    memset(widget, 0, sizeof(*widget));
    widget->type = OLED_UI_ASTRA_WIDGET_CHECKBOX;
    widget->data.checkbox.value = value;
}

void oled_ui_astra_widget_slider_bind(oled_ui_astra_widget_t *widget,
                                      rt_uint8_t *value,
                                      rt_uint8_t min,
                                      rt_uint8_t max,
                                      rt_uint8_t step)
{
    if (widget == RT_NULL)
    {
        return;
    }

    memset(widget, 0, sizeof(*widget));
    widget->type = OLED_UI_ASTRA_WIDGET_SLIDER;
    widget->data.slider.value = value;
    widget->data.slider.min = min;
    widget->data.slider.max = (max > min) ? max : (rt_uint8_t)(min + 1U);
    widget->data.slider.step = (step == 0U) ? 1U : step;
}

void oled_ui_astra_launcher_rotate(oled_ui_astra_launcher_t *launcher, rt_int32_t delta)
{
    oled_ui_astra_page_t *page;
    rt_bool_t handled = RT_FALSE;

    if (!astra_is_valid(launcher) || (delta == 0))
    {
        return;
    }

    page = launcher->current;
    if (page->input != RT_NULL)
    {
        handled = page->input(launcher, page, delta, RT_FALSE, page->user);
    }
    if (handled)
    {
        astra_update_targets(launcher, page);
        return;
    }

    if (page->item_count == 0U)
    {
        return;
    }

    if ((page->items != RT_NULL) && (page->selected < page->item_count))
    {
        oled_ui_astra_item_t *item = &page->items[page->selected];
        if ((item->widget != RT_NULL) && (item->widget->type == OLED_UI_ASTRA_WIDGET_SLIDER))
        {
            rt_uint8_t *val = item->widget->data.slider.value;
            int step = item->widget->data.slider.step;
            int min_v = item->widget->data.slider.min;
            int max_v = item->widget->data.slider.max;
            int next;

            if (val != RT_NULL)
            {
                if (delta > 0)
                {
                    next = *val + step;
                }
                else
                {
                    next = *val - step;
                }
                *val = (rt_uint8_t)astra_clampi(next, min_v, max_v);
                astra_update_targets(launcher, page);
                return;
            }
        }
    }

    page->selected = astra_step_index(launcher, page->selected, page->item_count, delta);
    astra_update_targets(launcher, page);
}

void oled_ui_astra_launcher_press(oled_ui_astra_launcher_t *launcher)
{
    oled_ui_astra_page_t *page;
    oled_ui_astra_item_t *item;
    rt_bool_t handled = RT_FALSE;

    if (!astra_is_valid(launcher))
    {
        return;
    }

    page = launcher->current;
    if (page->input != RT_NULL)
    {
        handled = page->input(launcher, page, 0, RT_TRUE, page->user);
    }
    if (handled)
    {
        return;
    }

    if ((page->items == RT_NULL) || (page->item_count == 0U) || (page->selected >= page->item_count))
    {
        return;
    }

    item = &page->items[page->selected];
    switch (item->type)
    {
    case OLED_UI_ASTRA_ITEM_MENU:
        if (item->target != RT_NULL)
        {
            oled_ui_astra_launcher_switch(launcher,
                                          item->target,
                                          OLED_UI_ANIM_PUSH_LEFT,
                                          launcher->transition_ms);
        }
        break;

    case OLED_UI_ASTRA_ITEM_BACK:
        oled_ui_astra_launcher_back(launcher);
        break;

    case OLED_UI_ASTRA_ITEM_ACTION:
    default:
        if (item->action != RT_NULL)
        {
            item->action(launcher, item, item->action_user);
        }
        break;
    }
}

void oled_ui_astra_launcher_back(oled_ui_astra_launcher_t *launcher)
{
    if (!astra_is_valid(launcher))
    {
        return;
    }

    if ((launcher->current->parent != RT_NULL) && (launcher->current != launcher->root))
    {
        oled_ui_astra_launcher_switch(launcher,
                                      launcher->current->parent,
                                      OLED_UI_ANIM_PUSH_RIGHT,
                                      launcher->transition_ms);
    }
}

void oled_ui_astra_launcher_switch(oled_ui_astra_launcher_t *launcher,
                                   oled_ui_astra_page_t *next,
                                   oled_ui_anim_t anim,
                                   rt_uint32_t duration_ms)
{
    oled_ui_astra_page_t *from;
    oled_ui_page_t from_page;
    oled_ui_page_t to_page;
    astra_render_ctx_t from_ctx;
    astra_render_ctx_t to_ctx;
    rt_uint32_t dur;

    if (!astra_is_valid(launcher) || (next == RT_NULL))
    {
        return;
    }

    from = launcher->current;
    if (from == next)
    {
        astra_update_targets(launcher, next);
        return;
    }

    launcher->current = next;
    astra_page_apply_enter(launcher, next);

    from_ctx.launcher = launcher;
    from_ctx.page = from;
    to_ctx.launcher = launcher;
    to_ctx.page = next;
    from_page.render = astra_page_render_bridge;
    from_page.user = &from_ctx;
    to_page.render = astra_page_render_bridge;
    to_page.user = &to_ctx;

    dur = (duration_ms == 0U) ? ASTRA_MIN_DURATION_MS : duration_ms;
    oled_ui_show_page(launcher->ui, &from_page, &to_page, anim, dur);
}

void oled_ui_astra_launcher_pop(oled_ui_astra_launcher_t *launcher,
                                const char *text,
                                rt_uint32_t duration_ms)
{
    if ((launcher == RT_NULL) || (text == RT_NULL))
    {
        return;
    }

    rt_strncpy(launcher->pop_text, text, sizeof(launcher->pop_text) - 1U);
    launcher->pop_text[sizeof(launcher->pop_text) - 1U] = '\0';
    launcher->pop_visible = 1U;
    launcher->pop_begin_tick = rt_tick_get();
    launcher->pop_duration_ms = duration_ms;
    if (launcher->pop_duration_ms == 0U)
    {
        launcher->pop_duration_ms = 900U;
    }
    if (launcher->pop_duration_ms > ASTRA_POPUP_MAX_MS)
    {
        launcher->pop_duration_ms = ASTRA_POPUP_MAX_MS;
    }
    launcher->pop_y = (float)OLED_UI_HEIGHT;
    launcher->pop_y_target = (float)(OLED_UI_HEIGHT - 22);
}

void oled_ui_astra_launcher_update(oled_ui_astra_launcher_t *launcher)
{
    static rt_tick_t s_last_tick = 0;
    rt_tick_t now;
    rt_uint32_t dt_ms;
    oled_ui_astra_page_t *page;
    rt_uint32_t alive_ms;

    if (!astra_is_valid(launcher))
    {
        return;
    }

    now = rt_tick_get();
    if (s_last_tick == 0)
    {
        s_last_tick = now;
    }
    dt_ms = (rt_uint32_t)(((now - s_last_tick) * 1000U) / RT_TICK_PER_SECOND);
    if (dt_ms > 50U)
    {
        dt_ms = 50U;
    }
    if (dt_ms == 0U)
    {
        dt_ms = 1U;
    }
    s_last_tick = now;
    launcher->tick++;

    page = launcher->current;
    astra_update_targets(launcher, page);

    page->cam_x = astra_lerp_to(page->cam_x, page->cam_x_target, launcher->cfg.camera_anim_speed, dt_ms);
    page->cam_y = astra_lerp_to(page->cam_y, page->cam_y_target, launcher->cfg.camera_anim_speed, dt_ms);
    launcher->selector_x = astra_lerp_to(launcher->selector_x,
                                         launcher->selector_x_target,
                                         launcher->cfg.selector_x_anim_speed,
                                         dt_ms);
    launcher->selector_y = astra_lerp_to(launcher->selector_y,
                                         launcher->selector_y_target,
                                         launcher->cfg.selector_y_anim_speed,
                                         dt_ms);
    launcher->selector_w = astra_lerp_to(launcher->selector_w,
                                         launcher->selector_w_target,
                                         launcher->cfg.selector_w_anim_speed,
                                         dt_ms);
    launcher->selector_h = astra_lerp_to(launcher->selector_h,
                                         launcher->selector_h_target,
                                         launcher->cfg.selector_h_anim_speed,
                                         dt_ms);
    launcher->selector_text_y = astra_lerp_to(launcher->selector_text_y,
                                              launcher->selector_text_y_target,
                                              launcher->cfg.selector_y_anim_speed,
                                              dt_ms);

    if (launcher->pop_visible != 0U)
    {
        alive_ms = (rt_uint32_t)(((now - launcher->pop_begin_tick) * 1000U) / RT_TICK_PER_SECOND);
        if (alive_ms >= launcher->pop_duration_ms)
        {
            launcher->pop_y_target = (float)OLED_UI_HEIGHT;
            if (astra_absf(launcher->pop_y - launcher->pop_y_target) < 0.8f)
            {
                launcher->pop_visible = 0U;
            }
        }
        launcher->pop_y = astra_lerp_to(launcher->pop_y,
                                        launcher->pop_y_target,
                                        launcher->cfg.popup_anim_speed,
                                        dt_ms);
    }

    oled_ui_select_buffer(launcher->ui, launcher->ui->front_buf);
    oled_ui_clear(launcher->ui, 0);
    astra_render_page_internal(launcher->ui, launcher, launcher->current);
    oled_ui_present(launcher->ui);
}

void oled_ui_astra_draw_tile_icon_default(oled_ui_t *ui,
                                          oled_ui_astra_launcher_t *launcher,
                                          oled_ui_astra_item_t *item,
                                          int x,
                                          int y,
                                          int selected,
                                          void *user)
{
    int c = (launcher->cfg.light_mode && selected) ? 0 : 1;

    RT_UNUSED(item);
    RT_UNUSED(user);

    oled_ui_draw_round_rect(ui, x + 3, y + 3, launcher->cfg.tile_pic_w - 6, launcher->cfg.tile_pic_h - 6, 4, c);
    oled_ui_draw_line(ui, x + 6, y + 9, x + launcher->cfg.tile_pic_w - 7, y + 9, c);
    oled_ui_draw_line(ui, x + 6, y + 14, x + launcher->cfg.tile_pic_w - 10, y + 14, c);
    oled_ui_draw_line(ui, x + 6, y + 19, x + launcher->cfg.tile_pic_w - 13, y + 19, c);
}
