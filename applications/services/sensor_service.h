#ifndef __SENSOR_SERVICE_H__
#define __SENSOR_SERVICE_H__

#include <rtthread.h>

#include "sensor/dht11.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    rt_bool_t started;
    rt_bool_t has_sample;
    rt_err_t last_error;
    rt_tick_t sample_tick;
    rt_tick_t last_attempt_tick;
    dht11_sample_t sample;
} sensor_service_status_t;

/**
 * @brief 启动后台传感器轮询服务。
 * @param 无。
 * @return 成功返回 RT_EOK，失败返回负数 RT-Thread 错误码。
 */
int sensor_service_start(void);

/**
 * @brief 复制最新传感器服务状态和缓存的 DHT11 采样。
 * @param status 输出参数，用于接收一致性的服务状态快照。
 * @return 无。
 */
void sensor_service_get_status(sensor_service_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_SERVICE_H__ */
