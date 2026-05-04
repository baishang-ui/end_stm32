#ifndef __OLED_UI_LAUNCHER_H
#define __OLED_UI_LAUNCHER_H

#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_gpio.h"
#include "main.h"
#include "oled_ui_core.h"
#include "ui/oled_ui_app/app_ui_pages.h"
#include "oled_graphics.h"

/**
 * @brief 初始化 OLED UI 启动器和默认页面状态。
 * @param 无。
 * @return 无。
 */
void oled_ui_launcher_init(void);

/**
 * @brief 启动 OLED UI 启动器主循环。
 * @param 无。
 * @return 无。
 */
void oled_ui_launcher_start(void);

#endif


