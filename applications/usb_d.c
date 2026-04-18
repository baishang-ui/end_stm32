/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-04-18     Codex        disable USB adapter, keep UI-facing stubs
 */

#include <rtthread.h>

#define USB_HID_POLLING_INTERVAL_MS    10U

rt_bool_t usb_hid_is_ready(void)
{
    return RT_FALSE;
}

rt_bool_t usb_hid_can_send(void)
{
    return RT_FALSE;
}

rt_uint32_t usb_hid_get_polling_interval_ms(void)
{
    return USB_HID_POLLING_INTERVAL_MS;
}

rt_err_t usb_hid_send_consumer_usage(rt_uint16_t usage)
{
    RT_UNUSED(usage);
    return -RT_ENOSYS;
}
