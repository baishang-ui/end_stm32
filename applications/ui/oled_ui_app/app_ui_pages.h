#ifndef __APP_UI_PAGES_H__
#define __APP_UI_PAGES_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "ui/oled_ui_core/oled_ui_core.h"

extern MenuItem MainMenuItems[];
extern MenuItem TodoMenuItems[];
extern MenuItem WiFiMenuItems[];
extern MenuItem WiFiInfoMenuItems[];
extern MenuItem WiFiScanMenuItems[];
extern MenuItem PCControlMenuItems[];
extern MenuItem PCShortcutMenuItems[];
extern MenuItem ShortcutPresetMenuItems[];
extern MenuItem StatusMenuItems[];
extern MenuItem SettingsMenuItems[];

extern MenuPage MainMenuPage;
extern MenuPage TodoMenuPage;
extern MenuPage WiFiMenuPage;
extern MenuPage WiFiInfoMenuPage;
extern MenuPage WiFiScanMenuPage;
extern MenuPage PCControlMenuPage;
extern MenuPage PCShortcutMenuPage;
extern MenuPage ShortcutPresetPage1;
extern MenuPage ShortcutPresetPage2;
extern MenuPage ShortcutPresetPage3;
extern MenuPage ShortcutPresetPage4;
extern MenuPage StatusMenuPage;
extern MenuPage SettingsMenuPage;

void app_ui_refresh_pages(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_UI_PAGES_H__ */
