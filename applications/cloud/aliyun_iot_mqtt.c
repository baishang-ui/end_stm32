/*
 * Alibaba Cloud IoT MQTT demo over ESP-01S AT firmware.
 *
 * The connection flow follows the LCKFB wiki reference:
 *   1) wait for WiFi STA ready
 *   2) AT+MQTTUSERCFG
 *   3) AT+MQTTCLIENTID
 *   4) AT+MQTTCONN
 *   5) publish simulated temperature/humidity periodically
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "network/esp01s_at.h"
#include "aliyun_iot_mqtt.h"
#include "data/todo_snapshot.h"
#include "sensor/dht11.h"

#define DBG_TAG    "ali.iot"
#define DBG_LVL    DBG_INFO
#include <rtdbg.h>

#define ALIYUN_IOT_REGION         "cn-shanghai"
#define ALIYUN_IOT_PRODUCT_KEY    "k1e98mYkQSF"
#define ALIYUN_IOT_DEVICE_NAME    "device"
#define ALIYUN_IOT_DEVICE_SECRET  "0f620a5ef141ff21cd9fccb0830442cd"

#define ALIYUN_IOT_KEEPALIVE_SEC   300
#define ALIYUN_IOT_PUB_PERIOD_MS   1000
#define ALIYUN_IOT_RETRY_MS        3000
#define ALIYUN_IOT_RECV_POLL_MS    200
#define ALIYUN_IOT_SUB_TOPIC       "/k1e98mYkQSF/device/user/recv_data"
#define ALIYUN_IOT_REPORT_TOPIC    "/k1e98mYkQSF/device/user/report_data"
#define ALIYUN_IOT_RECV_TOPIC_MAX  160
#define ALIYUN_IOT_RECV_BUF_MAX    2048
#define ALIYUN_IOT_TODO_JSON_MAX   4096
#define ALIYUN_IOT_REPORT_QUEUE_SIZE  6
#define ALIYUN_IOT_REPORT_BUF_MAX     256
#define ALIYUN_IOT_TOPIC_FMT       "/sys/%s/%s/thing/event/property/post"
#define ALIYUN_IOT_PUB_REPLY_FMT   "/sys/%s/%s/thing/event/property/post_reply"
#define ALIYUN_IOT_TEMP_KEY        "temperature"
#define ALIYUN_IOT_HUMI_KEY        "humidity"

typedef struct
{
    char product_key[32];
    char device_name[32];
    char device_secret[64];
    char region[24];
    char host[128];
    char topic[160];
    char base_client_id[128];
    char client_id[160];
    char username[96];
    char password[65];
} aliyun_iot_auth_t;

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
    aliyun_iot_auth_t auth;
} aliyun_iot_ctx_t;

typedef struct
{
    uint32_t state[5];
    uint64_t bitlen;
    uint8_t data[64];
    uint32_t datalen;
} sha1_ctx_t;

static aliyun_iot_ctx_t g_ali;
static dht11_sample_t g_dht11_last_sample = {235, 500};
static rt_bool_t g_dht11_has_sample = RT_FALSE;
static rt_err_t g_dht11_last_error = -RT_EEMPTY;
static rt_tick_t g_dht11_last_tick = 0;
static rt_bool_t g_ali_thread_started = RT_FALSE;
static char g_ali_recv_topic[ALIYUN_IOT_RECV_TOPIC_MAX];
static char g_ali_recv_buf[ALIYUN_IOT_RECV_BUF_MAX];
static char g_ali_pub_reply_topic[ALIYUN_IOT_RECV_TOPIC_MAX];
static char g_ali_todo_json[ALIYUN_IOT_TODO_JSON_MAX];
static rt_size_t g_ali_todo_json_len = 0;
static struct rt_mutex g_ali_report_lock;
static rt_bool_t g_ali_report_lock_ready = RT_FALSE;
static char g_ali_report_queue[ALIYUN_IOT_REPORT_QUEUE_SIZE][ALIYUN_IOT_REPORT_BUF_MAX];
static rt_uint8_t g_ali_report_head = 0;
static rt_uint8_t g_ali_report_tail = 0;
static rt_uint8_t g_ali_report_count = 0;

static void ali_report_queue_init(void)
{
    if (g_ali_report_lock_ready == RT_FALSE)
    {
        rt_mutex_init(&g_ali_report_lock, "alirpt", RT_IPC_FLAG_PRIO);
        g_ali_report_lock_ready = RT_TRUE;
    }
}

static void ali_set_state(const char *state)
{
    if (state == RT_NULL)
    {
        g_ali.state[0] = '\0';
        return;
    }

    rt_strncpy(g_ali.state, state, sizeof(g_ali.state) - 1);
    g_ali.state[sizeof(g_ali.state) - 1] = '\0';
}

static void ali_update_status(rt_bool_t mqtt_connected, rt_err_t last_error, const char *state)
{
    g_ali.mqtt_connected = mqtt_connected;
    g_ali.last_error = last_error;
    ali_set_state(state);
    if (!mqtt_connected)
    {
        g_ali.subscribed = RT_FALSE;
    }
}

static uint32_t rol32(uint32_t value, uint32_t bits)
{
    return (value << bits) | (value >> (32U - bits));
}

static void sha1_init(sha1_ctx_t *ctx)
{
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x67452301U;
    ctx->state[1] = 0xEFCDAB89U;
    ctx->state[2] = 0x98BADCFEU;
    ctx->state[3] = 0x10325476U;
    ctx->state[4] = 0xC3D2E1F0U;
}

static void sha1_transform(sha1_ctx_t *ctx, const uint8_t data[64])
{
    uint32_t w[80];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t k;
    uint32_t temp;
    uint32_t i;

    for (i = 0; i < 16; i++)
    {
        w[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }

    for (i = 16; i < 80; i++)
    {
        w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1U);
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];

    for (i = 0; i < 80; i++)
    {
        if (i < 20)
        {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999U;
        }
        else if (i < 40)
        {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1U;
        }
        else if (i < 60)
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCU;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xCA62C1D6U;
        }

        temp = rol32(a, 5U) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol32(b, 30U);
        b = a;
        a = temp;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
}

static void sha1_update(sha1_ctx_t *ctx, const uint8_t *data, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++)
    {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64U)
        {
            sha1_transform(ctx, ctx->data);
            ctx->bitlen += 512U;
            ctx->datalen = 0;
        }
    }
}

static void sha1_final(sha1_ctx_t *ctx, uint8_t hash[20])
{
    uint32_t i;
    uint64_t total_bits;

    total_bits = ctx->bitlen + ((uint64_t)ctx->datalen * 8U);

    i = ctx->datalen;
    ctx->data[i++] = 0x80U;

    if (i > 56U)
    {
        while (i < 64U)
        {
            ctx->data[i++] = 0x00U;
        }
        sha1_transform(ctx, ctx->data);
        i = 0;
    }

    while (i < 56U)
    {
        ctx->data[i++] = 0x00U;
    }

    ctx->data[56] = (uint8_t)((total_bits >> 56U) & 0xFFU);
    ctx->data[57] = (uint8_t)((total_bits >> 48U) & 0xFFU);
    ctx->data[58] = (uint8_t)((total_bits >> 40U) & 0xFFU);
    ctx->data[59] = (uint8_t)((total_bits >> 32U) & 0xFFU);
    ctx->data[60] = (uint8_t)((total_bits >> 24U) & 0xFFU);
    ctx->data[61] = (uint8_t)((total_bits >> 16U) & 0xFFU);
    ctx->data[62] = (uint8_t)((total_bits >> 8U) & 0xFFU);
    ctx->data[63] = (uint8_t)(total_bits & 0xFFU);
    sha1_transform(ctx, ctx->data);

    for (i = 0; i < 5U; i++)
    {
        hash[i * 4U] = (uint8_t)((ctx->state[i] >> 24U) & 0xFFU);
        hash[i * 4U + 1U] = (uint8_t)((ctx->state[i] >> 16U) & 0xFFU);
        hash[i * 4U + 2U] = (uint8_t)((ctx->state[i] >> 8U) & 0xFFU);
        hash[i * 4U + 3U] = (uint8_t)(ctx->state[i] & 0xFFU);
    }
}

static void hmac_sha1(const uint8_t *key,
                      size_t key_len,
                      const uint8_t *msg,
                      size_t msg_len,
                      uint8_t out[20])
{
    uint8_t k_ipad[64];
    uint8_t k_opad[64];
    uint8_t key_hash[20];
    uint8_t inner_hash[20];
    sha1_ctx_t ctx;
    size_t i;

    memset(k_ipad, 0x36, sizeof(k_ipad));
    memset(k_opad, 0x5C, sizeof(k_opad));

    if (key_len > 64U)
    {
        sha1_init(&ctx);
        sha1_update(&ctx, key, key_len);
        sha1_final(&ctx, key_hash);
        key = key_hash;
        key_len = sizeof(key_hash);
    }

    for (i = 0; i < key_len; i++)
    {
        k_ipad[i] ^= key[i];
        k_opad[i] ^= key[i];
    }

    sha1_init(&ctx);
    sha1_update(&ctx, k_ipad, sizeof(k_ipad));
    sha1_update(&ctx, msg, msg_len);
    sha1_final(&ctx, inner_hash);

    sha1_init(&ctx);
    sha1_update(&ctx, k_opad, sizeof(k_opad));
    sha1_update(&ctx, inner_hash, sizeof(inner_hash));
    sha1_final(&ctx, out);
}

static void bytes_to_hex_upper(char *out, size_t out_len, const uint8_t *data, size_t data_len)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t i;

    if (out_len < (data_len * 2U + 1U))
    {
        if (out_len > 0U)
        {
            out[0] = '\0';
        }
        return;
    }

    for (i = 0; i < data_len; i++)
    {
        out[i * 2U] = hex[(data[i] >> 4U) & 0x0FU];
        out[i * 2U + 1U] = hex[data[i] & 0x0FU];
    }
    out[data_len * 2U] = '\0';
}

static rt_bool_t ali_wifi_ready(esp01s_status_t *status)
{
    esp01s_get_status(status);
    g_ali.wifi_ready = (status->connected == RT_TRUE) && (status->ip[0] != '\0');
    return g_ali.wifi_ready;
}

static void ali_build_auth(aliyun_iot_auth_t *auth)
{
    char sign_content[256];
    uint8_t hmac[20];

    memset(auth, 0, sizeof(*auth));
    rt_strncpy(auth->product_key, ALIYUN_IOT_PRODUCT_KEY, sizeof(auth->product_key) - 1);
    rt_strncpy(auth->device_name, ALIYUN_IOT_DEVICE_NAME, sizeof(auth->device_name) - 1);
    rt_strncpy(auth->device_secret, ALIYUN_IOT_DEVICE_SECRET, sizeof(auth->device_secret) - 1);
    rt_strncpy(auth->region, ALIYUN_IOT_REGION, sizeof(auth->region) - 1);

    rt_snprintf(auth->base_client_id, sizeof(auth->base_client_id), "%s.%s",
                auth->product_key, auth->device_name);
    rt_snprintf(auth->client_id, sizeof(auth->client_id), "%s|securemode=3,signmethod=hmacsha1|",
                auth->base_client_id);
    rt_snprintf(auth->username, sizeof(auth->username), "%s&%s",
                auth->device_name, auth->product_key);
    rt_snprintf(auth->host, sizeof(auth->host), "%s.iot-as-mqtt.%s.aliyuncs.com",
                auth->product_key, auth->region);
    rt_snprintf(auth->topic, sizeof(auth->topic), ALIYUN_IOT_TOPIC_FMT,
                auth->product_key, auth->device_name);
    rt_snprintf(g_ali_pub_reply_topic, sizeof(g_ali_pub_reply_topic), ALIYUN_IOT_PUB_REPLY_FMT,
                auth->product_key, auth->device_name);

    rt_snprintf(sign_content, sizeof(sign_content),
                "clientId%sdeviceName%sproductKey%s",
                auth->base_client_id,
                auth->device_name,
                auth->product_key);

    hmac_sha1((const uint8_t *)auth->device_secret,
              rt_strlen(auth->device_secret),
              (const uint8_t *)sign_content,
              rt_strlen(sign_content),
              hmac);
    bytes_to_hex_upper(auth->password, sizeof(auth->password), hmac, sizeof(hmac));
}

static rt_err_t ali_send_best_effort(const char *cmd)
{
    rt_err_t ret = esp01s_at_command(cmd, "OK", "ERROR", 3000);
    if (ret != RT_EOK)
    {
        rt_kprintf("[ALI] ignore cmd fail: %s rsp=%s\r\n", cmd, esp01s_last_response());
    }
    return ret;
}

static rt_err_t ali_mqtt_prepare(const aliyun_iot_auth_t *auth)
{
    char cmd[256];
    rt_err_t ret;

    ali_send_best_effort("AT+MQTTCLEAN=0");

    rt_snprintf(cmd, sizeof(cmd),
                "AT+MQTTUSERCFG=0,1,\"NULL\",\"%s\",\"%s\",0,0,\"\"",
                auth->username,
                auth->password);
    ret = esp01s_at_command(cmd, "OK", "ERROR", 5000);
    if (ret != RT_EOK)
    {
        rt_kprintf("[ALI] MQTTUSERCFG rsp: %s\r\n", esp01s_last_response());
        return ret;
    }

    rt_snprintf(cmd, sizeof(cmd),
                "AT+MQTTCLIENTID=0,\"%s|securemode=3\\,signmethod=hmacsha1|\"",
                auth->base_client_id);
    ret = esp01s_at_command(cmd, "OK", "ERROR", 3000);
    if (ret != RT_EOK)
    {
        rt_kprintf("[ALI] MQTTCLIENTID rsp: %s\r\n", esp01s_last_response());
        return ret;
    }

    rt_snprintf(cmd, sizeof(cmd),
                "AT+MQTTCONNCFG=0,%u,0,\"\",\"\",0,0",
                ALIYUN_IOT_KEEPALIVE_SEC);
    ali_send_best_effort(cmd);

    rt_snprintf(cmd, sizeof(cmd),
                "AT+MQTTCONN=0,\"%s\",1883,1",
                auth->host);
    ret = esp01s_at_command(cmd, "OK", "ERROR", 30000);
    if (ret != RT_EOK)
    {
        rt_kprintf("[ALI] MQTTCONN rsp: %s\r\n", esp01s_last_response());
        return ret;
    }

    rt_thread_mdelay(1500);
    return RT_EOK;
}

static rt_err_t ali_subscribe_todo_topic(void)
{
    rt_err_t ret;

    ret = esp01s_mqtt_subscribe(ALIYUN_IOT_SUB_TOPIC, 0);
    if (ret != RT_EOK)
    {
        rt_kprintf("[ALI] MQTTSUB rsp: %s\r\n", esp01s_last_response());
        return ret;
    }

    g_ali.subscribed = RT_TRUE;
    ali_set_state("SUB");
    rt_kprintf("[ALI] subscribed topic: %s\r\n", ALIYUN_IOT_SUB_TOPIC);
    return RT_EOK;
}

static rt_bool_t ali_json_snapshot_complete(const char *json, rt_size_t len)
{
    rt_int32_t depth = 0;
    rt_bool_t in_string = RT_FALSE;
    rt_bool_t escape = RT_FALSE;
    rt_size_t i;

    if ((json == RT_NULL) || (len == 0U))
    {
        return RT_FALSE;
    }

    for (i = 0; i < len; i++)
    {
        char ch = json[i];

        if (escape)
        {
            escape = RT_FALSE;
            continue;
        }

        if (in_string)
        {
            if (ch == '\\')
            {
                escape = RT_TRUE;
            }
            else if (ch == '"')
            {
                in_string = RT_FALSE;
            }
            continue;
        }

        if (ch == '"')
        {
            in_string = RT_TRUE;
        }
        else if (ch == '{')
        {
            depth++;
        }
        else if (ch == '}')
        {
            depth--;
            if (depth == 0)
            {
                rt_size_t j;

                for (j = i + 1U; j < len; j++)
                {
                    char tail = json[j];

                    if ((tail != '\r') && (tail != '\n') && (tail != ' ') && (tail != '\t'))
                    {
                        return RT_FALSE;
                    }
                }
                return RT_TRUE;
            }
            if (depth < 0)
            {
                return RT_FALSE;
            }
        }
    }

    return RT_FALSE;
}

static rt_err_t ali_handle_todo_payload(const char *payload)
{
    rt_err_t parse_ret;
    rt_size_t chunk_len;

    if (payload == RT_NULL)
    {
        return -RT_EINVAL;
    }

    chunk_len = rt_strlen(payload);
    if (chunk_len == 0U)
    {
        return -RT_EINVAL;
    }

    if (payload[0] == '{')
    {
        g_ali_todo_json_len = 0;
        g_ali_todo_json[0] = '\0';
    }

    if ((g_ali_todo_json_len + chunk_len) >= sizeof(g_ali_todo_json))
    {
        g_ali_todo_json_len = 0;
        g_ali_todo_json[0] = '\0';
        return -RT_ENOMEM;
    }

    memcpy(g_ali_todo_json + g_ali_todo_json_len, payload, chunk_len);
    g_ali_todo_json_len += chunk_len;
    g_ali_todo_json[g_ali_todo_json_len] = '\0';

    if (!ali_json_snapshot_complete(g_ali_todo_json, g_ali_todo_json_len))
    {
        return -RT_EBUSY;
    }

    parse_ret = todo_snapshot_update_from_json(g_ali_todo_json);
    g_ali_todo_json_len = 0;
    g_ali_todo_json[0] = '\0';
    return parse_ret;
}

static rt_size_t ali_json_escape_copy(char *dst, rt_size_t dst_len, const char *src)
{
    rt_size_t used = 0;

    if ((dst == RT_NULL) || (dst_len == 0U))
    {
        return 0U;
    }

    if (src == RT_NULL)
    {
        dst[0] = '\0';
        return 0U;
    }

    while ((*src != '\0') && (used + 1U < dst_len))
    {
        char ch = *src++;

        if ((ch == '\"') || (ch == '\\'))
        {
            if ((used + 2U) >= dst_len)
            {
                break;
            }
            dst[used++] = '\\';
            dst[used++] = ch;
        }
        else if ((ch == '\r') || (ch == '\n') || (ch == '\t'))
        {
            if ((used + 2U) >= dst_len)
            {
                break;
            }
            dst[used++] = '\\';
            dst[used++] = (ch == '\r') ? 'r' : ((ch == '\n') ? 'n' : 't');
        }
        else
        {
            dst[used++] = ch;
        }
    }

    dst[used] = '\0';
    return used;
}

static rt_size_t ali_u64_to_dec(char *dst, rt_size_t dst_len, rt_uint64_t value)
{
    char temp[24];
    rt_size_t digits = 0;
    rt_size_t i;

    if ((dst == RT_NULL) || (dst_len == 0U))
    {
        return 0U;
    }

    if (value == 0U)
    {
        if (dst_len < 2U)
        {
            dst[0] = '\0';
            return 0U;
        }
        dst[0] = '0';
        dst[1] = '\0';
        return 1U;
    }

    while ((value != 0U) && (digits < sizeof(temp)))
    {
        temp[digits++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    if ((digits + 1U) > dst_len)
    {
        dst[0] = '\0';
        return 0U;
    }

    for (i = 0; i < digits; i++)
    {
        dst[i] = temp[digits - 1U - i];
    }
    dst[digits] = '\0';
    return digits;
}

static rt_err_t ali_json_append(char *dst, rt_size_t dst_len, rt_size_t *used, const char *src)
{
    rt_size_t src_len;

    if ((dst == RT_NULL) || (used == RT_NULL) || (src == RT_NULL))
    {
        return -RT_EINVAL;
    }

    src_len = rt_strlen(src);
    if ((*used + src_len + 1U) > dst_len)
    {
        return -RT_ENOMEM;
    }

    memcpy(dst + *used, src, src_len);
    *used += src_len;
    dst[*used] = '\0';
    return RT_EOK;
}

static rt_err_t ali_build_todo_report_payload(char *payload,
                                              rt_size_t payload_len,
                                              const todo_snapshot_change_t *change)
{
    char id_escaped[TODO_ID_MAX_LEN * 2U];
    char updated_at_str[24];
    rt_size_t used = 0U;
    rt_err_t ret;

    if ((payload == RT_NULL) || (change == RT_NULL) || (payload_len == 0U))
    {
        return -RT_EINVAL;
    }

    payload[0] = '\0';
    ali_json_escape_copy(id_escaped, sizeof(id_escaped), change->item.id);
    if (ali_u64_to_dec(updated_at_str, sizeof(updated_at_str), change->item.updated_at) == 0U)
    {
        return -RT_ENOMEM;
    }

    ret = ali_json_append(payload, payload_len, &used, "{\"id\":\"");
    if (ret != RT_EOK)
    {
        return ret;
    }
    ret = ali_json_append(payload, payload_len, &used, id_escaped);
    if (ret != RT_EOK)
    {
        return ret;
    }
    ret = ali_json_append(payload, payload_len, &used, "\",\"updatedAt\":");
    if (ret != RT_EOK)
    {
        return ret;
    }
    ret = ali_json_append(payload, payload_len, &used, updated_at_str);
    if (ret != RT_EOK)
    {
        return ret;
    }
    ret = ali_json_append(payload, payload_len, &used, ",\"completed\":");
    if (ret != RT_EOK)
    {
        return ret;
    }
    ret = ali_json_append(payload, payload_len, &used, change->item.completed ? "true" : "false");
    if (ret != RT_EOK)
    {
        return ret;
    }
    ret = ali_json_append(payload, payload_len, &used, "}");
    if (ret != RT_EOK)
    {
        return ret;
    }

    return RT_EOK;
}

static rt_err_t ali_publish_next_todo_report(void)
{
    char payload[ALIYUN_IOT_REPORT_BUF_MAX];
    rt_err_t ret = -RT_EEMPTY;

    if (g_ali_report_lock_ready == RT_FALSE)
    {
        return -RT_EEMPTY;
    }

    rt_mutex_take(&g_ali_report_lock, RT_WAITING_FOREVER);
    if (g_ali_report_count > 0U)
    {
        rt_strncpy(payload, g_ali_report_queue[g_ali_report_head], sizeof(payload) - 1U);
        payload[sizeof(payload) - 1U] = '\0';
        g_ali_report_head = (rt_uint8_t)((g_ali_report_head + 1U) % ALIYUN_IOT_REPORT_QUEUE_SIZE);
        g_ali_report_count--;
        ret = RT_EOK;
    }
    rt_mutex_release(&g_ali_report_lock);

    if (ret != RT_EOK)
    {
        return ret;
    }

    ret = esp01s_mqtt_pubraw(ALIYUN_IOT_REPORT_TOPIC, payload, rt_strlen(payload), 0, RT_FALSE);
    if (ret == RT_EOK)
    {
        ali_set_state("RPT");
    }
    return ret;
}

static rt_err_t ali_build_payload(char *payload,
                                  rt_size_t payload_len,
                                  rt_uint32_t seq,
                                  rt_int32_t temp_x10,
                                  rt_int32_t humi_x10)
{
    int len;
    rt_int32_t temp_int;
    rt_int32_t temp_frac;
    rt_int32_t humi_int;
    rt_int32_t humi_frac;

    temp_int = temp_x10 / 10;
    temp_frac = (temp_x10 % 10);
    if (temp_frac < 0)
    {
        temp_frac = -temp_frac;
    }
    humi_int = humi_x10 / 10;
    humi_frac = (humi_x10 % 10);
    if (humi_frac < 0)
    {
        humi_frac = -humi_frac;
    }

    len = rt_snprintf(payload, payload_len,
                      "{\"id\":\"%lu\",\"version\":\"1.0\",\"method\":\"thing.event.property.post\",\"params\":{\"%s\":%d.%d,\"%s\":%d.%d}}",
                      (unsigned long)seq,
                      ALIYUN_IOT_TEMP_KEY,
                      temp_int,
                      temp_frac,
                      ALIYUN_IOT_HUMI_KEY,
                      humi_int,
                      humi_frac);
    if ((len <= 0) || ((rt_size_t)len >= payload_len))
    {
        return -RT_ENOMEM;
    }

    return RT_EOK;
}

static rt_err_t ali_publish_sample(const aliyun_iot_auth_t *auth, rt_uint32_t seq)
{
    char payload[256];
    rt_int32_t temp_x10;
    rt_int32_t humi_x10;
    rt_err_t ret;

    ret = dht11_read(&g_dht11_last_sample);
    if (ret == RT_EOK)
    {
        g_dht11_has_sample = RT_TRUE;
        g_dht11_last_error = RT_EOK;
        g_dht11_last_tick = rt_tick_get();
    }
    else if (!g_dht11_has_sample)
    {
        g_dht11_last_error = ret;
        rt_kprintf("[DHT11] read failed before first valid sample: %d\r\n", ret);
        return ret;
    }
    else
    {
        g_dht11_last_error = ret;
        rt_kprintf("[DHT11] read failed, keep last sample: %d\r\n", ret);
    }

    temp_x10 = g_dht11_last_sample.temperature_x10;
    humi_x10 = g_dht11_last_sample.humidity_x10;

    ret = ali_build_payload(payload, sizeof(payload), seq, temp_x10, humi_x10);
    if (ret != RT_EOK)
    {
        return ret;
    }

    return esp01s_mqtt_pubraw(auth->topic, payload, rt_strlen(payload), 0, RT_FALSE);
}

static void ali_disconnect_mqtt(void)
{
    ali_send_best_effort("AT+MQTTCLEAN=0");
    g_ali.mqtt_connected = RT_FALSE;
    g_ali.subscribed = RT_FALSE;
    g_ali_todo_json_len = 0;
    g_ali_todo_json[0] = '\0';
}

rt_err_t aliyun_iot_queue_todo_report(const todo_snapshot_change_t *change)
{
    char payload[ALIYUN_IOT_REPORT_BUF_MAX];
    rt_err_t ret;

    ali_report_queue_init();

    ret = ali_build_todo_report_payload(payload, sizeof(payload), change);
    if (ret != RT_EOK)
    {
        return ret;
    }

    rt_mutex_take(&g_ali_report_lock, RT_WAITING_FOREVER);
    if (g_ali_report_count >= ALIYUN_IOT_REPORT_QUEUE_SIZE)
    {
        g_ali_report_head = (rt_uint8_t)((g_ali_report_head + 1U) % ALIYUN_IOT_REPORT_QUEUE_SIZE);
        g_ali_report_count--;
    }

    rt_strncpy(g_ali_report_queue[g_ali_report_tail], payload, ALIYUN_IOT_REPORT_BUF_MAX - 1U);
    g_ali_report_queue[g_ali_report_tail][ALIYUN_IOT_REPORT_BUF_MAX - 1U] = '\0';
    g_ali_report_tail = (rt_uint8_t)((g_ali_report_tail + 1U) % ALIYUN_IOT_REPORT_QUEUE_SIZE);
    g_ali_report_count++;
    rt_mutex_release(&g_ali_report_lock);

    return RT_EOK;
}

static void ali_iot_thread(void *parameter)
{
    esp01s_status_t wifi;
    rt_err_t ret;
    rt_err_t parse_ret;
    rt_tick_t last_pub_tick;

    RT_UNUSED(parameter);

    ali_report_queue_init();
    ali_build_auth(&g_ali.auth);
    rt_kprintf("[ALI] host=%s\r\n", g_ali.auth.host);
    rt_kprintf("[ALI] pub=%s\r\n", g_ali.auth.topic);
    rt_kprintf("[ALI] sub=%s\r\n", ALIYUN_IOT_SUB_TOPIC);
    rt_kprintf("[ALI] clientId=%s\r\n", g_ali.auth.client_id);
    rt_kprintf("[ALI] username=%s\r\n", g_ali.auth.username);
    last_pub_tick = rt_tick_get();

    g_ali.started = RT_TRUE;
    g_ali.wifi_ready = RT_FALSE;
    ali_update_status(RT_FALSE, -RT_EBUSY, "WAIT");

    while (1)
    {
        if (!ali_wifi_ready(&wifi))
        {
            if (g_ali.mqtt_connected)
            {
                ali_disconnect_mqtt();
                ali_update_status(RT_FALSE, -RT_EIO, "WIFI");
            }
            rt_thread_mdelay(1000);
            continue;
        }

        if (!g_ali.mqtt_connected)
        {
            ali_set_state("MQTT");
            ali_build_auth(&g_ali.auth);
            ret = ali_mqtt_prepare(&g_ali.auth);
            if (ret != RT_EOK)
            {
                g_ali.reconnect_count++;
                ali_update_status(RT_FALSE, ret, "RETRY");
                rt_kprintf("[ALI] mqtt connect failed: %d\r\n", ret);
                if (esp01s_last_response()[0] != '\0')
                {
                    rt_kprintf("[ALI] mqtt connect rsp: %s\r\n", esp01s_last_response());
                }
                ali_disconnect_mqtt();
                rt_thread_mdelay(ALIYUN_IOT_RETRY_MS);
                continue;
            }

            g_ali.mqtt_connected = RT_TRUE;
            g_ali.subscribed = RT_FALSE;
            g_ali.last_error = RT_EOK;
            ali_set_state("UP");
            rt_kprintf("[ALI] mqtt connected: %s\r\n", g_ali.auth.host);
        }

        if (!g_ali.subscribed)
        {
            ret = ali_subscribe_todo_topic();
            if (ret != RT_EOK)
            {
                g_ali.reconnect_count++;
                ali_update_status(RT_FALSE, ret, "RETRY");
                ali_disconnect_mqtt();
                rt_thread_mdelay(ALIYUN_IOT_RETRY_MS);
                continue;
            }
        }

        ret = esp01s_mqtt_recv(g_ali_recv_topic,
                               sizeof(g_ali_recv_topic),
                               g_ali_recv_buf,
                               sizeof(g_ali_recv_buf),
                               ALIYUN_IOT_RECV_POLL_MS);
        if (ret == RT_EOK)
        {
            if (rt_strcmp(g_ali_recv_topic, ALIYUN_IOT_SUB_TOPIC) == 0)
            {
                parse_ret = ali_handle_todo_payload(g_ali_recv_buf);
                if (parse_ret == RT_EOK)
                {
                    g_ali.recv_count++;
                    ali_update_status(RT_TRUE, RT_EOK, "TODO");
                    rt_kprintf("[ALI] todo snapshot recv #%lu\r\n", (unsigned long)g_ali.recv_count);
                }
                else if (parse_ret == -RT_EBUSY)
                {
                    ali_set_state("TODO");
                }
                else
                {
                    g_ali.last_error = parse_ret;
                    ali_set_state("JSON");
                    rt_kprintf("[ALI] todo json parse failed: %d\r\n", parse_ret);
                    rt_kprintf("[ALI] todo raw: %s\r\n", g_ali_todo_json[0] ? g_ali_todo_json : g_ali_recv_buf);
                    g_ali_todo_json_len = 0;
                    g_ali_todo_json[0] = '\0';
                }
            }
            else if (rt_strcmp(g_ali_recv_topic, g_ali_pub_reply_topic) == 0)
            {
                g_ali.last_error = RT_EOK;
                ali_set_state("ACK");
            }
            else
            {
                rt_kprintf("[ALI] ignore topic: %s\r\n", g_ali_recv_topic);
            }
            continue;
        }

        ret = ali_publish_next_todo_report();
        if (ret == RT_EOK)
        {
            g_ali.last_error = RT_EOK;
            rt_kprintf("[ALI] todo report sent\r\n");
            continue;
        }
        if ((ret != -RT_EEMPTY) && (ret != -RT_ETIMEOUT))
        {
            g_ali.reconnect_count++;
            g_ali.mqtt_connected = RT_FALSE;
            g_ali.last_error = ret;
            ali_set_state("RTRY");
            rt_kprintf("[ALI] todo report failed: %d\r\n", ret);
            if (esp01s_last_response()[0] != '\0')
            {
                rt_kprintf("[ALI] todo report rsp: %s\r\n", esp01s_last_response());
            }
            ali_disconnect_mqtt();
            rt_thread_mdelay(ALIYUN_IOT_RETRY_MS);
            continue;
        }

        if ((rt_tick_get() - last_pub_tick) >= rt_tick_from_millisecond(ALIYUN_IOT_PUB_PERIOD_MS))
        {
            ret = ali_publish_sample(&g_ali.auth, g_ali.publish_count + 1U);
            if (ret != RT_EOK)
            {
                g_ali.reconnect_count++;
                g_ali.mqtt_connected = RT_FALSE;
                g_ali.last_error = ret;
                ali_set_state("RECONN");
                rt_kprintf("[ALI] publish failed: %d\r\n", ret);
                if (esp01s_last_response()[0] != '\0')
                {
                    rt_kprintf("[ALI] publish rsp: %s\r\n", esp01s_last_response());
                }
                ali_disconnect_mqtt();
                rt_thread_mdelay(ALIYUN_IOT_RETRY_MS);
                continue;
            }

            g_ali.publish_count++;
            g_ali.last_error = RT_EOK;
            ali_set_state("PUB");
            last_pub_tick = rt_tick_get();
            rt_kprintf("[ALI] pub #%lu temp/humi sent\r\n", (unsigned long)g_ali.publish_count);
        }
    }
}

int aliyun_iot_start(void)
{
    rt_thread_t thread;

    if (g_ali_thread_started)
    {
        return RT_EOK;
    }

    ali_report_queue_init();
    thread = rt_thread_create("ali_iot", ali_iot_thread, RT_NULL, 4096, 22, 10);
    if (thread == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    g_ali_thread_started = RT_TRUE;
    rt_thread_startup(thread);
    return RT_EOK;
}

void aliyun_iot_get_status(aliyun_iot_status_t *status)
{
    esp01s_status_t wifi;

    if (status == RT_NULL)
    {
        return;
    }

    memset(status, 0, sizeof(*status));
    esp01s_get_status(&wifi);

    status->started = g_ali.started;
    status->wifi_ready = wifi.connected;
    status->mqtt_connected = g_ali.mqtt_connected;
    status->subscribed = g_ali.subscribed;
    status->publish_count = g_ali.publish_count;
    status->recv_count = g_ali.recv_count;
    status->reconnect_count = g_ali.reconnect_count;
    status->last_error = g_ali.last_error;
    rt_strncpy(status->state, g_ali.state, sizeof(status->state) - 1);
    rt_strncpy(status->host, g_ali.auth.host, sizeof(status->host) - 1);
    rt_strncpy(status->topic, g_ali.auth.topic, sizeof(status->topic) - 1);
    rt_strncpy(status->sub_topic, ALIYUN_IOT_SUB_TOPIC, sizeof(status->sub_topic) - 1);
    rt_strncpy(status->client_id, g_ali.auth.client_id, sizeof(status->client_id) - 1);
}

void aliyun_iot_get_sensor_status(aliyun_iot_sensor_status_t *status)
{
    if (status == RT_NULL)
    {
        return;
    }

    memset(status, 0, sizeof(*status));
    status->has_sample = g_dht11_has_sample;
    status->last_read_error = g_dht11_last_error;
    status->sample_tick = g_dht11_last_tick;
    status->sample = g_dht11_last_sample;
}

