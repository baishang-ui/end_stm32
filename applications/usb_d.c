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

#include "usb_device.h"
#include "usbd_def.h"
#include "usbd_customhid.h"

#define USB_HID_POLLING_INTERVAL_MS    10U

static rt_bool_t s_usb_init_done = RT_FALSE;
static rt_uint8_t s_last_state = 0xFFU;

static void usb_hid_log_state(void)
{
    if (hUsbDeviceFS.dev_state != s_last_state)
    {
        s_last_state = hUsbDeviceFS.dev_state;
        rt_kprintf("[USB HID] state=%u\n", (unsigned int)s_last_state);
    }
}

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

static rt_bool_t usb_hid_is_enumerated(void)
{
    return (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) ? RT_TRUE : RT_FALSE;
}

static rt_bool_t usb_hid_tx_idle(void)
{
    USBD_CUSTOM_HID_HandleTypeDef *hhid;

    if (hUsbDeviceFS.pClassData == RT_NULL)
    {
        return RT_FALSE;
    }

    hhid = (USBD_CUSTOM_HID_HandleTypeDef *)hUsbDeviceFS.pClassData;
    return (hhid->state == CUSTOM_HID_IDLE) ? RT_TRUE : RT_FALSE;
}

rt_bool_t usb_hid_is_ready(void)
{
    (void)usb_hid_ensure_init();
    return ((s_usb_init_done != RT_FALSE) &&
            (usb_hid_is_enumerated() != RT_FALSE)) ? RT_TRUE : RT_FALSE;
}

rt_bool_t usb_hid_is_started(void)
{
    (void)usb_hid_ensure_init();
    return s_usb_init_done;
}

rt_bool_t usb_hid_can_send(void)
{
    if (usb_hid_is_ready() == RT_FALSE)
    {
        return RT_FALSE;
    }

    return ((usb_hid_is_enumerated() != RT_FALSE) &&
            (usb_hid_tx_idle() != RT_FALSE)) ? RT_TRUE : RT_FALSE;
}

rt_uint32_t usb_hid_get_polling_interval_ms(void)
{
    return USB_HID_POLLING_INTERVAL_MS;
}

rt_err_t usb_hid_send_consumer_usage(rt_uint16_t usage)
{
    uint8_t report[2];
    uint8_t status;

    (void)usb_hid_ensure_init();

    if (usb_hid_is_enumerated() == RT_FALSE)
    {
        return -RT_EBUSY;
    }

    report[0] = (uint8_t)(usage & 0xFFU);
    report[1] = (uint8_t)((usage >> 8) & 0xFFU);

    status = USBD_CUSTOM_HID_SendReport(&hUsbDeviceFS, report, sizeof(report));
    if (status != USBD_OK)
    {
        return -RT_EBUSY;
    }

    return RT_EOK;
}

static int usb_hid_driver_init(void)
{
    (void)usb_hid_ensure_init();
    return RT_EOK;
}
INIT_APP_EXPORT(usb_hid_driver_init);
