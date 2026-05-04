#ifndef __USB_HID_H__
#define __USB_HID_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_CONSUMER_REPORT_SIZE           1U
#define USB_KEYBOARD_REPORT_SIZE           8U
#define USB_HID_KEYBOARD_MAX_KEYS          6U

/**
 * @brief 检查 USB HID 设备是否已经被主机配置完成。
 * @param 无。
 * @return HID 可用返回 RT_TRUE，否则返回 RT_FALSE。
 */
rt_bool_t usb_hid_is_ready(void);

/**
 * @brief 检查 CubeMX USB 设备栈是否已经初始化。
 * @param 无。
 * @return 已初始化返回 RT_TRUE，否则返回 RT_FALSE。
 */
rt_bool_t usb_hid_is_started(void);

/**
 * @brief 检查 HID 端点是否已配置且当前处于空闲状态。
 * @param 无。
 * @return 可以立即提交新报告返回 RT_TRUE，否则返回 RT_FALSE。
 */
rt_bool_t usb_hid_can_send(void);

/**
 * @brief 获取 HID 报告发送节流使用的主机轮询间隔。
 * @param 无。
 * @return 轮询间隔，单位为毫秒。
 */
rt_uint32_t usb_hid_get_polling_interval_ms(void);

/**
 * @brief 发送一次消费控制 HID 用法，例如音量或亮度。
 * @param usage Consumer Usage 编码；传入 0 表示发送释放报告。
 * @return 提交成功返回 RT_EOK，失败返回负数 RT-Thread 错误码。
 */
rt_err_t usb_hid_send_consumer_usage(rt_uint16_t usage);

/**
 * @brief 发送一次 Boot Keyboard HID 报告。
 * @param modifier 键盘修饰键位图，例如 Ctrl、Shift、Alt 或 GUI。
 * @param keys HID 键值数组；当 key_count 为 0 时可以为 RT_NULL。
 * @param key_count keys 中有效键值数量，最大为 USB_HID_KEYBOARD_MAX_KEYS。
 * @return 提交成功返回 RT_EOK，失败返回负数 RT-Thread 错误码。
 */
rt_err_t usb_hid_send_keyboard_report(rt_uint8_t modifier,
                                      const rt_uint8_t *keys,
                                      rt_uint8_t key_count);

#ifdef __cplusplus
}
#endif

#endif /* __USB_HID_H__ */
