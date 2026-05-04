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
    DEVICE_NET_JOB_TODO_PULL,
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
    DEVICE_USB_SHORTCUT_PRESET_COPY,
    DEVICE_USB_SHORTCUT_PRESET_PASTE,
    DEVICE_USB_SHORTCUT_PRESET_CUT,
    DEVICE_USB_SHORTCUT_PRESET_SELECT_ALL,
    DEVICE_USB_SHORTCUT_PRESET_UNDO,
    DEVICE_USB_SHORTCUT_PRESET_REDO,
    DEVICE_USB_SHORTCUT_PRESET_COUNT,
} device_usb_shortcut_preset_t;

/**
 * @brief 启动设备层网络、Wi-Fi 扫描和 USB HID 工作线程。
 * @param 无。
 * @return 成功返回 RT_EOK，失败返回负数 RT-Thread 错误码。
 */
int device_service_start(void);

/**
 * @brief 请求执行一次异步 Wi-Fi 扫描。
 * @param 无。
 * @return 扫描请求被接受或已经存在待执行扫描时返回 RT_EOK。
 */
rt_err_t device_net_service_request_scan(void);

/**
 * @brief 请求异步连接到扫描列表中的 Wi-Fi。
 * @param ssid 目标 Wi-Fi 名称。
 * @param ecn 扫描结果中记录的加密模式。
 * @return 请求入队成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t device_net_service_request_connect(const char *ssid, rt_uint8_t ecn);

/**
 * @brief 请求重新连接默认或预设 Wi-Fi。
 * @param 无。
 * @return 请求入队成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t device_net_service_request_rejoin(void);

/**
 * @brief 请求从后端拉取待办列表。
 * @param 无。
 * @return 后端请求被接受返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t device_net_service_request_todo_pull(void);

/**
 * @brief 向后端队列提交最小待办状态上报。
 * @param change 已变更待办项数据，包含 id、updated_at 和 completed。
 * @return 入队成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t device_net_service_queue_todo_report(const todo_snapshot_change_t *change);

/**
 * @brief 复制网络服务运行状态。
 * @param status 输出参数，用于接收一致性的状态快照。
 * @return 无。
 */
void device_net_service_get_status(device_net_service_status_t *status);

/**
 * @brief 设置 USB 音量和亮度同步的 UI 目标值。
 * @param volume UI 侧音量目标值。
 * @param brightness UI 侧亮度目标值。
 * @return 成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t device_usb_service_set_targets(rt_int16_t volume, rt_int16_t brightness);

/**
 * @brief 提交相对的 HID 音量和亮度调节步数。
 * @param volume_delta 音量相对变化步数。
 * @param brightness_delta 亮度相对变化步数。
 * @return 入队成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t device_usb_service_adjust(rt_int16_t volume_delta, rt_int16_t brightness_delta);

/**
 * @brief 为指定物理按键或 UI 槽位提交一次快捷键动作。
 * @param slot 快捷键槽位索引。
 * @return 入队成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t device_usb_service_trigger_shortcut(device_usb_shortcut_slot_t slot);

/**
 * @brief 为快捷键槽位设置预设组合键。
 * @param slot 快捷键槽位索引。
 * @param preset 预设组合键标识。
 * @return 成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t device_usb_service_set_shortcut_preset(device_usb_shortcut_slot_t slot,
                                                device_usb_shortcut_preset_t preset);

/**
 * @brief 读取指定快捷键槽位当前绑定的预设组合键。
 * @param slot 快捷键槽位索引。
 * @return 已绑定的预设；槽位无效时返回 DEVICE_USB_SHORTCUT_PRESET_NONE。
 */
device_usb_shortcut_preset_t device_usb_service_get_shortcut_preset(device_usb_shortcut_slot_t slot);

/**
 * @brief 获取快捷键预设对应的 UI 显示名称。
 * @param preset 预设组合键标识。
 * @return 指向静态 UTF-8 名称字符串的指针。
 */
const char *device_usb_service_get_shortcut_preset_label(device_usb_shortcut_preset_t preset);

/**
 * @brief 复制 USB HID 服务运行状态。
 * @param status 输出参数，用于接收一致性的状态快照。
 * @return 无。
 */
void device_usb_service_get_status(device_usb_service_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* __DEVICE_SERVICE_H__ */
