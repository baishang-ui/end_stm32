#ifndef __OLED_UI_DRIVER_H
#define __OLED_UI_DRIVER_H

#include <rtthread.h>
#include <rtdevice.h>
#include <drv_gpio.h>
#include <rthw.h>
#include "stm32f4xx_hal.h"
#include "stm32f4xx.h"

int oled_ui_read_enter_key(void);
int oled_ui_read_back_key(void);
rt_bool_t oled_ui_consume_enter_event(void);
rt_bool_t oled_ui_consume_back_event(void);

#define oled_ui_get_enter_status()    oled_ui_read_enter_key()
#define oled_ui_get_back_status()     oled_ui_read_back_key()
#define Key_GetUpStatus()       (1)
#define Key_GetDownStatus()     (1)

void oled_ui_timer_init(void);
void oled_ui_key_init(void);
void oled_ui_encoder_init(void);
void oled_ui_encoder_enable(void);
void oled_ui_encoder_disable(void);
int16_t oled_ui_encoder_get_delta(void);
void oled_ui_delay_ms(uint32_t xms);
void oled_ui_delay_s(uint32_t xs);

#endif


