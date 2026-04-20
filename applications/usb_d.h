#ifndef __USB_D_H__
#define __USB_D_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_CONSUMER_REPORT_SIZE           1U
#define USB_KEYBOARD_REPORT_SIZE           8U
#define USB_HID_KEYBOARD_MAX_KEYS          6U

rt_bool_t usb_hid_is_ready(void);
rt_bool_t usb_hid_is_started(void);
rt_bool_t usb_hid_can_send(void);
rt_uint32_t usb_hid_get_polling_interval_ms(void);

rt_err_t usb_hid_send_consumer_usage(rt_uint16_t usage);
rt_err_t usb_hid_send_keyboard_report(rt_uint8_t modifier,
                                      const rt_uint8_t *keys,
                                      rt_uint8_t key_count);

#ifdef __cplusplus
}
#endif

#endif /* __USB_D_H__ */
