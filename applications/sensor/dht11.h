#ifndef __DHT11_H__
#define __DHT11_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    rt_int32_t temperature_x10;
    rt_int32_t humidity_x10;
} dht11_sample_t;

/**
 * @brief 初始化 DHT11 的 GPIO 总线和时序状态。
 * @param 无。
 * @return 成功返回 RT_EOK，失败返回负数 RT-Thread 错误码。
 */
rt_err_t dht11_init(void);

/**
 * @brief 从 DHT11 读取一次温湿度采样。
 * @param sample 输出参数，用于接收 temperature_x10 和 humidity_x10。
 * @return 校验成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t dht11_read(dht11_sample_t *sample);

#ifdef __cplusplus
}
#endif

#endif /* __DHT11_H__ */
