/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-04-18     Codex        rewrite as minimal RT-Thread HID adapter
 */

#include <rtthread.h>
#include <rtdevice.h>
#include "drivers/usb_device.h"

#define USB_HID_DEVICE_NAME             "hidd"
#define USB_HID_POLLING_INTERVAL_MS     10U
#define USB_HID_CONSUMER_REPORT_SIZE    2U
#define USB_HID_REPORT_ID_MEDIA         4U

#define HID_USAGE_BRIGHTNESS_INC        0x006FU
#define HID_USAGE_BRIGHTNESS_DEC        0x0070U
#define HID_USAGE_VOLUME_INC            0x00E9U
#define HID_USAGE_VOLUME_DEC            0x00EAU

static rt_device_t s_hid_dev = RT_NULL;
static rt_bool_t s_hid_dev_opened = RT_FALSE;
static rt_device_t s_usbd_controller = RT_NULL;
static udevice_t s_usbd_device = RT_NULL;
static udevice_state_t s_last_usbd_state = USB_STATE_NOTATTACHED;
static rt_bool_t s_usb_stack_started = RT_FALSE;

static rt_err_t usb_hid_bind_device(void)
{
    rt_err_t result;

    if (s_hid_dev != RT_NULL)
    {
        return RT_EOK;
    }

    s_hid_dev = rt_device_find(USB_HID_DEVICE_NAME);
    if (s_hid_dev == RT_NULL)
    {
        rt_kprintf("[USB HID] device %s not found\n", USB_HID_DEVICE_NAME);
        return -RT_ENOSYS;
    }

    result = rt_device_open(s_hid_dev, RT_DEVICE_OFLAG_RDWR);
    if (result != RT_EOK)
    {
        rt_kprintf("[USB HID] open %s failed: %d\n", USB_HID_DEVICE_NAME, result);
        s_hid_dev = RT_NULL;
        return result;
    }

    s_hid_dev_opened = RT_TRUE;
    rt_kprintf("[USB HID] bound to %s\n", USB_HID_DEVICE_NAME);
    return RT_EOK;
}

static rt_err_t usb_hid_refresh_state(void)
{
    if (s_usbd_controller == RT_NULL)
    {
        s_usbd_controller = rt_device_find("usbd");
        if (s_usbd_controller == RT_NULL)
        {
            return -RT_EBUSY;
        }
    }

    if (s_usbd_device == RT_NULL)
    {
        s_usbd_device = rt_usbd_find_device((udcd_t)s_usbd_controller);
        if (s_usbd_device == RT_NULL)
        {
            return -RT_EBUSY;
        }
    }

    if (s_usbd_device->state != s_last_usbd_state)
    {
        rt_kprintf("[USB HID] usbd state -> %d\n", s_usbd_device->state);
        s_last_usbd_state = s_usbd_device->state;
    }

    if (s_usbd_device->state != USB_STATE_CONFIGURED)
    {
        return -RT_EBUSY;
    }

    return RT_EOK;
}

static rt_bool_t usb_hid_usage_to_bit(rt_uint16_t usage, rt_uint8_t *bit_index)
{
    RT_ASSERT(bit_index != RT_NULL);

    switch (usage)
    {
    case 0U:
        *bit_index = 0U;
        return RT_TRUE;

    case 0x00B5U: /* Next track */
        *bit_index = 0U;
        return RT_TRUE;

    case 0x00B6U: /* Previous track */
        *bit_index = 1U;
        return RT_TRUE;

    case 0x00B7U: /* Stop */
        *bit_index = 2U;
        return RT_TRUE;

    case 0x00CDU: /* Play/pause */
        *bit_index = 3U;
        return RT_TRUE;

    case 0x00E2U: /* Mute */
        *bit_index = 4U;
        return RT_TRUE;

    case HID_USAGE_VOLUME_INC:
        *bit_index = 5U;
        return RT_TRUE;

    case HID_USAGE_VOLUME_DEC:
        *bit_index = 6U;
        return RT_TRUE;

    case HID_USAGE_BRIGHTNESS_INC:
        *bit_index = 7U;
        return RT_TRUE;

    case HID_USAGE_BRIGHTNESS_DEC:
        *bit_index = 8U;
        return RT_TRUE;

    default:
        return RT_FALSE;
    }
}

static rt_bool_t usb_hid_build_consumer_report(rt_uint16_t usage, rt_uint8_t report[USB_HID_CONSUMER_REPORT_SIZE])
{
    rt_uint8_t bit_index = 0U;

    RT_ASSERT(report != RT_NULL);

    report[0] = 0U;
    report[1] = 0U;

    if (usage == 0U)
    {
        return RT_TRUE;
    }

    if (usb_hid_usage_to_bit(usage, &bit_index) == RT_FALSE)
    {
        return RT_FALSE;
    }

    report[bit_index / 8U] = (rt_uint8_t)(1U << (bit_index % 8U));
    return RT_TRUE;
}

rt_bool_t usb_hid_is_ready(void)
{
    if (s_usb_stack_started == RT_FALSE)
    {
        return RT_FALSE;
    }

    if ((s_hid_dev == RT_NULL) || (s_hid_dev_opened == RT_FALSE))
    {
        if (usb_hid_bind_device() != RT_EOK)
        {
            return RT_FALSE;
        }
    }

    return RT_TRUE;
}

rt_bool_t usb_hid_can_send(void)
{
    if (usb_hid_is_ready() == RT_FALSE)
    {
        return RT_FALSE;
    }

    return (usb_hid_refresh_state() == RT_EOK) ? RT_TRUE : RT_FALSE;
}

rt_uint32_t usb_hid_get_polling_interval_ms(void)
{
    return USB_HID_POLLING_INTERVAL_MS;
}

rt_err_t usb_hid_send_consumer_usage(rt_uint16_t usage)
{
    rt_uint8_t report[USB_HID_CONSUMER_REPORT_SIZE];
    rt_ssize_t written;

    if (usb_hid_build_consumer_report(usage, report) == RT_FALSE)
    {
        rt_kprintf("[USB HID] unsupported consumer usage: 0x%04x\n", usage);
        return -RT_EINVAL;
    }

    if (usb_hid_can_send() == RT_FALSE)
    {
        return -RT_EBUSY;
    }

    written = rt_device_write(s_hid_dev, USB_HID_REPORT_ID_MEDIA, report, sizeof(report));
    if (written != (rt_ssize_t)sizeof(report))
    {
        rt_kprintf("[USB HID] write failed: usage=0x%04x ret=%ld\n",
                   usage, (long)written);
        return -RT_ERROR;
    }

    return RT_EOK;
}

void hid_example(void)
{
    if (usb_hid_is_ready() == RT_FALSE)
    {
        rt_kprintf("[USB HID] device not ready\n");
        return;
    }

    rt_kprintf("[USB HID] demo: volume up, release, volume down, release\n");
    (void)usb_hid_send_consumer_usage(HID_USAGE_VOLUME_INC);
    rt_thread_mdelay(50);
    (void)usb_hid_send_consumer_usage(0U);
    rt_thread_mdelay(50);
    (void)usb_hid_send_consumer_usage(HID_USAGE_VOLUME_DEC);
    rt_thread_mdelay(50);
    (void)usb_hid_send_consumer_usage(0U);
}
MSH_CMD_EXPORT(hid_example, USB HID media demo)

static int usb_hid_driver_init(void)
{
    rt_err_t result;

    s_usb_stack_started = RT_TRUE;
    result = usb_hid_bind_device();
    if (result != RT_EOK)
    {
        rt_kprintf("[USB HID] defer bind: %d\n", result);
    }
    else
    {
        (void)usb_hid_refresh_state();
    }

    return RT_EOK;
}
INIT_APP_EXPORT(usb_hid_driver_init);
