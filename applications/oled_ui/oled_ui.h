/*
 * SSD1306 OLED UI library.
 */

#ifndef __OLED_UI_H__
#define __OLED_UI_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OLED_UI_WIDTH     128
#define OLED_UI_HEIGHT    64
#define OLED_UI_PAGES     (OLED_UI_HEIGHT / 8)
#define OLED_UI_BUF_SIZE  (OLED_UI_WIDTH * OLED_UI_PAGES)

typedef struct oled_ui oled_ui_t;

typedef enum
{
    OLED_UI_ANIM_NONE = 0,
    OLED_UI_ANIM_PUSH_LEFT,
    OLED_UI_ANIM_PUSH_RIGHT,
    OLED_UI_ANIM_PUSH_UP,
    OLED_UI_ANIM_PUSH_DOWN,
} oled_ui_anim_t;

typedef void (*oled_ui_render_cb)(oled_ui_t *ui, void *user);

typedef struct
{
    oled_ui_render_cb render;
    void *user;
} oled_ui_page_t;

struct oled_ui
{
    struct rt_spi_device *spi;
    rt_base_t dc_pin;
    rt_base_t rst_pin;
    rt_uint32_t present_count;
    rt_uint8_t *draw_buf;
    rt_uint8_t *shadow_buf;
    rt_uint8_t *front_buf;
    rt_uint8_t *back_buf;
    rt_uint8_t *scratch_buf;
};

rt_err_t oled_ui_init(oled_ui_t *ui,
                      const char *bus_name,
                      const char *dev_name,
                      rt_base_t cs_pin,
                      rt_base_t dc_pin,
                      rt_base_t rst_pin);

void oled_ui_deinit(oled_ui_t *ui);
void oled_ui_select_buffer(oled_ui_t *ui, rt_uint8_t *buffer);
void oled_ui_clear(oled_ui_t *ui, rt_uint8_t color);
void oled_ui_draw_pixel(oled_ui_t *ui, int x, int y, int color);
void oled_ui_draw_line(oled_ui_t *ui, int x0, int y0, int x1, int y1, int color);
void oled_ui_draw_rect(oled_ui_t *ui, int x, int y, int w, int h, int color);
void oled_ui_fill_rect(oled_ui_t *ui, int x, int y, int w, int h, int color);
void oled_ui_draw_round_rect(oled_ui_t *ui, int x, int y, int w, int h, int radius, int color);
void oled_ui_fill_round_rect(oled_ui_t *ui, int x, int y, int w, int h, int radius, int color);
void oled_ui_draw_char(oled_ui_t *ui, int x, int y, char ch, int scale, int color);
void oled_ui_draw_text(oled_ui_t *ui, int x, int y, const char *text, int scale, int color);
void oled_ui_draw_utf8_text(oled_ui_t *ui, int x, int y, const char *text, int scale, int color);
int oled_ui_measure_utf8_width(const char *text, int scale);
void oled_ui_draw_utf8_marquee(oled_ui_t *ui,
                               int x,
                               int y,
                               int w,
                               const char *text,
                               int scale,
                               int color,
                               rt_uint32_t phase);
void oled_ui_draw_progress(oled_ui_t *ui, int x, int y, int w, int h, int value, int max_value, int color);
rt_err_t oled_ui_set_contrast(oled_ui_t *ui, rt_uint8_t contrast);
rt_err_t oled_ui_present(oled_ui_t *ui);
rt_uint32_t oled_ui_get_present_count(const oled_ui_t *ui);
rt_err_t oled_ui_show_page(oled_ui_t *ui,
                           const oled_ui_page_t *from,
                           const oled_ui_page_t *to,
                           oled_ui_anim_t anim,
                           rt_uint32_t duration_ms);

#ifdef __cplusplus
}
#endif

#endif /* __OLED_UI_H__ */
