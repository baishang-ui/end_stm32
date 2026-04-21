#include "device_service.h"

#include <string.h>

#include <rtdevice.h>
#include <drv_gpio.h>

#include "network/esp01s_at.h"
#include "cloud/aliyun_iot_mqtt.h"
#include "usb_d.h"

#define DEVICE_NET_QUEUE_DEPTH              8U
#define DEVICE_USB_LEVEL_STEP               2
#define DEVICE_USB_PENDING_MAX              32767
#define DEVICE_USB_IDLE_DELAY_MS            5
#define DEVICE_USB_SHORTCUT_QUEUE_LIMIT     4U
#define DEVICE_USB_BUTTON_DEBOUNCE_MS       18U

/* External shortcut buttons: active-low, one side to GPIO and one side to GND. */
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

#define HID_KBD_KEY_D               0x07U
#define HID_KBD_KEY_E               0x08U
#define HID_KBD_KEY_I               0x0CU
#define HID_KBD_KEY_L               0x0FU
#define HID_KBD_KEY_R               0x15U
#define HID_KBD_KEY_S               0x16U
#define HID_KBD_KEY_ESCAPE          0x29U
#define HID_KBD_KEY_TAB             0x2BU

typedef enum
{
    DEVICE_NET_CMD_WIFI_SCAN = 0,
    DEVICE_NET_CMD_WIFI_CONNECT,
    DEVICE_NET_CMD_WIFI_REJOIN,
    DEVICE_NET_CMD_TODO_REPORT,
} device_net_cmd_t;

typedef struct
{
    rt_uint8_t cmd;
    rt_uint8_t ecn;
    char ssid[33];
    todo_snapshot_change_t change;
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
};

static struct rt_messagequeue s_net_mq;
static char s_net_mq_pool[sizeof(device_net_msg_t) * DEVICE_NET_QUEUE_DEPTH];
static struct rt_mutex s_net_lock;
static struct rt_mutex s_usb_lock;
static rt_bool_t s_net_inited = RT_FALSE;
static rt_bool_t s_usb_inited = RT_FALSE;
static rt_bool_t s_service_started = RT_FALSE;
static rt_bool_t s_service_starting = RT_FALSE;

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

static rt_uint16_t device_abs_i16(rt_int16_t value)
{
    return (value < 0) ? (rt_uint16_t)(-((rt_int32_t)value)) : (rt_uint16_t)value;
}

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

static void device_usb_clear_shortcuts_locked(void)
{
    memset(s_usb_shortcut_pending, 0, sizeof(s_usb_shortcut_pending));
}

static const device_usb_shortcut_preset_entry_t *device_usb_get_preset_entry(device_usb_shortcut_preset_t preset)
{
    if (((rt_uint32_t)preset) >= DEVICE_USB_SHORTCUT_PRESET_COUNT)
    {
        return RT_NULL;
    }

    return &s_usb_shortcut_presets[preset];
}

static device_usb_shortcut_preset_t device_usb_get_slot_preset_locked(device_usb_shortcut_slot_t slot)
{
    if (((rt_uint32_t)slot) >= DEVICE_USB_SHORTCUT_SLOT_COUNT)
    {
        return DEVICE_USB_SHORTCUT_PRESET_NONE;
    }

    return s_usb_shortcut_slot_presets[slot];
}

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

static void device_net_thread(void *parameter)
{
    device_net_msg_t msg;
    rt_err_t ret;

    RT_UNUSED(parameter);
    device_net_set_status(RT_FALSE, RT_EOK, DEVICE_NET_JOB_NONE, RT_NULL);

    while (1)
    {
        ret = rt_mq_recv(&s_net_mq, &msg, sizeof(msg), RT_WAITING_FOREVER);
        if (ret != RT_EOK)
        {
            continue;
        }

        switch (msg.cmd)
        {
        case DEVICE_NET_CMD_WIFI_SCAN:
            device_net_set_status(RT_TRUE, RT_EOK, DEVICE_NET_JOB_WIFI_SCAN, RT_NULL);
            ret = esp01s_scan_aps();
            device_net_set_status(RT_FALSE, ret, DEVICE_NET_JOB_NONE, RT_NULL);
            break;

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

        case DEVICE_NET_CMD_TODO_REPORT:
            device_net_set_status(RT_TRUE, RT_EOK, DEVICE_NET_JOB_TODO_REPORT, RT_NULL);
            ret = aliyun_iot_queue_todo_report(&msg.change);
            device_net_set_status(RT_FALSE, ret, DEVICE_NET_JOB_NONE, RT_NULL);
            break;

        default:
            device_net_set_status(RT_FALSE, -RT_EINVAL, DEVICE_NET_JOB_NONE, RT_NULL);
            break;
        }
    }
}

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

int device_service_start(void)
{
    rt_thread_t net_thread;
    rt_thread_t usb_thread;

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

    if (!s_usb_inited)
    {
        rt_mutex_init(&s_usb_lock, "devusblk", RT_IPC_FLAG_PRIO);
        device_usb_shortcut_buttons_init();
        s_usb_inited = RT_TRUE;
    }

    net_thread = rt_thread_create("devnet", device_net_thread, RT_NULL, 3072, 20, 10);
    if (net_thread == RT_NULL)
    {
        s_service_starting = RT_FALSE;
        return -RT_ENOMEM;
    }

    usb_thread = rt_thread_create("devusb", device_usb_thread, RT_NULL, 3072, 21, 10);
    if (usb_thread == RT_NULL)
    {
        rt_thread_delete(net_thread);
        s_service_starting = RT_FALSE;
        return -RT_ENOMEM;
    }

    rt_thread_startup(net_thread);
    rt_thread_startup(usb_thread);

    s_service_started = RT_TRUE;
    s_service_starting = RT_FALSE;
    return RT_EOK;
}

rt_err_t device_net_service_request_scan(void)
{
    device_net_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.cmd = DEVICE_NET_CMD_WIFI_SCAN;
    return device_net_post(&msg);
}

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

rt_err_t device_net_service_request_rejoin(void)
{
    device_net_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.cmd = DEVICE_NET_CMD_WIFI_REJOIN;
    return device_net_post(&msg);
}

rt_err_t device_net_service_request_todo_pull(void)
{
    rt_err_t ret;

    if (s_net_inited)
    {
        device_net_set_status(RT_TRUE, RT_EOK, DEVICE_NET_JOB_TODO_PULL, RT_NULL);
    }

    ret = aliyun_iot_request_todo_pull();

    if (s_net_inited)
    {
        device_net_set_status(RT_FALSE, ret, DEVICE_NET_JOB_NONE, RT_NULL);
    }

    rt_kprintf("[DEVNET] todo pull ret=%d\r\n", ret);

    return ret;
}

rt_err_t device_net_service_queue_todo_report(const todo_snapshot_change_t *change)
{
    rt_err_t ret;

    if (change == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (s_net_inited)
    {
        device_net_set_status(RT_TRUE, RT_EOK, DEVICE_NET_JOB_TODO_REPORT, RT_NULL);
    }

    ret = aliyun_iot_queue_todo_report(change);

    if (s_net_inited)
    {
        device_net_set_status(RT_FALSE, ret, DEVICE_NET_JOB_NONE, RT_NULL);
    }

    rt_kprintf("[DEVNET] todo queue ret=%d id=%s completed=%d updatedAt=%llu\r\n",
               ret,
               change->item.id[0] ? change->item.id : "(empty)",
               change->item.completed ? 1 : 0,
               (unsigned long long)change->item.updated_at);

    return ret;
}

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

rt_err_t device_usb_service_set_targets(rt_int16_t volume, rt_int16_t brightness)
{
    if (!s_service_started)
    {
        return -RT_ENOSYS;
    }

    if (volume < 0)
    {
        volume = 0;
    }
    else if (volume > 100)
    {
        volume = 100;
    }

    if (brightness < 0)
    {
        brightness = 0;
    }
    else if (brightness > 100)
    {
        brightness = 100;
    }

    rt_mutex_take(&s_usb_lock, RT_WAITING_FOREVER);
    s_usb_target_volume = volume;
    s_usb_target_brightness = brightness;
    rt_mutex_release(&s_usb_lock);

    return RT_EOK;
}

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

const char *device_usb_service_get_shortcut_preset_label(device_usb_shortcut_preset_t preset)
{
    const device_usb_shortcut_preset_entry_t *preset_entry = device_usb_get_preset_entry(preset);

    if (preset_entry == RT_NULL)
    {
        return "\xE6\x97\xA0\xE6\x93\x8D\xE4\xBD\x9C";
}

    return preset_entry->label;
}

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
