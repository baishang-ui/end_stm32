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

rt_err_t esp01s_init_port(const char *uart_name);
rt_err_t esp01s_probe(void);
rt_err_t esp01s_soft_reset(void);
rt_err_t esp01s_join_ap(const char *ssid, const char *password);
rt_err_t esp01s_join_default_ap(void);
rt_err_t esp01s_join_saved_ap(const char *ssid, rt_uint8_t ecn);
rt_err_t esp01s_get_ip(char *ip, rt_size_t len);
void esp01s_get_status(esp01s_status_t *status);
rt_err_t esp01s_scan_aps(void);
rt_size_t esp01s_get_scan_list(esp01s_ap_info_t *aps, rt_size_t max_aps);
rt_err_t esp01s_at_command(const char *cmd, const char *ok, const char *fail, rt_int32_t timeout_ms);
rt_err_t esp01s_at_command_data(const char *cmd,
                                const void *data,
                                rt_size_t len,
                                const char *ok,
                                const char *fail,
                                rt_int32_t timeout_ms);
rt_err_t esp01s_mqtt_pubraw(const char *topic,
                            const void *payload,
                            rt_size_t len,
                            rt_uint8_t qos,
                            rt_bool_t retain);
rt_err_t esp01s_mqtt_subscribe(const char *topic, rt_uint8_t qos);
rt_err_t esp01s_mqtt_recv(char *topic,
                          rt_size_t topic_len,
                          char *payload,
                          rt_size_t payload_len,
                          rt_int32_t timeout_ms);
int esp01s_start_connect(void);
rt_err_t esp01s_http_get(const char *host, const char *path);
const char *esp01s_last_response(void);

#ifdef __cplusplus
}
#endif

#endif /* __ESP01S_AT_H__ */
