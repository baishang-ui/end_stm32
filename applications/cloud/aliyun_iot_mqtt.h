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

int aliyun_iot_start(void);
void aliyun_iot_get_status(aliyun_iot_status_t *status);
void aliyun_iot_get_sensor_status(aliyun_iot_sensor_status_t *status);
rt_err_t aliyun_iot_queue_todo_report(const todo_snapshot_change_t *change);
rt_err_t aliyun_iot_request_todo_pull(void);

#ifdef __cplusplus
}
#endif

#endif /* __ALIYUN_IOT_MQTT_H__ */

