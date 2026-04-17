#ifndef __OLED_UI_MENUDATA_H
#define __OLED_UI_MENUDATA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "oled_ui_core.h"

extern MenuItem MainMenuItems[];
extern MenuItem TodoMenuItems[];
extern MenuItem WiFiMenuItems[];
extern MenuItem WiFiScanMenuItems[];
extern MenuItem StatusMenuItems[];
extern MenuItem SettingsMenuItems[];

extern MenuPage MainMenuPage;
extern MenuPage TodoMenuPage;
extern MenuPage WiFiMenuPage;
extern MenuPage WiFiScanMenuPage;
extern MenuPage StatusMenuPage;
extern MenuPage SettingsMenuPage;

void oled_ui_prepare_pages(void);

#ifdef __cplusplus
}
#endif

#endif


