#ifndef __DEVICE_SERVICE_H__
#define __DEVICE_SERVICE_H__

#include <rtthread.h>

#include "data/todo_snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    DEVICE_NET_JOB_NONE = 0,
    DEVICE_NET_JOB_WIFI_SCAN,
    DEVICE_NET_JOB_WIFI_CONNECT,
    DEVICE_NET_JOB_WIFI_REJOIN,
    DEVICE_NET_JOB_TODO_REPORT,
} device_net_job_t;

typedef struct
{
    rt_bool_t started;
    rt_bool_t busy;
    rt_err_t last_error;
    device_net_job_t active_job;
    char ssid[33];
} device_net_service_status_t;

typedef struct
{
    rt_bool_t started;
    rt_bool_t stack_started;
    rt_bool_t ready;
    rt_bool_t can_send;
    rt_err_t last_error;
    rt_uint16_t queued_steps;
} device_usb_service_status_t;

typedef enum
{
    DEVICE_USB_SHORTCUT_SLOT_1 = 0,
    DEVICE_USB_SHORTCUT_SLOT_2,
    DEVICE_USB_SHORTCUT_SLOT_3,
    DEVICE_USB_SHORTCUT_SLOT_4,
    DEVICE_USB_SHORTCUT_SLOT_COUNT,
} device_usb_shortcut_slot_t;

typedef device_usb_shortcut_slot_t device_usb_shortcut_id_t;

typedef enum
{
    DEVICE_USB_SHORTCUT_PRESET_NONE = 0,
    DEVICE_USB_SHORTCUT_PRESET_SHOW_DESKTOP,
    DEVICE_USB_SHORTCUT_PRESET_FILE_MANAGER,
    DEVICE_USB_SHORTCUT_PRESET_TASK_SWITCHER,
    DEVICE_USB_SHORTCUT_PRESET_TASK_MANAGER,
    DEVICE_USB_SHORTCUT_PRESET_LOCK_PC,
    DEVICE_USB_SHORTCUT_PRESET_SNIP_TOOL,
    DEVICE_USB_SHORTCUT_PRESET_SYSTEM_SETTINGS,
    DEVICE_USB_SHORTCUT_PRESET_RUN_DIALOG,
    DEVICE_USB_SHORTCUT_PRESET_COUNT,
} device_usb_shortcut_preset_t;

int device_service_start(void);

rt_err_t device_net_service_request_scan(void);
rt_err_t device_net_service_request_connect(const char *ssid, rt_uint8_t ecn);
rt_err_t device_net_service_request_rejoin(void);
rt_err_t device_net_service_queue_todo_report(const todo_snapshot_change_t *change);
void device_net_service_get_status(device_net_service_status_t *status);

rt_err_t device_usb_service_set_targets(rt_int16_t volume, rt_int16_t brightness);
rt_err_t device_usb_service_adjust(rt_int16_t volume_delta, rt_int16_t brightness_delta);
rt_err_t device_usb_service_trigger_shortcut(device_usb_shortcut_slot_t slot);
rt_err_t device_usb_service_set_shortcut_preset(device_usb_shortcut_slot_t slot,
                                                device_usb_shortcut_preset_t preset);
device_usb_shortcut_preset_t device_usb_service_get_shortcut_preset(device_usb_shortcut_slot_t slot);
const char *device_usb_service_get_shortcut_preset_label(device_usb_shortcut_preset_t preset);
void device_usb_service_get_status(device_usb_service_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* __DEVICE_SERVICE_H__ */
