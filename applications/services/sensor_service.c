#include "sensor_service.h"

#include <string.h>

#define SENSOR_POLL_MS  2000U

static struct rt_mutex s_sensor_lock;
static rt_bool_t s_sensor_lock_inited = RT_FALSE;
static rt_bool_t s_sensor_started = RT_FALSE;
static sensor_service_status_t s_sensor_status;
static rt_uint32_t s_sensor_fail_count = 0U;

/**
 * @brief 更新传感器服务状态缓存。
 * @param ret 本次 DHT11 读取结果。
 * @param attempt_tick 本次尝试读取时的系统 tick。
 * @param sample 成功读取到的采样数据，读取失败时为 RT_NULL。
 * @return 无。
 */
static void sensor_service_set_status(rt_err_t ret,
                                      rt_tick_t attempt_tick,
                                      const dht11_sample_t *sample)
{
    rt_mutex_take(&s_sensor_lock, RT_WAITING_FOREVER);
    s_sensor_status.started = RT_TRUE;
    s_sensor_status.last_attempt_tick = attempt_tick;
    s_sensor_status.last_error = ret;
    if ((ret == RT_EOK) && (sample != RT_NULL))
    {
        s_sensor_status.sample = *sample;
        s_sensor_status.sample_tick = attempt_tick;
        s_sensor_status.has_sample = RT_TRUE;
    }
    rt_mutex_release(&s_sensor_lock);
}

/**
 * @brief 传感器服务线程入口，周期性读取 DHT11 并缓存最新状态。
 * @param parameter RT-Thread 线程参数，当前未使用。
 * @return 无。
 */
static void sensor_service_thread(void *parameter)
{
    RT_UNUSED(parameter);

    while (1)
    {
        dht11_sample_t sample;
        rt_tick_t attempt_tick = rt_tick_get();
        rt_err_t ret = dht11_read(&sample);

        sensor_service_set_status(ret, attempt_tick, (ret == RT_EOK) ? &sample : RT_NULL);

        if (ret == RT_EOK)
        {
            s_sensor_fail_count = 0U;
        }
        else
        {
            s_sensor_fail_count++;
            if ((s_sensor_fail_count == 1U) || ((s_sensor_fail_count % 10U) == 0U))
            {
                rt_kprintf("[DHT11] sensor thread read failed: %d count=%lu\r\n",
                           ret,
                           (unsigned long)s_sensor_fail_count);
            }
        }

        rt_thread_mdelay(SENSOR_POLL_MS);
    }
}

/**
 * @brief 启动后台传感器轮询线程。
 * @param 无。
 * @return 成功返回 RT_EOK，失败返回负数 RT-Thread 错误码。
 */
int sensor_service_start(void)
{
    rt_thread_t thread;

    if (s_sensor_started)
    {
        return RT_EOK;
    }

    if (!s_sensor_lock_inited)
    {
        rt_mutex_init(&s_sensor_lock, "sensorlk", RT_IPC_FLAG_PRIO);
        s_sensor_lock_inited = RT_TRUE;
    }

    memset(&s_sensor_status, 0, sizeof(s_sensor_status));
    s_sensor_status.last_error = -RT_EEMPTY;

    thread = rt_thread_create("sensor", sensor_service_thread, RT_NULL, 2048, 19, 10);
    if (thread == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    s_sensor_started = RT_TRUE;
    rt_thread_startup(thread);
    return RT_EOK;
}

/**
 * @brief 复制传感器服务当前状态。
 * @param status 输出参数，用于接收状态快照。
 * @return 无。
 */
void sensor_service_get_status(sensor_service_status_t *status)
{
    if (status == RT_NULL)
    {
        return;
    }

    memset(status, 0, sizeof(*status));
    if (!s_sensor_lock_inited)
    {
        return;
    }

    rt_mutex_take(&s_sensor_lock, RT_WAITING_FOREVER);
    *status = s_sensor_status;
    rt_mutex_release(&s_sensor_lock);
}
