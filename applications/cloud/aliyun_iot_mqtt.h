#ifndef __ALIYUN_IOT_MQTT_H__
#define __ALIYUN_IOT_MQTT_H__

#include <rtthread.h>
#include "data/todo_snapshot.h"
#include "sensor/dht11.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    rt_bool_t started;
    rt_bool_t wifi_ready;
    rt_bool_t mqtt_connected;
    rt_bool_t subscribed;
    rt_uint32_t publish_count;
    rt_uint32_t recv_count;
    rt_uint32_t reconnect_count;
    rt_err_t last_error;
    char state[16];
    char host[128];
    char topic[160];
    char sub_topic[160];
    char client_id[128];
} aliyun_iot_status_t;

typedef struct
{
    rt_bool_t has_sample;
    rt_err_t last_read_error;
    rt_tick_t sample_tick;
    dht11_sample_t sample;
} aliyun_iot_sensor_status_t;

/**
 * @brief 启动阿里云 MQTT 工作线程和连接维护逻辑。
 * @param 无。
 * @return 成功返回 RT_EOK，失败返回负数 RT-Thread 错误码。
 */
int aliyun_iot_start(void);

/**
 * @brief 复制当前阿里云 MQTT 连接状态。
 * @param status 输出参数，用于接收最新 MQTT 状态快照。
 * @return 无。
 */
void aliyun_iot_get_status(aliyun_iot_status_t *status);

/**
 * @brief 复制阿里云发布线程当前使用的传感器采样状态。
 * @param status 输出参数，用于接收最新传感器状态快照。
 * @return 无。
 */
void aliyun_iot_get_sensor_status(aliyun_iot_sensor_status_t *status);

/**
 * @brief 将待办事项状态变化加入 MQTT 上报队列。
 * @param change 待办变化数据，包含 id、updated_at 和 completed 状态。
 * @return 入队成功返回 RT_EOK，失败返回负数 RT-Thread 错误码。
 */
rt_err_t aliyun_iot_queue_todo_report(const todo_snapshot_change_t *change);

/**
 * @brief 向后端请求拉取待办列表。
 * @param 无。
 * @return 请求被接受返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t aliyun_iot_request_todo_pull(void);

#ifdef __cplusplus
}
#endif

#endif /* __ALIYUN_IOT_MQTT_H__ */

