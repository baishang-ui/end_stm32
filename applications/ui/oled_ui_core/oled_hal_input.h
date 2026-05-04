#ifndef __OLED_UI_DRIVER_H
#define __OLED_UI_DRIVER_H

#include <rtthread.h>
#include <rtdevice.h>
#include <drv_gpio.h>
#include <rthw.h>
#include "stm32f4xx_hal.h"
#include "stm32f4xx.h"

/**
 * @brief 读取确认键当前电平状态。
 * @param 无。
 * @return 按键读取值，具体有效电平由硬件定义。
 */
int oled_ui_read_enter_key(void);

/**
 * @brief 读取返回键当前电平状态。
 * @param 无。
 * @return 按键读取值，具体有效电平由硬件定义。
 */
int oled_ui_read_back_key(void);

/**
 * @brief 消费一次确认键边沿事件。
 * @param 无。
 * @return 存在未消费事件返回 RT_TRUE，否则返回 RT_FALSE。
 */
rt_bool_t oled_ui_consume_enter_event(void);

/**
 * @brief 消费一次返回键边沿事件。
 * @param 无。
 * @return 存在未消费事件返回 RT_TRUE，否则返回 RT_FALSE。
 */
rt_bool_t oled_ui_consume_back_event(void);

#define oled_ui_get_enter_status()    oled_ui_read_enter_key()
#define oled_ui_get_back_status()     oled_ui_read_back_key()
#define Key_GetUpStatus()       (1)
#define Key_GetDownStatus()     (1)

/**
 * @brief 初始化 UI 定时相关资源。
 * @param 无。
 * @return 无。
 */
void oled_ui_timer_init(void);

/**
 * @brief 初始化 UI 按键 GPIO 和事件检测。
 * @param 无。
 * @return 无。
 */
void oled_ui_key_init(void);

/**
 * @brief 初始化旋钮编码器输入。
 * @param 无。
 * @return 无。
 */
void oled_ui_encoder_init(void);

/**
 * @brief 使能旋钮编码器计数。
 * @param 无。
 * @return 无。
 */
void oled_ui_encoder_enable(void);

/**
 * @brief 禁用旋钮编码器计数。
 * @param 无。
 * @return 无。
 */
void oled_ui_encoder_disable(void);

/**
 * @brief 获取并清除旋钮编码器增量。
 * @param 无。
 * @return 自上次读取以来的旋钮增量。
 */
int16_t oled_ui_encoder_get_delta(void);

/**
 * @brief 毫秒级阻塞延时。
 * @param xms 延时时长，单位毫秒。
 * @return 无。
 */
void oled_ui_delay_ms(uint32_t xms);

/**
 * @brief 秒级阻塞延时。
 * @param xs 延时时长，单位秒。
 * @return 无。
 */
void oled_ui_delay_s(uint32_t xs);

#endif


