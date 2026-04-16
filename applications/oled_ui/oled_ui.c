/*
 * SSD1306 OLED UI library for RT-Thread on STM32F407VET6.
 */

#include "oled_ui.h"

#include <drv_gpio.h>
#include <stm32f4xx_hal.h>
#include <drv_spi.h>
#include <rthw.h>
#include <string.h>

#define DBG_TAG    "oled.ui"
#define DBG_LVL    DBG_INFO
#include <rtdbg.h>

#define OLED_CMD_DISPLAY_OFF   0xAE
#define OLED_CMD_DISPLAY_ON    0xAF
#define OLED_TRANSITION_X      0
#define OLED_TRANSITION_Y      11
#define OLED_TRANSITION_W      OLED_UI_WIDTH
#define OLED_TRANSITION_H      (OLED_UI_HEIGHT - OLED_TRANSITION_Y)

static const struct
{
    char ch;
    rt_uint8_t rows[7];
} s_font[] =
{
    { ' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00} },
    { '-', {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00} },
    { ':', {0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00} },
    { '/', {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00} },

    { '0', {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e} },
    { '1', {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e} },
    { '2', {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f} },
    { '3', {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e} },
    { '4', {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02} },
    { '5', {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e} },
    { '6', {0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e} },
    { '7', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08} },
    { '8', {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e} },
    { '9', {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e} },

    { 'A', {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11} },
    { 'B', {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e} },
    { 'C', {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e} },
    { 'D', {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e} },
    { 'E', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f} },
    { 'F', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10} },
    { 'G', {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0e} },
    { 'I', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f} },
    { 'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11} },
    { 'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f} },
    { 'M', {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11} },
    { 'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11} },
    { 'O', {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e} },
    { 'P', {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10} },
    { 'R', {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11} },
    { 'S', {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e} },
    { 'T', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04} },
    { 'V', {0x11, 0x11, 0x11, 0x11, 0x0a, 0x0a, 0x04} },
    { 'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e} },
    { 'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11} },
    { 'X', {0x11, 0x0a, 0x04, 0x04, 0x04, 0x0a, 0x11} },
};

static const rt_uint8_t s_bayer4x4[4][4] =
{
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 },
};

typedef struct
{
    rt_uint32_t code;
    rt_uint8_t rows[32];
} oled_cn_glyph_t;

static const oled_cn_glyph_t s_cn_font[] =
{
    { 0x4E3B, { 0x00, 0x00, 0x01, 0x00, 0x01, 0x80, 0x1F, 0xF8, 0x01, 0x80, 0x01, 0x80, 0x0F, 0xF0, 0x0F, 0xF0, 0x01, 0x80, 0x01, 0x80, 0x3F, 0xFC, 0x3F, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { 0x83DC, { 0x02, 0x00, 0x3F, 0xFC, 0x3F, 0xFC, 0x00, 0xF0, 0x1F, 0xF8, 0x09, 0x90, 0x0D, 0xB0, 0x3F, 0xFC, 0x3F, 0xFC, 0x05, 0xA0, 0x09, 0x9C, 0x31, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { 0x5355, { 0x04, 0x00, 0x06, 0x60, 0x0F, 0xF0, 0x0F, 0xF0, 0x09, 0x90, 0x0F, 0xF0, 0x09, 0x90, 0x0F, 0xF0, 0x01, 0x80, 0x3F, 0xFC, 0x01, 0x80, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { 0x72B6, { 0x04, 0x40, 0x04, 0x60, 0x24, 0x68, 0x14, 0x60, 0x15, 0xF8, 0x04, 0x60, 0x0C, 0x60, 0x1C, 0x60, 0x34, 0xF0, 0x04, 0x90, 0x05, 0x98, 0x07, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { 0x6001, { 0x01, 0x00, 0x01, 0x00, 0x3F, 0xFC, 0x02, 0x40, 0x06, 0x40, 0x0F, 0x30, 0x39, 0x1C, 0x00, 0x00, 0x10, 0x98, 0x16, 0x88, 0x36, 0x3C, 0x23, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { 0x8BBE, { 0x00, 0x00, 0x19, 0xF0, 0x0D, 0x30, 0x01, 0x30, 0x3B, 0x38, 0x3A, 0x18, 0x0B, 0xF0, 0x09, 0x10, 0x08, 0xA0, 0x0C, 0xE0, 0x08, 0xE0, 0x03, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { 0x7F6E, { 0x00, 0x00, 0x1F, 0xF8, 0x1F, 0xF8, 0x0F, 0xF8, 0x1F, 0xF8, 0x07, 0xE0, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x3F, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { 0x5173, { 0x00, 0x00, 0x06, 0x60, 0x02, 0x40, 0x1F, 0xF8, 0x01, 0x80, 0x01, 0x80, 0x1F, 0xF8, 0x1F, 0xF8, 0x01, 0x80, 0x02, 0x60, 0x0C, 0x38, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { 0x4E8E, { 0x00, 0x00, 0x1F, 0xF8, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x3F, 0xFC, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { 0x4EAE, { 0x01, 0x00, 0x1F, 0xF8, 0x1F, 0xF8, 0x07, 0xE0, 0x0C, 0x30, 0x07, 0xE0, 0x1F, 0xF8, 0x3F, 0xFC, 0x03, 0xC0, 0x06, 0x40, 0x06, 0x4C, 0x1C, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { 0x5EA6, { 0x00, 0x80, 0x00, 0x80, 0x1F, 0xF8, 0x1B, 0x20, 0x1F, 0xF8, 0x1B, 0x20, 0x13, 0xE0, 0x17, 0xF0, 0x17, 0xF0, 0x11, 0x20, 0x31, 0xC0, 0x2F, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { 0x52A8, { 0x00, 0x20, 0x00, 0x20, 0x1F, 0x20, 0x00, 0xF8, 0x00, 0x2C, 0x3F, 0x2C, 0x08, 0x2C, 0x1A, 0x6C, 0x13, 0x6C, 0x3F, 0x4C, 0x00, 0xCC, 0x01, 0x98, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { 0x753B, { 0x00, 0x00, 0x1F, 0xFC, 0x00, 0x00, 0x07, 0xE0, 0x05, 0xAC, 0x35, 0xAC, 0x37, 0xEC, 0x35, 0xAC, 0x37, 0xEC, 0x37, 0xEC, 0x3F, 0xFC, 0x1F, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { 0x8FD4, { 0x00, 0x00, 0x19, 0xF8, 0x0B, 0x00, 0x03, 0x00, 0x01, 0xF8, 0x3B, 0x10, 0x0B, 0x50, 0x0B, 0x30, 0x0A, 0x38, 0x0E, 0xC8, 0x1C, 0x80, 0x33, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { 0x56DE, { 0x00, 0x00, 0x1F, 0xF8, 0x10, 0x08, 0x10, 0x08, 0x17, 0xC8, 0x16, 0x48, 0x16, 0x48, 0x17, 0xC8, 0x10, 0x08, 0x10, 0x08, 0x1F, 0xF8, 0x10, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { 0x5F85, { 0x08, 0x20, 0x10, 0x20, 0x23, 0xFC, 0x40, 0x20, 0x88, 0x20, 0x1F, 0xFE, 0x10, 0x00, 0x30, 0x08, 0x77, 0xFE, 0x50, 0x08, 0x12, 0x08, 0x11, 0x08, 0x10, 0x88, 0x10, 0x08, 0x10, 0x78, 0x00, 0x00 } },
    { 0x529E, { 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x7F, 0xF0, 0x02, 0x10, 0x22, 0x14, 0x62, 0x12, 0x44, 0x12, 0xC4, 0x13, 0x84, 0x11, 0x0C, 0x10, 0x18, 0x10, 0x30, 0x10, 0x63, 0xE0, 0x00, 0x00, 0x00, 0x00 } },
    { 0x4E70, { 0x00, 0x00, 0x7F, 0xFE, 0x00, 0x02, 0x10, 0x84, 0x0C, 0x88, 0x62, 0x80, 0x18, 0x80, 0x04, 0x80, 0x00, 0x80, 0xFF, 0xFF, 0x01, 0x80, 0x03, 0xE0, 0x06, 0x38, 0x18, 0x0E, 0xE0, 0x03, 0x00, 0x00 } },
    { 0x7535, { 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x7F, 0xFC, 0x41, 0x04, 0x41, 0x04, 0x41, 0x04, 0x7F, 0xFC, 0x41, 0x04, 0x41, 0x04, 0x41, 0x04, 0x7F, 0xFC, 0x41, 0x02, 0x01, 0x02, 0x01, 0xFC, 0x00, 0x00 } },
    { 0x6C60, { 0x00, 0x00, 0x60, 0x40, 0x30, 0x40, 0x1A, 0x40, 0x02, 0x5C, 0x43, 0xE4, 0x2E, 0x44, 0x12, 0x44, 0x02, 0x44, 0x12, 0x5C, 0x12, 0x42, 0x22, 0x42, 0x22, 0x02, 0x62, 0x06, 0x41, 0xFC, 0x00, 0x00 } },
    { 0x5012, { 0x10, 0x02, 0x1F, 0xE2, 0x21, 0x0A, 0x22, 0x8A, 0x24, 0x4A, 0x6F, 0xCA, 0x61, 0x2A, 0xA1, 0x0A, 0xA1, 0x0A, 0x2F, 0xEA, 0x21, 0x0A, 0x21, 0x02, 0x21, 0xE2, 0x2F, 0x02, 0x20, 0x0E, 0x00, 0x00 } },
    { 0x5783, { 0x00, 0x00, 0x20, 0x40, 0x20, 0x20, 0x20, 0x20, 0x27, 0xFE, 0xF8, 0x00, 0x22, 0x04, 0x23, 0x0C, 0x21, 0x08, 0x21, 0x08, 0x20, 0x90, 0x38, 0x90, 0x60, 0x20, 0x80, 0x20, 0x0F, 0xFF, 0x00, 0x00 } },
    { 0x573E, { 0x00, 0x00, 0x10, 0x00, 0x11, 0xF8, 0x10, 0x88, 0x10, 0x98, 0x7C, 0x90, 0x10, 0x9E, 0x10, 0xC2, 0x10, 0xC6, 0x11, 0x64, 0x1D, 0x2C, 0x31, 0x18, 0x42, 0x38, 0x04, 0x66, 0x09, 0x83, 0x00, 0x00 } },
};

static void oled_write_cmds(oled_ui_t *ui, const rt_uint8_t *cmds, rt_size_t len)
{
    if (ui == RT_NULL || ui->spi == RT_NULL || len == 0)
    {
        return;
    }

    rt_pin_write(ui->dc_pin, PIN_LOW);
    rt_spi_send(ui->spi, cmds, len);
    rt_pin_write(ui->dc_pin, PIN_HIGH);
}

static void oled_write_cmd(oled_ui_t *ui, rt_uint8_t cmd)
{
    oled_write_cmds(ui, &cmd, 1);
}

static void oled_write_data(oled_ui_t *ui, const rt_uint8_t *data, rt_size_t len)
{
    if (ui == RT_NULL || ui->spi == RT_NULL || len == 0)
    {
        return;
    }

    rt_pin_write(ui->dc_pin, PIN_HIGH);
    rt_spi_send(ui->spi, data, len);
}

static void oled_reset(oled_ui_t *ui)
{
    rt_pin_write(ui->rst_pin, PIN_LOW);
    rt_thread_mdelay(20);
    rt_pin_write(ui->rst_pin, PIN_HIGH);
    rt_thread_mdelay(100);
}

static void oled_set_addr_range(oled_ui_t *ui,
                                rt_uint8_t col_start,
                                rt_uint8_t col_end,
                                rt_uint8_t page_start,
                                rt_uint8_t page_end)
{
    rt_uint8_t cmds[6];

    cmds[0] = 0x21;
    cmds[1] = col_start;
    cmds[2] = col_end;
    cmds[3] = 0x22;
    cmds[4] = page_start;
    cmds[5] = page_end;
    oled_write_cmds(ui, cmds, sizeof(cmds));
}

static rt_uint8_t oled_get_pixel(const rt_uint8_t *buf, int x, int y)
{
    rt_uint16_t index;

    if ((buf == RT_NULL) ||
        (x < 0) || (x >= OLED_UI_WIDTH) ||
        (y < 0) || (y >= OLED_UI_HEIGHT))
    {
        return 0;
    }

    index = (rt_uint16_t)x + (rt_uint16_t)(y / 8) * OLED_UI_WIDTH;
    return (buf[index] >> (y & 0x07)) & 0x01;
}

static void oled_set_pixel(rt_uint8_t *buf, int x, int y, int color)
{
    rt_uint16_t index;
    rt_uint8_t bit;

    if ((buf == RT_NULL) ||
        (x < 0) || (x >= OLED_UI_WIDTH) ||
        (y < 0) || (y >= OLED_UI_HEIGHT))
    {
        return;
    }

    index = (rt_uint16_t)x + (rt_uint16_t)(y / 8) * OLED_UI_WIDTH;
    bit = (rt_uint8_t)(1U << (y & 0x07));

    if (color)
    {
        buf[index] |= bit;
    }
    else
    {
        buf[index] &= (rt_uint8_t)~bit;
    }
}

static rt_uint32_t oled_bezier_axis(rt_uint32_t t, rt_uint32_t p1, rt_uint32_t p2)
{
    rt_uint32_t inv = 1000U - t;
    rt_uint64_t num;
    rt_uint64_t den = 1000ULL * 1000ULL * 1000ULL;

    num = 3ULL * inv * inv * t * p1;
    num += 3ULL * inv * t * t * p2;
    num += 1ULL * t * t * t * 1000ULL;

    return (rt_uint32_t)((num + (den / 2ULL)) / den);
}

static rt_uint32_t oled_bezier_ease(rt_uint32_t progress)
{
    rt_uint32_t low = 0U;
    rt_uint32_t high = 1000U;
    rt_uint32_t mid = 0U;
    rt_uint32_t i;

    if (progress >= 1000U)
    {
        return 1000;
    }

    /* cubic-bezier(0.25, 0.10, 0.25, 1.00) */
    for (i = 0; i < 12U; i++)
    {
        rt_uint32_t x;

        mid = (low + high) / 2U;
        x = oled_bezier_axis(mid, 250U, 250U);
        if (x < progress)
        {
            low = mid;
        }
        else
        {
            high = mid;
        }
    }

    return oled_bezier_axis((low + high) / 2U, 100U, 1000U);
}

static rt_uint32_t oled_ease(rt_uint32_t progress)
{
    if (progress >= 1000U)
    {
        return 1000;
    }

    return oled_bezier_ease(progress);
}

static void oled_copy_frame(rt_uint8_t *dst, const rt_uint8_t *src)
{
    memcpy(dst, src, OLED_UI_BUF_SIZE);
}

static void oled_copy_rect(rt_uint8_t *dst,
                           const rt_uint8_t *src,
                           int x0,
                           int y0,
                           int w,
                           int h)
{
    int x;
    int y;

    if ((dst == RT_NULL) || (src == RT_NULL) || (w <= 0) || (h <= 0))
    {
        return;
    }

    for (y = y0; y < (y0 + h); y++)
    {
        for (x = x0; x < (x0 + w); x++)
        {
            oled_set_pixel(dst, x, y, oled_get_pixel(src, x, y));
        }
    }
}

static void oled_blend_transition(rt_uint8_t *dst,
                                  const rt_uint8_t *from,
                                  const rt_uint8_t *to,
                                  oled_ui_anim_t anim,
                                  rt_uint32_t progress)
{
    rt_uint32_t eased = oled_ease(progress);
    int shift_x = (int)((OLED_TRANSITION_W * eased) / 1000U);
    int shift_y = (int)((OLED_TRANSITION_H * eased) / 1000U);
    int x, y;

    oled_copy_frame(dst, to);

    for (y = OLED_TRANSITION_Y; y < (OLED_TRANSITION_Y + OLED_TRANSITION_H); y++)
    {
        for (x = OLED_TRANSITION_X; x < (OLED_TRANSITION_X + OLED_TRANSITION_W); x++)
        {
            int local_x = x - OLED_TRANSITION_X;
            int local_y = y - OLED_TRANSITION_Y;
            rt_uint8_t from_pixel = 0;
            rt_uint8_t to_pixel = 0;
            rt_uint8_t pixel = 0;
            rt_uint32_t threshold = ((rt_uint32_t)s_bayer4x4[y & 0x03][x & 0x03] * 1000U) / 16U;
            rt_uint32_t in_level = progress * 2U;
            rt_uint32_t out_level;

            if (in_level > 1000U)
            {
                in_level = 1000U;
            }
            if (progress <= 500U)
            {
                out_level = 1000U;
            }
            else
            {
                out_level = (1000U - progress) * 2U;
            }

            switch (anim)
            {
            case OLED_UI_ANIM_PUSH_LEFT:
                from_pixel = oled_get_pixel(from, OLED_TRANSITION_X + local_x + shift_x, y);
                to_pixel = oled_get_pixel(to, OLED_TRANSITION_X + local_x + shift_x - OLED_TRANSITION_W, y);
                break;

            case OLED_UI_ANIM_PUSH_RIGHT:
                from_pixel = oled_get_pixel(from, OLED_TRANSITION_X + local_x - shift_x, y);
                to_pixel = oled_get_pixel(to, OLED_TRANSITION_X + local_x - shift_x + OLED_TRANSITION_W, y);
                break;

            case OLED_UI_ANIM_PUSH_UP:
                from_pixel = oled_get_pixel(from, x, OLED_TRANSITION_Y + local_y + shift_y);
                to_pixel = oled_get_pixel(to, x, OLED_TRANSITION_Y + local_y + shift_y - OLED_TRANSITION_H);
                break;

            case OLED_UI_ANIM_PUSH_DOWN:
                from_pixel = oled_get_pixel(from, x, OLED_TRANSITION_Y + local_y - shift_y);
                to_pixel = oled_get_pixel(to, x, OLED_TRANSITION_Y + local_y - shift_y + OLED_TRANSITION_H);
                break;

            case OLED_UI_ANIM_NONE:
            default:
                to_pixel = oled_get_pixel(to, x, y);
                break;
            }

            if (anim == OLED_UI_ANIM_NONE)
            {
                pixel = to_pixel;
            }
            else
            {
                rt_uint8_t from_visible = (rt_uint8_t)(from_pixel && (threshold <= out_level));
                rt_uint8_t to_visible = (rt_uint8_t)(to_pixel && (threshold <= in_level));
                pixel = (rt_uint8_t)(from_visible || to_visible);
            }

            oled_set_pixel(dst, x, y, pixel);
        }
    }
}

static rt_err_t oled_find_dirty_span_in_page(const rt_uint8_t *curr,
                                             const rt_uint8_t *prev,
                                             rt_uint8_t page,
                                             rt_uint8_t *x0,
                                             rt_uint8_t *x1)
{
    rt_uint16_t base;
    rt_uint8_t x;
    rt_uint8_t min_x = OLED_UI_WIDTH - 1;
    rt_uint8_t max_x = 0;
    rt_bool_t dirty = RT_FALSE;

    if ((curr == RT_NULL) || (prev == RT_NULL) ||
        (x0 == RT_NULL) || (x1 == RT_NULL) ||
        (page >= OLED_UI_PAGES))
    {
        return -RT_EINVAL;
    }

    base = (rt_uint16_t)page * OLED_UI_WIDTH;
    for (x = 0; x < OLED_UI_WIDTH; x++)
    {
        rt_uint16_t idx = base + x;

        if (curr[idx] != prev[idx])
        {
            if (!dirty)
            {
                min_x = x;
                max_x = x;
                dirty = RT_TRUE;
            }
            else
            {
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
            }
        }
    }

    if (!dirty)
    {
        return -RT_ENOSYS;
    }

    *x0 = min_x;
    *x1 = max_x;
    return RT_EOK;
}

static const rt_uint8_t *oled_find_glyph(char ch)
{
    rt_size_t i;

    if (ch >= 'a' && ch <= 'z')
    {
        ch = (char)(ch - ('a' - 'A'));
    }

    for (i = 0; i < sizeof(s_font) / sizeof(s_font[0]); i++)
    {
        if (s_font[i].ch == ch)
        {
            return s_font[i].rows;
        }
    }

    return s_font[0].rows;
}

static rt_uint32_t oled_utf8_decode(const char **text)
{
    const rt_uint8_t *p;
    rt_uint32_t code = 0;

    if ((text == RT_NULL) || (*text == RT_NULL))
    {
        return 0;
    }

    p = (const rt_uint8_t *)(*text);
    if (*p == '\0')
    {
        return 0;
    }

    if (p[0] < 0x80U)
    {
        *text += 1;
        return p[0];
    }

    if (((p[0] & 0xE0U) == 0xC0U) && ((p[1] & 0xC0U) == 0x80U))
    {
        code = ((rt_uint32_t)(p[0] & 0x1FU) << 6) | (rt_uint32_t)(p[1] & 0x3FU);
        *text += 2;
        return code;
    }

    if (((p[0] & 0xF0U) == 0xE0U) &&
        ((p[1] & 0xC0U) == 0x80U) &&
        ((p[2] & 0xC0U) == 0x80U))
    {
        code = ((rt_uint32_t)(p[0] & 0x0FU) << 12) |
               ((rt_uint32_t)(p[1] & 0x3FU) << 6) |
               (rt_uint32_t)(p[2] & 0x3FU);
        *text += 3;
        return code;
    }

    if (((p[0] & 0xF8U) == 0xF0U) &&
        ((p[1] & 0xC0U) == 0x80U) &&
        ((p[2] & 0xC0U) == 0x80U) &&
        ((p[3] & 0xC0U) == 0x80U))
    {
        code = ((rt_uint32_t)(p[0] & 0x07U) << 18) |
               ((rt_uint32_t)(p[1] & 0x3FU) << 12) |
               ((rt_uint32_t)(p[2] & 0x3FU) << 6) |
               (rt_uint32_t)(p[3] & 0x3FU);
        *text += 4;
        return code;
    }

    *text += 1;
    return 0xFFFD;
}

static const oled_cn_glyph_t *oled_find_cn_glyph(rt_uint32_t code)
{
    rt_size_t i;

    for (i = 0; i < sizeof(s_cn_font) / sizeof(s_cn_font[0]); i++)
    {
        if (s_cn_font[i].code == code)
        {
            return &s_cn_font[i];
        }
    }

    return RT_NULL;
}

static void oled_draw_cn_glyph(oled_ui_t *ui, int x, int y, const rt_uint8_t *bitmap, int scale, int color)
{
    int row, col;

    if ((ui == RT_NULL) || (bitmap == RT_NULL) || (scale <= 0))
    {
        return;
    }

    for (row = 0; row < 16; row++)
    {
        for (col = 0; col < 16; col++)
        {
            rt_uint8_t bit = bitmap[row * 2 + (col / 8)];
            if (bit & (rt_uint8_t)(0x80U >> (col & 0x07)))
            {
                oled_ui_fill_rect(ui, x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

static void oled_draw_unknown_glyph(oled_ui_t *ui, int x, int y, int scale, int color)
{
    int size;

    if ((ui == RT_NULL) || (scale <= 0))
    {
        return;
    }

    size = 12 * scale;
    oled_ui_draw_rect(ui, x + scale, y + scale, size, size, color);
    oled_ui_draw_line(ui, x + scale * 2, y + scale * 2, x + size - scale, y + size - scale, color);
    oled_ui_draw_line(ui, x + size - scale, y + scale * 2, x + scale * 2, y + size - scale, color);
}

static int oled_glyph_advance(rt_uint32_t code, int scale)
{
    if (scale <= 0)
    {
        return 0;
    }

    return (code < 0x80U) ? (6 * scale) : (16 * scale);
}

static void oled_ui_fill_rect_clip(oled_ui_t *ui,
                                   int clip_x,
                                   int clip_w,
                                   int x,
                                   int y,
                                   int w,
                                   int h,
                                   int color)
{
    int row;
    int col;

    if ((ui == RT_NULL) || (clip_w <= 0) || (w <= 0) || (h <= 0))
    {
        return;
    }

    for (row = 0; row < h; row++)
    {
        int py = y + row;

        for (col = 0; col < w; col++)
        {
            int px = x + col;

            if ((px >= clip_x) && (px < (clip_x + clip_w)))
            {
                oled_ui_draw_pixel(ui, px, py, color);
            }
        }
    }
}

static void oled_ui_draw_char_clip(oled_ui_t *ui,
                                   int clip_x,
                                   int clip_w,
                                   int x,
                                   int y,
                                   char ch,
                                   int scale,
                                   int color)
{
    const rt_uint8_t *glyph;
    int row, col;

    if ((ui == RT_NULL) || (ui->draw_buf == RT_NULL) || (scale <= 0))
    {
        return;
    }

    glyph = oled_find_glyph(ch);
    for (row = 0; row < 7; row++)
    {
        for (col = 0; col < 5; col++)
        {
            if (glyph[row] & (1U << (4 - col)))
            {
                oled_ui_fill_rect_clip(ui,
                                       clip_x,
                                       clip_w,
                                       x + col * scale,
                                       y + row * scale,
                                       scale,
                                       scale,
                                       color);
            }
        }
    }
}

static void oled_draw_cn_glyph_clip(oled_ui_t *ui,
                                    int clip_x,
                                    int clip_w,
                                    int x,
                                    int y,
                                    const rt_uint8_t *bitmap,
                                    int scale,
                                    int color)
{
    int row, col;

    if ((ui == RT_NULL) || (bitmap == RT_NULL) || (scale <= 0))
    {
        return;
    }

    for (row = 0; row < 16; row++)
    {
        for (col = 0; col < 16; col++)
        {
            rt_uint8_t bit = bitmap[row * 2 + (col / 8)];
            if (bit & (rt_uint8_t)(0x80U >> (col & 0x07)))
            {
                oled_ui_fill_rect_clip(ui,
                                       clip_x,
                                       clip_w,
                                       x + col * scale,
                                       y + row * scale,
                                       scale,
                                       scale,
                                       color);
            }
        }
    }
}

static void oled_ui_draw_utf8_text_clip(oled_ui_t *ui,
                                        int clip_x,
                                        int clip_w,
                                        int x,
                                        int y,
                                        const char *text,
                                        int scale,
                                        int color)
{
    int cursor_x = x;

    if ((ui == RT_NULL) || (text == RT_NULL) || (scale <= 0))
    {
        return;
    }

    while (*text != '\0')
    {
        rt_uint32_t code;
        const oled_cn_glyph_t *glyph;
        const char *next = text;

        if (*text == '\r')
        {
            text++;
            continue;
        }

        if (*text == '\n')
        {
            break;
        }

        code = oled_utf8_decode(&next);
        if (code == 0)
        {
            break;
        }
        text = next;

        if (code < 0x80U)
        {
            oled_ui_draw_char_clip(ui, clip_x, clip_w, cursor_x, y, (char)code, scale, color);
        }
        else
        {
            glyph = oled_find_cn_glyph(code);
            if (glyph != RT_NULL)
            {
                oled_draw_cn_glyph_clip(ui, clip_x, clip_w, cursor_x, y, glyph->rows, scale, color);
            }
            else
            {
                oled_draw_unknown_glyph(ui, cursor_x, y, scale, color);
            }
        }

        cursor_x += oled_glyph_advance(code, scale);
    }
}

void oled_ui_draw_utf8_text(oled_ui_t *ui, int x, int y, const char *text, int scale, int color)
{
    int cursor_x = x;
    int cursor_y = y;

    if ((ui == RT_NULL) || (text == RT_NULL) || (scale <= 0))
    {
        return;
    }

    while (*text != '\0')
    {
        rt_uint32_t code;
        const oled_cn_glyph_t *glyph;
        const char *next = text;

        if (*text == '\r')
        {
            text++;
            continue;
        }

        if (*text == '\n')
        {
            cursor_x = x;
            cursor_y += (16 * scale);
            text++;
            continue;
        }

        code = oled_utf8_decode(&next);
        if (code == 0)
        {
            break;
        }
        text = next;

        if (code < 0x80U)
        {
            oled_ui_draw_char(ui, cursor_x, cursor_y, (char)code, scale, color);
            cursor_x += (6 * scale);
        }
        else
        {
            glyph = oled_find_cn_glyph(code);
            if (glyph != RT_NULL)
            {
                oled_draw_cn_glyph(ui, cursor_x, cursor_y, glyph->rows, scale, color);
            }
            else
            {
                oled_draw_unknown_glyph(ui, cursor_x, cursor_y, scale, color);
            }
            cursor_x += (16 * scale);
        }
    }
}

int oled_ui_measure_utf8_width(const char *text, int scale)
{
    int width = 0;

    if ((text == RT_NULL) || (scale <= 0))
    {
        return 0;
    }

    while (*text != '\0')
    {
        const char *next = text;
        rt_uint32_t code;

        if ((*text == '\r') || (*text == '\n'))
        {
            break;
        }

        code = oled_utf8_decode(&next);
        if (code == 0)
        {
            break;
        }

        width += oled_glyph_advance(code, scale);
        text = next;
    }

    return width;
}

void oled_ui_draw_utf8_marquee(oled_ui_t *ui,
                               int x,
                               int y,
                               int w,
                               const char *text,
                               int scale,
                               int color,
                               rt_uint32_t phase)
{
    int text_w;
    int gap;
    int offset;
    int overflow;
    int move_frames;
    int hold_frames;
    int cycle;
    int frame;

    if ((ui == RT_NULL) || (text == RT_NULL) || (w <= 0))
    {
        return;
    }

    text_w = oled_ui_measure_utf8_width(text, scale);
    if (text_w <= w)
    {
        oled_ui_draw_utf8_text(ui, x, y, text, scale, color);
        return;
    }

    overflow = text_w - w;
    if (overflow <= 0)
    {
        oled_ui_draw_utf8_text(ui, x, y, text, scale, color);
        return;
    }

    gap = 12 * scale;
    hold_frames = 10;
    move_frames = overflow * 2;
    if (move_frames < 18)
    {
        move_frames = 18;
    }

    cycle = hold_frames + move_frames + hold_frames + move_frames;
    frame = (int)(phase % (rt_uint32_t)cycle);

    if (frame < hold_frames)
    {
        offset = 0;
    }
    else if (frame < (hold_frames + move_frames))
    {
        rt_uint32_t progress = (rt_uint32_t)((frame - hold_frames) * 1000 / move_frames);
        offset = (overflow * (int)oled_bezier_ease(progress)) / 1000;
    }
    else if (frame < (hold_frames + move_frames + hold_frames))
    {
        offset = overflow;
    }
    else
    {
        rt_uint32_t progress = (rt_uint32_t)((frame - hold_frames - move_frames - hold_frames) * 1000 / move_frames);
        offset = overflow - ((overflow * (int)oled_bezier_ease(progress)) / 1000);
    }

    oled_ui_draw_utf8_text_clip(ui, x, w, x - offset, y, text, scale, color);
    oled_ui_draw_utf8_text_clip(ui, x, w, x - offset + text_w + gap, y, text, scale, color);
}

rt_err_t oled_ui_init(oled_ui_t *ui,
                      const char *bus_name,
                      const char *dev_name,
                      rt_base_t cs_pin,
                      rt_base_t dc_pin,
                      rt_base_t rst_pin)
{
    struct rt_spi_configuration cfg;
    rt_err_t result;

    if ((ui == RT_NULL) || (bus_name == RT_NULL) || (dev_name == RT_NULL))
    {
        return -RT_EINVAL;
    }

    rt_memset(ui, 0, sizeof(*ui));

    ui->shadow_buf = (rt_uint8_t *)rt_calloc(1, OLED_UI_BUF_SIZE);
    ui->front_buf = (rt_uint8_t *)rt_calloc(1, OLED_UI_BUF_SIZE);
    ui->back_buf = (rt_uint8_t *)rt_calloc(1, OLED_UI_BUF_SIZE);
    ui->scratch_buf = (rt_uint8_t *)rt_calloc(1, OLED_UI_BUF_SIZE);
    if ((ui->shadow_buf == RT_NULL) || (ui->front_buf == RT_NULL) ||
        (ui->back_buf == RT_NULL) || (ui->scratch_buf == RT_NULL))
    {
        oled_ui_deinit(ui);
        return -RT_ENOMEM;
    }

    ui->dc_pin = dc_pin;
    ui->rst_pin = rst_pin;
    ui->draw_buf = ui->front_buf;

    rt_pin_mode(dc_pin, PIN_MODE_OUTPUT);
    rt_pin_mode(rst_pin, PIN_MODE_OUTPUT);
    rt_pin_write(dc_pin, PIN_HIGH);
    rt_pin_write(rst_pin, PIN_HIGH);

    result = rt_hw_spi_device_attach(bus_name, dev_name, cs_pin);
    if (result != RT_EOK)
    {
        /* Device may already be attached by earlier init path. */
        ui->spi = (struct rt_spi_device *)rt_device_find(dev_name);
        if (ui->spi == RT_NULL)
        {
            LOG_E("attach %s to %s failed: %d", dev_name, bus_name, result);
            oled_ui_deinit(ui);
            return result;
        }
    }
    else
    {
        ui->spi = (struct rt_spi_device *)rt_device_find(dev_name);
    }

    if (ui->spi == RT_NULL)
    {
        oled_ui_deinit(ui);
        return -RT_ERROR;
    }

    cfg.mode = RT_SPI_MODE_0 | RT_SPI_MSB;
    cfg.data_width = 8;
    cfg.max_hz = 24 * 1000 * 1000;

    result = rt_spi_configure(ui->spi, &cfg);
    if (result != RT_EOK)
    {
        LOG_E("spi configure failed: %d", result);
        oled_ui_deinit(ui);
        return result;
    }

    oled_reset(ui);

    {
        static const rt_uint8_t init_cmds[] =
        {
            OLED_CMD_DISPLAY_OFF,
            0xD5, 0x80,
            0xA8, 0x3F,
            0xD3, 0x00,
            0x40,
            0x8D, 0x14,
            0x20, 0x00,
            0xA1,
            0xC8,
            0xDA, 0x12,
            0x81, 0xFF,
            0xD9, 0xF1,
            0xDB, 0x40,
            0xA4,
            0xA6,
            0x2E,
            OLED_CMD_DISPLAY_ON,
        };

        oled_write_cmds(ui, init_cmds, sizeof(init_cmds));
    }

    oled_ui_clear(ui, 0);
    return oled_ui_present(ui);
}

void oled_ui_deinit(oled_ui_t *ui)
{
    if (ui == RT_NULL)
    {
        return;
    }

    if (ui->front_buf != RT_NULL)
    {
        rt_free(ui->front_buf);
    }

    if (ui->shadow_buf != RT_NULL)
    {
        rt_free(ui->shadow_buf);
    }

    if (ui->back_buf != RT_NULL)
    {
        rt_free(ui->back_buf);
    }

    if (ui->scratch_buf != RT_NULL)
    {
        rt_free(ui->scratch_buf);
    }

    rt_memset(ui, 0, sizeof(*ui));
}

void oled_ui_select_buffer(oled_ui_t *ui, rt_uint8_t *buffer)
{
    if (ui == RT_NULL)
    {
        return;
    }

    ui->draw_buf = (buffer != RT_NULL) ? buffer : ui->front_buf;
}

void oled_ui_clear(oled_ui_t *ui, rt_uint8_t color)
{
    if ((ui == RT_NULL) || (ui->draw_buf == RT_NULL))
    {
        return;
    }

    memset(ui->draw_buf, color ? 0xFF : 0x00, OLED_UI_BUF_SIZE);
}

void oled_ui_draw_pixel(oled_ui_t *ui, int x, int y, int color)
{
    rt_uint16_t index;
    rt_uint8_t bit;

    if ((ui == RT_NULL) || (ui->draw_buf == RT_NULL))
    {
        return;
    }

    if ((x < 0) || (x >= OLED_UI_WIDTH) || (y < 0) || (y >= OLED_UI_HEIGHT))
    {
        return;
    }

    index = (rt_uint16_t)x + (rt_uint16_t)(y / 8) * OLED_UI_WIDTH;
    bit = (rt_uint8_t)(1U << (y & 0x07));

    if (color)
    {
        ui->draw_buf[index] |= bit;
    }
    else
    {
        ui->draw_buf[index] &= (rt_uint8_t)~bit;
    }
}

void oled_ui_draw_line(oled_ui_t *ui, int x0, int y0, int x1, int y1, int color)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -((y1 > y0) ? (y1 - y0) : (y0 - y1));
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (1)
    {
        oled_ui_draw_pixel(ui, x0, y0, color);
        if ((x0 == x1) && (y0 == y1))
        {
            break;
        }

        if (2 * err >= dy)
        {
            err += dy;
            x0 += sx;
        }

        if (2 * err <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void oled_ui_draw_rect(oled_ui_t *ui, int x, int y, int w, int h, int color)
{
    if ((w <= 0) || (h <= 0))
    {
        return;
    }

    oled_ui_draw_line(ui, x, y, x + w - 1, y, color);
    oled_ui_draw_line(ui, x, y + h - 1, x + w - 1, y + h - 1, color);
    oled_ui_draw_line(ui, x, y, x, y + h - 1, color);
    oled_ui_draw_line(ui, x + w - 1, y, x + w - 1, y + h - 1, color);
}

void oled_ui_fill_rect(oled_ui_t *ui, int x, int y, int w, int h, int color)
{
    int row;

    if ((w <= 0) || (h <= 0))
    {
        return;
    }

    for (row = 0; row < h; row++)
    {
        oled_ui_draw_line(ui, x, y + row, x + w - 1, y + row, color);
    }
}

void oled_ui_fill_round_rect(oled_ui_t *ui, int x, int y, int w, int h, int radius, int color)
{
    int row;
    int rad;

    if ((w <= 0) || (h <= 0))
    {
        return;
    }

    rad = radius;
    if (rad < 1)
    {
        oled_ui_fill_rect(ui, x, y, w, h, color);
        return;
    }

    if (rad > (w / 2))
    {
        rad = w / 2;
    }
    if (rad > (h / 2))
    {
        rad = h / 2;
    }

    for (row = 0; row < h; row++)
    {
        int inset = 0;

        if (row < rad)
        {
            int dy = (rad - 1) - row;
            int rx = rad - 1;
            int circle = (rad - 1) * (rad - 1);

            while ((rx > 0) && ((rx * rx) + (dy * dy) > circle))
            {
                rx--;
            }
            inset = (rad - 1) - rx;
        }
        else if (row >= (h - rad))
        {
            int dy = row - (h - rad);
            int rx = rad - 1;
            int circle = (rad - 1) * (rad - 1);

            while ((rx > 0) && ((rx * rx) + (dy * dy) > circle))
            {
                rx--;
            }
            inset = (rad - 1) - rx;
        }

        oled_ui_draw_line(ui, x + inset, y + row, x + w - 1 - inset, y + row, color);
    }
}

void oled_ui_draw_round_rect(oled_ui_t *ui, int x, int y, int w, int h, int radius, int color)
{
    int rad;
    int row;

    if ((w <= 0) || (h <= 0))
    {
        return;
    }

    rad = radius;
    if (rad < 1)
    {
        oled_ui_draw_rect(ui, x, y, w, h, color);
        return;
    }

    if (rad > (w / 2))
    {
        rad = w / 2;
    }
    if (rad > (h / 2))
    {
        rad = h / 2;
    }

    oled_ui_draw_line(ui, x + rad, y, x + w - 1 - rad, y, color);
    oled_ui_draw_line(ui, x + rad, y + h - 1, x + w - 1 - rad, y + h - 1, color);
    oled_ui_draw_line(ui, x, y + rad, x, y + h - 1 - rad, color);
    oled_ui_draw_line(ui, x + w - 1, y + rad, x + w - 1, y + h - 1 - rad, color);

    for (row = 0; row < rad; row++)
    {
        int dy_top = (rad - 1) - row;
        int dy_bottom = row;
        int rx = rad - 1;
        int circle = (rad - 1) * (rad - 1);
        int inset;

        while ((rx > 0) && ((rx * rx) + (dy_top * dy_top) > circle))
        {
            rx--;
        }

        inset = (rad - 1) - rx;
        oled_ui_draw_pixel(ui, x + inset, y + row, color);
        oled_ui_draw_pixel(ui, x + w - 1 - inset, y + row, color);
        oled_ui_draw_pixel(ui, x + inset, y + h - 1 - row, color);
        oled_ui_draw_pixel(ui, x + w - 1 - inset, y + h - 1 - row, color);

        RT_UNUSED(dy_bottom);
    }
}

void oled_ui_draw_char(oled_ui_t *ui, int x, int y, char ch, int scale, int color)
{
    const rt_uint8_t *glyph;
    int row, col;

    if ((ui == RT_NULL) || (ui->draw_buf == RT_NULL) || (scale <= 0))
    {
        return;
    }

    glyph = oled_find_glyph(ch);

    for (row = 0; row < 7; row++)
    {
        for (col = 0; col < 5; col++)
        {
            if (glyph[row] & (1U << (4 - col)))
            {
                oled_ui_fill_rect(ui, x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

void oled_ui_draw_text(oled_ui_t *ui, int x, int y, const char *text, int scale, int color)
{
    int cursor_x = x;

    if ((ui == RT_NULL) || (text == RT_NULL))
    {
        return;
    }

    while (*text != '\0')
    {
        if (*text == '\n')
        {
            cursor_x = x;
            y += (8 * scale);
        }
        else
        {
            oled_ui_draw_char(ui, cursor_x, y, *text, scale, color);
            cursor_x += (6 * scale);
        }

        text++;
    }
}

void oled_ui_draw_progress(oled_ui_t *ui, int x, int y, int w, int h, int value, int max_value, int color)
{
    int fill_w;
    int radius;

    if (max_value <= 0)
    {
        return;
    }

    if (value < 0)
    {
        value = 0;
    }

    if (value > max_value)
    {
        value = max_value;
    }

    radius = h / 2;
    fill_w = ((w - 2) * value) / max_value;
    oled_ui_draw_round_rect(ui, x, y, w, h, radius, color);
    if (fill_w > 0)
    {
        int inner_radius = (radius > 1) ? (radius - 1) : 1;
        oled_ui_fill_round_rect(ui, x + 1, y + 1, fill_w, h - 2, inner_radius, color);
    }
}

rt_err_t oled_ui_set_contrast(oled_ui_t *ui, rt_uint8_t contrast)
{
    rt_uint8_t cmds[2];

    if ((ui == RT_NULL) || (ui->spi == RT_NULL))
    {
        return -RT_EINVAL;
    }

    cmds[0] = 0x81;
    cmds[1] = contrast;
    oled_write_cmds(ui, cmds, sizeof(cmds));
    return RT_EOK;
}

rt_err_t oled_ui_present(oled_ui_t *ui)
{
    rt_uint8_t page;
    rt_bool_t dirty = RT_FALSE;

    if ((ui == RT_NULL) || (ui->spi == RT_NULL) || (ui->draw_buf == RT_NULL))
    {
        return -RT_EINVAL;
    }

    if (ui->shadow_buf == RT_NULL)
    {
        return -RT_EINVAL;
    }

    for (page = 0; page < OLED_UI_PAGES; page++)
    {
        rt_uint8_t x0;
        rt_uint8_t x1;
        rt_err_t ret = oled_find_dirty_span_in_page(ui->draw_buf, ui->shadow_buf, page, &x0, &x1);

        if (ret == -RT_ENOSYS)
        {
            continue;
        }
        if (ret != RT_EOK)
        {
            return ret;
        }

        {
            rt_size_t offset = (rt_size_t)page * OLED_UI_WIDTH + x0;
            rt_size_t len = (rt_size_t)(x1 - x0 + 1U);

            oled_set_addr_range(ui, x0, x1, page, page);
            oled_write_data(ui, ui->draw_buf + offset, len);
            memcpy(ui->shadow_buf + offset, ui->draw_buf + offset, len);
            dirty = RT_TRUE;
        }
    }

    if (!dirty)
    {
        return RT_EOK;
    }

    ui->present_count++;
    return RT_EOK;
}

rt_uint32_t oled_ui_get_present_count(const oled_ui_t *ui)
{
    if (ui == RT_NULL)
    {
        return 0;
    }

    return ui->present_count;
}

rt_err_t oled_ui_show_page(oled_ui_t *ui,
                           const oled_ui_page_t *from,
                           const oled_ui_page_t *to,
                           oled_ui_anim_t anim,
                           rt_uint32_t duration_ms)
{
    rt_tick_t start_tick;
    rt_uint32_t elapsed_ms;
    rt_uint32_t progress = 0U;

    if ((ui == RT_NULL) || (to == RT_NULL) || (to->render == RT_NULL))
    {
        return -RT_EINVAL;
    }

    if ((from == RT_NULL) || (from->render == RT_NULL) || (anim == OLED_UI_ANIM_NONE) || (duration_ms == 0))
    {
        oled_ui_select_buffer(ui, ui->front_buf);
        oled_ui_clear(ui, 0);
        to->render(ui, to->user);
        return oled_ui_present(ui);
    }

    oled_ui_select_buffer(ui, ui->front_buf);
    oled_ui_clear(ui, 0);
    from->render(ui, from->user);

    oled_ui_select_buffer(ui, ui->back_buf);
    oled_ui_clear(ui, 0);
    to->render(ui, to->user);

    start_tick = rt_tick_get();
    while (1)
    {
        elapsed_ms = (rt_uint32_t)(((rt_tick_get() - start_tick) * 1000U) / RT_TICK_PER_SECOND);
        if (elapsed_ms >= duration_ms)
        {
            progress = 1000U;
        }
        else
        {
            progress = (elapsed_ms * 1000U) / duration_ms;
        }

        oled_copy_rect(ui->scratch_buf, ui->back_buf, 0, 0, OLED_UI_WIDTH, OLED_TRANSITION_Y);
        oled_blend_transition(ui->scratch_buf, ui->front_buf, ui->back_buf, anim, progress);
        oled_ui_select_buffer(ui, ui->scratch_buf);
        oled_ui_present(ui);

        if (progress >= 1000U)
        {
            break;
        }

        rt_thread_mdelay(1);
    }

    oled_ui_select_buffer(ui, ui->back_buf);
    oled_ui_present(ui);
    return RT_EOK;
}
