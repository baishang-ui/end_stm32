#ifndef __OLED_UI_LAUNCHER_H
#define __OLED_UI_LAUNCHER_H

#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_gpio.h"
#include "main.h"
#include "oled_ui_core.h"
#include "oled_ui_pages.h"
#include "oled_ui_utils.h"
#include "oled_graphics.h"

void oled_ui_launcher_init(void);
void oled_ui_launcher_start(void);

#endif


