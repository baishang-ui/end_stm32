/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-04-20     Codex        switch to CubeMX custom HID consumer control
 */

#include <rtthread.h>

#include "usb_hid.h"
#include "usb_device.h"
#include "usbd_def.h"
#include "usbd_customhid.h"

#define USB_HID_POLLING_INTERVAL_MS    10U
#define USB_HID_CONSUMER_BRIGHTNESS_INC_MASK  0x01U
#define USB_HID_CONSUMER_BRIGHTNESS_DEC_MASK  0x02U
#define USB_HID_CONSUMER_VOLUME_INC_MASK      0x04U
#define USB_HID_CONSUMER_VOLUME_DEC_MASK      0x08U

static rt_bool_t s_usb_init_done = RT_FALSE;
static rt_uint8_t s_last_state = 0xFFU;

/**
 * @brief 在 USB 设备状态变化时打印一次 HID 状态日志。
 * @param 无。
 * @return 无。
 */
static void usb_hid_log_state(void)
{
    if (hUsbDeviceFS.dev_state != s_last_state)
    {
        s_last_state = hUsbDeviceFS.dev_state;
        rt_kprintf("[USB HID] state=%u old=%u cfg=%u addr=%u\n",
                   (unsigned int)hUsbDeviceFS.dev_state,
                   (unsigned int)hUsbDeviceFS.dev_old_state,
                   (unsigned int)hUsbDeviceFS.dev_config,
                   (unsigned int)hUsbDeviceFS.dev_address);
    }
}

/**
 * @brief 确保 CubeMX USB HID 协议栈已经初始化。
 * @param 无。
 * @return 成功返回 RT_EOK。
 */
static rt_err_t usb_hid_ensure_init(void)
{
    if (s_usb_init_done == RT_FALSE)
    {
        MX_USB_DEVICE_Init();
        s_usb_init_done = RT_TRUE;
        rt_kprintf("[USB HID] CubeMX custom HID started\n");
    }

    usb_hid_log_state();
    return RT_EOK;
}

/**
 * @brief 判断 USB HID 是否已经完成主机枚举。
 * @param 无。
 * @return 已配置或挂起前状态为已配置时返回 RT_TRUE，否则返回 RT_FALSE。
 */
static rt_bool_t usb_hid_is_enumerated(void)
{
    if (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED)
    {
        return RT_TRUE;
    }

    if ((hUsbDeviceFS.dev_state == USBD_STATE_SUSPENDED) &&
        (hUsbDeviceFS.dev_old_state == USBD_STATE_CONFIGURED))
    {
        return RT_TRUE;
    }

    return RT_FALSE;
}

/**
 * @brief 检查 Consumer 和 Keyboard 两个 HID 端点是否都处于空闲状态。
 * @param 无。
 * @return 端点空闲返回 RT_TRUE，否则返回 RT_FALSE。
 */
static rt_bool_t usb_hid_tx_idle(void)
{
    USBD_CUSTOM_HID_HandleTypeDef *hhid;

    if (hUsbDeviceFS.pClassData == RT_NULL)
    {
        return RT_FALSE;
    }

    hhid = (USBD_CUSTOM_HID_HandleTypeDef *)hUsbDeviceFS.pClassData;
    return ((hhid->ConsumerState == CUSTOM_HID_IDLE) &&
            (hhid->KeyboardState == CUSTOM_HID_IDLE)) ? RT_TRUE : RT_FALSE;
}

/**
 * @brief 检查 USB HID 是否已经可用。
 * @param 无。
 * @return HID 已初始化且已枚举返回 RT_TRUE，否则返回 RT_FALSE。
 */
rt_bool_t usb_hid_is_ready(void)
{
    (void)usb_hid_ensure_init();
    return ((s_usb_init_done != RT_FALSE) &&
            (usb_hid_is_enumerated() != RT_FALSE)) ? RT_TRUE : RT_FALSE;
}

/**
 * @brief 检查 USB HID 协议栈是否已经启动。
 * @param 无。
 * @return 已启动返回 RT_TRUE，否则返回 RT_FALSE。
 */
rt_bool_t usb_hid_is_started(void)
{
    (void)usb_hid_ensure_init();
    return s_usb_init_done;
}

/**
 * @brief 检查当前是否可以发送新的 HID 报告。
 * @param 无。
 * @return 已枚举且端点空闲返回 RT_TRUE，否则返回 RT_FALSE。
 */
rt_bool_t usb_hid_can_send(void)
{
    if (usb_hid_is_ready() == RT_FALSE)
    {
        return RT_FALSE;
    }

    return ((hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) &&
            (usb_hid_tx_idle() != RT_FALSE)) ? RT_TRUE : RT_FALSE;
}

/**
 * @brief 获取 HID 报告发送节流使用的轮询间隔。
 * @param 无。
 * @return 轮询间隔，单位毫秒。
 */
rt_uint32_t usb_hid_get_polling_interval_ms(void)
{
    return USB_HID_POLLING_INTERVAL_MS;
}

/**
 * @brief 根据报告长度选择 Consumer 或 Keyboard 端点并发送报告。
 * @param report HID 报告缓冲区。
 * @param report_size HID 报告长度。
 * @return 成功返回 RT_EOK，忙或参数无效时返回负数错误码。
 */
static rt_err_t usb_hid_send_report(const uint8_t *report, uint16_t report_size)
{
    uint8_t status;

    if ((report == RT_NULL) || (report_size == 0U))
    {
        return -RT_EINVAL;
    }

    (void)usb_hid_ensure_init();

    if (usb_hid_is_enumerated() == RT_FALSE)
    {
        return -RT_EBUSY;
    }

    if (report_size == USB_CONSUMER_REPORT_SIZE)
    {
        status = USBD_CUSTOM_HID_SendConsumerReport(&hUsbDeviceFS, (uint8_t *)report, report_size);
    }
    else if (report_size == USB_KEYBOARD_REPORT_SIZE)
    {
        status = USBD_CUSTOM_HID_SendKeyboardReport(&hUsbDeviceFS, (uint8_t *)report, report_size);
    }
    else
    {
        return -RT_EINVAL;
    }

    if (status != USBD_OK)
    {
        return -RT_EBUSY;
    }

    return RT_EOK;
}

/**
 * @brief 发送 Consumer Control HID 用法。
 * @param usage Consumer Usage 编码，0 表示释放当前 Consumer 按键。
 * @return 成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t usb_hid_send_consumer_usage(rt_uint16_t usage)
{
    uint8_t report[USB_CONSUMER_REPORT_SIZE];
    uint8_t consumer_bits = 0U;

    switch (usage)
    {
    case 0x0000U:
        consumer_bits = 0U;
        break;

    case 0x006FU:
        consumer_bits = USB_HID_CONSUMER_BRIGHTNESS_INC_MASK;
        break;

    case 0x0070U:
        consumer_bits = USB_HID_CONSUMER_BRIGHTNESS_DEC_MASK;
        break;

    case 0x00E9U:
        consumer_bits = USB_HID_CONSUMER_VOLUME_INC_MASK;
        break;

    case 0x00EAU:
        consumer_bits = USB_HID_CONSUMER_VOLUME_DEC_MASK;
        break;

    default:
        return -RT_EINVAL;
    }

    report[0] = consumer_bits;

    return usb_hid_send_report(report, sizeof(report));
}

/**
 * @brief 发送 Boot Keyboard HID 报告。
 * @param modifier 键盘修饰键位图。
 * @param keys HID 键值数组。
 * @param key_count keys 中有效键值数量。
 * @return 成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t usb_hid_send_keyboard_report(rt_uint8_t modifier,
                                      const rt_uint8_t *keys,
                                      rt_uint8_t key_count)
{
    uint8_t report[USB_KEYBOARD_REPORT_SIZE] = {0};

    if (key_count > USB_HID_KEYBOARD_MAX_KEYS)
    {
        return -RT_EINVAL;
    }

    report[0] = modifier;

    if ((keys != RT_NULL) && (key_count > 0U))
    {
        rt_memcpy(&report[2], keys, key_count);
    }

    return usb_hid_send_report(report, sizeof(report));
}

/**
 * @brief RT-Thread 应用初始化阶段的 USB HID 自动初始化入口。
 * @param 无。
 * @return 成功返回 RT_EOK。
 */
static int usb_hid_driver_init(void)
{
    (void)usb_hid_ensure_init();
    return RT_EOK;
}
INIT_APP_EXPORT(usb_hid_driver_init);
