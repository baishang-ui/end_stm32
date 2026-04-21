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
#include <string.h>
#ifndef RT_USING_NANO
#include <rtdevice.h>
#endif /* RT_USING_NANO */
#include "data/todo_snapshot.h"
#include "sensor/dht11.h"
#include "network/esp01s_at.h"
#include "cloud/aliyun_iot_mqtt.h"
#include "oled_ui_core_app.h"
#include "device_service.h"
#include "sensor_service.h"
#include "usb_d.h"

#define GPIO_LED    GET_PIN(B, 2)
#define BOOT_STEP_TOTAL               11U
#define BOOT_POWER_STABLE_MS          80U
#define BOOT_SERVICE_STABLE_MS        120U
#define BOOT_ESP_READY_TIMEOUT_MS     2500U
#define BOOT_CLOUD_READY_TIMEOUT_MS   400U
#define BOOT_USB_STABLE_MS            180U
#define BOOT_UI_READY_TIMEOUT_MS      2000U
#define BOOT_WAIT_POLL_MS             20U

static const char * const g_boot_step_labels[BOOT_STEP_TOTAL] =
{
    "oled init",
    "power init",
    "todolist init",
    "sensor init",
    "sensor server init",
    "net init",
    "net server init",
    "cloud init",
    "cloud update thread init",
    "device server init",
    "USB init",
};

static rt_err_t app_boot_delay(rt_uint32_t delay_ms)
{
    if (delay_ms > 0U)
    {
        rt_thread_mdelay(delay_ms);
    }

    return RT_EOK;
}

static rt_err_t app_wait_for_esp_ready(rt_uint32_t timeout_ms)
{
    esp01s_status_t status;
    rt_tick_t start_tick = rt_tick_get();
    rt_tick_t timeout_tick = rt_tick_from_millisecond(timeout_ms);

    while (1)
    {
        memset(&status, 0, sizeof(status));
        esp01s_get_status(&status);
        if ((status.ready != RT_FALSE) &&
            ((status.connected != RT_FALSE) || (rt_strcmp(status.state, "INIT") != 0)))
        {
            return RT_EOK;
        }

        if ((rt_tick_get() - start_tick) >= timeout_tick)
        {
            return (status.last_error != RT_EOK) ? status.last_error : -RT_ETIMEOUT;
        }

        rt_thread_mdelay(BOOT_WAIT_POLL_MS);
    }
}

static rt_err_t app_wait_for_cloud_started(rt_uint32_t timeout_ms)
{
    aliyun_iot_status_t status;
    rt_tick_t start_tick = rt_tick_get();
    rt_tick_t timeout_tick = rt_tick_from_millisecond(timeout_ms);

    while (1)
    {
        memset(&status, 0, sizeof(status));
        aliyun_iot_get_status(&status);
        if (status.started != RT_FALSE)
        {
            return RT_EOK;
        }

        if ((rt_tick_get() - start_tick) >= timeout_tick)
        {
            return -RT_ETIMEOUT;
        }

        rt_thread_mdelay(BOOT_WAIT_POLL_MS);
    }
}

static rt_err_t app_warm_up_usb_stack(rt_uint32_t delay_ms)
{
    if (usb_hid_is_started() == RT_FALSE)
    {
        return -RT_EIO;
    }

    rt_thread_mdelay(delay_ms);
    return RT_EOK;
}

int main(void)
{
    rt_err_t ret;
    rt_uint32_t step = 0U;

    rt_pin_mode(GPIO_LED, PIN_MODE_OUTPUT);

    oled_ui_boot_init();
    oled_ui_boot_set_plan(BOOT_STEP_TOTAL, g_boot_step_labels);
    oled_ui_boot_step(step, BOOT_STEP_TOTAL, g_boot_step_labels[step], RT_EOK);
    step++;
    oled_ui_boot_step(step, BOOT_STEP_TOTAL, g_boot_step_labels[step], app_boot_delay(BOOT_POWER_STABLE_MS));
    step++;

    todo_snapshot_init();
    oled_ui_boot_step(step, BOOT_STEP_TOTAL, g_boot_step_labels[step], RT_EOK);
    step++;

    ret = dht11_init();
    oled_ui_boot_step(step, BOOT_STEP_TOTAL, g_boot_step_labels[step], ret);
    step++;

    ret = sensor_service_start();
    oled_ui_boot_step(step, BOOT_STEP_TOTAL, g_boot_step_labels[step], ret);
    step++;

    ret = esp01s_start_connect();
    oled_ui_boot_step(step, BOOT_STEP_TOTAL, g_boot_step_labels[step], ret);
    step++;
    ret = (ret == RT_EOK) ? app_wait_for_esp_ready(BOOT_ESP_READY_TIMEOUT_MS) : ret;
    oled_ui_boot_step(step, BOOT_STEP_TOTAL, g_boot_step_labels[step], ret);
    step++;

    ret = aliyun_iot_start();
    oled_ui_boot_step(step, BOOT_STEP_TOTAL, g_boot_step_labels[step], ret);
    step++;
    ret = (ret == RT_EOK) ? app_wait_for_cloud_started(BOOT_CLOUD_READY_TIMEOUT_MS) : ret;
    oled_ui_boot_step(step, BOOT_STEP_TOTAL, g_boot_step_labels[step], ret);
    step++;

    ret = device_service_start();
    oled_ui_boot_step(step, BOOT_STEP_TOTAL, g_boot_step_labels[step], ret);
    step++;
    ret = (ret == RT_EOK) ? app_warm_up_usb_stack(BOOT_USB_STABLE_MS) : ret;
    oled_ui_boot_step(step, BOOT_STEP_TOTAL, g_boot_step_labels[step], ret);
    step++;

    oled_ui_boot_complete();
    ret = oled_ui_core_start();
    if (ret == RT_EOK)
    {
        (void)oled_ui_core_wait_ready(BOOT_UI_READY_TIMEOUT_MS);
        (void)app_boot_delay(BOOT_SERVICE_STABLE_MS);
    }

    while (1)
    {
        rt_pin_write(GPIO_LED, PIN_HIGH);
        rt_thread_mdelay(500);
        rt_pin_write(GPIO_LED, PIN_LOW);
        rt_thread_mdelay(500);
    }
}

