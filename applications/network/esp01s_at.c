/*
 * Minimal ESP-01S AT driver for RT-Thread.
 *
 * Recommended wiring:
 *   ESP TXD  -> PA3  (USART2_RX / uart2)
 *   ESP RXD  -> PA2  (USART2_TX / uart2)
 *   ESP VCC  -> 3.3V
 *   ESP GND  -> GND
 *   ESP EN   -> 3.3V pull-up
 *   ESP RST  -> 3.3V pull-up
 *   ESP GPIO0/GPIO2 -> 3.3V pull-up (normal boot)
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/serial.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "esp01s_at.h"

#define DBG_TAG    "esp01s"
#define DBG_LVL    DBG_INFO
#include <rtdbg.h>

#define ESP01S_UART_NAME      "uart2"
#define ESP01S_RX_BUF_SZ      4096
#define ESP01S_TMP_BUF_SZ     128
#define ESP01S_STA_SSID       "Bai"
#define ESP01S_STA_PASSWORD   "12345678"
#define ESP01S_BOOT_RETRY     5
#define ESP01S_JOIN_TIMEOUT   60000
#define ESP01S_SCAN_TIMEOUT   15000
#define ESP01S_MAX_APS        8

typedef struct
{
    const char *ssid;
    const char *password;
} esp01s_cred_t;

typedef struct
{
    rt_device_t serial;
    struct rt_semaphore rx_sem;
    char uart_name[16];
    char rx_buf[ESP01S_RX_BUF_SZ];
    rt_size_t rx_len;
    rt_uint32_t baud_rate;
    rt_bool_t ready;
    rt_bool_t connected;
    rt_bool_t scan_busy;
    rt_bool_t init_busy;
    rt_err_t last_error;
    rt_int16_t rssi;
    char ssid[33];
    char ip[32];
    char state[16];
    esp01s_ap_info_t aps[ESP01S_MAX_APS];
    rt_size_t ap_count;
} esp01s_ctx_t;

static esp01s_ctx_t g_esp;
static rt_bool_t g_esp_connect_thread_started = RT_FALSE;
static struct rt_mutex g_esp_at_lock;
static rt_bool_t g_esp_at_lock_ready = RT_FALSE;
static const esp01s_cred_t s_saved_creds[] =
{
    { ESP01S_STA_SSID, ESP01S_STA_PASSWORD },
};

static void esp01s_boot_thread(void *parameter);
static rt_err_t esp01s_configure_baud(rt_uint32_t baud_rate);
static rt_err_t esp01s_sync_baud(void);
static void esp01s_set_state(const char *state);
static void esp01s_set_error(rt_err_t err);
static void esp01s_clear_scan_list(void);
static void esp01s_update_rssi_from_scan(void);
static rt_err_t esp01s_parse_scan_results(void);
static const char *esp01s_find_saved_password(const char *ssid);
static rt_err_t esp01s_force_reset(void);
static rt_err_t esp01s_connect_default_once(void);
static rt_bool_t esp01s_station_matches(const char *ssid);
static rt_err_t esp01s_apply_station_tuning(void);
static rt_err_t esp01s_exec_any(const char *cmd_primary,
                                const char *cmd_fallback,
                                const char *ok,
                                const char *fail,
                                rt_int32_t timeout_ms);
static rt_err_t esp01s_write_bytes_nolock(const void *data, rt_size_t len);
static rt_err_t esp01s_write_line_nolock(const char *fmt, ...);
static void esp01s_at_lock_take(void);
static void esp01s_at_lock_release(void);

static rt_err_t esp01s_rx_ind(rt_device_t dev, rt_size_t size)
{
    RT_UNUSED(dev);
    RT_UNUSED(size);
    rt_sem_release(&g_esp.rx_sem);
    return RT_EOK;
}

static void esp01s_trim_buffer(rt_size_t append_len)
{
    rt_size_t overflow;

    if (append_len >= (ESP01S_RX_BUF_SZ - 1))
    {
        g_esp.rx_len = 0;
        return;
    }

    if ((g_esp.rx_len + append_len) < ESP01S_RX_BUF_SZ)
    {
        return;
    }

    overflow = g_esp.rx_len + append_len - (ESP01S_RX_BUF_SZ - 1);
    if (overflow >= g_esp.rx_len)
    {
        g_esp.rx_len = 0;
        return;
    }

    memmove(g_esp.rx_buf, g_esp.rx_buf + overflow, g_esp.rx_len - overflow);
    g_esp.rx_len -= overflow;
}

static void esp01s_pump_rx(void)
{
    char tmp[ESP01S_TMP_BUF_SZ];
    rt_ssize_t nread;

    if (g_esp.serial == RT_NULL)
    {
        return;
    }

    while ((nread = rt_device_read(g_esp.serial, 0, tmp, sizeof(tmp))) > 0)
    {
        esp01s_trim_buffer((rt_size_t)nread);
        memcpy(g_esp.rx_buf + g_esp.rx_len, tmp, (rt_size_t)nread);
        g_esp.rx_len += (rt_size_t)nread;
        g_esp.rx_buf[g_esp.rx_len] = '\0';
    }
}

static void esp01s_clear_rx(void)
{
    g_esp.rx_len = 0;
    g_esp.rx_buf[0] = '\0';
    esp01s_pump_rx();
    g_esp.rx_len = 0;
    g_esp.rx_buf[0] = '\0';
}

static void esp01s_set_state(const char *state)
{
    if (state == RT_NULL)
    {
        g_esp.state[0] = '\0';
        return;
    }

    rt_strncpy(g_esp.state, state, sizeof(g_esp.state) - 1);
    g_esp.state[sizeof(g_esp.state) - 1] = '\0';
}

static void esp01s_set_error(rt_err_t err)
{
    g_esp.last_error = err;
}

static void esp01s_at_lock_take(void)
{
    if (!g_esp_at_lock_ready)
    {
        return;
    }

    rt_mutex_take(&g_esp_at_lock, RT_WAITING_FOREVER);
}

static void esp01s_at_lock_release(void)
{
    if (!g_esp_at_lock_ready)
    {
        return;
    }

    rt_mutex_release(&g_esp_at_lock);
}

static void esp01s_clear_scan_list(void)
{
    g_esp.ap_count = 0;
    memset(g_esp.aps, 0, sizeof(g_esp.aps));
}

static const char *esp01s_find_saved_password(const char *ssid)
{
    rt_size_t i;

    if (ssid == RT_NULL)
    {
        return RT_NULL;
    }

    for (i = 0; i < sizeof(s_saved_creds) / sizeof(s_saved_creds[0]); i++)
    {
        if (rt_strcmp(s_saved_creds[i].ssid, ssid) == 0)
        {
            return s_saved_creds[i].password;
        }
    }

    return RT_NULL;
}

static void esp01s_update_rssi_from_scan(void)
{
    rt_size_t i;

    g_esp.rssi = 0;
    if (g_esp.ssid[0] == '\0')
    {
        return;
    }

    for (i = 0; i < g_esp.ap_count; i++)
    {
        if (rt_strcmp(g_esp.aps[i].ssid, g_esp.ssid) == 0)
        {
            g_esp.rssi = g_esp.aps[i].rssi;
            return;
        }
    }
}

static rt_err_t esp01s_parse_scan_results(void)
{
    char *cursor;
    rt_size_t count = 0;
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    esp01s_clear_scan_list();
    rt_hw_interrupt_enable(level);

    cursor = g_esp.rx_buf;
    while ((cursor = rt_strstr(cursor, "+CWLAP:(")) != RT_NULL)
    {
        int ecn = 0;
        int rssi = 0;
        int channel = 0;
        char ssid[33] = {0};
        esp01s_ap_info_t ap;
        int matched;

        matched = sscanf(cursor, "+CWLAP:(%d,\"%32[^\"]\",%d,\"%*[^\"]\",%d", &ecn, ssid, &rssi, &channel);
        if ((matched >= 3) && (count < ESP01S_MAX_APS))
        {
            memset(&ap, 0, sizeof(ap));
            rt_strncpy(ap.ssid, ssid, sizeof(ap.ssid) - 1);
            ap.ssid[sizeof(ap.ssid) - 1] = '\0';
            ap.ecn = (rt_uint8_t)((ecn < 0) ? 0 : ecn);
            ap.rssi = (rt_int16_t)rssi;
            ap.channel = (rt_uint8_t)((matched >= 4 && channel >= 0) ? channel : 0);

            level = rt_hw_interrupt_disable();
            g_esp.aps[count] = ap;
            count++;
            g_esp.ap_count = count;
            rt_hw_interrupt_enable(level);
        }

        cursor += 8;
    }

    esp01s_update_rssi_from_scan();
    return (count > 0U) ? RT_EOK : -RT_ERROR;
}

static rt_err_t esp01s_write_bytes_nolock(const void *data, rt_size_t len)
{
    rt_ssize_t written;

    if ((g_esp.serial == RT_NULL) || (data == RT_NULL) || (len == 0))
    {
        return -RT_EINVAL;
    }

    written = rt_device_write(g_esp.serial, 0, data, len);
    return (written == (rt_ssize_t)len) ? RT_EOK : -RT_ERROR;
}

static rt_err_t esp01s_write_bytes(const void *data, rt_size_t len)
{
    rt_err_t ret;

    esp01s_at_lock_take();
    ret = esp01s_write_bytes_nolock(data, len);
    esp01s_at_lock_release();
    return ret;
}

static rt_err_t esp01s_write_line_nolock(const char *fmt, ...)
{
    char line[256];
    int len;
    va_list args;

    va_start(args, fmt);
    len = rt_vsnprintf(line, sizeof(line) - 3, fmt, args);
    va_end(args);
    if (len < 0)
    {
        return -RT_ERROR;
    }

    line[len++] = '\r';
    line[len++] = '\n';
    line[len] = '\0';
    return esp01s_write_bytes_nolock(line, (rt_size_t)len);
}

static rt_err_t esp01s_write_line(const char *fmt, ...)
{
    char line[256];
    int len;
    va_list args;
    rt_err_t ret;

    va_start(args, fmt);
    len = rt_vsnprintf(line, sizeof(line) - 3, fmt, args);
    va_end(args);
    if (len < 0)
    {
        return -RT_ERROR;
    }

    line[len++] = '\r';
    line[len++] = '\n';
    line[len] = '\0';

    esp01s_at_lock_take();
    ret = esp01s_write_bytes_nolock(line, (rt_size_t)len);
    esp01s_at_lock_release();
    return ret;
}

static rt_bool_t esp01s_has_any_token(const char *token1, const char *token2, const char *token3)
{
    if ((token1 != RT_NULL) && (rt_strstr(g_esp.rx_buf, token1) != RT_NULL))
    {
        return RT_TRUE;
    }
    if ((token2 != RT_NULL) && (rt_strstr(g_esp.rx_buf, token2) != RT_NULL))
    {
        return RT_TRUE;
    }
    if ((token3 != RT_NULL) && (rt_strstr(g_esp.rx_buf, token3) != RT_NULL))
    {
        return RT_TRUE;
    }

    return RT_FALSE;
}

static rt_err_t esp01s_wait_tokens(const char *ok1,
                                   const char *ok2,
                                   const char *ok3,
                                   const char *fail1,
                                   const char *fail2,
                                   const char *fail3,
                                   rt_int32_t timeout_ms)
{
    rt_tick_t deadline;
    rt_tick_t now;

    deadline = rt_tick_get() + rt_tick_from_millisecond(timeout_ms);
    while (1)
    {
        esp01s_pump_rx();

        if (esp01s_has_any_token(ok1, ok2, ok3))
        {
            return RT_EOK;
        }
        if (esp01s_has_any_token(fail1, fail2, fail3))
        {
            return -RT_ERROR;
        }

        now = rt_tick_get();
        if ((rt_int32_t)(deadline - now) <= 0)
        {
            break;
        }

        rt_sem_take(&g_esp.rx_sem, rt_tick_from_millisecond(50));
    }

    esp01s_pump_rx();
    if (esp01s_has_any_token(ok1, ok2, ok3))
    {
        return RT_EOK;
    }

    return -RT_ETIMEOUT;
}

static rt_err_t esp01s_wait_token(const char *ok, const char *fail, rt_int32_t timeout_ms)
{
    return esp01s_wait_tokens(ok, RT_NULL, RT_NULL, fail, RT_NULL, RT_NULL, timeout_ms);
}

static rt_err_t esp01s_exec(const char *cmd, const char *ok, const char *fail, rt_int32_t timeout_ms)
{
    rt_err_t ret;

    esp01s_at_lock_take();
    esp01s_clear_rx();
    ret = esp01s_write_line_nolock("%s", cmd);
    if (ret != RT_EOK)
    {
        esp01s_at_lock_release();
        return ret;
    }

    ret = esp01s_wait_token(ok, fail, timeout_ms);
    esp01s_at_lock_release();
    return ret;
}

static rt_err_t esp01s_exec_any(const char *cmd_primary,
                                const char *cmd_fallback,
                                const char *ok,
                                const char *fail,
                                rt_int32_t timeout_ms)
{
    rt_err_t ret;

    ret = esp01s_exec(cmd_primary, ok, fail, timeout_ms);
    if ((ret == RT_EOK) || (cmd_fallback == RT_NULL))
    {
        return ret;
    }

    return esp01s_exec(cmd_fallback, ok, fail, timeout_ms);
}

static rt_err_t esp01s_configure_baud(rt_uint32_t baud_rate)
{
    struct serial_configure cfg = RT_SERIAL_CONFIG_DEFAULT;

    if (g_esp.serial == RT_NULL)
    {
        return -RT_ERROR;
    }

    cfg.baud_rate = baud_rate;
    cfg.bufsz = 256;
    rt_device_control(g_esp.serial, RT_DEVICE_CTRL_CONFIG, &cfg);
    g_esp.baud_rate = baud_rate;
    esp01s_set_state("BAUD");
    esp01s_clear_rx();
    rt_thread_mdelay(80);
    return RT_EOK;
}

static rt_err_t esp01s_sync_baud(void)
{
    static const rt_uint32_t s_baud_table[] = {115200, 9600, 57600, 38400};
    rt_size_t i;
    rt_err_t ret;

    for (i = 0; i < sizeof(s_baud_table) / sizeof(s_baud_table[0]); i++)
    {
        esp01s_configure_baud(s_baud_table[i]);
        ret = esp01s_exec("AT", "OK", "ERROR", 1200);
        if (ret == RT_EOK)
        {
            LOG_I("esp synced at %d baud", s_baud_table[i]);
            esp01s_set_state("SYNC");
            esp01s_set_error(RT_EOK);
            return RT_EOK;
        }
    }

    esp01s_set_error(-RT_ETIMEOUT);
    esp01s_set_state("NOSYNC");
    return -RT_ETIMEOUT;
}

rt_err_t esp01s_init_port(const char *uart_name)
{
    rt_err_t ret;
    rt_device_t serial;
    const char *name = (uart_name != RT_NULL) ? uart_name : ESP01S_UART_NAME;

    serial = rt_device_find(name);
    if (serial == RT_NULL)
    {
        LOG_E("serial %s not found", name);
        return -RT_ENOSYS;
    }

    if (g_esp.serial != RT_NULL && g_esp.serial != serial)
    {
        rt_device_close(g_esp.serial);
    }

    if (g_esp.serial == RT_NULL)
    {
        rt_sem_init(&g_esp.rx_sem, "esp_rx", 0, RT_IPC_FLAG_FIFO);
    }
    if (!g_esp_at_lock_ready)
    {
        rt_mutex_init(&g_esp_at_lock, "esp_at", RT_IPC_FLAG_PRIO);
        g_esp_at_lock_ready = RT_TRUE;
    }

    g_esp.serial = serial;
    rt_strncpy(g_esp.uart_name, name, sizeof(g_esp.uart_name) - 1);
    g_esp.uart_name[sizeof(g_esp.uart_name) - 1] = '\0';

    ret = rt_device_open(g_esp.serial, RT_DEVICE_FLAG_INT_RX | RT_DEVICE_FLAG_RDWR);
    if ((ret != RT_EOK) && (ret != -RT_EBUSY))
    {
        LOG_E("open %s failed: %d", name, ret);
        return ret;
    }

    rt_device_set_rx_indicate(g_esp.serial, esp01s_rx_ind);
    g_esp.ready = RT_TRUE;
    g_esp.connected = RT_FALSE;
    g_esp.scan_busy = RT_FALSE;
    g_esp.init_busy = RT_FALSE;
    g_esp.ip[0] = '\0';
    g_esp.ssid[0] = '\0';
    g_esp.rssi = 0;
    esp01s_clear_scan_list();
    esp01s_set_error(RT_EOK);
    esp01s_set_state("INIT");
    return esp01s_configure_baud(BAUD_RATE_115200);
}

rt_err_t esp01s_probe(void)
{
    rt_err_t ret;

    if (!g_esp.ready)
    {
        ret = esp01s_init_port(ESP01S_UART_NAME);
        if (ret != RT_EOK)
        {
            esp01s_set_error(ret);
            return ret;
        }
    }

    ret = esp01s_sync_baud();
    if (ret != RT_EOK)
    {
        esp01s_set_error(ret);
        return ret;
    }

    ret = esp01s_exec("ATE0", "OK", "ERROR", 2000);
    if (ret != RT_EOK)
    {
        esp01s_set_error(ret);
        return ret;
    }

    ret = esp01s_exec_any("AT+CWMODE_CUR=1", "AT+CWMODE=1", "OK", "ERROR", 3000);
    if (ret != RT_EOK)
    {
        esp01s_set_error(ret);
        return ret;
    }

    ret = esp01s_exec("AT+CIPMUX=0", "OK", "ERROR", 3000);
    if (ret != RT_EOK)
    {
        esp01s_set_error(ret);
        return ret;
    }

    esp01s_apply_station_tuning();

    esp01s_set_state("STA");
    esp01s_set_error(RT_EOK);
    return RT_EOK;
}

rt_err_t esp01s_soft_reset(void)
{
    return esp01s_exec("AT+RST", "ready", "ERROR", 5000);
}

static rt_err_t esp01s_force_reset(void)
{
    rt_err_t ret;

    esp01s_set_state("RESET");
    esp01s_clear_rx();
    ret = esp01s_write_line("AT+RST");
    if (ret != RT_EOK)
    {
        esp01s_set_error(ret);
        return ret;
    }

    rt_thread_mdelay(1500);
    esp01s_pump_rx();
    ret = esp01s_sync_baud();
    if (ret != RT_EOK)
    {
        esp01s_set_error(ret);
        return ret;
    }

    esp01s_set_error(RT_EOK);
    return RT_EOK;
}

static rt_bool_t esp01s_station_matches(const char *ssid)
{
    rt_err_t ret;

    if ((ssid == RT_NULL) || (ssid[0] == '\0'))
    {
        return RT_FALSE;
    }

    ret = esp01s_exec("AT+CWJAP?", "OK", "ERROR", 3000);
    if (ret != RT_EOK)
    {
        return RT_FALSE;
    }

    return (rt_strstr(g_esp.rx_buf, ssid) != RT_NULL) ? RT_TRUE : RT_FALSE;
}

static rt_err_t esp01s_apply_station_tuning(void)
{
    rt_err_t ret;

    ret = esp01s_exec_any("AT+SLEEP=0", RT_NULL, "OK", "ERROR", 2000);
    rt_kprintf("[ESP] Sleep disable: %s\r\n", (ret == RT_EOK) ? "ok" : "skip");

    ret = esp01s_exec_any("AT+CWRECONNCFG=5,0", RT_NULL, "OK", "ERROR", 2000);
    rt_kprintf("[ESP] Reconnect cfg: %s\r\n", (ret == RT_EOK) ? "ok" : "skip");

    return RT_EOK;
}

rt_err_t esp01s_join_ap(const char *ssid, const char *password)
{
    char cmd[160];
    rt_err_t ret;

    if ((ssid == RT_NULL) || (password == RT_NULL))
    {
        return -RT_EINVAL;
    }

    rt_strncpy(g_esp.ssid, ssid, sizeof(g_esp.ssid) - 1);
    g_esp.ssid[sizeof(g_esp.ssid) - 1] = '\0';
    g_esp.connected = RT_FALSE;
    g_esp.ip[0] = '\0';
    esp01s_set_state("JOIN");

    rt_kprintf("[ESP] Joining AP: %s\r\n", ssid);

    ret = esp01s_probe();
    if (ret != RT_EOK)
    {
        rt_kprintf("[ESP] Probe failed: %d\r\n", ret);
        esp01s_set_error(ret);
        return ret;
    }

    esp01s_clear_rx();
    rt_snprintf(cmd, sizeof(cmd), "AT+CWJAP_CUR=\"%s\",\"%s\"", ssid, password);
    ret = esp01s_write_line("%s", cmd);
    if (ret != RT_EOK)
    {
        esp01s_set_error(ret);
        esp01s_set_state("ERR");
        return ret;
    }

    ret = esp01s_wait_tokens("WIFI GOT IP", "WIFI CONNECTED", "OK", "FAIL", "ERROR", "busy", ESP01S_JOIN_TIMEOUT);
    if (ret != RT_EOK)
    {
        esp01s_clear_rx();
        rt_snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, password);
        ret = esp01s_write_line("%s", cmd);
        if (ret == RT_EOK)
        {
            rt_kprintf("[ESP] Fallback join command sent\r\n");
            ret = esp01s_wait_tokens("WIFI GOT IP", "WIFI CONNECTED", "OK", "FAIL", "ERROR", "busy", ESP01S_JOIN_TIMEOUT);
        }
    }

    if (g_esp.rx_len > 0)
    {
        rt_kprintf("[ESP] Join response: %s\r\n", g_esp.rx_buf);
    }

    if ((rt_strstr(g_esp.rx_buf, "WIFI GOT IP") != RT_NULL) ||
        (rt_strstr(g_esp.rx_buf, "WIFI CONNECTED") != RT_NULL) ||
        (rt_strstr(g_esp.rx_buf, "OK") != RT_NULL))
    {
        g_esp.connected = RT_TRUE;
        esp01s_update_rssi_from_scan();
        esp01s_set_error(RT_EOK);
        esp01s_set_state("UP");
        return RT_EOK;
    }

    if (esp01s_station_matches(ssid))
    {
        g_esp.connected = RT_TRUE;
        esp01s_update_rssi_from_scan();
        esp01s_set_error(RT_EOK);
        esp01s_set_state("UP");
        return RT_EOK;
    }

    g_esp.connected = RT_FALSE;
    esp01s_set_error(ret);
    esp01s_set_state("ERR");
    return ret;
}

rt_err_t esp01s_join_default_ap(void)
{
    return esp01s_join_ap(ESP01S_STA_SSID, ESP01S_STA_PASSWORD);
}

static rt_err_t esp01s_connect_default_once(void)
{
    rt_err_t ret;
    char ip[32];
    int retry = 3;

    rt_kprintf("[ESP] Connecting to AP: %s\r\n", ESP01S_STA_SSID);

    ret = esp01s_init_port(ESP01S_UART_NAME);
    if (ret != RT_EOK)
    {
        rt_kprintf("[ESP] Init port failed: %d\r\n", ret);
        return ret;
    }

    ret = esp01s_sync_baud();
    if (ret != RT_EOK)
    {
        rt_kprintf("[ESP] Sync baud failed: %d\r\n", ret);
        return ret;
    }

    ret = esp01s_force_reset();
    if (ret != RT_EOK)
    {
        rt_kprintf("[ESP] Force reset failed: %d\r\n", ret);
        return ret;
    }

    ret = esp01s_exec("ATE0", "OK", "ERROR", 1000);
    if (ret != RT_EOK)
    {
        rt_kprintf("[ESP] ATE0 failed: %d\r\n", ret);
        return ret;
    }

    ret = esp01s_exec_any("AT+CWMODE_CUR=1", "AT+CWMODE=1", "OK", "ERROR", 3000);
    if (ret != RT_EOK)
    {
        rt_kprintf("[ESP] Set mode failed: %d\r\n", ret);
        return ret;
    }

    ret = esp01s_exec_any("AT+CWDHCP_CUR=1,1", "AT+CWDHCP=1,1", "OK", "ERROR", 3000);
    rt_kprintf("[ESP] DHCP enable: %s\r\n", (ret == RT_EOK) ? "ok" : "skip");

    ret = esp01s_exec_any("AT+CWAUTOCONN=1", RT_NULL, "OK", "ERROR", 2000);
    rt_kprintf("[ESP] Set autoconn: %s\r\n", (ret == RT_EOK) ? "ok" : "skip");

    esp01s_apply_station_tuning();

    ret = esp01s_exec_any("AT+CWQAP", RT_NULL, "OK", "ERROR", 3000);
    rt_kprintf("[ESP] Disconnect previous: %s\r\n", (ret == RT_EOK) ? "ok" : "skip");

    rt_thread_mdelay(1000);

    while (retry-- > 0)
    {
        rt_kprintf("[ESP] Attempt %d to join AP\r\n", 3 - retry);
        ret = esp01s_join_default_ap();
        if (ret == RT_EOK)
        {
            break;
        }
        if (g_esp.rx_len > 0)
        {
            rt_kprintf("[ESP] Response: %s\r\n", g_esp.rx_buf);
        }
        rt_thread_mdelay(2000);
    }

    if (ret != RT_EOK)
    {
        return ret;
    }

    ret = esp01s_get_ip(ip, sizeof(ip));
    if (ret == RT_EOK)
    {
        rt_kprintf("[ESP] Get IP success: %s\r\n", ip);
        return RT_EOK;
    }

    rt_kprintf("[ESP] Get IP query failed after join, keep link alive: %d\r\n", ret);
    g_esp.connected = RT_TRUE;
    esp01s_set_state("UP");
    esp01s_set_error(RT_EOK);
    return RT_EOK;
}

rt_err_t esp01s_get_ip(char *ip, rt_size_t len)
{
    char *start;
    char *end;
    rt_err_t ret;

    if ((ip == RT_NULL) || (len == 0))
    {
        return -RT_EINVAL;
    }

    ret = esp01s_exec("AT+CIPSTA?", "OK", "ERROR", 3000);
    if (ret != RT_EOK)
    {
        ret = esp01s_exec("AT+CIFSR", "OK", "ERROR", 3000);
    }
    if (ret != RT_EOK)
    {
        esp01s_set_error(ret);
        return ret;
    }

    start = rt_strstr(g_esp.rx_buf, "+CIPSTA:ip:\"");
    if (start != RT_NULL)
    {
        start += rt_strlen("+CIPSTA:ip:\"");
    }
    else
    {
        start = rt_strstr(g_esp.rx_buf, "+CIFSR:STAIP,\"");
        if (start != RT_NULL)
        {
            start += rt_strlen("+CIFSR:STAIP,\"");
        }
    }

    if (start == RT_NULL)
    {
        return -RT_ERROR;
    }

    end = strchr(start, '"');
    if (end == RT_NULL)
    {
        return -RT_ERROR;
    }

    if ((rt_size_t)(end - start + 1) > len)
    {
        return -RT_ENOMEM;
    }

    memcpy(ip, start, (rt_size_t)(end - start));
    ip[end - start] = '\0';
    rt_strncpy(g_esp.ip, ip, sizeof(g_esp.ip) - 1);
    g_esp.ip[sizeof(g_esp.ip) - 1] = '\0';
    g_esp.connected = RT_TRUE;
    esp01s_update_rssi_from_scan();
    esp01s_set_state("UP");
    esp01s_set_error(RT_EOK);
    return RT_EOK;
}

rt_err_t esp01s_scan_aps(void)
{
    rt_err_t ret;
    rt_bool_t was_connected;

    was_connected = g_esp.connected;

    g_esp.scan_busy = RT_TRUE;
    esp01s_set_state("SCAN");
    esp01s_clear_rx();
    esp01s_clear_scan_list();

    ret = esp01s_write_line("AT+CWLAP");
    if (ret != RT_EOK)
    {
        g_esp.scan_busy = RT_FALSE;
        esp01s_set_error(ret);
        esp01s_set_state("ERR");
        return ret;
    }

    ret = esp01s_wait_token("OK", "ERROR", ESP01S_SCAN_TIMEOUT);
    g_esp.scan_busy = RT_FALSE;
    if (ret != RT_EOK)
    {
        esp01s_set_error(ret);
        esp01s_set_state("ERR");
        return ret;
    }

    ret = esp01s_parse_scan_results();
    esp01s_set_error(ret);
    esp01s_set_state((ret == RT_EOK) ? (g_esp.connected ? "UP" : "SCANOK") : "ERR");

    if ((was_connected == RT_TRUE) && (g_esp.connected == RT_FALSE))
    {
        ret = esp01s_join_default_ap();
        if (ret == RT_EOK)
        {
            esp01s_get_ip(g_esp.ip, sizeof(g_esp.ip));
        }
        else
        {
            esp01s_set_error(ret);
            esp01s_set_state("RETRY");
        }
    }

    return ret;
}

rt_size_t esp01s_get_scan_list(esp01s_ap_info_t *aps, rt_size_t max_aps)
{
    rt_size_t copy_count;
    rt_base_t level;

    if ((aps == RT_NULL) || (max_aps == 0))
    {
        return 0;
    }

    level = rt_hw_interrupt_disable();
    copy_count = (g_esp.ap_count < max_aps) ? g_esp.ap_count : max_aps;
    memcpy(aps, g_esp.aps, copy_count * sizeof(esp01s_ap_info_t));
    rt_hw_interrupt_enable(level);
    return copy_count;
}

rt_err_t esp01s_at_command(const char *cmd, const char *ok, const char *fail, rt_int32_t timeout_ms)
{
    if (cmd == RT_NULL)
    {
        return -RT_EINVAL;
    }

    return esp01s_exec(cmd, ok, fail, timeout_ms);
}

rt_err_t esp01s_at_command_data(const char *cmd,
                                const void *data,
                                rt_size_t len,
                                const char *ok,
                                const char *fail,
                                rt_int32_t timeout_ms)
{
    rt_err_t ret;

    if ((cmd == RT_NULL) || (data == RT_NULL) || (len == 0))
    {
        return -RT_EINVAL;
    }

    esp01s_at_lock_take();
    esp01s_clear_rx();

    ret = esp01s_write_line_nolock("%s", cmd);
    if (ret != RT_EOK)
    {
        esp01s_at_lock_release();
        return ret;
    }

    ret = esp01s_wait_token(">", "ERROR", 5000);
    if (ret != RT_EOK)
    {
        esp01s_at_lock_release();
        return ret;
    }

    ret = esp01s_write_bytes_nolock(data, len);
    if (ret != RT_EOK)
    {
        esp01s_at_lock_release();
        return ret;
    }

    ret = esp01s_wait_token(ok, fail, timeout_ms);
    esp01s_at_lock_release();
    return ret;
}

rt_err_t esp01s_mqtt_pubraw(const char *topic,
                            const void *payload,
                            rt_size_t len,
                            rt_uint8_t qos,
                            rt_bool_t retain)
{
    char cmd[192];
    rt_err_t ret;

    if ((topic == RT_NULL) || (payload == RT_NULL) || (len == 0))
    {
        return -RT_EINVAL;
    }

    if (len > 1024U)
    {
        return -RT_EINVAL;
    }

    esp01s_at_lock_take();
    esp01s_clear_rx();
    rt_snprintf(cmd, sizeof(cmd), "AT+MQTTPUBRAW=0,\"%s\",%u,%u,%u",
                topic,
                (unsigned int)len,
                (unsigned int)qos,
                retain ? 1U : 0U);
    ret = esp01s_write_line_nolock("%s", cmd);
    if (ret != RT_EOK)
    {
        esp01s_at_lock_release();
        return ret;
    }

    ret = esp01s_wait_token(">", "ERROR", 5000);
    if (ret != RT_EOK)
    {
        esp01s_at_lock_release();
        return ret;
    }

    ret = esp01s_write_bytes_nolock(payload, len);
    if (ret != RT_EOK)
    {
        esp01s_at_lock_release();
        return ret;
    }

    ret = esp01s_wait_tokens("+MQTTPUB:OK", "OK", RT_NULL, "+MQTTPUB:FAIL", "ERROR", RT_NULL, 10000);
    esp01s_at_lock_release();
    return ret;
}

rt_err_t esp01s_mqtt_subscribe(const char *topic, rt_uint8_t qos)
{
    char cmd[256];

    if (topic == RT_NULL)
    {
        return -RT_EINVAL;
    }

    rt_snprintf(cmd, sizeof(cmd), "AT+MQTTSUB=0,\"%s\",%u", topic, (unsigned int)qos);
    return esp01s_at_command(cmd, "OK", "ERROR", 5000);
}

static char *esp01s_find_subrecv_prefix(void)
{
    return rt_strstr(g_esp.rx_buf, "+MQTTSUBRECV:");
}

static void esp01s_consume_rx_prefix(rt_size_t count)
{
    if (count >= g_esp.rx_len)
    {
        g_esp.rx_len = 0;
        g_esp.rx_buf[0] = '\0';
        return;
    }

    memmove(g_esp.rx_buf, g_esp.rx_buf + count, g_esp.rx_len - count);
    g_esp.rx_len -= count;
    g_esp.rx_buf[g_esp.rx_len] = '\0';
}

static rt_err_t esp01s_try_parse_subrecv(char *topic,
                                         rt_size_t topic_len,
                                         char *payload,
                                         rt_size_t payload_len)
{
    char *prefix;
    char *p;
    char *topic_start;
    char *topic_end;
    char *payload_start;
    rt_size_t prefix_offset;
    rt_size_t msg_len;
    rt_size_t consumed;

    prefix = esp01s_find_subrecv_prefix();
    if (prefix == RT_NULL)
    {
        if (g_esp.rx_len > 1024U)
        {
            esp01s_consume_rx_prefix(g_esp.rx_len - 256U);
        }
        return -RT_ENOSYS;
    }

    prefix_offset = (rt_size_t)(prefix - g_esp.rx_buf);
    if (prefix_offset > 0U)
    {
        esp01s_consume_rx_prefix(prefix_offset);
        prefix = g_esp.rx_buf;
    }

    p = prefix;
    if (strncmp(p, "+MQTTSUBRECV:", 13) != 0)
    {
        return -RT_ERROR;
    }
    p += 13;

    while ((*p >= '0') && (*p <= '9'))
    {
        p++;
    }
    if (*p != ',')
    {
        return -RT_EBUSY;
    }
    p++;
    if (*p != '"')
    {
        return -RT_EBUSY;
    }
    p++;

    topic_start = p;
    topic_end = strchr(topic_start, '"');
    if (topic_end == RT_NULL)
    {
        return -RT_EBUSY;
    }
    if ((topic != RT_NULL) && (topic_len > 0U))
    {
        rt_size_t copy_len = (rt_size_t)(topic_end - topic_start);
        if (copy_len >= topic_len)
        {
            copy_len = topic_len - 1U;
        }
        memcpy(topic, topic_start, copy_len);
        topic[copy_len] = '\0';
    }

    p = topic_end + 1;
    if (*p != ',')
    {
        return -RT_EBUSY;
    }
    p++;
    msg_len = (rt_size_t)strtoul(p, &payload_start, 10);
    if ((payload_start == p) || (*payload_start != ','))
    {
        return -RT_EBUSY;
    }
    payload_start++;

    if (((rt_size_t)(g_esp.rx_buf + g_esp.rx_len - payload_start)) < msg_len)
    {
        return -RT_EBUSY;
    }

    if ((payload != RT_NULL) && (payload_len > 0U))
    {
        rt_size_t copy_len = msg_len;
        if (copy_len >= payload_len)
        {
            copy_len = payload_len - 1U;
        }
        memcpy(payload, payload_start, copy_len);
        payload[copy_len] = '\0';
    }

    consumed = (rt_size_t)(payload_start - g_esp.rx_buf) + msg_len;
    while ((consumed < g_esp.rx_len) &&
           ((g_esp.rx_buf[consumed] == '\r') || (g_esp.rx_buf[consumed] == '\n')))
    {
        consumed++;
    }

    esp01s_consume_rx_prefix(consumed);
    return RT_EOK;
}

rt_err_t esp01s_mqtt_recv(char *topic,
                          rt_size_t topic_len,
                          char *payload,
                          rt_size_t payload_len,
                          rt_int32_t timeout_ms)
{
    rt_tick_t deadline = rt_tick_get() + rt_tick_from_millisecond(timeout_ms);

    while (1)
    {
        rt_err_t ret;

        esp01s_at_lock_take();
        esp01s_pump_rx();
        ret = esp01s_try_parse_subrecv(topic, topic_len, payload, payload_len);
        esp01s_at_lock_release();

        if (ret == RT_EOK)
        {
            return RT_EOK;
        }
        if (ret != -RT_EBUSY && ret != -RT_ENOSYS)
        {
            return ret;
        }
        if ((rt_int32_t)(deadline - rt_tick_get()) <= 0)
        {
            return -RT_ETIMEOUT;
        }

        rt_sem_take(&g_esp.rx_sem, rt_tick_from_millisecond(50));
    }
}

rt_err_t esp01s_join_saved_ap(const char *ssid, rt_uint8_t ecn)
{
    const char *password;

    if (ssid == RT_NULL)
    {
        return -RT_EINVAL;
    }

    password = esp01s_find_saved_password(ssid);
    if (password != RT_NULL)
    {
        return esp01s_join_ap(ssid, password);
    }

    if (ecn == 0U)
    {
        return esp01s_join_ap(ssid, "");
    }

    rt_strncpy(g_esp.ssid, ssid, sizeof(g_esp.ssid) - 1);
    g_esp.ssid[sizeof(g_esp.ssid) - 1] = '\0';
    g_esp.connected = RT_FALSE;
    esp01s_set_error(-RT_EINVAL);
    esp01s_set_state("NEEDPWD");
    return -RT_EINVAL;
}

void esp01s_get_status(esp01s_status_t *status)
{
    rt_base_t level;

    if (status == RT_NULL)
    {
        return;
    }

    level = rt_hw_interrupt_disable();
    status->baud_rate = g_esp.baud_rate;
    status->ready = g_esp.ready;
    status->connected = g_esp.connected;
    status->scan_busy = g_esp.scan_busy;
    status->ap_count = g_esp.ap_count;
    status->last_error = g_esp.last_error;
    status->rssi = g_esp.rssi;
    rt_strncpy(status->ssid, g_esp.ssid, sizeof(status->ssid) - 1);
    status->ssid[sizeof(status->ssid) - 1] = '\0';
    rt_strncpy(status->ip, g_esp.ip, sizeof(status->ip) - 1);
    status->ip[sizeof(status->ip) - 1] = '\0';
    rt_strncpy(status->state, g_esp.state, sizeof(status->state) - 1);
    status->state[sizeof(status->state) - 1] = '\0';
    rt_hw_interrupt_enable(level);
}

rt_err_t esp01s_http_get(const char *host, const char *path)
{
    char req[256];
    int req_len;
    rt_err_t ret;
    rt_tick_t deadline;

    if ((host == RT_NULL) || (path == RT_NULL))
    {
        return -RT_EINVAL;
    }

    ret = esp01s_probe();
    if (ret != RT_EOK)
    {
        esp01s_set_error(ret);
        return ret;
    }

    esp01s_exec("AT+CIPCLOSE", "OK", "ERROR", 1000);

    esp01s_clear_rx();
    ret = esp01s_write_line("AT+CIPSTART=\"TCP\",\"%s\",80", host);
    if (ret != RT_EOK)
    {
        return ret;
    }
    ret = esp01s_wait_token("OK", "ERROR", 8000);
    if (ret != RT_EOK)
    {
        esp01s_set_error(ret);
        return ret;
    }

    req_len = rt_snprintf(req, sizeof(req),
                          "GET %s HTTP/1.1\r\n"
                          "Host: %s\r\n"
                          "Connection: close\r\n"
                          "User-Agent: stm32f407-esp01s\r\n"
                          "\r\n",
                          path,
                          host);
    if ((req_len <= 0) || (req_len >= (int)sizeof(req)))
    {
        return -RT_ENOMEM;
    }

    esp01s_clear_rx();
    ret = esp01s_write_line("AT+CIPSEND=%d", req_len);
    if (ret != RT_EOK)
    {
        return ret;
    }
    ret = esp01s_wait_token(">", "ERROR", 5000);
    if (ret != RT_EOK)
    {
        esp01s_set_error(ret);
        return ret;
    }

    ret = esp01s_write_bytes(req, (rt_size_t)req_len);
    if (ret != RT_EOK)
    {
        return ret;
    }

    ret = esp01s_wait_token("SEND OK", "ERROR", 5000);
    if (ret != RT_EOK)
    {
        esp01s_set_error(ret);
        return ret;
    }

    deadline = rt_tick_get() + rt_tick_from_millisecond(5000);
    while ((rt_int32_t)(deadline - rt_tick_get()) > 0)
    {
        esp01s_pump_rx();
        if (rt_strstr(g_esp.rx_buf, "CLOSED") != RT_NULL)
        {
            break;
        }
        rt_sem_take(&g_esp.rx_sem, rt_tick_from_millisecond(100));
    }

    rt_kprintf("%s\r\n", g_esp.rx_buf);
    esp01s_set_error(RT_EOK);
    return RT_EOK;
}

const char *esp01s_last_response(void)
{
    return g_esp.rx_buf;
}

static void esp01s_boot_thread(void *parameter)
{
    rt_err_t ret;
    char ip[32];
    rt_uint32_t retry = 0;

    RT_UNUSED(parameter);

    rt_thread_mdelay(1200);
    g_esp.init_busy = RT_TRUE;
    esp01s_set_state("BOOT");

    while (1)
    {
        ret = esp01s_connect_default_once();
        if (ret == RT_EOK)
        {
            g_esp.init_busy = RT_FALSE;
            if (esp01s_get_ip(ip, sizeof(ip)) == RT_EOK)
            {
                rt_kprintf("esp sta connected: ssid=%s ip=%s baud=%d\r\n",
                           ESP01S_STA_SSID,
                           ip,
                           g_esp.baud_rate);
            }
            else
            {
                rt_kprintf("esp sta connected: ssid=%s ip=unknown baud=%d\r\n",
                           ESP01S_STA_SSID,
                           g_esp.baud_rate);
            }
            return;
        }

        retry++;
        g_esp.connected = RT_FALSE;
        esp01s_set_state("RETRY");
        rt_kprintf("esp sta init thread retry=%lu ret=%d\r\n", (unsigned long)retry, ret);
        if (g_esp.rx_len > 0)
        {
            rt_kprintf("%s\r\n", g_esp.rx_buf);
        }
        if (retry >= ESP01S_BOOT_RETRY)
        {
            retry = 0;
        }
        rt_thread_mdelay(3000);
    }
}

static int esp01s_auto_connect_init(void)
{
    rt_thread_t thread;

    if (g_esp_connect_thread_started)
    {
        return RT_EOK;
    }

    thread = rt_thread_create("esp_sta", esp01s_boot_thread, RT_NULL, 2048, 20, 10);
    if (thread == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    g_esp_connect_thread_started = RT_TRUE;
    rt_thread_startup(thread);
    return RT_EOK;
}

int esp01s_start_connect(void)
{
    return esp01s_auto_connect_init();
}

static int esp_init_cmd(int argc, char **argv)
{
    const char *uart_name = (argc > 1) ? argv[1] : ESP01S_UART_NAME;
    rt_err_t ret = esp01s_init_port(uart_name);

    rt_kprintf("esp init on %s %s\r\n", uart_name, (ret == RT_EOK) ? "ok" : "fail");
    return ret;
}
MSH_CMD_EXPORT_ALIAS(esp_init_cmd, esp_init, esp_init [uart_name]);

static int esp_probe_cmd(void)
{
    rt_err_t ret = esp01s_probe();

    rt_kprintf("esp probe: %s baud=%d\r\n", (ret == RT_EOK) ? "ok" : "fail", g_esp.baud_rate);
    if (g_esp.rx_len > 0)
    {
        rt_kprintf("%s\r\n", g_esp.rx_buf);
    }
    return ret;
}
MSH_CMD_EXPORT_ALIAS(esp_probe_cmd, esp_probe, esp_probe);

static int esp_join_cmd(int argc, char **argv)
{
    rt_err_t ret;

    if (argc < 3)
    {
        rt_kprintf("usage: esp_join <ssid> <password>\r\n");
        return -RT_EINVAL;
    }

    ret = esp01s_join_ap(argv[1], argv[2]);
    rt_kprintf("esp join: %s\r\n", (ret == RT_EOK) ? "ok" : "fail");
    if (g_esp.rx_len > 0)
    {
        rt_kprintf("%s\r\n", g_esp.rx_buf);
    }
    return ret;
}
MSH_CMD_EXPORT_ALIAS(esp_join_cmd, esp_join, esp_join <ssid> <password>);

static int esp_sta_cmd(void)
{
    rt_err_t ret = esp01s_join_default_ap();

    rt_kprintf("esp sta(Bai): %s baud=%d\r\n", (ret == RT_EOK) ? "ok" : "fail", g_esp.baud_rate);
    if (g_esp.rx_len > 0)
    {
        rt_kprintf("%s\r\n", g_esp.rx_buf);
    }
    return ret;
}
MSH_CMD_EXPORT_ALIAS(esp_sta_cmd, esp_sta, esp_sta);

static int esp_ip_cmd(void)
{
    char ip[32];
    rt_err_t ret = esp01s_get_ip(ip, sizeof(ip));

    if (ret == RT_EOK)
    {
        rt_kprintf("ip: %s\r\n", ip);
    }
    else
    {
        rt_kprintf("get ip failed: %d\r\n", ret);
        if (g_esp.rx_len > 0)
        {
            rt_kprintf("%s\r\n", g_esp.rx_buf);
        }
    }
    return ret;
}
MSH_CMD_EXPORT_ALIAS(esp_ip_cmd, esp_ip, esp_ip);

static int esp_status_cmd(void)
{
    rt_err_t ret;
    esp01s_status_t status;
    esp01s_ap_info_t aps[ESP01S_MAX_APS];
    rt_size_t count;
    rt_size_t i;

    ret = esp01s_exec_any("AT+CWJAP?", RT_NULL, "OK", "ERROR", 3000);
    rt_kprintf("cwjap?: %s\r\n", (ret == RT_EOK) ? "ok" : "fail");
    if (g_esp.rx_len > 0)
    {
        rt_kprintf("%s\r\n", g_esp.rx_buf);
    }

    ret = esp01s_exec_any("AT+CIPSTA?", "AT+CIFSR", "OK", "ERROR", 3000);
    rt_kprintf("ip query: %s\r\n", (ret == RT_EOK) ? "ok" : "fail");
    if (g_esp.rx_len > 0)
    {
        rt_kprintf("%s\r\n", g_esp.rx_buf);
    }

    esp01s_get_status(&status);
    rt_kprintf("state=%s ready=%d conn=%d baud=%lu rssi=%d ap_count=%u err=%d\r\n",
               status.state,
               status.ready,
               status.connected,
               (unsigned long)status.baud_rate,
               status.rssi,
               (unsigned int)status.ap_count,
               status.last_error);

    count = esp01s_get_scan_list(aps, ESP01S_MAX_APS);
    for (i = 0; i < count; i++)
    {
        rt_kprintf("[%u] ecn=%u rssi=%d ch=%u ssid=%s\r\n",
                   (unsigned int)i,
                   aps[i].ecn,
                   aps[i].rssi,
                   aps[i].channel,
                   aps[i].ssid);
    }
    return ret;
}
MSH_CMD_EXPORT_ALIAS(esp_status_cmd, esp_status, esp_status);

static int esp_scan_cmd(void)
{
    rt_err_t ret;
    esp01s_ap_info_t aps[ESP01S_MAX_APS];
    rt_size_t count;
    rt_size_t i;

    ret = esp01s_scan_aps();
    rt_kprintf("esp scan: %s\r\n", (ret == RT_EOK) ? "ok" : "fail");
    count = esp01s_get_scan_list(aps, ESP01S_MAX_APS);
    for (i = 0; i < count; i++)
    {
        rt_kprintf("[%u] ecn=%u rssi=%d ch=%u ssid=%s\r\n",
                   (unsigned int)i,
                   aps[i].ecn,
                   aps[i].rssi,
                   aps[i].channel,
                   aps[i].ssid);
    }
    if (g_esp.rx_len > 0)
    {
        rt_kprintf("%s\r\n", g_esp.rx_buf);
    }
    return ret;
}
MSH_CMD_EXPORT_ALIAS(esp_scan_cmd, esp_scan, esp_scan);

static int esp_http_cmd(int argc, char **argv)
{
    const char *path = (argc > 2) ? argv[2] : "/";
    rt_err_t ret;

    if (argc < 2)
    {
        rt_kprintf("usage: esp_http <host> [path]\r\n");
        return -RT_EINVAL;
    }

    ret = esp01s_http_get(argv[1], path);
    rt_kprintf("esp http: %s\r\n", (ret == RT_EOK) ? "ok" : "fail");
    return ret;
}
MSH_CMD_EXPORT_ALIAS(esp_http_cmd, esp_http, esp_http <host> [path]);

static int esp_cmd_cmd(int argc, char **argv)
{
    rt_err_t ret;

    if (argc < 2)
    {
        rt_kprintf("usage: esp_cmd <AT+...>\r\n");
        return -RT_EINVAL;
    }

    ret = esp01s_exec(argv[1], "OK", "ERROR", 5000);
    rt_kprintf("%s\r\n", g_esp.rx_buf);
    return ret;
}
MSH_CMD_EXPORT_ALIAS(esp_cmd_cmd, esp_cmd, esp_cmd <AT+...>);

