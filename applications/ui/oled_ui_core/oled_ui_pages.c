#include "oled_ui_pages.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <rtthread.h>
#include "data/todo_snapshot.h"
#include "cloud/aliyun_iot_mqtt.h"
#include "network/esp01s_at.h"

#define UI_SCAN_MAX_APS     8
#define UI_TODO_LINE_LEN    80
#define UI_AP_LINE_LEN      48
#define UI_WIFI_SSID_LEN    32
#define UI_STATUS_LINE_LEN  32
#define UI_PC_STATUS_LEN    24
#define UI_PC_STEP          2
#define UI_PC_MAX_LEVEL     100
#define HID_USAGE_BRIGHTNESS_INC   0x006FU
#define HID_USAGE_BRIGHTNESS_DEC   0x0070U
#define HID_USAGE_VOLUME_INC       0x00E9U
#define HID_USAGE_VOLUME_DEC       0x00EAU

typedef enum
{
    WIFI_JOB_NONE = 0,
    WIFI_JOB_SCAN,
    WIFI_JOB_CONNECT,
    WIFI_JOB_REJOIN,
} wifi_job_type_t;

typedef struct
{
    wifi_job_type_t type;
    char ssid[33];
    rt_uint8_t ecn;
} wifi_job_ctx_t;

extern MenuPage *CurrentMenuPage;
extern int16_t OLED_UI_Brightness;
extern bool OLED_UI_ShowFps;
extern bool ColorMode;
extern rt_bool_t usb_hid_is_ready(void);
extern rt_bool_t usb_hid_can_send(void);
extern rt_err_t usb_hid_send_consumer_usage(rt_uint16_t usage);
extern rt_uint32_t usb_hid_get_polling_interval_ms(void);

static char s_todo_lines[TODO_SNAPSHOT_MAX_ITEMS][UI_TODO_LINE_LEN];
static char s_ap_lines[UI_SCAN_MAX_APS][UI_AP_LINE_LEN];
static char s_status_lines[4][UI_STATUS_LINE_LEN];
static char s_pc_status_line[UI_PC_STATUS_LEN];
static rt_uint32_t s_todo_display_count = 0;
static rt_size_t s_scan_display_count = 0;
static volatile rt_uint8_t s_wifi_job_busy = 0;
static char s_saved_join_ssid[33] = "Bai";
static rt_uint8_t s_saved_join_ecn = 0;
static MenuPage *s_last_page = RT_NULL;
static int16_t s_pc_volume = 50;
static int16_t s_pc_brightness = 50;
static int16_t s_pc_volume_shadow = 50;
static int16_t s_pc_brightness_shadow = 50;
static rt_int16_t s_pc_volume_pending = 0;
static rt_int16_t s_pc_brightness_pending = 0;
static rt_uint8_t s_pc_release_pending = 0U;
static rt_tick_t s_pc_last_report_tick = 0U;

static void ui_handle_wifi_scan_selection(void);
static void ui_toggle_todo_completion(void);
static void ui_clamp_page_cursor(MenuPage *page, rt_uint32_t count);
static void ui_sync_status_items(void);
static void ui_open_pc_volume_window(void);
static void ui_open_pc_brightness_window(void);
static void ui_sync_pc_items(void);
static void ui_update_pc_control(void);

static void ui_copy_visible_text(char *dst, rt_size_t len, const char *src)
{
    rt_size_t used = 0;
    rt_size_t copy_len = 0;

    if ((dst == RT_NULL) || (len == 0U))
    {
        return;
    }

    dst[0] = '\0';
    if (src == RT_NULL)
    {
        return;
    }

    while ((*src != '\0') && (used + 1U < len))
    {
        unsigned char ch = (unsigned char)(*src);

        if ((ch >= 32U) && (ch < 127U))
        {
            dst[used++] = (char)ch;
            src++;
            continue;
        }

        if ((ch & 0x80U) != 0U)
        {
            if ((ch & 0xF0U) == 0xF0U)
            {
                copy_len = 4U;
            }
            else if ((ch & 0xE0U) == 0xE0U)
            {
                copy_len = 3U;
            }
            else if ((ch & 0xC0U) == 0xC0U)
            {
                copy_len = 2U;
            }
            else
            {
                copy_len = 1U;
            }

            if ((used + copy_len) >= len)
            {
                break;
            }

            while (copy_len-- > 0U)
            {
                dst[used++] = *src++;
            }
            continue;
        }

        if (ch >= 32U)
        {
            dst[used++] = (char)ch;
        }
        src++;
    }

    dst[used] = '\0';
}


static void ui_sync_todo_items(void)
{
    todo_snapshot_t snapshot;
    rt_uint32_t count;
    rt_uint32_t i;
    char text[UI_TODO_LINE_LEN];
    char title[UI_TODO_LINE_LEN - 6];

    todo_snapshot_get(&snapshot);
    count = snapshot.count;
    if (count > TODO_SNAPSHOT_MAX_ITEMS)
    {
        count = TODO_SNAPSHOT_MAX_ITEMS;
    }

    if (count == 0U)
    {
        rt_strncpy(s_todo_lines[0], "[ ] NO TODO", sizeof(s_todo_lines[0]) - 1U);
        s_todo_lines[0][sizeof(s_todo_lines[0]) - 1U] = '\0';
        TodoMenuItems[0].General_item_text = s_todo_lines[0];
        TodoMenuItems[0].General_callback = RT_NULL;
        TodoMenuItems[0].General_SubMenuPage = RT_NULL;
        TodoMenuItems[0].List_BoolRadioBox = RT_NULL;
        TodoMenuItems[0].List_IntBox = RT_NULL;
        TodoMenuItems[0].List_FloatBox = RT_NULL;
        TodoMenuItems[1].General_item_text = RT_NULL;
        s_todo_display_count = 1U;
        return;
    }

    for (i = 0; i < count; i++)
    {
        const char *src = snapshot.items[i].title[0] ? snapshot.items[i].title : snapshot.items[i].id;

        ui_copy_visible_text(title, sizeof(title), src);
        if (title[0] == '\0')
        {
            rt_snprintf(title, sizeof(title), "ITEM-%u", (unsigned int)(i + 1U));
        }

        rt_snprintf(text, sizeof(text), "%s %s", snapshot.items[i].completed ? "[x]" : "[ ]", title);
        rt_strncpy(s_todo_lines[i], text, sizeof(s_todo_lines[i]) - 1U);
        s_todo_lines[i][sizeof(s_todo_lines[i]) - 1U] = '\0';

        TodoMenuItems[i].General_item_text = s_todo_lines[i];
        TodoMenuItems[i].General_callback = ui_toggle_todo_completion;
        TodoMenuItems[i].General_SubMenuPage = RT_NULL;
        TodoMenuItems[i].List_BoolRadioBox = RT_NULL;
        TodoMenuItems[i].List_IntBox = RT_NULL;
        TodoMenuItems[i].List_FloatBox = RT_NULL;
    }

    TodoMenuItems[count].General_item_text = RT_NULL;
    s_todo_display_count = count;
}

static void ui_toggle_todo_completion(void)
{
    todo_snapshot_change_t change;
    rt_uint32_t index;

    if (CurrentMenuPage == RT_NULL)
    {
        return;
    }

    index = (rt_uint32_t)CurrentMenuPage->_ActiveMenuID;
    if (todo_snapshot_toggle_completed(index, &change) != RT_EOK)
    {
        return;
    }

    ui_sync_todo_items();
    ui_clamp_page_cursor(&TodoMenuPage, s_todo_display_count);
    (void)aliyun_iot_queue_todo_report(&change);
}

static void ui_sync_scan_items(void)
{
    esp01s_ap_info_t aps[UI_SCAN_MAX_APS];
    rt_size_t count;
    rt_size_t i;

    WiFiScanMenuItems[0].General_item_text = "RESCAN";
    WiFiScanMenuItems[0].General_callback = ui_handle_wifi_scan_selection;
    WiFiScanMenuItems[0].General_SubMenuPage = RT_NULL;
    WiFiScanMenuItems[0].List_BoolRadioBox = RT_NULL;
    WiFiScanMenuItems[0].List_IntBox = RT_NULL;
    WiFiScanMenuItems[0].List_FloatBox = RT_NULL;

    count = esp01s_get_scan_list(aps, UI_SCAN_MAX_APS);
    if (count > UI_SCAN_MAX_APS)
    {
        count = UI_SCAN_MAX_APS;
    }

    if (count == 0U)
    {
        rt_strncpy(s_ap_lines[0], "NO AP FOUND", sizeof(s_ap_lines[0]) - 1U);
        s_ap_lines[0][sizeof(s_ap_lines[0]) - 1U] = '\0';
        WiFiScanMenuItems[1].General_item_text = s_ap_lines[0];
        WiFiScanMenuItems[1].General_callback = RT_NULL;
        WiFiScanMenuItems[1].General_SubMenuPage = RT_NULL;
        WiFiScanMenuItems[2].General_item_text = RT_NULL;
        s_scan_display_count = 2U;
        return;
    }

    for (i = 0; i < count; i++)
    {
        char ssid[24];

        ui_copy_visible_text(ssid, sizeof(ssid), aps[i].ssid);
        if (ssid[0] == '\0')
        {
            rt_strncpy(ssid, "<hidden>", sizeof(ssid) - 1U);
            ssid[sizeof(ssid) - 1U] = '\0';
        }

        rt_snprintf(s_ap_lines[i], sizeof(s_ap_lines[i]), "%s %ddBm", ssid, aps[i].rssi);
        WiFiScanMenuItems[i + 1U].General_item_text = s_ap_lines[i];
        WiFiScanMenuItems[i + 1U].General_callback = ui_handle_wifi_scan_selection;
        WiFiScanMenuItems[i + 1U].General_SubMenuPage = RT_NULL;
        WiFiScanMenuItems[i + 1U].List_BoolRadioBox = RT_NULL;
        WiFiScanMenuItems[i + 1U].List_IntBox = RT_NULL;
        WiFiScanMenuItems[i + 1U].List_FloatBox = RT_NULL;
    }

    WiFiScanMenuItems[count + 1U].General_item_text = RT_NULL;
    s_scan_display_count = count + 1U;
}

static void ui_clamp_page_cursor(MenuPage *page, rt_uint32_t count)
{
    if ((page == RT_NULL) || (count == 0U))
    {
        return;
    }

    if ((rt_uint32_t)page->_ActiveMenuID >= count)
    {
        page->_ActiveMenuID = (MenuID)(count - 1U);
    }
    if ((rt_uint32_t)page->_Slot >= count)
    {
        page->_Slot = (MenuID)(count - 1U);
    }
}

static void ui_wifi_job_thread(void *parameter)
{
    wifi_job_ctx_t *ctx = (wifi_job_ctx_t *)parameter;

    if (ctx != RT_NULL)
    {
        if (ctx->type == WIFI_JOB_SCAN)
        {
            esp01s_scan_aps();
        }
        else if (ctx->type == WIFI_JOB_CONNECT)
        {
            esp01s_join_saved_ap(ctx->ssid, ctx->ecn);
        }
        else if (ctx->type == WIFI_JOB_REJOIN)
        {
            esp01s_join_default_ap();
        }
    }

    s_wifi_job_busy = 0U;
    if (ctx != RT_NULL)
    {
        rt_free(ctx);
    }
}

static void ui_start_wifi_job(wifi_job_type_t type, const char *ssid, rt_uint8_t ecn)
{
    rt_thread_t thread;
    wifi_job_ctx_t *ctx;

    if (s_wifi_job_busy != 0U)
    {
        return;
    }

    ctx = (wifi_job_ctx_t *)rt_calloc(1, sizeof(*ctx));
    if (ctx == RT_NULL)
    {
        return;
    }

    ctx->type = type;
    ctx->ecn = ecn;
    if (ssid != RT_NULL)
    {
        rt_strncpy(ctx->ssid, ssid, sizeof(ctx->ssid) - 1U);
        ctx->ssid[sizeof(ctx->ssid) - 1U] = '\0';
    }

    thread = rt_thread_create("uiwifi", ui_wifi_job_thread, ctx, 2048, 17, 10);
    if (thread == RT_NULL)
    {
        rt_free(ctx);
        return;
    }

    s_wifi_job_busy = 1U;
    rt_thread_startup(thread);
}

static void ui_open_brightness_window(void)
{
    static MenuWindow window = {
        .General_Width = 96,
        .General_Height = 28,
        .General_ContinueTime = 8.0f,
        .General_WindowType = WINDOW_ROUNDRECTANGLE,
        .Text_String = "BRIGHTNESS",
        .Text_FontSize = OLED_UI_FONT_12,
        .Text_FontSideDistance = 4,
        .Text_FontTopDistance = 3,
        .Prob_Data_Int = &OLED_UI_Brightness,
        .Prob_DataStep = 8,
        .Prob_MinData = 0,
        .Prob_MaxData = 255,
        .Prob_BottomDistance = 3,
        .Prob_SideDistance = 4,
        .Prob_LineHeight = 8,
    };

    oled_ui_create_window(&window);
}

static void ui_open_pc_volume_window(void)
{
    static MenuWindow window = {
        .General_Width = 96,
        .General_Height = 28,
        .General_ContinueTime = 2.0f,
        .General_WindowType = WINDOW_ROUNDRECTANGLE,
        .Text_String = "PC VOLUME",
        .Text_FontSize = OLED_UI_FONT_12,
        .Text_FontSideDistance = 4,
        .Text_FontTopDistance = 3,
        .Prob_Data_Int = &s_pc_volume,
        .Prob_DataStep = UI_PC_STEP,
        .Prob_MinData = 0,
        .Prob_MaxData = UI_PC_MAX_LEVEL,
        .Prob_BottomDistance = 3,
        .Prob_SideDistance = 4,
        .Prob_LineHeight = 8,
    };

    oled_ui_create_window(&window);
}

static void ui_open_pc_brightness_window(void)
{
    static MenuWindow window = {
        .General_Width = 96,
        .General_Height = 28,
        .General_ContinueTime = 2.0f,
        .General_WindowType = WINDOW_ROUNDRECTANGLE,
        .Text_String = "PC BRIGHT",
        .Text_FontSize = OLED_UI_FONT_12,
        .Text_FontSideDistance = 4,
        .Text_FontTopDistance = 3,
        .Prob_Data_Int = &s_pc_brightness,
        .Prob_DataStep = UI_PC_STEP,
        .Prob_MinData = 0,
        .Prob_MaxData = UI_PC_MAX_LEVEL,
        .Prob_BottomDistance = 3,
        .Prob_SideDistance = 4,
        .Prob_LineHeight = 8,
    };

    oled_ui_create_window(&window);
}

static void ui_request_wifi_scan(void)
{
    ui_start_wifi_job(WIFI_JOB_SCAN, RT_NULL, 0U);
}

static void ui_request_wifi_rejoin(void)
{
    ui_start_wifi_job(WIFI_JOB_REJOIN, s_saved_join_ssid, s_saved_join_ecn);
}

static void ui_handle_wifi_scan_selection(void)
{
    esp01s_ap_info_t aps[UI_SCAN_MAX_APS];
    rt_size_t count = esp01s_get_scan_list(aps, UI_SCAN_MAX_APS);
    MenuID index = (CurrentMenuPage != RT_NULL) ? CurrentMenuPage->_ActiveMenuID : 0;

    if (index <= 0)
    {
        ui_start_wifi_job(WIFI_JOB_SCAN, RT_NULL, 0U);
        return;
    }

    if (((rt_size_t)(index - 1)) < count)
    {
        ui_start_wifi_job(WIFI_JOB_CONNECT, aps[index - 1].ssid, aps[index - 1].ecn);
        rt_strncpy(s_saved_join_ssid, aps[index - 1].ssid, sizeof(s_saved_join_ssid) - 1U);
        s_saved_join_ssid[sizeof(s_saved_join_ssid) - 1U] = '\0';
        s_saved_join_ecn = aps[index - 1].ecn;
    }
}


static void ui_sync_status_items(void)
{
    aliyun_iot_status_t ali_status;
    aliyun_iot_sensor_status_t sensor_status;
    rt_int32_t temp_int;
    rt_int32_t temp_frac;
    rt_int32_t humi_int;
    rt_int32_t humi_frac;
    rt_tick_t age_tick = 0;
    rt_uint32_t age_ms = 0;
    rt_size_t i;

    aliyun_iot_get_status(&ali_status);
    aliyun_iot_get_sensor_status(&sensor_status);

    if (sensor_status.sample_tick != 0)
    {
        age_tick = rt_tick_get() - sensor_status.sample_tick;
        age_ms = (rt_uint32_t)(((rt_uint64_t)age_tick * 1000U) / RT_TICK_PER_SECOND);
    }

    temp_int = sensor_status.sample.temperature_x10 / 10;
    temp_frac = sensor_status.sample.temperature_x10 % 10;
    if (temp_frac < 0)
    {
        temp_frac = -temp_frac;
    }

    humi_int = sensor_status.sample.humidity_x10 / 10;
    humi_frac = sensor_status.sample.humidity_x10 % 10;
    if (humi_frac < 0)
    {
        humi_frac = -humi_frac;
    }

    if (sensor_status.has_sample)
    {
        rt_snprintf(s_status_lines[0], sizeof(s_status_lines[0]), "TEMP: %d.%dC", temp_int, temp_frac);
        rt_snprintf(s_status_lines[1], sizeof(s_status_lines[1]), "HUMI: %d.%d%%", humi_int, humi_frac);
        rt_snprintf(s_status_lines[3], sizeof(s_status_lines[3]), "AGE:%lums E:%d",
                    (unsigned long)age_ms,
                    sensor_status.last_read_error);
    }
    else
    {
        rt_snprintf(s_status_lines[0], sizeof(s_status_lines[0]), "TEMP: --.-C");
        rt_snprintf(s_status_lines[1], sizeof(s_status_lines[1]), "HUMI: --.-%%");
        rt_snprintf(s_status_lines[3], sizeof(s_status_lines[3]), "WAIT E:%d",
                    sensor_status.last_read_error);
    }

    rt_snprintf(s_status_lines[2], sizeof(s_status_lines[2]), "MQTT: %s/%lu",
                ali_status.mqtt_connected ? "UP" : "DOWN",
                (unsigned long)ali_status.publish_count);

    for (i = 0; i < 4U; i++)
    {
        StatusMenuItems[i].General_item_text = s_status_lines[i];
        StatusMenuItems[i].General_callback = RT_NULL;
        StatusMenuItems[i].General_SubMenuPage = RT_NULL;
        StatusMenuItems[i].List_BoolRadioBox = RT_NULL;
        StatusMenuItems[i].List_IntBox = RT_NULL;
        StatusMenuItems[i].List_FloatBox = RT_NULL;
    }
    StatusMenuItems[4].General_item_text = RT_NULL;
}

static void ui_pc_queue_steps(rt_int16_t *pending, rt_int32_t delta)
{
    rt_int32_t steps;
    rt_int32_t total;

    if ((pending == RT_NULL) || (delta == 0))
    {
        return;
    }

    steps = delta / UI_PC_STEP;
    if (steps == 0)
    {
        steps = (delta > 0) ? 1 : -1;
    }

    total = (rt_int32_t)(*pending) + steps;
    if (total > 120)
    {
        total = 120;
    }
    else if (total < -120)
    {
        total = -120;
    }

    *pending = (rt_int16_t)total;
}

static void ui_pc_service_hid(rt_bool_t usb_ready)
{
    rt_tick_t now;
    rt_tick_t min_gap;
    rt_uint32_t poll_ms;
    rt_uint16_t usage = 0U;

    if (usb_ready == RT_FALSE)
    {
        s_pc_volume_pending = 0;
        s_pc_brightness_pending = 0;
        s_pc_release_pending = 0U;
        return;
    }

    poll_ms = usb_hid_get_polling_interval_ms();
    min_gap = (rt_tick_t)(((rt_uint64_t)poll_ms * RT_TICK_PER_SECOND + 999U) / 1000U);
    if (min_gap == 0U)
    {
        min_gap = 1U;
    }

    now = rt_tick_get();
    if ((s_pc_last_report_tick != 0U) && ((now - s_pc_last_report_tick) < min_gap))
    {
        return;
    }

    if (s_pc_release_pending != 0U)
    {
        if (usb_hid_send_consumer_usage(0U) == RT_EOK)
        {
            s_pc_release_pending = 0U;
            s_pc_last_report_tick = now;
        }
        return;
    }

    if (s_pc_volume_pending > 0)
    {
        usage = HID_USAGE_VOLUME_INC;
        s_pc_volume_pending--;
    }
    else if (s_pc_volume_pending < 0)
    {
        usage = HID_USAGE_VOLUME_DEC;
        s_pc_volume_pending++;
    }
    else if (s_pc_brightness_pending > 0)
    {
        usage = HID_USAGE_BRIGHTNESS_INC;
        s_pc_brightness_pending--;
    }
    else if (s_pc_brightness_pending < 0)
    {
        usage = HID_USAGE_BRIGHTNESS_DEC;
        s_pc_brightness_pending++;
    }

    if ((usage != 0U) && (usb_hid_send_consumer_usage(usage) == RT_EOK))
    {
        s_pc_release_pending = 1U;
        s_pc_last_report_tick = now;
    }
}

static void ui_update_pc_control(void)
{
    rt_bool_t usb_ready = usb_hid_is_ready();

    if (usb_ready != RT_FALSE)
    {
        if (s_pc_volume != s_pc_volume_shadow)
        {
            ui_pc_queue_steps(&s_pc_volume_pending, (rt_int32_t)s_pc_volume - (rt_int32_t)s_pc_volume_shadow);
            s_pc_volume_shadow = s_pc_volume;
        }

        if (s_pc_brightness != s_pc_brightness_shadow)
        {
            ui_pc_queue_steps(&s_pc_brightness_pending, (rt_int32_t)s_pc_brightness - (rt_int32_t)s_pc_brightness_shadow);
            s_pc_brightness_shadow = s_pc_brightness;
        }
    }
    else
    {
        s_pc_volume_shadow = s_pc_volume;
        s_pc_brightness_shadow = s_pc_brightness;
    }

    ui_pc_service_hid(usb_ready);
}

static void ui_sync_pc_items(void)
{
    const char *usb_state;

    if (usb_hid_is_ready() == RT_FALSE)
    {
        usb_state = "USB: WAIT";
    }
    else if ((usb_hid_can_send() == RT_FALSE) ||
             (s_pc_release_pending != 0U) ||
             (s_pc_volume_pending != 0) ||
             (s_pc_brightness_pending != 0))
    {
        usb_state = "USB: SEND";
    }
    else
    {
        usb_state = "USB: READY";
    }

    rt_strncpy(s_pc_status_line, usb_state, sizeof(s_pc_status_line) - 1U);
    s_pc_status_line[sizeof(s_pc_status_line) - 1U] = '\0';

    PCControlMenuItems[0].General_item_text = "VOLUME";
    PCControlMenuItems[0].General_callback = ui_open_pc_volume_window;
    PCControlMenuItems[0].General_SubMenuPage = RT_NULL;
    PCControlMenuItems[0].List_BoolRadioBox = RT_NULL;
    PCControlMenuItems[0].List_IntBox = &s_pc_volume;
    PCControlMenuItems[0].List_FloatBox = RT_NULL;

    PCControlMenuItems[1].General_item_text = "BRIGHT";
    PCControlMenuItems[1].General_callback = ui_open_pc_brightness_window;
    PCControlMenuItems[1].General_SubMenuPage = RT_NULL;
    PCControlMenuItems[1].List_BoolRadioBox = RT_NULL;
    PCControlMenuItems[1].List_IntBox = &s_pc_brightness;
    PCControlMenuItems[1].List_FloatBox = RT_NULL;

    PCControlMenuItems[2].General_item_text = s_pc_status_line;
    PCControlMenuItems[2].General_callback = RT_NULL;
    PCControlMenuItems[2].General_SubMenuPage = RT_NULL;
    PCControlMenuItems[2].List_BoolRadioBox = RT_NULL;
    PCControlMenuItems[2].List_IntBox = RT_NULL;
    PCControlMenuItems[2].List_FloatBox = RT_NULL;

    PCControlMenuItems[3].General_item_text = RT_NULL;
}

MenuItem MainMenuItems[] = {
    { .General_item_text = "TODO", .General_callback = RT_NULL, .General_SubMenuPage = &TodoMenuPage, .Tiles_Icon = Image_window },
    { .General_item_text = "WIFI", .General_callback = RT_NULL, .General_SubMenuPage = &WiFiMenuPage, .Tiles_Icon = Image_wechat },
    { .General_item_text = "PC",   .General_callback = RT_NULL, .General_SubMenuPage = &PCControlMenuPage, .Tiles_Icon = Image_setings },
    { .General_item_text = "STAT", .General_callback = RT_NULL, .General_SubMenuPage = &StatusMenuPage, .Tiles_Icon = Image_window },
    { .General_item_text = "SET",  .General_callback = RT_NULL, .General_SubMenuPage = &SettingsMenuPage, .Tiles_Icon = Image_setings },
    { .General_item_text = RT_NULL },
};

MenuItem TodoMenuItems[TODO_SNAPSHOT_MAX_ITEMS + 1] = {
    { .General_item_text = "[ ] NO TODO" },
    { .General_item_text = RT_NULL },
};

MenuItem WiFiMenuItems[] = {
    { .General_item_text = "SCAN NOW", .General_callback = ui_request_wifi_scan, .General_SubMenuPage = RT_NULL },
    { .General_item_text = "AP LIST",  .General_callback = RT_NULL, .General_SubMenuPage = &WiFiScanMenuPage },
    { .General_item_text = "REJOIN BAI", .General_callback = ui_request_wifi_rejoin, .General_SubMenuPage = RT_NULL },
    { .General_item_text = RT_NULL },
};

MenuItem WiFiScanMenuItems[UI_SCAN_MAX_APS + 2] = {
    { .General_item_text = "RESCAN", .General_callback = ui_handle_wifi_scan_selection, .General_SubMenuPage = RT_NULL },
    { .General_item_text = "NO AP FOUND" },
    { .General_item_text = RT_NULL },
};

MenuItem PCControlMenuItems[4] = {
    { .General_item_text = "VOLUME", .General_callback = ui_open_pc_volume_window, .General_SubMenuPage = RT_NULL, .List_IntBox = &s_pc_volume },
    { .General_item_text = "BRIGHT", .General_callback = ui_open_pc_brightness_window, .General_SubMenuPage = RT_NULL, .List_IntBox = &s_pc_brightness },
    { .General_item_text = "USB: WAIT" },
    { .General_item_text = RT_NULL },
};

MenuItem StatusMenuItems[5] = {
    { .General_item_text = "TEMP: --.-C" },
    { .General_item_text = "HUMI: --.-%" },
    { .General_item_text = "MQTT: DOWN/0" },
    { .General_item_text = "WAIT E:0" },
    { .General_item_text = RT_NULL },
};

MenuItem SettingsMenuItems[] = {
    { .General_item_text = "BRIGHTNESS", .General_callback = ui_open_brightness_window, .General_SubMenuPage = RT_NULL, .List_IntBox = &OLED_UI_Brightness },
    { .General_item_text = RT_NULL },
};

MenuPage MainMenuPage = {
    .General_MenuType = MENU_TYPE_TILES,
    .General_MovingSpeed = 7.2f,
    .General_CursorStyle = REVERSE_ROUNDRECTANGLE,
    .General_MoveStyle = PID_CURVE,
    .General_FontSize = OLED_UI_FONT_12,
    .General_ParentMenuPage = RT_NULL,
    .General_MenuItems = MainMenuItems,
    .General_LineSpace = 14,
    .Tiles_ScreenWidth = OLED_WIDTH,
    .Tiles_ScreenHeight = OLED_HEIGHT,
    .Tiles_TileWidth = 32,
    .Tiles_TileHeight = 32,
};

MenuPage TodoMenuPage = {
    .General_MenuType = MENU_TYPE_LIST,
    .General_MovingSpeed = 7.0f,
    .General_CursorStyle = REVERSE_ROUNDRECTANGLE,
    .General_MoveStyle = PID_CURVE,
    .General_FontSize = OLED_UI_FONT_16,
    .General_ParentMenuPage = &MainMenuPage,
    .General_MenuItems = TodoMenuItems,
    .General_LineSpace = 4,
    .General_ShowAuxiliaryFunction = RT_NULL,
    .List_MenuArea = {0, 0, 128, 64},
    .List_IfDrawFrame = false,
    .List_IfDrawLinePerfix = true,
    .List_StartPointX = 2,
    .List_StartPointY = 2,
};

MenuPage WiFiMenuPage = {
    .General_MenuType = MENU_TYPE_LIST,
    .General_MovingSpeed = 7.0f,
    .General_CursorStyle = REVERSE_ROUNDRECTANGLE,
    .General_MoveStyle = UNLINEAR,
    .General_FontSize = OLED_UI_FONT_16,
    .General_ParentMenuPage = &MainMenuPage,
    .General_MenuItems = WiFiMenuItems,
    .General_LineSpace = 4,
    .General_ShowAuxiliaryFunction = RT_NULL,
    .List_MenuArea = {0, 0, 128, 64},
    .List_IfDrawFrame = false,
    .List_IfDrawLinePerfix = true,
    .List_StartPointX = 2,
    .List_StartPointY = 2,
};

MenuPage WiFiScanMenuPage = {
    .General_MenuType = MENU_TYPE_LIST,
    .General_MovingSpeed = 7.0f,
    .General_CursorStyle = REVERSE_ROUNDRECTANGLE,
    .General_MoveStyle = PID_CURVE,
    .General_FontSize = OLED_UI_FONT_16,
    .General_ParentMenuPage = &WiFiMenuPage,
    .General_MenuItems = WiFiScanMenuItems,
    .General_LineSpace = 4,
    .General_ShowAuxiliaryFunction = RT_NULL,
    .List_MenuArea = {0, 0, 128, 64},
    .List_IfDrawFrame = false,
    .List_IfDrawLinePerfix = true,
    .List_StartPointX = 2,
    .List_StartPointY = 2,
};

MenuPage PCControlMenuPage = {
    .General_MenuType = MENU_TYPE_LIST,
    .General_MovingSpeed = 7.0f,
    .General_CursorStyle = REVERSE_ROUNDRECTANGLE,
    .General_MoveStyle = PID_CURVE,
    .General_FontSize = OLED_UI_FONT_16,
    .General_ParentMenuPage = &MainMenuPage,
    .General_MenuItems = PCControlMenuItems,
    .General_LineSpace = 4,
    .General_ShowAuxiliaryFunction = RT_NULL,
    .List_MenuArea = {0, 0, 128, 64},
    .List_IfDrawFrame = false,
    .List_IfDrawLinePerfix = true,
    .List_StartPointX = 2,
    .List_StartPointY = 2,
};

MenuPage StatusMenuPage = {
    .General_MenuType = MENU_TYPE_LIST,
    .General_MovingSpeed = 6.8f,
    .General_CursorStyle = NOT_SHOW,
    .General_MoveStyle = PID_CURVE,
    .General_FontSize = OLED_UI_FONT_16,
    .General_ParentMenuPage = &MainMenuPage,
    .General_MenuItems = StatusMenuItems,
    .General_LineSpace = 4,
    .General_ShowAuxiliaryFunction = RT_NULL,
    .List_MenuArea = {0, 0, 128, 64},
    .List_IfDrawFrame = false,
    .List_IfDrawLinePerfix = true,
    .List_StartPointX = 2,
    .List_StartPointY = 2,
};

MenuPage SettingsMenuPage = {
    .General_MenuType = MENU_TYPE_LIST,
    .General_MovingSpeed = 7.0f,
    .General_CursorStyle = REVERSE_ROUNDRECTANGLE,
    .General_MoveStyle = PID_CURVE,
    .General_FontSize = OLED_UI_FONT_16,
    .General_ParentMenuPage = &MainMenuPage,
    .General_MenuItems = SettingsMenuItems,
    .General_LineSpace = 4,
    .General_ShowAuxiliaryFunction = RT_NULL,
    .List_MenuArea = {0, 0, 128, 64},
    .List_IfDrawFrame = false,
    .List_IfDrawLinePerfix = true,
    .List_StartPointX = 2,
    .List_StartPointY = 2,
};

void oled_ui_prepare_pages(void)
{
    ui_sync_todo_items();
    ui_sync_scan_items();
    ui_sync_status_items();
    ui_update_pc_control();
    ui_sync_pc_items();

    if (CurrentMenuPage == &TodoMenuPage)
    {
        ui_clamp_page_cursor(&TodoMenuPage, s_todo_display_count);
    }
    else if (CurrentMenuPage == &PCControlMenuPage)
    {
        ui_clamp_page_cursor(&PCControlMenuPage, 3U);
    }
    else if (CurrentMenuPage == &WiFiScanMenuPage)
    {
        ui_clamp_page_cursor(&WiFiScanMenuPage, s_scan_display_count);
        if ((s_last_page != &WiFiScanMenuPage) && (s_wifi_job_busy == 0U))
        {
            ui_start_wifi_job(WIFI_JOB_SCAN, RT_NULL, 0U);
        }
    }
    else if (CurrentMenuPage == &StatusMenuPage)
    {
        ui_clamp_page_cursor(&StatusMenuPage, 4U);
    }

    s_last_page = CurrentMenuPage;
}

