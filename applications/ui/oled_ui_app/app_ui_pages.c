#include "app_ui_pages.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rtthread.h>

#include "app_ui_assets.h"
#include "app_ui_runtime.h"
#include "data/todo_snapshot.h"
#include "cloud/aliyun_iot_mqtt.h"
#include "network/esp01s_at.h"
#include "app/device_service.h"

#define APP_UI_SCAN_MAX_APS          8
#define APP_UI_TODO_LINE_LEN         96
#define APP_UI_SCAN_LINE_LEN         64
#define APP_UI_STATUS_LINE_LEN       64
#define APP_UI_SHORTCUT_LINE_LEN     64
#define APP_UI_USB_STATUS_LEN        32
#define APP_UI_PC_DELTA_STEP         2
#define APP_UI_PC_REPORT_UNIT        2
#define APP_UI_PC_DELTA_LIMIT        24

#define APP_UI_TXT_TODO              "待办"
#define APP_UI_TXT_NETWORK           "网络"
#define APP_UI_TXT_COMPUTER          "电脑"
#define APP_UI_TXT_STATUS            "状态"
#define APP_UI_TXT_SETTINGS          "设置"
#define APP_UI_TXT_SCREEN_BRIGHTNESS "屏幕亮度"
#define APP_UI_TXT_FONT_SIZE         "界面字号"
#define APP_UI_TXT_ANIMATION_SPEED   "界面动画"
#define APP_UI_TXT_TEXT_SCROLL       "滚动速度"
#define APP_UI_TXT_FADE_SPEED        "淡出时长"
#define APP_UI_TXT_SMOOTH_ANIM       "平滑过渡"
#define APP_UI_TXT_FPS               "显示帧率"
#define APP_UI_TXT_VOLUME_ADJUST     "音量调节"
#define APP_UI_TXT_BRIGHTNESS_ADJUST "亮度调节"
#define APP_UI_TXT_KEY_CONFIG        "快捷键设置"
#define APP_UI_TXT_USB_STATUS        "USB连接状态"
#define APP_UI_TXT_EMPTY_TODO        "暂无待办"
#define APP_UI_TXT_KEY_PREFIX        "按键"
#define APP_UI_TXT_WIFI_INFO         "当前网络信息"
#define APP_UI_TXT_WIFI_LIST         "无线网络列表"
#define APP_UI_TXT_WIFI_REJOIN       "重连预设网络"
#define APP_UI_TXT_WIFI_STATUS       "连接状态"
#define APP_UI_TXT_WIFI_NAME         "网络名称"
#define APP_UI_TXT_WIFI_IP           "IP地址"
#define APP_UI_TXT_WIFI_RSSI         "信号强度"
#define APP_UI_TXT_WIFI_RESCAN       "重新扫描"
#define APP_UI_TXT_WIFI_EMPTY        "未发现网络"
#define APP_UI_TXT_WIFI_HIDDEN       "隐藏网络"

extern MenuPage *CurrentMenuPage;
extern MenuWindow *CurrentWindow;
extern int16_t OLED_UI_Brightness;
extern bool OLED_UI_ShowFps;

static char s_todo_lines[TODO_SNAPSHOT_MAX_ITEMS][APP_UI_TODO_LINE_LEN];
static char s_wifi_info_lines[4][APP_UI_STATUS_LINE_LEN];
static char s_scan_lines[APP_UI_SCAN_MAX_APS][APP_UI_SCAN_LINE_LEN];
static char s_status_lines[4][APP_UI_STATUS_LINE_LEN];
static char s_shortcut_lines[DEVICE_USB_SHORTCUT_SLOT_COUNT][APP_UI_SHORTCUT_LINE_LEN];
static char s_shortcut_preset_lines[DEVICE_USB_SHORTCUT_PRESET_COUNT][APP_UI_SHORTCUT_LINE_LEN];
static char s_usb_state_line[APP_UI_USB_STATUS_LEN];
static rt_uint32_t s_visible_todo_count = 0U;
static rt_size_t s_visible_scan_count = 0U;
static MenuPage *s_previous_page = RT_NULL;
static bool s_smooth_animation_enabled = true;
static rt_int16_t s_pc_volume_delta = 0;
static rt_int16_t s_pc_brightness_delta = 0;
static rt_int16_t s_pc_volume_shadow = 0;
static rt_int16_t s_pc_brightness_shadow = 0;

static void app_ui_request_scan(void);
static void app_ui_request_rejoin(void);
static void app_ui_toggle_todo_item(void);
static void app_ui_handle_scan_selection(void);
static void app_ui_apply_shortcut_preset_selection(void);
static void app_ui_open_screen_brightness_window(void);
static void app_ui_open_font_size_window(void);
static void app_ui_open_page_speed_window(void);
static void app_ui_open_text_speed_window(void);
static void app_ui_open_fade_step_window(void);
static void app_ui_open_pc_volume_window(void);
static void app_ui_open_pc_brightness_window(void);
static void app_ui_refresh_wifi_info_items(void);
static void app_ui_refresh_shortcut_items(void);
static void app_ui_refresh_shortcut_preset_items(void);
static void app_ui_refresh_pc_items(void);
static void app_ui_refresh_status_items(void);
static void app_ui_refresh_scan_items(void);
static void app_ui_refresh_todo_items(void);
static void app_ui_update_pc_delta(void);

static void app_ui_copy_visible_text(char *dst, rt_size_t dst_len, const char *src)
{
    rt_size_t used = 0;

    if ((dst == RT_NULL) || (dst_len == 0U))
    {
        return;
    }

    dst[0] = '\0';
    if (src == RT_NULL)
    {
        return;
    }

    while ((*src != '\0') && (used + 1U < dst_len))
    {
        unsigned char ch = (unsigned char)(*src);
        rt_size_t copy_len = 1U;

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

            if ((used + copy_len) >= dst_len)
            {
                break;
            }

            while (copy_len-- > 0U)
            {
                dst[used++] = *src++;
            }
            continue;
        }

        src++;
    }

    dst[used] = '\0';
}

static void app_ui_clamp_cursor(MenuPage *page, rt_uint32_t count)
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

static const char *app_ui_get_wifi_state_text(const esp01s_status_t *status)
{
    if (status == RT_NULL)
    {
        return "状态未知";
    }

    if (status->ready == RT_FALSE)
    {
        return "模块未就绪";
    }

    if (status->scan_busy != RT_FALSE)
    {
        return "正在扫描";
    }

    if (status->connected != RT_FALSE)
    {
        return "已连接";
    }

    if (status->last_error != RT_EOK)
    {
        return "连接异常";
    }

    return "未连接";
}

static device_usb_shortcut_slot_t app_ui_get_shortcut_slot_from_page(MenuPage *page)
{
    if (page == &ShortcutPresetPage1)
    {
        return DEVICE_USB_SHORTCUT_SLOT_1;
    }
    if (page == &ShortcutPresetPage2)
    {
        return DEVICE_USB_SHORTCUT_SLOT_2;
    }
    if (page == &ShortcutPresetPage3)
    {
        return DEVICE_USB_SHORTCUT_SLOT_3;
    }

    return DEVICE_USB_SHORTCUT_SLOT_4;
}

static void app_ui_open_screen_brightness_window(void)
{
    static MenuWindow window = {
        .General_Width = 96,
        .General_Height = 28,
        .General_ContinueTime = 8.0f,
        .General_WindowType = WINDOW_ROUNDRECTANGLE,
        .Text_String = APP_UI_TXT_SCREEN_BRIGHTNESS,
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
        .Prob_ShowBar = RT_TRUE,
    };

    oled_ui_create_window(&window);
}

static void app_ui_open_font_size_window(void)
{
    static MenuWindow window = {
        .General_Width = 96,
        .General_Height = 28,
        .General_ContinueTime = 8.0f,
        .General_WindowType = WINDOW_ROUNDRECTANGLE,
        .Text_String = APP_UI_TXT_FONT_SIZE,
        .Text_FontSize = OLED_UI_FONT_12,
        .Text_FontSideDistance = 4,
        .Text_FontTopDistance = 3,
        .Prob_Data_Int = &AppUI_FontSize,
        .Prob_DataStep = 4,
        .Prob_MinData = OLED_UI_FONT_8,
        .Prob_MaxData = OLED_UI_FONT_16,
        .Prob_BottomDistance = 3,
        .Prob_SideDistance = 4,
        .Prob_LineHeight = 8,
        .Prob_ShowBar = RT_TRUE,
    };

    oled_ui_create_window(&window);
}

static void app_ui_open_page_speed_window(void)
{
    static MenuWindow window = {
        .General_Width = 96,
        .General_Height = 28,
        .General_ContinueTime = 8.0f,
        .General_WindowType = WINDOW_ROUNDRECTANGLE,
        .Text_String = APP_UI_TXT_ANIMATION_SPEED,
        .Text_FontSize = OLED_UI_FONT_12,
        .Text_FontSideDistance = 4,
        .Text_FontTopDistance = 3,
        .Prob_Data_Float = &AppUI_PageMotionSpeed,
        .Prob_DataStep = 0.5f,
        .Prob_MinData = 1.0f,
        .Prob_MaxData = 12.0f,
        .Prob_BottomDistance = 3,
        .Prob_SideDistance = 4,
        .Prob_LineHeight = 8,
        .Prob_ShowBar = RT_TRUE,
    };

    oled_ui_create_window(&window);
}

static void app_ui_open_text_speed_window(void)
{
    static MenuWindow window = {
        .General_Width = 96,
        .General_Height = 28,
        .General_ContinueTime = 8.0f,
        .General_WindowType = WINDOW_ROUNDRECTANGLE,
        .Text_String = APP_UI_TXT_TEXT_SCROLL,
        .Text_FontSize = OLED_UI_FONT_12,
        .Text_FontSideDistance = 4,
        .Text_FontTopDistance = 3,
        .Prob_Data_Float = &AppUI_TextScrollSpeed,
        .Prob_DataStep = 0.05f,
        .Prob_MinData = 0.05f,
        .Prob_MaxData = 1.00f,
        .Prob_BottomDistance = 3,
        .Prob_SideDistance = 4,
        .Prob_LineHeight = 8,
        .Prob_ShowBar = RT_TRUE,
    };

    oled_ui_create_window(&window);
}

static void app_ui_open_fade_step_window(void)
{
    static MenuWindow window = {
        .General_Width = 96,
        .General_Height = 28,
        .General_ContinueTime = 8.0f,
        .General_WindowType = WINDOW_ROUNDRECTANGLE,
        .Text_String = APP_UI_TXT_FADE_SPEED,
        .Text_FontSize = OLED_UI_FONT_12,
        .Text_FontSideDistance = 4,
        .Text_FontTopDistance = 3,
        .Prob_Data_Int = &AppUI_FadeStepMs,
        .Prob_DataStep = 5,
        .Prob_MinData = 10,
        .Prob_MaxData = 120,
        .Prob_BottomDistance = 3,
        .Prob_SideDistance = 4,
        .Prob_LineHeight = 8,
        .Prob_ShowBar = RT_TRUE,
    };

    oled_ui_create_window(&window);
}

static void app_ui_open_pc_volume_window(void)
{
    static MenuWindow window = {
        .General_Width = 96,
        .General_Height = 28,
        .General_ContinueTime = 2.0f,
        .General_WindowType = WINDOW_ROUNDRECTANGLE,
        .Text_String = APP_UI_TXT_VOLUME_ADJUST,
        .Text_FontSize = OLED_UI_FONT_12,
        .Text_FontSideDistance = 4,
        .Text_FontTopDistance = 3,
        .Prob_Data_Int = &s_pc_volume_delta,
        .Prob_DataStep = APP_UI_PC_DELTA_STEP,
        .Prob_MinData = -APP_UI_PC_DELTA_LIMIT,
        .Prob_MaxData = APP_UI_PC_DELTA_LIMIT,
        .Prob_BottomDistance = 3,
        .Prob_SideDistance = 4,
        .Prob_LineHeight = 8,
        .Prob_ShowBar = RT_FALSE,
    };

    s_pc_volume_delta = 0;
    s_pc_volume_shadow = 0;
    oled_ui_create_window(&window);
}

static void app_ui_open_pc_brightness_window(void)
{
    static MenuWindow window = {
        .General_Width = 96,
        .General_Height = 28,
        .General_ContinueTime = 2.0f,
        .General_WindowType = WINDOW_ROUNDRECTANGLE,
        .Text_String = APP_UI_TXT_BRIGHTNESS_ADJUST,
        .Text_FontSize = OLED_UI_FONT_12,
        .Text_FontSideDistance = 4,
        .Text_FontTopDistance = 3,
        .Prob_Data_Int = &s_pc_brightness_delta,
        .Prob_DataStep = APP_UI_PC_DELTA_STEP,
        .Prob_MinData = -APP_UI_PC_DELTA_LIMIT,
        .Prob_MaxData = APP_UI_PC_DELTA_LIMIT,
        .Prob_BottomDistance = 3,
        .Prob_SideDistance = 4,
        .Prob_LineHeight = 8,
        .Prob_ShowBar = RT_FALSE,
    };

    s_pc_brightness_delta = 0;
    s_pc_brightness_shadow = 0;
    oled_ui_create_window(&window);
}

static void app_ui_request_scan(void)
{
    (void)device_net_service_request_scan();
}

static void app_ui_request_rejoin(void)
{
    (void)device_net_service_request_rejoin();
}

static void app_ui_refresh_todo_items(void)
{
    todo_snapshot_t snapshot;
    rt_uint32_t index;

    todo_snapshot_get(&snapshot);
    s_visible_todo_count = snapshot.count;
    if (s_visible_todo_count > TODO_SNAPSHOT_MAX_ITEMS)
    {
        s_visible_todo_count = TODO_SNAPSHOT_MAX_ITEMS;
    }

    if (s_visible_todo_count == 0U)
    {
        rt_strncpy(s_todo_lines[0], "[ ] " APP_UI_TXT_EMPTY_TODO, sizeof(s_todo_lines[0]) - 1U);
        s_todo_lines[0][sizeof(s_todo_lines[0]) - 1U] = '\0';
        TodoMenuItems[0].General_item_text = s_todo_lines[0];
        TodoMenuItems[0].General_callback = RT_NULL;
        TodoMenuItems[0].General_SubMenuPage = RT_NULL;
        TodoMenuItems[0].List_BoolRadioBox = RT_NULL;
        TodoMenuItems[0].List_IntBox = RT_NULL;
        TodoMenuItems[0].List_FloatBox = RT_NULL;
        TodoMenuItems[1].General_item_text = RT_NULL;
        s_visible_todo_count = 1U;
        return;
    }

    for (index = 0; index < s_visible_todo_count; index++)
    {
        char visible_title[APP_UI_TODO_LINE_LEN - 6];
        const char *src = snapshot.items[index].title[0] ? snapshot.items[index].title : snapshot.items[index].id;

        app_ui_copy_visible_text(visible_title, sizeof(visible_title), src);
        if (visible_title[0] == '\0')
        {
            rt_snprintf(visible_title, sizeof(visible_title), "待办%u", (unsigned int)(index + 1U));
        }

        rt_snprintf(s_todo_lines[index],
                    sizeof(s_todo_lines[index]),
                    "%s %s",
                    snapshot.items[index].completed ? "[x]" : "[ ]",
                    visible_title);
        TodoMenuItems[index].General_item_text = s_todo_lines[index];
        TodoMenuItems[index].General_callback = app_ui_toggle_todo_item;
        TodoMenuItems[index].General_SubMenuPage = RT_NULL;
        TodoMenuItems[index].List_BoolRadioBox = RT_NULL;
        TodoMenuItems[index].List_IntBox = RT_NULL;
        TodoMenuItems[index].List_FloatBox = RT_NULL;
    }

    TodoMenuItems[s_visible_todo_count].General_item_text = RT_NULL;
}

static void app_ui_toggle_todo_item(void)
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

    app_ui_refresh_todo_items();
    app_ui_clamp_cursor(&TodoMenuPage, s_visible_todo_count);
    (void)device_net_service_queue_todo_report(&change);
}

static void app_ui_refresh_wifi_info_items(void)
{
    esp01s_status_t wifi_status;
    char ssid[33];
    const char *ip_text = "--";

    esp01s_get_status(&wifi_status);

    app_ui_copy_visible_text(ssid, sizeof(ssid), wifi_status.ssid);
    if (ssid[0] == '\0')
    {
        rt_strncpy(ssid, "未连接", sizeof(ssid) - 1U);
        ssid[sizeof(ssid) - 1U] = '\0';
    }

    if (wifi_status.ip[0] != '\0')
    {
        ip_text = wifi_status.ip;
    }

    rt_snprintf(s_wifi_info_lines[0],
                sizeof(s_wifi_info_lines[0]),
                APP_UI_TXT_WIFI_STATUS ": %s",
                app_ui_get_wifi_state_text(&wifi_status));
    rt_snprintf(s_wifi_info_lines[1],
                sizeof(s_wifi_info_lines[1]),
                APP_UI_TXT_WIFI_NAME ": %s",
                ssid);
    rt_snprintf(s_wifi_info_lines[2],
                sizeof(s_wifi_info_lines[2]),
                APP_UI_TXT_WIFI_IP ": %s",
                ip_text);

    if ((wifi_status.connected != RT_FALSE) || (wifi_status.rssi != 0))
    {
        rt_snprintf(s_wifi_info_lines[3],
                    sizeof(s_wifi_info_lines[3]),
                    APP_UI_TXT_WIFI_RSSI ": %ddBm",
                    wifi_status.rssi);
    }
    else
    {
        rt_snprintf(s_wifi_info_lines[3],
                    sizeof(s_wifi_info_lines[3]),
                    APP_UI_TXT_WIFI_RSSI ": --");
    }

    WiFiInfoMenuItems[0].General_item_text = s_wifi_info_lines[0];
    WiFiInfoMenuItems[1].General_item_text = s_wifi_info_lines[1];
    WiFiInfoMenuItems[2].General_item_text = s_wifi_info_lines[2];
    WiFiInfoMenuItems[3].General_item_text = s_wifi_info_lines[3];
}

static void app_ui_refresh_scan_items(void)
{
    esp01s_ap_info_t ap_list[APP_UI_SCAN_MAX_APS];
    rt_size_t count;
    rt_size_t index;

    WiFiScanMenuItems[0].General_item_text = APP_UI_TXT_WIFI_RESCAN;
    WiFiScanMenuItems[0].General_callback = app_ui_handle_scan_selection;
    WiFiScanMenuItems[0].General_SubMenuPage = RT_NULL;
    WiFiScanMenuItems[0].List_BoolRadioBox = RT_NULL;
    WiFiScanMenuItems[0].List_IntBox = RT_NULL;
    WiFiScanMenuItems[0].List_FloatBox = RT_NULL;

    count = esp01s_get_scan_list(ap_list, APP_UI_SCAN_MAX_APS);
    if (count > APP_UI_SCAN_MAX_APS)
    {
        count = APP_UI_SCAN_MAX_APS;
    }

    if (count == 0U)
    {
        rt_strncpy(s_scan_lines[0], APP_UI_TXT_WIFI_EMPTY, sizeof(s_scan_lines[0]) - 1U);
        s_scan_lines[0][sizeof(s_scan_lines[0]) - 1U] = '\0';
        WiFiScanMenuItems[1].General_item_text = s_scan_lines[0];
        WiFiScanMenuItems[1].General_callback = RT_NULL;
        WiFiScanMenuItems[1].General_SubMenuPage = RT_NULL;
        WiFiScanMenuItems[1].List_BoolRadioBox = RT_NULL;
        WiFiScanMenuItems[1].List_IntBox = RT_NULL;
        WiFiScanMenuItems[1].List_FloatBox = RT_NULL;
        WiFiScanMenuItems[2].General_item_text = RT_NULL;
        s_visible_scan_count = 2U;
        return;
    }

    for (index = 0; index < count; index++)
    {
        char ssid[24];

        app_ui_copy_visible_text(ssid, sizeof(ssid), ap_list[index].ssid);
        if (ssid[0] == '\0')
        {
            rt_strncpy(ssid, APP_UI_TXT_WIFI_HIDDEN, sizeof(ssid) - 1U);
            ssid[sizeof(ssid) - 1U] = '\0';
        }

        rt_snprintf(s_scan_lines[index], sizeof(s_scan_lines[index]), "%s %ddBm", ssid, ap_list[index].rssi);
        WiFiScanMenuItems[index + 1U].General_item_text = s_scan_lines[index];
        WiFiScanMenuItems[index + 1U].General_callback = app_ui_handle_scan_selection;
        WiFiScanMenuItems[index + 1U].General_SubMenuPage = RT_NULL;
        WiFiScanMenuItems[index + 1U].List_BoolRadioBox = RT_NULL;
        WiFiScanMenuItems[index + 1U].List_IntBox = RT_NULL;
        WiFiScanMenuItems[index + 1U].List_FloatBox = RT_NULL;
    }

    WiFiScanMenuItems[count + 1U].General_item_text = RT_NULL;
    s_visible_scan_count = count + 1U;
}

static void app_ui_handle_scan_selection(void)
{
    esp01s_ap_info_t ap_list[APP_UI_SCAN_MAX_APS];
    rt_size_t count = esp01s_get_scan_list(ap_list, APP_UI_SCAN_MAX_APS);
    MenuID current_index = (CurrentMenuPage != RT_NULL) ? CurrentMenuPage->_ActiveMenuID : 0;

    if (current_index <= 0)
    {
        app_ui_request_scan();
        return;
    }

    if (((rt_size_t)(current_index - 1)) < count)
    {
        (void)device_net_service_request_connect(ap_list[current_index - 1].ssid, ap_list[current_index - 1].ecn);
    }
}

static void app_ui_refresh_status_items(void)
{
    aliyun_iot_status_t cloud_status;
    aliyun_iot_sensor_status_t sensor_status;
    rt_int32_t temp_int;
    rt_int32_t temp_frac;
    rt_int32_t humi_int;
    rt_int32_t humi_frac;
    rt_uint32_t age_ms = 0U;
    rt_tick_t age_tick = 0U;
    rt_size_t index;

    aliyun_iot_get_status(&cloud_status);
    aliyun_iot_get_sensor_status(&sensor_status);

    if (sensor_status.sample_tick != 0U)
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
        rt_snprintf(s_status_lines[0], sizeof(s_status_lines[0]), "温度数据: %d.%dC", temp_int, temp_frac);
        rt_snprintf(s_status_lines[1], sizeof(s_status_lines[1]), "湿度数据: %d.%d%%", humi_int, humi_frac);
        rt_snprintf(s_status_lines[3],
                    sizeof(s_status_lines[3]),
                    "数据延迟: %lums 错误:%d",
                    (unsigned long)age_ms,
                    sensor_status.last_read_error);
    }
    else
    {
        rt_strncpy(s_status_lines[0], "温度数据: --.-C", sizeof(s_status_lines[0]) - 1U);
        s_status_lines[0][sizeof(s_status_lines[0]) - 1U] = '\0';
        rt_strncpy(s_status_lines[1], "湿度数据: --.-%", sizeof(s_status_lines[1]) - 1U);
        s_status_lines[1][sizeof(s_status_lines[1]) - 1U] = '\0';
        rt_snprintf(s_status_lines[3],
                    sizeof(s_status_lines[3]),
                    "数据延迟: 等待 错误:%d",
                    sensor_status.last_read_error);
    }

    rt_snprintf(s_status_lines[2],
                sizeof(s_status_lines[2]),
                "云端连接: %s",
                cloud_status.mqtt_connected ? "已连接" : "未连接");

    for (index = 0; index < 4U; index++)
    {
        StatusMenuItems[index].General_item_text = s_status_lines[index];
    }
}

static void app_ui_refresh_shortcut_preset_items(void)
{
    rt_uint32_t preset;

    for (preset = 0; preset < DEVICE_USB_SHORTCUT_PRESET_COUNT; preset++)
    {
        const char *preset_name = device_usb_service_get_shortcut_preset_label((device_usb_shortcut_preset_t)preset);

        if (preset_name == RT_NULL)
        {
            preset_name = "无操作";
        }

        rt_snprintf(s_shortcut_preset_lines[preset],
                    sizeof(s_shortcut_preset_lines[preset]),
                    "%s",
                    preset_name);
        ShortcutPresetMenuItems[preset].General_item_text = s_shortcut_preset_lines[preset];
    }
}

static void app_ui_refresh_shortcut_items(void)
{
    rt_uint32_t slot;

    for (slot = 0; slot < DEVICE_USB_SHORTCUT_SLOT_COUNT; slot++)
    {
        const char *preset_name = device_usb_service_get_shortcut_preset_label(
            device_usb_service_get_shortcut_preset((device_usb_shortcut_slot_t)slot));

        if (preset_name == RT_NULL)
        {
            preset_name = "无操作";
        }

        rt_snprintf(s_shortcut_lines[slot],
                    sizeof(s_shortcut_lines[slot]),
                    APP_UI_TXT_KEY_PREFIX "%u: %s",
                    (unsigned int)(slot + 1U),
                    preset_name);
        PCShortcutMenuItems[slot].General_item_text = s_shortcut_lines[slot];
    }
}

static void app_ui_refresh_pc_items(void)
{
    const char *usb_state = "USB: 未启动";
    device_usb_service_status_t usb_status;

    device_usb_service_get_status(&usb_status);

    if (usb_status.ready == RT_FALSE)
    {
        usb_state = (usb_status.stack_started == RT_FALSE) ? "USB: 未启动" : "USB: 枚举中";
    }
    else if ((usb_status.can_send == RT_FALSE) || (usb_status.queued_steps != 0U))
    {
        usb_state = "USB: 发送中";
    }
    else
    {
        usb_state = "USB: 已就绪";
    }

    rt_strncpy(s_usb_state_line, usb_state, sizeof(s_usb_state_line) - 1U);
    s_usb_state_line[sizeof(s_usb_state_line) - 1U] = '\0';
    PCControlMenuItems[4].General_item_text = s_usb_state_line;
}

static void app_ui_update_pc_delta(void)
{
    rt_int16_t volume_delta = (rt_int16_t)(s_pc_volume_delta - s_pc_volume_shadow);
    rt_int16_t brightness_delta = (rt_int16_t)(s_pc_brightness_delta - s_pc_brightness_shadow);
    rt_int16_t volume_steps = (rt_int16_t)(volume_delta / APP_UI_PC_REPORT_UNIT);
    rt_int16_t brightness_steps = (rt_int16_t)(brightness_delta / APP_UI_PC_REPORT_UNIT);

    if ((volume_steps != 0) || (brightness_steps != 0))
    {
        (void)device_usb_service_adjust(volume_steps, brightness_steps);
        s_pc_volume_shadow = s_pc_volume_delta;
        s_pc_brightness_shadow = s_pc_brightness_delta;
    }

    if ((CurrentWindow == RT_NULL) && ((s_pc_volume_delta != 0) || (s_pc_brightness_delta != 0)))
    {
        s_pc_volume_delta = 0;
        s_pc_brightness_delta = 0;
        s_pc_volume_shadow = 0;
        s_pc_brightness_shadow = 0;
    }
}

static void app_ui_apply_shortcut_preset_selection(void)
{
    device_usb_shortcut_slot_t slot;
    device_usb_shortcut_preset_t preset;

    if (CurrentMenuPage == RT_NULL)
    {
        return;
    }

    slot = app_ui_get_shortcut_slot_from_page(CurrentMenuPage);
    preset = (device_usb_shortcut_preset_t)CurrentMenuPage->_ActiveMenuID;
    (void)device_usb_service_set_shortcut_preset(slot, preset);
    app_ui_refresh_shortcut_items();
}

MenuItem MainMenuItems[] = {
    { .General_item_text = APP_UI_TXT_TODO, .General_callback = RT_NULL, .General_SubMenuPage = &TodoMenuPage, .Tiles_Icon = AppUI_IconList },
    { .General_item_text = APP_UI_TXT_NETWORK, .General_callback = RT_NULL, .General_SubMenuPage = &WiFiMenuPage, .Tiles_Icon = AppUI_IconNetwork },
    { .General_item_text = APP_UI_TXT_COMPUTER, .General_callback = RT_NULL, .General_SubMenuPage = &PCControlMenuPage, .Tiles_Icon = AppUI_IconComputer },
    { .General_item_text = APP_UI_TXT_STATUS, .General_callback = RT_NULL, .General_SubMenuPage = &StatusMenuPage, .Tiles_Icon = AppUI_IconStatus },
    { .General_item_text = APP_UI_TXT_SETTINGS, .General_callback = RT_NULL, .General_SubMenuPage = &SettingsMenuPage, .Tiles_Icon = AppUI_IconSettings },
    { .General_item_text = RT_NULL },
};

MenuItem TodoMenuItems[TODO_SNAPSHOT_MAX_ITEMS + 1] = {
    { .General_item_text = "[ ] " APP_UI_TXT_EMPTY_TODO },
    { .General_item_text = RT_NULL },
};

MenuItem WiFiMenuItems[] = {
    { .General_item_text = APP_UI_TXT_WIFI_INFO, .General_callback = RT_NULL, .General_SubMenuPage = &WiFiInfoMenuPage },
    { .General_item_text = APP_UI_TXT_WIFI_LIST, .General_callback = RT_NULL, .General_SubMenuPage = &WiFiScanMenuPage },
    { .General_item_text = APP_UI_TXT_WIFI_REJOIN, .General_callback = app_ui_request_rejoin, .General_SubMenuPage = RT_NULL },
    { .General_item_text = RT_NULL },
};

MenuItem WiFiInfoMenuItems[] = {
    { .General_item_text = "连接状态: 状态未知" },
    { .General_item_text = "网络名称: 未连接" },
    { .General_item_text = "IP地址: --" },
    { .General_item_text = "信号强度: --" },
    { .General_item_text = RT_NULL },
};

MenuItem WiFiScanMenuItems[APP_UI_SCAN_MAX_APS + 2] = {
    { .General_item_text = APP_UI_TXT_WIFI_RESCAN, .General_callback = app_ui_handle_scan_selection, .General_SubMenuPage = RT_NULL },
    { .General_item_text = APP_UI_TXT_WIFI_EMPTY },
    { .General_item_text = RT_NULL },
};

MenuItem PCControlMenuItems[7] = {
    { .General_item_text = APP_UI_TXT_VOLUME_ADJUST, .General_callback = app_ui_open_pc_volume_window, .General_SubMenuPage = RT_NULL },
    { .General_item_text = APP_UI_TXT_BRIGHTNESS_ADJUST, .General_callback = app_ui_open_pc_brightness_window, .General_SubMenuPage = RT_NULL },
    { .General_item_text = APP_UI_TXT_KEY_CONFIG, .General_callback = RT_NULL, .General_SubMenuPage = &PCShortcutMenuPage },
    { .General_item_text = APP_UI_TXT_USB_STATUS },
    { .General_item_text = "USB: 未启动" },
    { .General_item_text = "相对调节, 未同步" },
    { .General_item_text = RT_NULL },
};

MenuItem PCShortcutMenuItems[DEVICE_USB_SHORTCUT_SLOT_COUNT + 1] = {
    { .General_item_text = "按键1: 显示桌面", .General_callback = RT_NULL, .General_SubMenuPage = &ShortcutPresetPage1 },
    { .General_item_text = "按键2: 文件管理", .General_callback = RT_NULL, .General_SubMenuPage = &ShortcutPresetPage2 },
    { .General_item_text = "按键3: 任务切换", .General_callback = RT_NULL, .General_SubMenuPage = &ShortcutPresetPage3 },
    { .General_item_text = "按键4: 任务管理", .General_callback = RT_NULL, .General_SubMenuPage = &ShortcutPresetPage4 },
    { .General_item_text = RT_NULL },
};

MenuItem ShortcutPresetMenuItems[DEVICE_USB_SHORTCUT_PRESET_COUNT + 1] = {
    { .General_item_text = "无操作", .General_callback = app_ui_apply_shortcut_preset_selection, .General_SubMenuPage = RT_NULL },
    { .General_item_text = "显示桌面", .General_callback = app_ui_apply_shortcut_preset_selection, .General_SubMenuPage = RT_NULL },
    { .General_item_text = "文件管理", .General_callback = app_ui_apply_shortcut_preset_selection, .General_SubMenuPage = RT_NULL },
    { .General_item_text = "任务切换", .General_callback = app_ui_apply_shortcut_preset_selection, .General_SubMenuPage = RT_NULL },
    { .General_item_text = "任务管理", .General_callback = app_ui_apply_shortcut_preset_selection, .General_SubMenuPage = RT_NULL },
    { .General_item_text = "锁定电脑", .General_callback = app_ui_apply_shortcut_preset_selection, .General_SubMenuPage = RT_NULL },
    { .General_item_text = "截图工具", .General_callback = app_ui_apply_shortcut_preset_selection, .General_SubMenuPage = RT_NULL },
    { .General_item_text = "系统设置", .General_callback = app_ui_apply_shortcut_preset_selection, .General_SubMenuPage = RT_NULL },
    { .General_item_text = "运行窗口", .General_callback = app_ui_apply_shortcut_preset_selection, .General_SubMenuPage = RT_NULL },
    { .General_item_text = RT_NULL },
};

MenuItem StatusMenuItems[5] = {
    { .General_item_text = "温度数据: --.-C" },
    { .General_item_text = "湿度数据: --.-%" },
    { .General_item_text = "云端连接: 未连接" },
    { .General_item_text = "数据延迟: 等待 错误:0" },
    { .General_item_text = RT_NULL },
};

MenuItem SettingsMenuItems[] = {
    { .General_item_text = APP_UI_TXT_SCREEN_BRIGHTNESS, .General_callback = app_ui_open_screen_brightness_window, .General_SubMenuPage = RT_NULL, .List_IntBox = &OLED_UI_Brightness },
    { .General_item_text = APP_UI_TXT_FONT_SIZE, .General_callback = app_ui_open_font_size_window, .General_SubMenuPage = RT_NULL, .List_IntBox = &AppUI_FontSize },
    { .General_item_text = APP_UI_TXT_ANIMATION_SPEED, .General_callback = app_ui_open_page_speed_window, .General_SubMenuPage = RT_NULL, .List_FloatBox = &AppUI_PageMotionSpeed },
    { .General_item_text = APP_UI_TXT_TEXT_SCROLL, .General_callback = app_ui_open_text_speed_window, .General_SubMenuPage = RT_NULL, .List_FloatBox = &AppUI_TextScrollSpeed },
    { .General_item_text = APP_UI_TXT_FADE_SPEED, .General_callback = app_ui_open_fade_step_window, .General_SubMenuPage = RT_NULL, .List_IntBox = &AppUI_FadeStepMs },
    { .General_item_text = APP_UI_TXT_SMOOTH_ANIM, .General_callback = RT_NULL, .General_SubMenuPage = RT_NULL, .List_BoolRadioBox = &s_smooth_animation_enabled },
    { .General_item_text = APP_UI_TXT_FPS, .General_callback = RT_NULL, .General_SubMenuPage = RT_NULL, .List_BoolRadioBox = &OLED_UI_ShowFps },
    { .General_item_text = RT_NULL },
};

MenuPage MainMenuPage = {
    .General_MenuType = MENU_TYPE_TILES,
    .General_MovingSpeed = 7.0f,
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
    .General_FontSize = OLED_UI_FONT_12,
    .General_ParentMenuPage = &MainMenuPage,
    .General_MenuItems = TodoMenuItems,
    .General_LineSpace = 3,
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
    .General_MoveStyle = PID_CURVE,
    .General_FontSize = OLED_UI_FONT_12,
    .General_ParentMenuPage = &MainMenuPage,
    .General_MenuItems = WiFiMenuItems,
    .General_LineSpace = 3,
    .List_MenuArea = {0, 0, 128, 64},
    .List_IfDrawFrame = false,
    .List_IfDrawLinePerfix = true,
    .List_StartPointX = 2,
    .List_StartPointY = 2,
};

MenuPage WiFiInfoMenuPage = {
    .General_MenuType = MENU_TYPE_LIST,
    .General_MovingSpeed = 7.0f,
    .General_CursorStyle = NOT_SHOW,
    .General_MoveStyle = PID_CURVE,
    .General_FontSize = OLED_UI_FONT_12,
    .General_ParentMenuPage = &WiFiMenuPage,
    .General_MenuItems = WiFiInfoMenuItems,
    .General_LineSpace = 3,
    .List_MenuArea = {0, 0, 128, 64},
    .List_IfDrawFrame = false,
    .List_IfDrawLinePerfix = false,
    .List_StartPointX = 2,
    .List_StartPointY = 2,
};

MenuPage WiFiScanMenuPage = {
    .General_MenuType = MENU_TYPE_LIST,
    .General_MovingSpeed = 7.0f,
    .General_CursorStyle = REVERSE_ROUNDRECTANGLE,
    .General_MoveStyle = PID_CURVE,
    .General_FontSize = OLED_UI_FONT_12,
    .General_ParentMenuPage = &WiFiMenuPage,
    .General_MenuItems = WiFiScanMenuItems,
    .General_LineSpace = 3,
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
    .General_FontSize = OLED_UI_FONT_12,
    .General_ParentMenuPage = &MainMenuPage,
    .General_MenuItems = PCControlMenuItems,
    .General_LineSpace = 3,
    .List_MenuArea = {0, 0, 128, 64},
    .List_IfDrawFrame = false,
    .List_IfDrawLinePerfix = true,
    .List_StartPointX = 2,
    .List_StartPointY = 2,
};

MenuPage PCShortcutMenuPage = {
    .General_MenuType = MENU_TYPE_LIST,
    .General_MovingSpeed = 7.0f,
    .General_CursorStyle = REVERSE_ROUNDRECTANGLE,
    .General_MoveStyle = PID_CURVE,
    .General_FontSize = OLED_UI_FONT_12,
    .General_ParentMenuPage = &PCControlMenuPage,
    .General_MenuItems = PCShortcutMenuItems,
    .General_LineSpace = 3,
    .List_MenuArea = {0, 0, 128, 64},
    .List_IfDrawFrame = false,
    .List_IfDrawLinePerfix = true,
    .List_StartPointX = 2,
    .List_StartPointY = 2,
};

MenuPage ShortcutPresetPage1 = {
    .General_MenuType = MENU_TYPE_LIST,
    .General_MovingSpeed = 7.0f,
    .General_CursorStyle = REVERSE_ROUNDRECTANGLE,
    .General_MoveStyle = PID_CURVE,
    .General_FontSize = OLED_UI_FONT_12,
    .General_ParentMenuPage = &PCShortcutMenuPage,
    .General_MenuItems = ShortcutPresetMenuItems,
    .General_LineSpace = 3,
    .List_MenuArea = {0, 0, 128, 64},
    .List_IfDrawFrame = false,
    .List_IfDrawLinePerfix = true,
    .List_StartPointX = 2,
    .List_StartPointY = 2,
};

MenuPage ShortcutPresetPage2 = {
    .General_MenuType = MENU_TYPE_LIST,
    .General_MovingSpeed = 7.0f,
    .General_CursorStyle = REVERSE_ROUNDRECTANGLE,
    .General_MoveStyle = PID_CURVE,
    .General_FontSize = OLED_UI_FONT_12,
    .General_ParentMenuPage = &PCShortcutMenuPage,
    .General_MenuItems = ShortcutPresetMenuItems,
    .General_LineSpace = 3,
    .List_MenuArea = {0, 0, 128, 64},
    .List_IfDrawFrame = false,
    .List_IfDrawLinePerfix = true,
    .List_StartPointX = 2,
    .List_StartPointY = 2,
};

MenuPage ShortcutPresetPage3 = {
    .General_MenuType = MENU_TYPE_LIST,
    .General_MovingSpeed = 7.0f,
    .General_CursorStyle = REVERSE_ROUNDRECTANGLE,
    .General_MoveStyle = PID_CURVE,
    .General_FontSize = OLED_UI_FONT_12,
    .General_ParentMenuPage = &PCShortcutMenuPage,
    .General_MenuItems = ShortcutPresetMenuItems,
    .General_LineSpace = 3,
    .List_MenuArea = {0, 0, 128, 64},
    .List_IfDrawFrame = false,
    .List_IfDrawLinePerfix = true,
    .List_StartPointX = 2,
    .List_StartPointY = 2,
};

MenuPage ShortcutPresetPage4 = {
    .General_MenuType = MENU_TYPE_LIST,
    .General_MovingSpeed = 7.0f,
    .General_CursorStyle = REVERSE_ROUNDRECTANGLE,
    .General_MoveStyle = PID_CURVE,
    .General_FontSize = OLED_UI_FONT_12,
    .General_ParentMenuPage = &PCShortcutMenuPage,
    .General_MenuItems = ShortcutPresetMenuItems,
    .General_LineSpace = 3,
    .List_MenuArea = {0, 0, 128, 64},
    .List_IfDrawFrame = false,
    .List_IfDrawLinePerfix = true,
    .List_StartPointX = 2,
    .List_StartPointY = 2,
};

MenuPage StatusMenuPage = {
    .General_MenuType = MENU_TYPE_LIST,
    .General_MovingSpeed = 7.0f,
    .General_CursorStyle = NOT_SHOW,
    .General_MoveStyle = PID_CURVE,
    .General_FontSize = OLED_UI_FONT_12,
    .General_ParentMenuPage = &MainMenuPage,
    .General_MenuItems = StatusMenuItems,
    .General_LineSpace = 3,
    .List_MenuArea = {0, 0, 128, 64},
    .List_IfDrawFrame = false,
    .List_IfDrawLinePerfix = false,
    .List_StartPointX = 2,
    .List_StartPointY = 2,
};

MenuPage SettingsMenuPage = {
    .General_MenuType = MENU_TYPE_LIST,
    .General_MovingSpeed = 7.0f,
    .General_CursorStyle = REVERSE_ROUNDRECTANGLE,
    .General_MoveStyle = PID_CURVE,
    .General_FontSize = OLED_UI_FONT_12,
    .General_ParentMenuPage = &MainMenuPage,
    .General_MenuItems = SettingsMenuItems,
    .General_LineSpace = 3,
    .List_MenuArea = {0, 0, 128, 64},
    .List_IfDrawFrame = false,
    .List_IfDrawLinePerfix = true,
    .List_StartPointX = 2,
    .List_StartPointY = 2,
};

void app_ui_refresh_pages(void)
{
    MenuPage *runtime_pages[] = {
        &MainMenuPage,
        &TodoMenuPage,
        &WiFiMenuPage,
        &WiFiInfoMenuPage,
        &WiFiScanMenuPage,
        &PCControlMenuPage,
        &PCShortcutMenuPage,
        &ShortcutPresetPage1,
        &ShortcutPresetPage2,
        &ShortcutPresetPage3,
        &ShortcutPresetPage4,
        &StatusMenuPage,
        &SettingsMenuPage,
    };

    AppUI_UseSmoothCurve = s_smooth_animation_enabled ? RT_TRUE : RT_FALSE;
    app_ui_apply_runtime_to_pages(runtime_pages, sizeof(runtime_pages) / sizeof(runtime_pages[0]));

    app_ui_refresh_todo_items();
    app_ui_refresh_wifi_info_items();
    app_ui_refresh_scan_items();
    app_ui_refresh_status_items();
    app_ui_refresh_shortcut_preset_items();
    app_ui_refresh_shortcut_items();
    app_ui_update_pc_delta();
    app_ui_refresh_pc_items();

    if (CurrentMenuPage == &MainMenuPage)
    {
        app_ui_clamp_cursor(&MainMenuPage, 5U);
    }
    else if (CurrentMenuPage == &TodoMenuPage)
    {
        app_ui_clamp_cursor(&TodoMenuPage, s_visible_todo_count);
    }
    else if (CurrentMenuPage == &WiFiMenuPage)
    {
        app_ui_clamp_cursor(&WiFiMenuPage, 3U);
    }
    else if (CurrentMenuPage == &WiFiInfoMenuPage)
    {
        app_ui_clamp_cursor(&WiFiInfoMenuPage, 4U);
    }
    else if (CurrentMenuPage == &WiFiScanMenuPage)
    {
        device_net_service_status_t net_status;

        app_ui_clamp_cursor(&WiFiScanMenuPage, (rt_uint32_t)s_visible_scan_count);
        device_net_service_get_status(&net_status);
        if ((s_previous_page != &WiFiScanMenuPage) && (net_status.busy == RT_FALSE))
        {
            app_ui_request_scan();
        }
    }
    else if (CurrentMenuPage == &PCControlMenuPage)
    {
        app_ui_clamp_cursor(&PCControlMenuPage, 6U);
    }
    else if (CurrentMenuPage == &PCShortcutMenuPage)
    {
        app_ui_clamp_cursor(&PCShortcutMenuPage, DEVICE_USB_SHORTCUT_SLOT_COUNT);
    }
    else if ((CurrentMenuPage == &ShortcutPresetPage1) ||
             (CurrentMenuPage == &ShortcutPresetPage2) ||
             (CurrentMenuPage == &ShortcutPresetPage3) ||
             (CurrentMenuPage == &ShortcutPresetPage4))
    {
        app_ui_clamp_cursor(CurrentMenuPage, DEVICE_USB_SHORTCUT_PRESET_COUNT);
    }
    else if (CurrentMenuPage == &StatusMenuPage)
    {
        app_ui_clamp_cursor(&StatusMenuPage, 4U);
    }
    else if (CurrentMenuPage == &SettingsMenuPage)
    {
        app_ui_clamp_cursor(&SettingsMenuPage, 7U);
    }

    s_previous_page = CurrentMenuPage;
}
