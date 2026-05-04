#include "dht11.h"

#include <board.h>
#include <drv_gpio.h>
#include <string.h>

#define DBG_TAG "dht11"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/*
 * 建议接线：
 * - VCC  -> 3V3
 * - GND  -> GND
 * - DATA -> PB15
 * - DATA -> 4.7k~10k 上拉电阻 -> 3V3
 */
#define DHT11_PIN          GET_PIN(B, 15)
#define DHT11_START_MS     20U
#define DHT11_RESP_US      100U
#define DHT11_BIT_WAIT_US  80U

static rt_bool_t g_dht11_ready = RT_FALSE;

/**
 * @brief 将 DHT11 数据引脚切换为开漏输出模式。
 * @param 无。
 * @return 无。
 */
static void dht11_set_output(void)
{
    rt_pin_mode(DHT11_PIN, PIN_MODE_OUTPUT_OD);
}

/**
 * @brief 将 DHT11 数据引脚切换为上拉输入模式。
 * @param 无。
 * @return 无。
 */
static void dht11_set_input(void)
{
    rt_pin_mode(DHT11_PIN, PIN_MODE_INPUT_PULLUP);
}

/**
 * @brief 读取 DHT11 数据引脚当前电平。
 * @param 无。
 * @return 高电平返回 1，低电平返回 0。
 */
static rt_uint8_t dht11_read_level(void)
{
    return (rt_uint8_t)(rt_pin_read(DHT11_PIN) == PIN_HIGH ? 1U : 0U);
}

/**
 * @brief 等待 DHT11 数据引脚进入指定电平。
 * @param level 目标电平，0 或 1。
 * @param timeout_us 超时时间，单位微秒。
 * @return 等到目标电平返回 RT_EOK，超时返回 -RT_ETIMEOUT。
 */
static rt_err_t dht11_wait_level(rt_uint8_t level, rt_uint32_t timeout_us)
{
    while (timeout_us-- > 0U)
    {
        if (dht11_read_level() == level)
        {
            return RT_EOK;
        }
        rt_hw_us_delay(1U);
    }

    return -RT_ETIMEOUT;
}

/**
 * @brief 按 DHT11 时序读取 1 字节数据。
 * @param value 输出参数，用于接收读取到的字节。
 * @return 读取成功返回 RT_EOK，失败返回负数错误码。
 */
static rt_err_t dht11_read_byte(rt_uint8_t *value)
{
    rt_uint8_t data = 0;
    rt_uint8_t bit;

    if (value == RT_NULL)
    {
        return -RT_EINVAL;
    }

    for (bit = 0; bit < 8U; bit++)
    {
        if (dht11_wait_level(1U, DHT11_BIT_WAIT_US) != RT_EOK)
        {
            return -RT_ETIMEOUT;
        }

        rt_hw_us_delay(35U);
        data <<= 1;
        if (dht11_read_level() != 0U)
        {
            data |= 0x01U;
        }

        if (dht11_wait_level(0U, DHT11_BIT_WAIT_US) != RT_EOK)
        {
            return -RT_ETIMEOUT;
        }
    }

    *value = data;
    return RT_EOK;
}

/**
 * @brief 初始化 DHT11 数据引脚。
 * @param 无。
 * @return 成功返回 RT_EOK。
 */
rt_err_t dht11_init(void)
{
    dht11_set_input();
    g_dht11_ready = RT_TRUE;
    return RT_EOK;
}

/**
 * @brief 读取一次 DHT11 温湿度数据并校验校验和。
 * @param sample 输出参数，用于接收温湿度采样。
 * @return 读取成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t dht11_read(dht11_sample_t *sample)
{
    rt_uint8_t raw[5];
    rt_err_t ret;

    if (sample == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (!g_dht11_ready)
    {
        dht11_init();
    }

    memset(raw, 0, sizeof(raw));

    dht11_set_output();
    rt_pin_write(DHT11_PIN, PIN_LOW);
    rt_thread_mdelay(DHT11_START_MS);
    rt_pin_write(DHT11_PIN, PIN_HIGH);
    rt_hw_us_delay(30U);
    dht11_set_input();

    ret = dht11_wait_level(0U, DHT11_RESP_US);
    if (ret != RT_EOK)
    {
        return ret;
    }

    ret = dht11_wait_level(1U, DHT11_RESP_US);
    if (ret != RT_EOK)
    {
        return ret;
    }

    ret = dht11_wait_level(0U, DHT11_RESP_US);
    if (ret != RT_EOK)
    {
        return ret;
    }

    for (rt_uint8_t i = 0; i < 5U; i++)
    {
        ret = dht11_read_byte(&raw[i]);
        if (ret != RT_EOK)
        {
            return ret;
        }
    }

    if (((rt_uint8_t)(raw[0] + raw[1] + raw[2] + raw[3])) != raw[4])
    {
        return -RT_ERROR;
    }

    sample->humidity_x10 = (rt_int32_t)raw[0] * 10 + raw[1];
    sample->temperature_x10 = (rt_int32_t)raw[2] * 10 + raw[3];
    return RT_EOK;
}
