#ifndef __ESP01S_AT_H__
#define __ESP01S_AT_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    char ssid[33];
    rt_int16_t rssi;
    rt_uint8_t ecn;
    rt_uint8_t channel;
} esp01s_ap_info_t;

typedef struct
{
    rt_uint32_t baud_rate;
    rt_bool_t ready;
    rt_bool_t connected;
    rt_bool_t scan_busy;
    rt_size_t ap_count;
    rt_err_t last_error;
    rt_int16_t rssi;
    char ssid[33];
    char ip[32];
    char state[16];
} esp01s_status_t;

/**
 * @brief 将 ESP01S AT 驱动绑定到指定 UART 设备。
 * @param uart_name RT-Thread UART 设备名，例如 "uart2"。
 * @return 成功返回 RT_EOK，失败返回负数 RT-Thread 错误码。
 */
rt_err_t esp01s_init_port(const char *uart_name);

/**
 * @brief 使用基础 AT 指令探测 ESP01S 模块。
 * @param 无。
 * @return 模块响应正常返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t esp01s_probe(void);

/**
 * @brief 通过 AT 软件复位 ESP01S 模块。
 * @param 无。
 * @return 复位指令被接受返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t esp01s_soft_reset(void);

/**
 * @brief 使用显式账号密码连接 Wi-Fi 接入点。
 * @param ssid 目标 Wi-Fi 名称。
 * @param password 目标 Wi-Fi 密码；开放网络可以为空字符串。
 * @return 连接成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t esp01s_join_ap(const char *ssid, const char *password);

/**
 * @brief 连接固件中配置的默认 Wi-Fi 接入点。
 * @param 无。
 * @return 连接成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t esp01s_join_default_ap(void);

/**
 * @brief 连接扫描列表中选中的已知 Wi-Fi 接入点。
 * @param ssid 目标 Wi-Fi 名称。
 * @param ecn ESP01S 扫描结果中返回的加密模式。
 * @return 连接成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t esp01s_join_saved_ap(const char *ssid, rt_uint8_t ecn);

/**
 * @brief 读取模块 STA 模式下的 IP 地址。
 * @param ip 输出字符串缓冲区。
 * @param len ip 缓冲区长度，单位字节。
 * @return 成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t esp01s_get_ip(char *ip, rt_size_t len);

/**
 * @brief 复制最新 ESP01S 驱动状态。
 * @param status 输出参数，用于接收状态快照。
 * @return 无。
 */
void esp01s_get_status(esp01s_status_t *status);

/**
 * @brief 扫描附近 Wi-Fi 接入点并刷新内部 AP 缓存。
 * @param 无。
 * @return 扫描成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t esp01s_scan_aps(void);

/**
 * @brief 复制缓存的 Wi-Fi 扫描结果。
 * @param aps 输出数组，用于接收 AP 条目。
 * @param max_aps aps 最多可写入的条目数量。
 * @return 实际复制的 AP 条目数量。
 */
rt_size_t esp01s_get_scan_list(esp01s_ap_info_t *aps, rt_size_t max_aps);

/**
 * @brief 发送普通 AT 指令并等待成功或失败标记。
 * @param cmd AT 指令字符串。
 * @param ok 表示成功的匹配标记。
 * @param fail 表示失败的匹配标记；不需要时可以为 RT_NULL。
 * @param timeout_ms 最大等待时间，单位毫秒。
 * @return 匹配到 ok 返回 RT_EOK，否则返回负数错误码。
 */
rt_err_t esp01s_at_command(const char *cmd, const char *ok, const char *fail, rt_int32_t timeout_ms);

/**
 * @brief 发送 AT 指令后继续发送原始负载数据。
 * @param cmd 用于进入数据发送模式的 AT 指令字符串。
 * @param data 指令被接受后发送的数据缓冲区。
 * @param len 数据长度，单位字节。
 * @param ok 表示成功的匹配标记。
 * @param fail 表示失败的匹配标记；不需要时可以为 RT_NULL。
 * @param timeout_ms 最大等待时间，单位毫秒。
 * @return 匹配到 ok 返回 RT_EOK，否则返回负数错误码。
 */
rt_err_t esp01s_at_command_data(const char *cmd,
                                const void *data,
                                rt_size_t len,
                                const char *ok,
                                const char *fail,
                                rt_int32_t timeout_ms);

/**
 * @brief 通过 ESP01S AT MQTT 指令发布一段原始 MQTT 数据。
 * @param topic MQTT 主题字符串。
 * @param payload 需要发布的负载数据。
 * @param len 负载长度，单位字节。
 * @param qos MQTT QoS 等级。
 * @param retain MQTT retain 标志。
 * @return 发布成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t esp01s_mqtt_pubraw(const char *topic,
                            const void *payload,
                            rt_size_t len,
                            rt_uint8_t qos,
                            rt_bool_t retain);

/**
 * @brief 通过 ESP01S AT MQTT 指令订阅一个 MQTT 主题。
 * @param topic MQTT 主题字符串。
 * @param qos MQTT QoS 等级。
 * @return 订阅成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t esp01s_mqtt_subscribe(const char *topic, rt_uint8_t qos);

/**
 * @brief 从 ESP01S AT 数据流中接收一条 MQTT 消息。
 * @param topic 输出主题缓冲区。
 * @param topic_len topic 缓冲区长度，单位字节。
 * @param payload 输出负载缓冲区。
 * @param payload_len payload 缓冲区长度，单位字节。
 * @param timeout_ms 最大等待时间，单位毫秒。
 * @return 成功接收消息返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t esp01s_mqtt_recv(char *topic,
                          rt_size_t topic_len,
                          char *payload,
                          rt_size_t payload_len,
                          rt_int32_t timeout_ms);

/**
 * @brief 启动 ESP01S 联网引导任务。
 * @param 无。
 * @return 任务启动成功返回 RT_EOK，失败返回负数 RT-Thread 错误码。
 */
int esp01s_start_connect(void);

/**
 * @brief 通过 ESP01S 模块发起一次简单 HTTP GET 请求。
 * @param host 远端主机名。
 * @param path 以 '/' 开头的 HTTP 路径。
 * @return 请求成功返回 RT_EOK，失败返回负数错误码。
 */
rt_err_t esp01s_http_get(const char *host, const char *path);

/**
 * @brief 获取最近一次 AT 响应内容，便于诊断。
 * @param 无。
 * @return 指向内部以空字符结尾的响应字符串。
 */
const char *esp01s_last_response(void);

#ifdef __cplusplus
}
#endif

#endif /* __ESP01S_AT_H__ */

