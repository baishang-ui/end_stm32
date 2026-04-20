#include "oled_ui_pages.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <rtthread.h>
#include "data/todo_snapshot.h"
#include "cloud/aliyun_iot_mqtt.h"
#include "network/esp01s_at.h"
#include "app/device_service.h"

#define UI_SCAN_MAX_APS     8
#define UI_TODO_LINE_LEN    80
#define UI_AP_LINE_LEN      48
#define UI_WIFI_SSID_LEN    32
#define UI_STATUS_LINE_LEN  32
#define UI_PC_STATUS_LEN    24
#define UI_PC_STEP          2
#define UI_PC_REL_LIMIT     24

extern MenuPage *CurrentMenuPage;
extern MenuWindow *CurrentWindow;
extern int16_t OLED_UI_Brightness;
extern bool OLED_UI_ShowFps;
extern bool ColorMode;

static char s_todo_lines[TODO_SNAPSHOT_MAX_ITEMS][UI_TODO_LINE_LEN];
static char s_ap_lines[UI_SCAN_MAX_APS][UI_AP_LINE_LEN];
static char s_status_lines[4][UI_STATUS_LINE_LEN];
static char s_pc_status_line[UI_PC_STATUS_LEN];
static char s_pc_hint_line[UI_PC_STATUS_LEN];
static rt_uint32_t s_todo_display_count = 0;
static rt_size_t s_scan_display_count = 0;
static char s_saved_join_ssid[33] = "Bai";
static rt_uint8_t s_saved_join_ecn = 0;
static MenuPage *s_last_page = RT_NULL;
static int16_t s_pc_volume = 0;
static int16_t s_pc_brightness = 0;
static int16_t s_pc_volume_shadow = 0;
static int16_t s_pc_brightness_shadow = 0;

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
    (void)device_net_service_queue_todo_report(&change);
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
    s_pc_volume = 0;
    s_pc_volume_shadow = 0;

    static MenuWindow window = {
        .General_Width = 96,
        .General_Height = 28,
        .General_ContinueTime = 2.0f,
        .General_WindowType = WINDOW_ROUNDRECTANGLE,
        .Text_String = "VOL DELTA",
        .Text_FontSize = OLED_UI_FONT_12,
        .Text_FontSideDistance = 4,
        .Text_FontTopDistance = 3,
        .Prob_Data_Int = &s_pc_volume,
        .Prob_DataStep = UI_PC_STEP,
        .Prob_MinData = -UI_PC_REL_LIMIT,
        .Prob_MaxData = UI_PC_REL_LIMIT,
        .Prob_BottomDistance = 3,
        .Prob_SideDistance = 4,
        .Prob_LineHeight = 8,
    };

    oled_ui_create_window(&window);
}

static void ui_open_pc_brightness_window(void)
{
    s_pc_brightness = 0;
    s_pc_brightness_shadow = 0;

    static MenuWindow window = {
        .General_Width = 96,
        .General_Height = 28,
        .General_ContinueTime = 2.0f,
        .General_WindowType = WINDOW_ROUNDRECTANGLE,
        .Text_String = "BRT DELTA",
        .Text_FontSize = OLED_UI_FONT_12,
        .Text_FontSideDistance = 4,
        .Text_FontTopDistance = 3,
        .Prob_Data_Int = &s_pc_brightness,
        .Prob_DataStep = UI_PC_STEP,
        .Prob_MinData = -UI_PC_REL_LIMIT,
        .Prob_MaxData = UI_PC_REL_LIMIT,
        .Prob_BottomDistance = 3,
        .Prob_SideDistance = 4,
        .Prob_LineHeight = 8,
    };

    oled_ui_create_window(&window);
}

static void ui_request_wifi_scan(void)
{
    (void)device_net_service_request_scan();
}

static void ui_request_wifi_rejoin(void)
{
    (void)device_net_service_request_rejoin();
}

static void ui_handle_wifi_scan_selection(void)
{
    esp01s_ap_info_t aps[UI_SCAN_MAX_APS];
    rt_size_t count = esp01s_get_scan_list(aps, UI_SCAN_MAX_APS);
    MenuID index = (CurrentMenuPage != RT_NULL) ? CurrentMenuPage->_ActiveMenuID : 0;

    if (index <= 0)
    {
        (void)device_net_service_request_scan();
        return;
    }

    if (((rt_size_t)(index - 1)) < count)
    {
        (void)device_net_service_request_connect(aps[index - 1].ssid, aps[index - 1].ecn);
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

static void ui_update_pc_control(void)
{
    rt_int16_t volume_delta = (rt_int16_t)(s_pc_volume - s_pc_volume_shadow);
    rt_int16_t brightness_delta = (rt_int16_t)(s_pc_brightness - s_pc_brightness_shadow);

    if ((volume_delta != 0) || (brightness_delta != 0))
    {
        (void)device_usb_service_adjust(volume_delta, brightness_delta);
        s_pc_volume_shadow = s_pc_volume;
        s_pc_brightness_shadow = s_pc_brightness;
    }

    if ((CurrentWindow == RT_NULL) && ((s_pc_volume != 0) || (s_pc_brightness != 0)))
    {
        s_pc_volume = 0;
        s_pc_brightness = 0;
        s_pc_volume_shadow = 0;
        s_pc_brightness_shadow = 0;
    }
}

static void ui_sync_pc_items(void)
{
    const char *usb_state;
    device_usb_service_status_t usb_status;

    device_usb_service_get_status(&usb_status);

    if (usb_status.ready == RT_FALSE)
    {
        usb_state = (usb_status.stack_started == RT_FALSE) ? "USB: OFF" : "USB: ENUM";
    }
    else if ((usb_status.can_send == RT_FALSE) || (usb_status.queued_steps != 0U))
    {
        usb_state = "USB: SEND";
    }
    else
    {
        usb_state = "USB: READY";
    }

    rt_strncpy(s_pc_status_line, usb_state, sizeof(s_pc_status_line) - 1U);
    s_pc_status_line[sizeof(s_pc_status_line) - 1U] = '\0';
    rt_strncpy(s_pc_hint_line, "REL NO SYNC", sizeof(s_pc_hint_line) - 1U);
    s_pc_hint_line[sizeof(s_pc_hint_line) - 1U] = '\0';

    PCControlMenuItems[0].General_item_text = "VOL +/-";
    PCControlMenuItems[0].General_callback = ui_open_pc_volume_window;
    PCControlMenuItems[0].General_SubMenuPage = RT_NULL;
    PCControlMenuItems[0].List_BoolRadioBox = RT_NULL;
    PCControlMenuItems[0].List_IntBox = RT_NULL;
    PCControlMenuItems[0].List_FloatBox = RT_NULL;

    PCControlMenuItems[1].General_item_text = "BRT +/-";
    PCControlMenuItems[1].General_callback = ui_open_pc_brightness_window;
    PCControlMenuItems[1].General_SubMenuPage = RT_NULL;
    PCControlMenuItems[1].List_BoolRadioBox = RT_NULL;
    PCControlMenuItems[1].List_IntBox = RT_NULL;
    PCControlMenuItems[1].List_FloatBox = RT_NULL;

    PCControlMenuItems[2].General_item_text = s_pc_status_line;
    PCControlMenuItems[2].General_callback = RT_NULL;
    PCControlMenuItems[2].General_SubMenuPage = RT_NULL;
    PCControlMenuItems[2].List_BoolRadioBox = RT_NULL;
    PCControlMenuItems[2].List_IntBox = RT_NULL;
    PCControlMenuItems[2].List_FloatBox = RT_NULL;

    PCControlMenuItems[3].General_item_text = s_pc_hint_line;
    PCControlMenuItems[3].General_callback = RT_NULL;
    PCControlMenuItems[3].General_SubMenuPage = RT_NULL;
    PCControlMenuItems[3].List_BoolRadioBox = RT_NULL;
    PCControlMenuItems[3].List_IntBox = RT_NULL;
    PCControlMenuItems[3].List_FloatBox = RT_NULL;

    PCControlMenuItems[4].General_item_text = RT_NULL;
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

MenuItem PCControlMenuItems[5] = {
    { .General_item_text = "VOL +/-", .General_callback = ui_open_pc_volume_window, .General_SubMenuPage = RT_NULL },
    { .General_item_text = "BRT +/-", .General_callback = ui_open_pc_brightness_window, .General_SubMenuPage = RT_NULL },
    { .General_item_text = "USB: WAIT" },
    { .General_item_text = "REL NO SYNC" },
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
        ui_clamp_page_cursor(&PCControlMenuPage, 4U);
    }
    else if (CurrentMenuPage == &WiFiScanMenuPage)
    {
        device_net_service_status_t net_status;

        ui_clamp_page_cursor(&WiFiScanMenuPage, s_scan_display_count);
        device_net_service_get_status(&net_status);
        if ((s_last_page != &WiFiScanMenuPage) && (net_status.busy == RT_FALSE))
        {
            (void)device_net_service_request_scan();
        }
    }
    else if (CurrentMenuPage == &StatusMenuPage)
    {
        ui_clamp_page_cursor(&StatusMenuPage, 4U);
    }

    s_last_page = CurrentMenuPage;
}

