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
#include "ui/oled_ui_service/oled_ui_core_app.h"
#include "services/device_service.h"
#include "services/sensor_service.h"
#include "usb/usb_hid.h"

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

/**
 * @brief 在启动流程中执行一段可显示进度的稳定延时。
 * @param delay_ms 延时时长，单位毫秒。
 * @return 始终返回 RT_EOK。
 */
static rt_err_t app_boot_delay(rt_uint32_t delay_ms)
{
    if (delay_ms > 0U)
    {
        rt_thread_mdelay(delay_ms);
    }

    return RT_EOK;
}

/**
 * @brief 等待 ESP01S 完成基础初始化或进入可用状态。
 * @param timeout_ms 最大等待时间，单位毫秒。
 * @return 就绪返回 RT_EOK，超时或模块错误时返回负数错误码。
 */
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

/**
 * @brief 等待阿里云 MQTT 服务完成启动。
 * @param timeout_ms 最大等待时间，单位毫秒。
 * @return 服务启动返回 RT_EOK，超时返回 -RT_ETIMEOUT。
 */
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

/**
 * @brief 对 USB HID 协议栈执行启动预热，避免主循环刚开始时立即发送失败。
 * @param delay_ms 预热等待时间，单位毫秒。
 * @return USB 协议栈已启动返回 RT_EOK，否则返回 -RT_EIO。
 */
static rt_err_t app_warm_up_usb_stack(rt_uint32_t delay_ms)
{
    if (usb_hid_is_started() == RT_FALSE)
    {
        return -RT_EIO;
    }

    rt_thread_mdelay(delay_ms);
    return RT_EOK;
}

/**
 * @brief 应用主入口，按顺序初始化 UI、传感器、网络、云端和设备服务。
 * @param 无。
 * @return 嵌入式主循环不会返回。
 */
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

