#include "device_service.h"

#include <string.h>

#include <rtdevice.h>
#include <drv_gpio.h>

#include "network/esp01s_at.h"
#include "cloud/aliyun_iot_mqtt.h"
#include "usb/usb_hid.h"

#define DEVICE_NET_QUEUE_DEPTH              8U
#define DEVICE_NET_THREAD_STACK_SIZE        4096U
#define DEVICE_SCAN_THREAD_STACK_SIZE       4096U
#define DEVICE_USB_THREAD_STACK_SIZE        3072U
#define DEVICE_USB_LEVEL_STEP               2
#define DEVICE_USB_PENDING_MAX              32767
#define DEVICE_USB_IDLE_DELAY_MS            5
#define DEVICE_USB_SHORTCUT_QUEUE_LIMIT     4U
#define DEVICE_USB_BUTTON_DEBOUNCE_MS       18U

/* 外部快捷键按键：低电平有效，一端接 GPIO，另一端接 GND。 */
#define DEVICE_USB_SHORTCUT_PIN_1           GET_PIN(C, 0)
#define DEVICE_USB_SHORTCUT_PIN_2           GET_PIN(C, 1)
#define DEVICE_USB_SHORTCUT_PIN_3           GET_PIN(C, 2)
#define DEVICE_USB_SHORTCUT_PIN_4           GET_PIN(C, 3)

#define HID_USAGE_BRIGHTNESS_INC    0x006FU
#define HID_USAGE_BRIGHTNESS_DEC    0x0070U
#define HID_USAGE_VOLUME_INC        0x00E9U
#define HID_USAGE_VOLUME_DEC        0x00EAU

#define HID_KBD_MOD_LCTRL           0x01U
#define HID_KBD_MOD_LSHIFT          0x02U
#define HID_KBD_MOD_LALT            0x04U
#define HID_KBD_MOD_LGUI            0x08U

#define HID_KBD_KEY_A               0x04U
#define HID_KBD_KEY_C               0x06U
#define HID_KBD_KEY_D               0x07U
#define HID_KBD_KEY_E               0x08U
#define HID_KBD_KEY_I               0x0CU
#define HID_KBD_KEY_L               0x0FU
#define HID_KBD_KEY_R               0x15U
#define HID_KBD_KEY_S               0x16U
#define HID_KBD_KEY_V               0x19U
#define HID_KBD_KEY_X               0x1BU
#define HID_KBD_KEY_Y               0x1CU
#define HID_KBD_KEY_Z               0x1DU
#define HID_KBD_KEY_ESCAPE          0x29U
#define HID_KBD_KEY_TAB             0x2BU

typedef enum
{
    DEVICE_NET_CMD_WIFI_CONNECT = 0,
    DEVICE_NET_CMD_WIFI_REJOIN,
} device_net_cmd_t;

typedef struct
{
    rt_uint8_t cmd;
    rt_uint8_t ecn;
    char ssid[33];
} device_net_msg_t;

typedef struct
{
    rt_base_t pin;
    rt_uint8_t stable_level;
    rt_uint8_t last_raw_level;
    rt_tick_t change_tick;
    rt_tick_t debounce_ticks;
} device_usb_button_t;

typedef struct
{
    rt_uint8_t modifier;
    rt_uint8_t key_count;
    rt_uint8_t keys[USB_HID_KEYBOARD_MAX_KEYS];
} device_usb_shortcut_binding_t;

typedef struct
{
    device_usb_shortcut_preset_t preset;
    const char *label;
    device_usb_shortcut_binding_t binding;
} device_usb_shortcut_preset_entry_t;

typedef enum
{
    DEVICE_USB_RELEASE_NONE = 0,
    DEVICE_USB_RELEASE_CONSUMER,
    DEVICE_USB_RELEASE_KEYBOARD,
} device_usb_release_t;

static const rt_base_t s_usb_shortcut_pins[DEVICE_USB_SHORTCUT_SLOT_COUNT] =
{
    DEVICE_USB_SHORTCUT_PIN_1,
    DEVICE_USB_SHORTCUT_PIN_2,
    DEVICE_USB_SHORTCUT_PIN_3,
    DEVICE_USB_SHORTCUT_PIN_4,
};

static const device_usb_shortcut_preset_entry_t s_usb_shortcut_presets[DEVICE_USB_SHORTCUT_PRESET_COUNT] =
{
    { DEVICE_USB_SHORTCUT_PRESET_NONE, "\xE6\x97\xA0\xE6\x93\x8D\xE4\xBD\x9C", { 0U, 0U, { 0U, 0U, 0U, 0U, 0U, 0U } } },
    { DEVICE_USB_SHORTCUT_PRESET_SHOW_DESKTOP, "\xE6\x98\xBE\xE7\xA4\xBA\xE6\xA1\x8C\xE9\x9D\xA2", { HID_KBD_MOD_LGUI, 1U, { HID_KBD_KEY_D, 0U, 0U, 0U, 0U, 0U } } },
    { DEVICE_USB_SHORTCUT_PRESET_FILE_MANAGER, "\xE6\x96\x87\xE4\xBB\xB6\xE7\xAE\xA1\xE7\x90\x86", { HID_KBD_MOD_LGUI, 1U, { HID_KBD_KEY_E, 0U, 0U, 0U, 0U, 0U } } },
    { DEVICE_USB_SHORTCUT_PRESET_TASK_SWITCHER, "\xE4\xBB\xBB\xE5\x8A\xA1\xE5\x88\x87\xE6\x8D\xA2", { HID_KBD_MOD_LALT, 1U, { HID_KBD_KEY_TAB, 0U, 0U, 0U, 0U, 0U } } },
    { DEVICE_USB_SHORTCUT_PRESET_TASK_MANAGER, "\xE4\xBB\xBB\xE5\x8A\xA1\xE7\xAE\xA1\xE7\x90\x86", { (rt_uint8_t)(HID_KBD_MOD_LCTRL | HID_KBD_MOD_LSHIFT), 1U, { HID_KBD_KEY_ESCAPE, 0U, 0U, 0U, 0U, 0U } } },
    { DEVICE_USB_SHORTCUT_PRESET_LOCK_PC, "\xE9\x94\x81\xE5\xAE\x9A\xE7\x94\xB5\xE8\x84\x91", { HID_KBD_MOD_LGUI, 1U, { HID_KBD_KEY_L, 0U, 0U, 0U, 0U, 0U } } },
    { DEVICE_USB_SHORTCUT_PRESET_SNIP_TOOL, "\xE6\x88\xAA\xE5\x9B\xBE\xE5\xB7\xA5\xE5\x85\xB7", { (rt_uint8_t)(HID_KBD_MOD_LGUI | HID_KBD_MOD_LSHIFT), 1U, { HID_KBD_KEY_S, 0U, 0U, 0U, 0U, 0U } } },
    { DEVICE_USB_SHORTCUT_PRESET_SYSTEM_SETTINGS, "\xE7\xB3\xBB\xE7\xBB\x9F\xE8\xAE\xBE\xE7\xBD\xAE", { HID_KBD_MOD_LGUI, 1U, { HID_KBD_KEY_I, 0U, 0U, 0U, 0U, 0U } } },
    { DEVICE_USB_SHORTCUT_PRESET_RUN_DIALOG, "\xE8\xBF\x90\xE8\xA1\x8C\xE7\xAA\x97\xE5\x8F\xA3", { HID_KBD_MOD_LGUI, 1U, { HID_KBD_KEY_R, 0U, 0U, 0U, 0U, 0U } } },
    { DEVICE_USB_SHORTCUT_PRESET_COPY, "\xE5\xA4\x8D\xE5\x88\xB6", { HID_KBD_MOD_LCTRL, 1U, { HID_KBD_KEY_C, 0U, 0U, 0U, 0U, 0U } } },
    { DEVICE_USB_SHORTCUT_PRESET_PASTE, "\xE7\xB2\x98\xE8\xB4\xB4", { HID_KBD_MOD_LCTRL, 1U, { HID_KBD_KEY_V, 0U, 0U, 0U, 0U, 0U } } },
    { DEVICE_USB_SHORTCUT_PRESET_CUT, "\xE5\x89\xAA\xE5\x88\x87", { HID_KBD_MOD_LCTRL, 1U, { HID_KBD_KEY_X, 0U, 0U, 0U, 0U, 0U } } },
    { DEVICE_USB_SHORTCUT_PRESET_SELECT_ALL, "\xE5\x85\xA8\xE9\x80\x89", { HID_KBD_MOD_LCTRL, 1U, { HID_KBD_KEY_A, 0U, 0U, 0U, 0U, 0U } } },
    { DEVICE_USB_SHORTCUT_PRESET_UNDO, "\xE6\x92\xA4\xE9\x94\x80", { HID_KBD_MOD_LCTRL, 1U, { HID_KBD_KEY_Z, 0U, 0U, 0U, 0U, 0U } } },
    { DEVICE_USB_SHORTCUT_PRESET_REDO, "\xE9\x87\x8D\xE5\x81\x9A", { HID_KBD_MOD_LCTRL, 1U, { HID_KBD_KEY_Y, 0U, 0U, 0U, 0U, 0U } } },
};

static struct rt_messagequeue s_net_mq;
static char s_net_mq_pool[sizeof(device_net_msg_t) * DEVICE_NET_QUEUE_DEPTH];
static struct rt_semaphore s_scan_sem;
static struct rt_mutex s_net_lock;
static struct rt_mutex s_usb_lock;
static rt_bool_t s_net_inited = RT_FALSE;
static rt_bool_t s_scan_inited = RT_FALSE;
static rt_bool_t s_usb_inited = RT_FALSE;
static rt_bool_t s_service_started = RT_FALSE;
static rt_bool_t s_service_starting = RT_FALSE;
static rt_thread_t s_net_thread = RT_NULL;
static rt_thread_t s_scan_thread = RT_NULL;
static rt_thread_t s_usb_thread = RT_NULL;
static rt_bool_t s_scan_pending = RT_FALSE;

static device_net_service_status_t s_net_status;
static device_usb_service_status_t s_usb_status;
static rt_int16_t s_usb_target_volume = 50;
static rt_int16_t s_usb_target_brightness = 50;
static rt_int16_t s_usb_shadow_volume = 50;
static rt_int16_t s_usb_shadow_brightness = 50;
static rt_int16_t s_usb_pending_volume = 0;
static rt_int16_t s_usb_pending_brightness = 0;
static rt_int16_t s_usb_cmd_volume = 0;
static rt_int16_t s_usb_cmd_brightness = 0;
static rt_uint8_t s_usb_shortcut_pending[DEVICE_USB_SHORTCUT_SLOT_COUNT];
static device_usb_button_t s_usb_shortcut_buttons[DEVICE_USB_SHORTCUT_SLOT_COUNT];
static device_usb_shortcut_preset_t s_usb_shortcut_slot_presets[DEVICE_USB_SHORTCUT_SLOT_COUNT] =
{
    DEVICE_USB_SHORTCUT_PRESET_SHOW_DESKTOP,
    DEVICE_USB_SHORTCUT_PRESET_FILE_MANAGER,
    DEVICE_USB_SHORTCUT_PRESET_TASK_SWITCHER,
    DEVICE_USB_SHORTCUT_PRESET_TASK_MANAGER,
};
static rt_bool_t s_usb_shortcut_buttons_inited = RT_FALSE;
static device_usb_release_t s_usb_release_pending = DEVICE_USB_RELEASE_NONE;
static rt_tick_t s_usb_last_report_tick = 0U;

/**
 * @brief 计算 int16_t 数值的绝对值，并以 uint16_t 返回。
 * @param value 输入的有符号数值。
 * @return value 的绝对值。
 */
static rt_uint16_t device_abs_i16(rt_int16_t value)
{
    return (value < 0) ? (rt_uint16_t)(-((rt_int32_t)value)) : (rt_uint16_t)value;
}

/**
 * @brief 将 int16_t 数值限制到指定范围。
 * @param value 原始数值。
 * @param min_value 最小允许值。
 * @param max_value 最大允许值。
 * @return 限幅后的数值。
 */
static rt_int16_t device_clamp_i16(rt_int16_t value, rt_int16_t min_value, rt_int16_t max_value)
{
    if (value < min_value)
    {
        return min_value;
    }

    if (value > max_value)
    {
        return max_value;
    }

    return value;
}

/**
 * @brief 将指定步数累加到 USB 待发送队列，并限制在安全范围内。
 * @param pending 待累加的队列计数指针。
 * @param steps 需要累加的步数，可以为负数。
 * @return 无。
 */
static void device_usb_accumulate_steps(rt_int16_t *pending, rt_int32_t steps)
{
    rt_int32_t total;

    if ((pending == RT_NULL) || (steps == 0))
    {
        return;
    }

    total = (rt_int32_t)(*pending) + steps;
    if (total > DEVICE_USB_PENDING_MAX)
    {
        total = DEVICE_USB_PENDING_MAX;
    }
    else if (total < -DEVICE_USB_PENDING_MAX)
    {
        total = -DEVICE_USB_PENDING_MAX;
    }

    *pending = (rt_int16_t)total;
}

/**
 * @brief 将 UI 数值差值转换为 HID 调节步数并入队。
 * @param pending 待累加的队列计数指针。
 * @param delta UI 目标值与影子值之间的差值。
 * @return 无。
 */
static void device_usb_queue_steps(rt_int16_t *pending, rt_int32_t delta)
{
    rt_int32_t steps;

    if ((pending == RT_NULL) || (delta == 0))
    {
        return;
    }

    steps = delta / DEVICE_USB_LEVEL_STEP;
    if (steps == 0)
    {
        steps = (delta > 0) ? 1 : -1;
    }

    device_usb_accumulate_steps(pending, steps);
}

/**
 * @brief 初始化一个外部快捷键按键的消抖状态。
 * @param button 按键状态对象。
 * @param pin GPIO 引脚编号。
 * @param debounce_ticks 消抖时间，单位为 RT-Thread tick。
 * @return 无。
 */
static void device_usb_button_init(device_usb_button_t *button,
                                   rt_base_t pin,
                                   rt_tick_t debounce_ticks)
{
    button->pin = pin;
    button->debounce_ticks = debounce_ticks;
    rt_pin_mode(pin, PIN_MODE_INPUT_PULLUP);
    button->stable_level = (rt_uint8_t)(rt_pin_read(pin) ? 1U : 0U);
    button->last_raw_level = button->stable_level;
    button->change_tick = rt_tick_get();
}

/**
 * @brief 轮询一个外部快捷键按键，并在稳定按下边沿出现时返回事件。
 * @param button 按键状态对象。
 * @return 检测到一次有效按下返回 RT_TRUE，否则返回 RT_FALSE。
 */
static rt_bool_t device_usb_button_poll_press(device_usb_button_t *button)
{
    rt_uint8_t raw_level;
    rt_uint8_t previous_level;
    rt_tick_t now;

    if (button == RT_NULL)
    {
        return RT_FALSE;
    }

    raw_level = (rt_uint8_t)(rt_pin_read(button->pin) ? 1U : 0U);
    previous_level = button->stable_level;
    now = rt_tick_get();

    if (raw_level != button->last_raw_level)
    {
        button->last_raw_level = raw_level;
        button->change_tick = now;
    }
    else if ((button->stable_level != raw_level) &&
             ((now - button->change_tick) >= button->debounce_ticks))
    {
        button->stable_level = raw_level;
        if ((previous_level != 0U) && (raw_level == 0U))
        {
            return RT_TRUE;
        }
    }

    return RT_FALSE;
}

/**
 * @brief 初始化全部外部快捷键按键。
 * @param 无。
 * @return 无。
 */
static void device_usb_shortcut_buttons_init(void)
{
    rt_uint32_t index;
    rt_tick_t debounce_ticks;

    if (s_usb_shortcut_buttons_inited != RT_FALSE)
    {
        return;
    }

    debounce_ticks = rt_tick_from_millisecond(DEVICE_USB_BUTTON_DEBOUNCE_MS);
    for (index = 0; index < DEVICE_USB_SHORTCUT_SLOT_COUNT; index++)
    {
        device_usb_button_init(&s_usb_shortcut_buttons[index],
                               s_usb_shortcut_pins[index],
                               debounce_ticks);
    }

    s_usb_shortcut_buttons_inited = RT_TRUE;
}

/**
 * @brief 在 USB 锁保护下统计待发送快捷键数量。
 * @param 无。
 * @return 当前积压的快捷键动作数量。
 */
static rt_uint16_t device_usb_count_shortcuts_locked(void)
{
    rt_uint32_t index;
    rt_uint16_t total = 0U;

    for (index = 0; index < DEVICE_USB_SHORTCUT_SLOT_COUNT; index++)
    {
        total = (rt_uint16_t)(total + s_usb_shortcut_pending[index]);
    }

    return total;
}

/**
 * @brief 在 USB 锁保护下清空待发送快捷键队列。
 * @param 无。
 * @return 无。
 */
static void device_usb_clear_shortcuts_locked(void)
{
    memset(s_usb_shortcut_pending, 0, sizeof(s_usb_shortcut_pending));
}

/**
 * @brief 根据预设编号获取快捷键预设表项。
 * @param preset 快捷键预设编号。
 * @return 有效时返回预设表项指针，无效时返回 RT_NULL。
 */
static const device_usb_shortcut_preset_entry_t *device_usb_get_preset_entry(device_usb_shortcut_preset_t preset)
{
    if (((rt_uint32_t)preset) >= DEVICE_USB_SHORTCUT_PRESET_COUNT)
    {
        return RT_NULL;
    }

    return &s_usb_shortcut_presets[preset];
}

/**
 * @brief 在 USB 锁保护下读取槽位绑定的快捷键预设。
 * @param slot 快捷键槽位。
 * @return 槽位当前绑定的快捷键预设。
 */
static device_usb_shortcut_preset_t device_usb_get_slot_preset_locked(device_usb_shortcut_slot_t slot)
{
    if (((rt_uint32_t)slot) >= DEVICE_USB_SHORTCUT_SLOT_COUNT)
    {
        return DEVICE_USB_SHORTCUT_PRESET_NONE;
    }

    return s_usb_shortcut_slot_presets[slot];
}

/**
 * @brief 在 USB 锁保护下向指定槽位追加一次快捷键触发请求。
 * @param slot 快捷键槽位。
 * @return 成功返回 RT_EOK，槽位无效返回 -RT_EINVAL。
 */
static rt_err_t device_usb_enqueue_shortcut_locked(device_usb_shortcut_slot_t slot)
{
    if (((rt_uint32_t)slot) >= DEVICE_USB_SHORTCUT_SLOT_COUNT)
    {
        return -RT_EINVAL;
    }

    if (s_usb_shortcut_pending[slot] < DEVICE_USB_SHORTCUT_QUEUE_LIMIT)
    {
        s_usb_shortcut_pending[slot]++;
    }

    return RT_EOK;
}

/**
 * @brief 从待发送快捷键队列中取出一个动作。
 * @param slot 输出参数，用于接收被取出的快捷键槽位。
 * @return 成功取出返回 RT_TRUE，队列为空返回 RT_FALSE。
 */
static rt_bool_t device_usb_pop_shortcut(device_usb_shortcut_slot_t *slot)
{
    rt_uint32_t index;
    rt_bool_t found = RT_FALSE;

    if (slot == RT_NULL)
    {
        return RT_FALSE;
    }

    rt_mutex_take(&s_usb_lock, RT_WAITING_FOREVER);
    for (index = 0; index < DEVICE_USB_SHORTCUT_SLOT_COUNT; index++)
    {
        if (s_usb_shortcut_pending[index] > 0U)
        {
            s_usb_shortcut_pending[index]--;
            *slot = (device_usb_shortcut_slot_t)index;
            found = RT_TRUE;
            break;
        }
    }
    rt_mutex_release(&s_usb_lock);

    return found;
}

/**
 * @brief 扫描外部快捷键按键，并把有效按下事件加入 USB 队列。
 * @param 无。
 * @return 无。
 */
static void device_usb_scan_shortcut_buttons(void)
{
    rt_uint32_t index;

    if (s_usb_shortcut_buttons_inited == RT_FALSE)
    {
        return;
    }

    for (index = 0; index < DEVICE_USB_SHORTCUT_SLOT_COUNT; index++)
    {
        if (device_usb_button_poll_press(&s_usb_shortcut_buttons[index]) != RT_FALSE)
        {
            rt_mutex_take(&s_usb_lock, RT_WAITING_FOREVER);
            (void)device_usb_enqueue_shortcut_locked((device_usb_shortcut_slot_t)index);
            rt_mutex_release(&s_usb_lock);
        }
    }
}

/**
 * @brief 发送 HID 释放报告，用于结束上一帧按键或消费控制动作。
 * @param release_type 需要释放的报告类型。
 * @return 成功返回 RT_EOK，失败返回负数 RT-Thread 错误码。
 */
static rt_err_t device_usb_send_release_report(device_usb_release_t release_type)
{
    switch (release_type)
    {
    case DEVICE_USB_RELEASE_CONSUMER:
        return usb_hid_send_consumer_usage(0U);

    case DEVICE_USB_RELEASE_KEYBOARD:
        return usb_hid_send_keyboard_report(0U, RT_NULL, 0U);

    default:
        return -RT_EINVAL;
    }
}

/**
 * @brief 发送指定快捷键槽位绑定的键盘 HID 报告。
 * @param slot 快捷键槽位。
 * @return 成功返回 RT_EOK，无操作预设返回 -RT_ENOSYS，失败返回负数错误码。
 */
static rt_err_t device_usb_send_shortcut(device_usb_shortcut_slot_t slot)
{
    const device_usb_shortcut_preset_entry_t *preset_entry;
    device_usb_shortcut_preset_t preset;

    if (((rt_uint32_t)slot) >= DEVICE_USB_SHORTCUT_SLOT_COUNT)
    {
        return -RT_EINVAL;
    }

    rt_mutex_take(&s_usb_lock, RT_WAITING_FOREVER);
    preset = device_usb_get_slot_preset_locked(slot);
    rt_mutex_release(&s_usb_lock);

    preset_entry = device_usb_get_preset_entry(preset);
    if (preset_entry == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (preset_entry->preset == DEVICE_USB_SHORTCUT_PRESET_NONE)
    {
        return -RT_ENOSYS;
    }

    return usb_hid_send_keyboard_report(preset_entry->binding.modifier,
                                        preset_entry->binding.keys,
                                        preset_entry->binding.key_count);
}

/**
 * @brief 更新网络服务状态快照。
 * @param busy 当前是否正在执行网络任务。
 * @param last_error 最近一次任务错误码。
 * @param active_job 当前网络任务类型。
 * @param ssid 当前任务关联的 SSID，可以为 RT_NULL。
 * @return 无。
 */
static void device_net_set_status(rt_bool_t busy,
                                  rt_err_t last_error,
                                  device_net_job_t active_job,
                                  const char *ssid)
{
    rt_mutex_take(&s_net_lock, RT_WAITING_FOREVER);
    s_net_status.started = RT_TRUE;
    s_net_status.busy = busy;
    s_net_status.last_error = last_error;
    s_net_status.active_job = active_job;
    if (ssid != RT_NULL)
    {
        rt_strncpy(s_net_status.ssid, ssid, sizeof(s_net_status.ssid) - 1U);
        s_net_status.ssid[sizeof(s_net_status.ssid) - 1U] = '\0';
    }
    else
    {
        s_net_status.ssid[0] = '\0';
    }
    rt_mutex_release(&s_net_lock);
}

/**
 * @brief 在网络锁保护下判断 Wi-Fi 扫描是否已经处于活动状态。
 * @param 无。
 * @return 扫描已挂起或正在执行返回 RT_TRUE，否则返回 RT_FALSE。
 */
static rt_bool_t device_scan_is_active_locked(void)
{
    return (s_scan_pending != RT_FALSE) ||
           ((s_net_status.busy != RT_FALSE) && (s_net_status.active_job == DEVICE_NET_JOB_WIFI_SCAN));
}

/**
 * @brief 在网络服务已初始化时更新当前网络任务状态。
 * @param busy 当前是否忙碌。
 * @param last_error 最近一次错误码。
 * @param active_job 当前任务类型。
 * @return 无。
 */
static void device_net_set_job_status(rt_bool_t busy,
                                      rt_err_t last_error,
                                      device_net_job_t active_job)
{
    if (s_net_inited)
    {
        device_net_set_status(busy,
                              last_error,
                              busy ? active_job : DEVICE_NET_JOB_NONE,
                              RT_NULL);
    }
}

/**
 * @brief 更新 USB HID 服务状态快照。
 * @param stack_started USB 协议栈是否已启动。
 * @param ready USB HID 是否枚举完成。
 * @param can_send 当前是否可以立即发送 HID 报告。
 * @param last_error 最近一次 USB 发送错误码。
 * @return 无。
 */
static void device_usb_update_status(rt_bool_t stack_started,
                                     rt_bool_t ready,
                                     rt_bool_t can_send,
                                     rt_err_t last_error)
{
    rt_uint32_t queued_steps;
    rt_uint16_t queued_shortcuts;

    rt_mutex_take(&s_usb_lock, RT_WAITING_FOREVER);
    queued_shortcuts = device_usb_count_shortcuts_locked();
    queued_steps = (rt_uint32_t)device_abs_i16(s_usb_pending_volume) +
                   (rt_uint32_t)device_abs_i16(s_usb_pending_brightness) +
                   (rt_uint32_t)queued_shortcuts +
                   ((s_usb_release_pending != DEVICE_USB_RELEASE_NONE) ? 1U : 0U);
    if (queued_steps > 65535U)
    {
        queued_steps = 65535U;
    }
    s_usb_status.started = RT_TRUE;
    s_usb_status.stack_started = stack_started;
    s_usb_status.ready = ready;
    s_usb_status.can_send = can_send;
    s_usb_status.last_error = last_error;
    s_usb_status.queued_steps = (rt_uint16_t)queued_steps;
    rt_mutex_release(&s_usb_lock);
}

/**
 * @brief 网络服务线程入口，串行处理 Wi-Fi 连接和待办上报请求。
 * @param parameter RT-Thread 线程参数，当前未使用。
 * @return 无。
 */
static void device_net_thread(void *parameter)
{
    device_net_msg_t msg;
    rt_err_t ret;

    RT_UNUSED(parameter);
    rt_kprintf("[DEVNET] thread boot\r\n");
    device_net_set_status(RT_FALSE, RT_EOK, DEVICE_NET_JOB_NONE, RT_NULL);
    rt_kprintf("[DEVNET] thread ready\r\n");

    while (1)
    {
        ret = rt_mq_recv(&s_net_mq, &msg, sizeof(msg), RT_WAITING_FOREVER);
        if (ret != RT_EOK)
        {
            continue;
        }

        switch (msg.cmd)
        {
        case DEVICE_NET_CMD_WIFI_CONNECT:
            device_net_set_status(RT_TRUE, RT_EOK, DEVICE_NET_JOB_WIFI_CONNECT, msg.ssid);
            ret = esp01s_join_saved_ap(msg.ssid, msg.ecn);
            device_net_set_status(RT_FALSE, ret, DEVICE_NET_JOB_NONE, msg.ssid);
            break;

        case DEVICE_NET_CMD_WIFI_REJOIN:
            device_net_set_status(RT_TRUE, RT_EOK, DEVICE_NET_JOB_WIFI_REJOIN, RT_NULL);
            ret = esp01s_join_default_ap();
            device_net_set_status(RT_FALSE, ret, DEVICE_NET_JOB_NONE, RT_NULL);
            break;

        default:
            device_net_set_status(RT_FALSE, -RT_EINVAL, DEVICE_NET_JOB_NONE, RT_NULL);
            break;
        }
    }
}

/**
 * @brief Wi-Fi 扫描线程入口，独立处理扫描请求以避免阻塞其它网络任务。
 * @param parameter RT-Thread 线程参数，当前未使用。
 * @return 无。
 */
static void device_scan_thread(void *parameter)
{
    rt_err_t ret;

    RT_UNUSED(parameter);
    rt_kprintf("[DEVSCAN] thread boot\r\n");

    while (1)
    {
        ret = rt_sem_take(&s_scan_sem, RT_WAITING_FOREVER);
        if (ret != RT_EOK)
        {
            continue;
        }

        while (rt_sem_take(&s_scan_sem, 0) == RT_EOK)
        {
        }

        device_net_set_status(RT_TRUE, RT_EOK, DEVICE_NET_JOB_WIFI_SCAN, RT_NULL);
        rt_mutex_take(&s_net_lock, RT_WAITING_FOREVER);
        s_scan_pending = RT_FALSE;
        rt_mutex_release(&s_net_lock);

        rt_kprintf("[DEVSCAN] wifi scan start\r\n");
        ret = esp01s_scan_aps();
        device_net_set_status(RT_FALSE, ret, DEVICE_NET_JOB_NONE, RT_NULL);
        rt_kprintf("[DEVSCAN] wifi scan done ret=%d\r\n", ret);
    }
}

/**
 * @brief USB HID 服务线程入口，处理音量、亮度和快捷键报告发送。
 * @param parameter RT-Thread 线程参数，当前未使用。
 * @return 无。
 */
static void device_usb_thread(void *parameter)
{
    rt_err_t last_error = RT_EOK;

    RT_UNUSED(parameter);

    while (1)
    {
        rt_bool_t stack_started = usb_hid_is_started();
        rt_bool_t ready = usb_hid_is_ready();
        rt_bool_t can_send = (ready != RT_FALSE) ? usb_hid_can_send() : RT_FALSE;
        device_usb_shortcut_slot_t shortcut_slot;
        rt_bool_t has_shortcut = RT_FALSE;
        rt_tick_t min_gap;
        rt_tick_t now;
        rt_int16_t target_volume;
        rt_int16_t target_brightness;
        rt_int16_t cmd_volume;
        rt_int16_t cmd_brightness;

        device_usb_scan_shortcut_buttons();

        rt_mutex_take(&s_usb_lock, RT_WAITING_FOREVER);
        target_volume = s_usb_target_volume;
        target_brightness = s_usb_target_brightness;
        cmd_volume = s_usb_cmd_volume;
        cmd_brightness = s_usb_cmd_brightness;
        s_usb_cmd_volume = 0;
        s_usb_cmd_brightness = 0;
        rt_mutex_release(&s_usb_lock);

        if (!ready)
        {
            s_usb_shadow_volume = target_volume;
            s_usb_shadow_brightness = target_brightness;
            s_usb_pending_volume = 0;
            s_usb_pending_brightness = 0;
            rt_mutex_take(&s_usb_lock, RT_WAITING_FOREVER);
            s_usb_cmd_volume = 0;
            s_usb_cmd_brightness = 0;
            device_usb_clear_shortcuts_locked();
            rt_mutex_release(&s_usb_lock);
            s_usb_release_pending = DEVICE_USB_RELEASE_NONE;
            s_usb_last_report_tick = 0U;
            device_usb_update_status(stack_started, RT_FALSE, RT_FALSE, last_error);
            rt_thread_mdelay(DEVICE_USB_IDLE_DELAY_MS);
            continue;
        }

        if (target_volume != s_usb_shadow_volume)
        {
            device_usb_queue_steps(&s_usb_pending_volume,
                                   (rt_int32_t)target_volume - (rt_int32_t)s_usb_shadow_volume);
            s_usb_shadow_volume = target_volume;
        }

        if (target_brightness != s_usb_shadow_brightness)
        {
            device_usb_queue_steps(&s_usb_pending_brightness,
                                   (rt_int32_t)target_brightness - (rt_int32_t)s_usb_shadow_brightness);
            s_usb_shadow_brightness = target_brightness;
        }

        if (cmd_volume != 0)
        {
            device_usb_accumulate_steps(&s_usb_pending_volume, cmd_volume);
        }

        if (cmd_brightness != 0)
        {
            device_usb_accumulate_steps(&s_usb_pending_brightness, cmd_brightness);
        }

        if (can_send)
        {
            rt_uint32_t poll_ms = usb_hid_get_polling_interval_ms();

            min_gap = (rt_tick_t)(((rt_uint64_t)poll_ms * RT_TICK_PER_SECOND + 999U) / 1000U);
            if (min_gap == 0U)
            {
                min_gap = 1U;
            }

            now = rt_tick_get();
            if ((s_usb_last_report_tick == 0U) || ((now - s_usb_last_report_tick) >= min_gap))
            {
                if (s_usb_release_pending != DEVICE_USB_RELEASE_NONE)
                {
                    if (device_usb_send_release_report(s_usb_release_pending) == RT_EOK)
                    {
                        s_usb_release_pending = DEVICE_USB_RELEASE_NONE;
                        s_usb_last_report_tick = now;
                        last_error = RT_EOK;
                    }
                    else
                    {
                        last_error = -RT_EBUSY;
                    }
                }
                else
                {
                    rt_uint16_t usage = 0U;

                    has_shortcut = device_usb_pop_shortcut(&shortcut_slot);
                    if (has_shortcut != RT_FALSE)
                    {
                        rt_err_t shortcut_ret = device_usb_send_shortcut(shortcut_slot);

                        if (shortcut_ret == RT_EOK)
                        {
                            s_usb_release_pending = DEVICE_USB_RELEASE_KEYBOARD;
                            s_usb_last_report_tick = now;
                            last_error = RT_EOK;
                        }
                        else if (shortcut_ret == -RT_ENOSYS)
                        {
                            last_error = RT_EOK;
                        }
                        else
                        {
                            rt_mutex_take(&s_usb_lock, RT_WAITING_FOREVER);
                            (void)device_usb_enqueue_shortcut_locked(shortcut_slot);
                            rt_mutex_release(&s_usb_lock);
                            last_error = -RT_EBUSY;
                        }
                    }
                    else if (s_usb_pending_volume > 0)
                    {
                        usage = HID_USAGE_VOLUME_INC;
                        s_usb_pending_volume--;
                    }
                    else if (s_usb_pending_volume < 0)
                    {
                        usage = HID_USAGE_VOLUME_DEC;
                        s_usb_pending_volume++;
                    }
                    else if (s_usb_pending_brightness > 0)
                    {
                        usage = HID_USAGE_BRIGHTNESS_INC;
                        s_usb_pending_brightness--;
                    }
                    else if (s_usb_pending_brightness < 0)
                    {
                        usage = HID_USAGE_BRIGHTNESS_DEC;
                        s_usb_pending_brightness++;
                    }

                    if (usage != 0U)
                    {
                        if (usb_hid_send_consumer_usage(usage) == RT_EOK)
                        {
                            s_usb_release_pending = DEVICE_USB_RELEASE_CONSUMER;
                            s_usb_last_report_tick = now;
                            last_error = RT_EOK;
                        }
                        else
                        {
                            last_error = -RT_EBUSY;
                        }
                    }
                }
            }
        }

        device_usb_update_status(stack_started, ready, can_send, last_error);
        rt_thread_mdelay(DEVICE_USB_IDLE_DELAY_MS);
    }
}

/**
 * @brief 将网络服务消息投递到设备网络队列。
 * @param msg 待投递的网络消息。
 * @return 投递成功返回 RT_EOK，失败返回负数 RT-Thread 错误码。
 */
static rt_err_t device_net_post(const device_net_msg_t *msg)
{
    if (msg == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (!s_service_started)
    {
        return -RT_ENOSYS;
    }

    return rt_mq_send(&s_net_mq, (void *)msg, sizeof(*msg));
}

/**
 * @brief 删除指定线程并清空线程句柄。
 * @param thread 线程句柄指针。
 * @return 无。
 */
static void device_service_delete_thread(rt_thread_t *thread)
{
    if ((thread != RT_NULL) && (*thread != RT_NULL))
    {
        rt_thread_delete(*thread);
        *thread = RT_NULL;
    }
}

/**
 * @brief 清理设备服务启动阶段已经创建的线程。
 * @param 无。
 * @return 无。
 */
static void device_service_cleanup_threads(void)
{
    device_service_delete_thread(&s_net_thread);
    device_service_delete_thread(&s_scan_thread);
    device_service_delete_thread(&s_usb_thread);
}

/**
 * @brief 创建设备服务线程。
 * @param thread 输出参数，用于保存线程句柄。
 * @param name 线程名称。
 * @param entry 线程入口函数。
 * @param stack_size 线程栈大小。
 * @param priority 线程优先级。
 * @return 成功返回 RT_EOK，失败返回负数错误码。
 */
static rt_err_t device_service_create_thread(rt_thread_t *thread,
                                             const char *name,
                                             void (*entry)(void *parameter),
                                             rt_uint32_t stack_size,
                                             rt_uint8_t priority)
{
    if (thread == RT_NULL)
    {
        return -RT_EINVAL;
    }

    *thread = rt_thread_create(name, entry, RT_NULL, stack_size, priority, 10);
    return (*thread != RT_NULL) ? RT_EOK : -RT_ENOMEM;
}

/**
 * @brief 启动设备服务线程并输出统一日志。
 * @param thread 需要启动的线程句柄。
 * @param tag 日志标签。
 * @param stack_size 线程栈大小，用于日志输出。
 * @return 成功返回 RT_EOK，失败返回 rt_thread_startup 的错误码。
 */
static rt_err_t device_service_start_thread(rt_thread_t thread,
                                            const char *tag,
                                            rt_uint32_t stack_size)
{
    rt_err_t ret = rt_thread_startup(thread);

    rt_kprintf("[%s] startup ret=%d stack=%u\r\n",
               tag,
               ret,
               (unsigned int)stack_size);

    return ret;
}

/**
 * @brief 启动设备服务中的网络队列、Wi-Fi 扫描线程和 USB HID 线程。
 * @param 无。
 * @return 成功返回 RT_EOK，失败返回负数 RT-Thread 错误码。
 */
int device_service_start(void)
{
    rt_err_t ret;

    if (s_service_started || s_service_starting)
    {
        return RT_EOK;
    }

    s_service_starting = RT_TRUE;

    if (!s_net_inited)
    {
        rt_err_t ret = rt_mq_init(&s_net_mq,
                                  "devnet",
                                  s_net_mq_pool,
                                  sizeof(device_net_msg_t),
                                  sizeof(s_net_mq_pool),
                                  RT_IPC_FLAG_PRIO);
        if (ret != RT_EOK)
        {
            s_service_starting = RT_FALSE;
            return ret;
        }

        rt_mutex_init(&s_net_lock, "devnetlk", RT_IPC_FLAG_PRIO);
        s_net_inited = RT_TRUE;
    }

    if (!s_scan_inited)
    {
        rt_sem_init(&s_scan_sem, "devscan", 0, RT_IPC_FLAG_PRIO);
        s_scan_inited = RT_TRUE;
    }

    if (!s_usb_inited)
    {
        rt_mutex_init(&s_usb_lock, "devusblk", RT_IPC_FLAG_PRIO);
        device_usb_shortcut_buttons_init();
        s_usb_inited = RT_TRUE;
    }

    ret = device_service_create_thread(&s_net_thread,
                                       "devnet",
                                       device_net_thread,
                                       DEVICE_NET_THREAD_STACK_SIZE,
                                       20);
    if (ret != RT_EOK)
    {
        s_service_starting = RT_FALSE;
        return ret;
    }

    ret = device_service_create_thread(&s_scan_thread,
                                       "devscan",
                                       device_scan_thread,
                                       DEVICE_SCAN_THREAD_STACK_SIZE,
                                       20);
    if (ret != RT_EOK)
    {
        device_service_cleanup_threads();
        s_service_starting = RT_FALSE;
        return ret;
    }

    ret = device_service_create_thread(&s_usb_thread,
                                       "devusb",
                                       device_usb_thread,
                                       DEVICE_USB_THREAD_STACK_SIZE,
                                       21);
    if (ret != RT_EOK)
    {
        device_service_cleanup_threads();
        s_service_starting = RT_FALSE;
        return ret;
    }

    ret = device_service_start_thread(s_net_thread, "DEVNET", DEVICE_NET_THREAD_STACK_SIZE);
    if (ret != RT_EOK)
    {
        device_service_cleanup_threads();
        s_service_starting = RT_FALSE;
        return ret;
    }

    ret = device_service_start_thread(s_scan_thread, "DEVSCAN", DEVICE_SCAN_THREAD_STACK_SIZE);
    if (ret != RT_EOK)
    {
        device_service_cleanup_threads();
        s_service_starting = RT_FALSE;
        return ret;
    }

    ret = device_service_start_thread(s_usb_thread, "DEVUSB", DEVICE_USB_THREAD_STACK_SIZE);
    if (ret != RT_EOK)
    {
        device_service_cleanup_threads();
        s_service_starting = RT_FALSE;
        return ret;
    }

    s_service_started = RT_TRUE;
    s_service_starting = RT_FALSE;
    return RT_EOK;
}

/**
 * @brief 请求执行一次异步 Wi-Fi 扫描。
 * @param 无。
 * @return 请求成功返回 RT_EOK，服务未启动返回 -RT_ENOSYS。
 */
rt_err_t device_net_service_request_scan(void)
{
    rt_err_t ret;

    if (!s_service_started || (s_scan_thread == RT_NULL))
    {
        return -RT_ENOSYS;
    }

    rt_mutex_take(&s_net_lock, RT_WAITING_FOREVER);
    if (device_scan_is_active_locked() != RT_FALSE)
    {
        rt_mutex_release(&s_net_lock);
        rt_kprintf("[DEVSCAN] request ignored busy=1\r\n");
        return RT_EOK;
    }
    s_scan_pending = RT_TRUE;
    rt_mutex_release(&s_net_lock);

    ret = rt_sem_release(&s_scan_sem);
    if (ret != RT_EOK)
    {
        rt_mutex_take(&s_net_lock, RT_WAITING_FOREVER);
        s_scan_pending = RT_FALSE;
        rt_mutex_release(&s_net_lock);
    }

    rt_kprintf("[DEVSCAN] request ret=%d thread=%d\r\n", ret, (rt_thread_find("devscan") != RT_NULL) ? 1 : 0);
    return ret;
}

/**
 * @brief 请求连接指定 Wi-Fi。
 * @param ssid 目标 Wi-Fi 名称。
 * @param ecn Wi-Fi 加密模式。
 * @return 请求入队成功返回 RT_EOK，参数无效或服务异常时返回负数错误码。
 */
rt_err_t device_net_service_request_connect(const char *ssid, rt_uint8_t ecn)
{
    device_net_msg_t msg;

    if ((ssid == RT_NULL) || (ssid[0] == '\0'))
    {
        return -RT_EINVAL;
    }

    memset(&msg, 0, sizeof(msg));
    msg.cmd = DEVICE_NET_CMD_WIFI_CONNECT;
    msg.ecn = ecn;
    rt_strncpy(msg.ssid, ssid, sizeof(msg.ssid) - 1U);
    msg.ssid[sizeof(msg.ssid) - 1U] = '\0';
    return device_net_post(&msg);
}

/**
 * @brief 请求重新连接默认 Wi-Fi。
 * @param 无。
 * @return 请求入队成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t device_net_service_request_rejoin(void)
{
    device_net_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.cmd = DEVICE_NET_CMD_WIFI_REJOIN;
    return device_net_post(&msg);
}

/**
 * @brief 请求从后端拉取待办列表。
 * @param 无。
 * @return 后端请求成功发起返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t device_net_service_request_todo_pull(void)
{
    rt_err_t ret;

    device_net_set_job_status(RT_TRUE, RT_EOK, DEVICE_NET_JOB_TODO_PULL);
    ret = aliyun_iot_request_todo_pull();
    device_net_set_job_status(RT_FALSE, ret, DEVICE_NET_JOB_TODO_PULL);

    rt_kprintf("[DEVNET] todo pull ret=%d\r\n", ret);

    return ret;
}

/**
 * @brief 将待办变更加入后端上报队列。
 * @param change 待上报的待办变更数据。
 * @return 入队成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t device_net_service_queue_todo_report(const todo_snapshot_change_t *change)
{
    rt_err_t ret;

    if (change == RT_NULL)
    {
        return -RT_EINVAL;
    }

    device_net_set_job_status(RT_TRUE, RT_EOK, DEVICE_NET_JOB_TODO_REPORT);
    ret = aliyun_iot_queue_todo_report(change);
    device_net_set_job_status(RT_FALSE, ret, DEVICE_NET_JOB_TODO_REPORT);

    rt_kprintf("[DEVNET] todo queue ret=%d id=%s completed=%d updatedAt=%llu\r\n",
               ret,
               change->item.id[0] ? change->item.id : "(empty)",
               change->item.completed ? 1 : 0,
               (unsigned long long)change->item.updated_at);

    return ret;
}

/**
 * @brief 获取网络服务状态快照。
 * @param status 输出参数，用于接收网络服务状态。
 * @return 无。
 */
void device_net_service_get_status(device_net_service_status_t *status)
{
    if (status == RT_NULL)
    {
        return;
    }

    memset(status, 0, sizeof(*status));
    if (!s_net_inited)
    {
        return;
    }

    rt_mutex_take(&s_net_lock, RT_WAITING_FOREVER);
    *status = s_net_status;
    rt_mutex_release(&s_net_lock);
}

/**
 * @brief 设置音量和亮度的绝对目标值。
 * @param volume UI 侧音量目标值。
 * @param brightness UI 侧亮度目标值。
 * @return 成功返回 RT_EOK，服务未启动返回 -RT_ENOSYS。
 */
rt_err_t device_usb_service_set_targets(rt_int16_t volume, rt_int16_t brightness)
{
    if (!s_service_started)
    {
        return -RT_ENOSYS;
    }

    volume = device_clamp_i16(volume, 0, 100);
    brightness = device_clamp_i16(brightness, 0, 100);

    rt_mutex_take(&s_usb_lock, RT_WAITING_FOREVER);
    s_usb_target_volume = volume;
    s_usb_target_brightness = brightness;
    rt_mutex_release(&s_usb_lock);

    return RT_EOK;
}

/**
 * @brief 追加音量和亮度的相对调节步数。
 * @param volume_delta 音量相对变化步数。
 * @param brightness_delta 亮度相对变化步数。
 * @return 成功返回 RT_EOK，服务未启动返回 -RT_ENOSYS。
 */
rt_err_t device_usb_service_adjust(rt_int16_t volume_delta, rt_int16_t brightness_delta)
{
    if (!s_service_started)
    {
        return -RT_ENOSYS;
    }

    rt_mutex_take(&s_usb_lock, RT_WAITING_FOREVER);
    device_usb_accumulate_steps(&s_usb_cmd_volume, (rt_int32_t)volume_delta);
    device_usb_accumulate_steps(&s_usb_cmd_brightness, (rt_int32_t)brightness_delta);
    rt_mutex_release(&s_usb_lock);

    return RT_EOK;
}

/**
 * @brief 触发指定快捷键槽位。
 * @param slot 快捷键槽位。
 * @return 成功返回 RT_EOK，服务未启动或槽位无效时返回负数错误码。
 */
rt_err_t device_usb_service_trigger_shortcut(device_usb_shortcut_slot_t slot)
{
    rt_err_t ret;

    if (!s_service_started)
    {
        return -RT_ENOSYS;
    }

    rt_mutex_take(&s_usb_lock, RT_WAITING_FOREVER);
    ret = device_usb_enqueue_shortcut_locked(slot);
    rt_mutex_release(&s_usb_lock);

    return ret;
}

/**
 * @brief 设置快捷键槽位绑定的预设组合键。
 * @param slot 快捷键槽位。
 * @param preset 预设组合键。
 * @return 成功返回 RT_EOK，参数无效返回 -RT_EINVAL。
 */
rt_err_t device_usb_service_set_shortcut_preset(device_usb_shortcut_slot_t slot,
                                                device_usb_shortcut_preset_t preset)
{
    if (((rt_uint32_t)slot) >= DEVICE_USB_SHORTCUT_SLOT_COUNT)
    {
        return -RT_EINVAL;
    }

    if (((rt_uint32_t)preset) >= DEVICE_USB_SHORTCUT_PRESET_COUNT)
    {
        return -RT_EINVAL;
    }

    rt_mutex_take(&s_usb_lock, RT_WAITING_FOREVER);
    s_usb_shortcut_slot_presets[slot] = preset;
    rt_mutex_release(&s_usb_lock);

    return RT_EOK;
}

/**
 * @brief 获取快捷键槽位绑定的预设组合键。
 * @param slot 快捷键槽位。
 * @return 当前预设组合键，槽位无效时返回 DEVICE_USB_SHORTCUT_PRESET_NONE。
 */
device_usb_shortcut_preset_t device_usb_service_get_shortcut_preset(device_usb_shortcut_slot_t slot)
{
    device_usb_shortcut_preset_t preset = DEVICE_USB_SHORTCUT_PRESET_NONE;

    if (((rt_uint32_t)slot) >= DEVICE_USB_SHORTCUT_SLOT_COUNT)
    {
        return DEVICE_USB_SHORTCUT_PRESET_NONE;
    }

    if (!s_usb_inited)
    {
        return s_usb_shortcut_slot_presets[slot];
    }

    rt_mutex_take(&s_usb_lock, RT_WAITING_FOREVER);
    preset = s_usb_shortcut_slot_presets[slot];
    rt_mutex_release(&s_usb_lock);

    return preset;
}

/**
 * @brief 获取快捷键预设的中文显示名称。
 * @param preset 快捷键预设。
 * @return 指向静态 UTF-8 字符串的指针。
 */
const char *device_usb_service_get_shortcut_preset_label(device_usb_shortcut_preset_t preset)
{
    const device_usb_shortcut_preset_entry_t *preset_entry = device_usb_get_preset_entry(preset);

    if (preset_entry == RT_NULL)
    {
        return "\xE6\x97\xA0\xE6\x93\x8D\xE4\xBD\x9C";
    }

    return preset_entry->label;
}

/**
 * @brief 获取 USB HID 服务状态快照。
 * @param status 输出参数，用于接收 USB HID 服务状态。
 * @return 无。
 */
void device_usb_service_get_status(device_usb_service_status_t *status)
{
    if (status == RT_NULL)
    {
        return;
    }

    memset(status, 0, sizeof(*status));
    if (!s_usb_inited)
    {
        return;
    }

    rt_mutex_take(&s_usb_lock, RT_WAITING_FOREVER);
    *status = s_usb_status;
    rt_mutex_release(&s_usb_lock);
}
