#ifndef UI_WIFI_SETTINGS_H
#define UI_WIFI_SETTINGS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Font with both Cyrillic and Latin characters
LV_FONT_DECLARE(montserrat_20_en_ru);

// Keyboard maps
extern const char *kb_map_ru[];
extern const char *kb_map_ru_uc[];
extern const char *kb_map_en[];
extern const char *kb_map_en_uc[];
extern const char *kb_map_num[];

// Keyboard control maps
extern const lv_btnmatrix_ctrl_t kb_ctrl_ru[];
extern const lv_btnmatrix_ctrl_t kb_ctrl_en[];
extern const lv_btnmatrix_ctrl_t kb_ctrl_num[];

// Shared language state
extern uint8_t current_kb_lang; // 0=EN, 1=RU

// Value changed callback for EN/RU, 123, and manual control
void kb_value_cb(lv_event_t *e);

/**
 * @brief Show the WiFi settings popup overlay
 */
void ui_show_wifi_settings(void);

#ifdef __cplusplus
}
#endif

#endif // UI_WIFI_SETTINGS_H
