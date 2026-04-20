#include "device_service.h"

#include <string.h>

#include "network/esp01s_at.h"
#include "cloud/aliyun_iot_mqtt.h"

#define DEVICE_NET_QUEUE_DEPTH      8U
#define DEVICE_USB_LEVEL_STEP       2
#define DEVICE_USB_PENDING_LIMIT    120
#define DEVICE_USB_IDLE_DELAY_MS    5

#define HID_USAGE_BRIGHTNESS_INC    0x006FU
#define HID_USAGE_BRIGHTNESS_DEC    0x0070U
#define HID_USAGE_VOLUME_INC        0x00E9U
#define HID_USAGE_VOLUME_DEC        0x00EAU

extern rt_bool_t usb_hid_is_ready(void);
extern rt_bool_t usb_hid_is_started(void);
extern rt_bool_t usb_hid_can_send(void);
extern rt_err_t usb_hid_send_consumer_usage(rt_uint16_t usage);
extern rt_uint32_t usb_hid_get_polling_interval_ms(void);

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
static rt_uint8_t s_usb_release_pending = 0U;
static rt_tick_t s_usb_last_report_tick = 0U;

static rt_int16_t device_abs_i16(rt_int16_t value)
{
    return (value < 0) ? (rt_int16_t)(-value) : value;
}

static void device_usb_queue_steps(rt_int16_t *pending, rt_int32_t delta)
{
    rt_int32_t steps;
    rt_int32_t total;

    if ((pending == RT_NULL) || (delta == 0))
    {
        return;
    }

    steps = delta / DEVICE_USB_LEVEL_STEP;
    if (steps == 0)
    {
        steps = (delta > 0) ? 1 : -1;
    }

    total = (rt_int32_t)(*pending) + steps;
    if (total > DEVICE_USB_PENDING_LIMIT)
    {
        total = DEVICE_USB_PENDING_LIMIT;
    }
    else if (total < -DEVICE_USB_PENDING_LIMIT)
    {
        total = -DEVICE_USB_PENDING_LIMIT;
    }

    *pending = (rt_int16_t)total;
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
    rt_uint16_t queued_steps;

    queued_steps = (rt_uint16_t)(device_abs_i16(s_usb_pending_volume) +
                                 device_abs_i16(s_usb_pending_brightness) +
                                 (s_usb_release_pending ? 1 : 0));

    rt_mutex_take(&s_usb_lock, RT_WAITING_FOREVER);
    s_usb_status.started = RT_TRUE;
    s_usb_status.stack_started = stack_started;
    s_usb_status.ready = ready;
    s_usb_status.can_send = can_send;
    s_usb_status.last_error = last_error;
    s_usb_status.queued_steps = queued_steps;
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
        rt_tick_t min_gap;
        rt_tick_t now;
        rt_int16_t target_volume;
        rt_int16_t target_brightness;

        rt_mutex_take(&s_usb_lock, RT_WAITING_FOREVER);
        target_volume = s_usb_target_volume;
        target_brightness = s_usb_target_brightness;
        rt_mutex_release(&s_usb_lock);

        if (!ready)
        {
            s_usb_shadow_volume = target_volume;
            s_usb_shadow_brightness = target_brightness;
            s_usb_pending_volume = 0;
            s_usb_pending_brightness = 0;
            s_usb_release_pending = 0U;
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
                if (s_usb_release_pending != 0U)
                {
                    if (usb_hid_send_consumer_usage(0U) == RT_EOK)
                    {
                        s_usb_release_pending = 0U;
                        s_usb_last_report_tick = now;
                        last_error = RT_EOK;
                    }
                }
                else
                {
                    rt_uint16_t usage = 0U;

                    if (s_usb_pending_volume > 0)
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
                            s_usb_release_pending = 1U;
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

rt_err_t device_net_service_queue_todo_report(const todo_snapshot_change_t *change)
{
    device_net_msg_t msg;

    if (change == RT_NULL)
    {
        return -RT_EINVAL;
    }

    memset(&msg, 0, sizeof(msg));
    msg.cmd = DEVICE_NET_CMD_TODO_REPORT;
    msg.change = *change;
    return device_net_post(&msg);
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
