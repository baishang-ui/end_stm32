/*
 *  OLED_UI_Launcher.c
 *  Version: 0.2.1
 *  Created on: Jun 26, 2025
 *  Author: 5akura1
 */
#include "oled_ui_launcher.h"

void oled_ui_launcher_init(void)
{
    oled_ui_init(&MainMenuPage);
}

void oled_ui_launcher_start(void)
{
    while (1)
    {
        oled_ui_main_loop();
    }
}


