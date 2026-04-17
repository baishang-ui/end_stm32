/*
 * Copyright (c) 2023, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2023-07-06     Supperthomas first version
 * 2023-12-03     Meco Man     support nano version
 * 2024-04-13     yuanzihao    adaptation for SkyStar STM32F407 version
 */

#include <board.h>
#include <rtthread.h>
#include <drv_gpio.h>
#ifndef RT_USING_NANO
#include <rtdevice.h>
#endif /* RT_USING_NANO */
#include "data/todo_snapshot.h"
#include "sensor/dht11.h"
#include "network/esp01s_at.h"
#include "cloud/aliyun_iot_mqtt.h"
#include "oled_ui_core_app.h"

#define GPIO_LED    GET_PIN(B, 2)
#define BOOT_STEP_TOTAL 5U

int main(void)
{
    rt_err_t ret;
    rt_uint32_t step = 0U;

    rt_pin_mode(GPIO_LED, PIN_MODE_OUTPUT);

    oled_ui_boot_init();
    oled_ui_boot_step(step++, BOOT_STEP_TOTAL, "OLED READY", RT_EOK);

    todo_snapshot_init();
    oled_ui_boot_step(step++, BOOT_STEP_TOTAL, "TODO SNAPSHOT", RT_EOK);

    ret = dht11_init();
    oled_ui_boot_step(step++, BOOT_STEP_TOTAL, "DHT11 INIT", ret);

    ret = esp01s_start_connect();
    oled_ui_boot_step(step++, BOOT_STEP_TOTAL, "ESP THREAD", ret);

    ret = aliyun_iot_start();
    oled_ui_boot_step(step++, BOOT_STEP_TOTAL, "ALIYUN THREAD", ret);

    oled_ui_boot_complete();
    ret = oled_ui_core_start();
    if (ret == RT_EOK)
    {
        (void)oled_ui_core_wait_ready(1500);
    }

    while (1)
    {
        rt_pin_write(GPIO_LED, PIN_HIGH);
        rt_thread_mdelay(500);
        rt_pin_write(GPIO_LED, PIN_LOW);
        rt_thread_mdelay(500);
    }
}

