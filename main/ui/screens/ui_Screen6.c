// ECU Dashboard Screen 6 - Device Parameters Settings
// Sixth screen dedicated to device configuration and settings

#include "ui_Screen6.h"
#include "../ui.h"
#include "ai_manager.h" // Include AI Manager
#include "ui_Screen1.h"
#include "ui_Screen2.h"
#include "ui_Screen4.h"
#include "ui_Screen5.h"
#include "ui_screen_manager.h"

#include "../background_task.h" // Фоновая задача для асинхронных операций
#include "can_definitions.h" // Platform definitions
#include "can_parser.h"      // For can_parser_set_platform
#include "ecu_data.h"        // For system_settings_t
#include "settings_config.h" // Убедитесь, что этот файл подключен
#include "ui_events.h"
#include "ui_helpers.h"
#include "ui_sound_selector.h"
#include "ui_wifi_settings.h"
#include "wifi_controller.h"

#include <esp_log.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(lv_font_montserrat_24);
LV_FONT_DECLARE(montserrat_20_en_ru); // Cyrillic support

// Screen object
lv_obj_t *ui_Screen6;

// Device Parameters Settings Objects
void *ui_Label_Device_Title;
void *ui_Button_Demo_Mode;
void *ui_Button_Enable_Screen3;
void *ui_Button_Nav_Buttons;
void *ui_Button_Save_Settings;
void *ui_Button_Reset_Settings;
void *ui_Button_AI;          // AI Button
void *ui_Button_Intro_Sound; // Intro Sound Button
void *ui_Button_Wifi;        // WiFi Settings Button
void *ui_Button_Voice_AI;    // Voice AI Toggle Button
void *ui_Label_Intro_Sound;
void *ui_Label_Demo_Mode;
void *ui_Label_Enable_Screen3;
void *ui_Label_Nav_Buttons;
void *ui_Label_Save_Settings;

// Touch cursor object
lv_obj_t *ui_Touch_Cursor_Screen6;

// Gauge List Container
static lv_obj_t *ui_Container_GaugeList = NULL;
// Platform List Container
static lv_obj_t *ui_Container_PlatformList = NULL;

// AI Status Objects and Lua Terminal
lv_obj_t *ui_Label_AIInfo = NULL;
lv_obj_t *ui_TextArea_Gemini = NULL;
lv_obj_t *ui_TextArea_Lua = NULL;
lv_obj_t *ui_Keyboard_Lua = NULL; // Re-added for dynamic keyboard
lv_obj_t *ui_Button_Run_Rule = NULL;
lv_obj_t *ui_Button_Save_Rule = NULL;
lv_obj_t *ui_Button_Help_Rule = NULL; // Added Help button
lv_obj_t *ui_Button_GPIO_Map = NULL; // Added GPIO Map button

// Settings state
static int settings_modified = 0;
static bool demo_mode_enabled = false;
static bool screen3_enabled = false;
static bool nav_buttons_enabled = true;
static bool show_map = true;
static bool show_wastegate = true;
static bool show_tps = true;
static bool show_rpm = true;
static bool show_boost = true;
static bool show_tcu = true;

// Screen 2
static bool show_oil_press = true;
static bool show_oil_temp = true;
static bool show_water_temp = true;
static bool show_fuel_press = true;
static bool show_battery = true;

// Screen 4
static bool show_pedal = true;
static bool show_wg_pos = true;
static bool show_bov = true;
static bool show_tcu_req = true;
static bool show_tcu_act = true;
static bool show_eng_req = true;

// Screen 5
static bool show_eng_act = true;
static bool show_limit_tq = true;

// Function prototypes
static void save_settings_event_cb(lv_event_t *e);
static void reset_settings_event_cb(lv_event_t *e);
static void reset_settings_event_cb(lv_event_t *e);
static void demo_mode_event_cb(lv_event_t *e);
static void enable_screen3_event_cb(lv_event_t *e);
static void nav_buttons_event_cb(lv_event_t *e);
static void gauge_checkbox_event_cb(lv_event_t *e);
static void platform_checkbox_event_cb(lv_event_t *e);
static void ai_button_event_cb(lv_event_t *e); // New callback
static void intro_sound_event_cb(lv_event_t *e);
static void wifi_button_event_cb(lv_event_t *e);
static void voice_ai_event_cb(lv_event_t *e);
static void lua_run_event_cb(lv_event_t *e);
static void lua_save_event_cb(lv_event_t *e);
static void gemini_ta_event_cb(lv_event_t *e);
static void ta_event_cb(lv_event_t *e);
// WiFi status update removed

void ui_update_touch_cursor_screen6(void *point);

// Screen6 utility functions
void ui_Screen6_load_settings(void);
void ui_Screen6_save_settings(void);
void ui_Screen6_update_button_states(void);
void ui_save_device_settings(void);
void ui_reset_device_settings(void);
void ui_Screen6_set_ai_info(const char *text);

// Touch handler for general touch events removed - using global handler

// Custom swipe handler removed in favor of global ui_screen_swipe_event_cb

// Save settings button event callback
static void save_settings_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    ui_save_device_settings();
    settings_modified = 0;
    ESP_LOGI("SCREEN6", "Device settings save triggered");
  }
}

// Reset settings button event callback
static void reset_settings_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    ui_reset_device_settings();
    settings_modified = 0;
    ESP_LOGI("SCREEN6", "Device settings reset triggered");
  }
}

// Demo mode button event callback
static void demo_mode_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    demo_mode_enabled = !demo_mode_enabled;

    // Update button visuals on this screen
    ui_Screen6_update_button_states();

    // Update animations on all screens
    ui_set_global_demo_mode(demo_mode_enabled);

    settings_modified = 1;
    ESP_LOGI("SCREEN6", "Global demo mode toggled to: %s",
             demo_mode_enabled ? "ON" : "OFF");
  }
}

// Enable Screen3 button event callback
static void enable_screen3_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    screen3_enabled = !screen3_enabled;
    ui_Screen6_update_button_states();
    settings_modified = 1;
    ESP_LOGI("SCREEN6", "Screen3 toggled to: %s",
             screen3_enabled ? "ON" : "OFF");
  }
}

// AI Button event callback
static lv_obj_t *ai_button_ref = NULL; // Store button reference for color reset

static void ai_button_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_t *btn = lv_event_get_target(e);
    ai_button_ref = btn;

    // Set to "active/recording" color (blue)
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0088FF), 0);

    // Force immediate UI refresh before blocking call
    lv_refr_now(NULL);

    // Trigger AI listening
    ai_manager_trigger_listening();

    ESP_LOGI("SCREEN6", "AI Listening Triggered");
  }
}

// Reset AI button color to green (called from ai_manager task)
void ui_Screen6_reset_ai_button(void) {
  if (ai_button_ref) {
    lv_obj_set_style_bg_color(ai_button_ref, lv_color_hex(0x00FF88), 0);
  } else if (ui_Button_AI) {
    lv_obj_set_style_bg_color((lv_obj_t *)ui_Button_AI, lv_color_hex(0x00FF88),
                              0);
  }
}

// Introductory Sound button event callback
static void intro_sound_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    ui_show_sound_selector();
    ESP_LOGI("SCREEN6", "Sound selector popup triggered");
  }
}

// WiFi Settings button event callback
static void wifi_button_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    ui_show_wifi_settings();
    ESP_LOGI("SCREEN6", "WiFi settings popup triggered");
  }
}

static bool voice_ai_active = false;
static void voice_ai_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    voice_ai_active = !voice_ai_active;
    ai_manager_set_voice_activation(voice_ai_active);
    ui_Screen6_update_button_states();
    settings_modified = 1;
    ESP_LOGI("SCREEN6", "Voice AI toggled to: %s",
             voice_ai_active ? "ON" : "OFF");
  }
}

// Navigation buttons toggle event callback
static void nav_buttons_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    nav_buttons_enabled = !nav_buttons_enabled;
    ui_Screen6_update_button_states();
    settings_modified = 1;
    ESP_LOGI("SCREEN6", "Nav buttons toggled to: %s",
             nav_buttons_enabled ? "ON" : "OFF");
  }
}

// Helper to find gauge ID from text
static gauge_id_t get_gauge_id_from_text(const char *txt) {
  if (strcmp(txt, "MAP") == 0)
    return GAUGE_MAP;
  if (strcmp(txt, "Wastegate") == 0)
    return GAUGE_WASTEGATE;
  if (strcmp(txt, "TPS") == 0)
    return GAUGE_TPS;
  if (strcmp(txt, "RPM") == 0)
    return GAUGE_RPM;
  if (strcmp(txt, "Boost") == 0)
    return GAUGE_BOOST;
  if (strcmp(txt, "TCU") == 0)
    return GAUGE_TCU;

  if (strcmp(txt, "Oil Press") == 0)
    return GAUGE_OIL_PRESS;
  if (strcmp(txt, "Oil Temp") == 0)
    return GAUGE_OIL_TEMP;
  if (strcmp(txt, "Water Temp") == 0)
    return GAUGE_WATER_TEMP;
  if (strcmp(txt, "Fuel Press") == 0)
    return GAUGE_FUEL_PRESS;
  if (strcmp(txt, "Battery") == 0)
    return GAUGE_BATTERY;

  if (strcmp(txt, "Pedal") == 0)
    return GAUGE_PEDAL;
  if (strcmp(txt, "WG Pos") == 0)
    return GAUGE_WG_POS;
  if (strcmp(txt, "BOV") == 0)
    return GAUGE_BOV;
  if (strcmp(txt, "TCU Req") == 0)
    return GAUGE_TCU_REQ;
  if (strcmp(txt, "TCU Act") == 0)
    return GAUGE_TCU_ACT;
  if (strcmp(txt, "Eng Req") == 0)
    return GAUGE_ENG_REQ;

  if (strcmp(txt, "Eng Act") == 0)
    return GAUGE_ENG_ACT;
  if (strcmp(txt, "Limit TQ") == 0)
    return GAUGE_LIMIT_TQ;

  return GAUGE_NONE;
}

// Helper to add gauge to ordered list
static void add_gauge_to_list(system_settings_t *settings, gauge_id_t id) {
  if (!settings || id == GAUGE_NONE)
    return;

  // Check if already exists
  for (int i = 0; i < settings->active_gauge_count; i++) {
    if (settings->active_gauge_ids[i] == id)
      return;
  }

  if (settings->active_gauge_count < 24) {
    settings->active_gauge_ids[settings->active_gauge_count++] = id;
  }
}

// Helper to remove gauge from ordered list
static void remove_gauge_from_list(system_settings_t *settings, gauge_id_t id) {
  if (!settings || id == GAUGE_NONE)
    return;

  int found_idx = -1;
  for (int i = 0; i < settings->active_gauge_count; i++) {
    if (settings->active_gauge_ids[i] == id) {
      found_idx = i;
      break;
    }
  }

  if (found_idx != -1) {
    // Shift remaining items
    for (int i = found_idx; i < settings->active_gauge_count - 1; i++) {
      settings->active_gauge_ids[i] = settings->active_gauge_ids[i + 1];
    }
    settings->active_gauge_count--;
  }
}

// Gauge checkbox event callback
static void gauge_checkbox_event_cb(lv_event_t *e) {
  lv_obj_t *cb = lv_event_get_target(e);
  const char *txt = lv_checkbox_get_text(cb);
  bool checked = lv_obj_has_state(cb, LV_STATE_CHECKED);

  if (strcmp(txt, "MAP") == 0)
    show_map = checked;
  else if (strcmp(txt, "Wastegate") == 0)
    show_wastegate = checked;
  else if (strcmp(txt, "TPS") == 0)
    show_tps = checked;
  else if (strcmp(txt, "RPM") == 0)
    show_rpm = checked;
  else if (strcmp(txt, "Boost") == 0)
    show_boost = checked;
  else if (strcmp(txt, "TCU") == 0)
    show_tcu = checked;

  // Screen 2
  else if (strcmp(txt, "Oil Press") == 0)
    show_oil_press = checked;
  else if (strcmp(txt, "Oil Temp") == 0)
    show_oil_temp = checked;
  else if (strcmp(txt, "Water Temp") == 0)
    show_water_temp = checked;
  else if (strcmp(txt, "Fuel Press") == 0)
    show_fuel_press = checked;
  else if (strcmp(txt, "Battery") == 0)
    show_battery = checked;

  // Screen 4
  else if (strcmp(txt, "Pedal") == 0)
    show_pedal = checked;
  else if (strcmp(txt, "WG Pos") == 0)
    show_wg_pos = checked;
  else if (strcmp(txt, "BOV") == 0)
    show_bov = checked;
  else if (strcmp(txt, "TCU Req") == 0)
    show_tcu_req = checked;
  else if (strcmp(txt, "TCU Act") == 0)
    show_tcu_act = checked;
  else if (strcmp(txt, "Eng Req") == 0)
    show_eng_req = checked;

  // Screen 5
  else if (strcmp(txt, "Eng Act") == 0)
    show_eng_act = checked;
  else if (strcmp(txt, "Limit TQ") == 0)
    show_limit_tq = checked;

  settings_modified = 1;
  ESP_LOGI("SCREEN6", "Gauge %s toggled to: %s", txt, checked ? "ON" : "OFF");

  // Update settings immediately in memory
  system_settings_t *settings = system_settings_get();
  if (settings) {
    if (strcmp(txt, "MAP") == 0)
      settings->show_map = checked;
    else if (strcmp(txt, "Wastegate") == 0)
      settings->show_wastegate = checked;
    else if (strcmp(txt, "TPS") == 0)
      settings->show_tps = checked;
    else if (strcmp(txt, "RPM") == 0)
      settings->show_rpm = checked;
    else if (strcmp(txt, "Boost") == 0)
      settings->show_boost = checked;
    else if (strcmp(txt, "TCU") == 0)
      settings->show_tcu = checked;

    else if (strcmp(txt, "Oil Press") == 0)
      settings->show_oil_press = checked;
    else if (strcmp(txt, "Oil Temp") == 0)
      settings->show_oil_temp = checked;
    else if (strcmp(txt, "Water Temp") == 0)
      settings->show_water_temp = checked;
    else if (strcmp(txt, "Fuel Press") == 0)
      settings->show_fuel_press = checked;
    else if (strcmp(txt, "Battery") == 0)
      settings->show_battery = checked;

    else if (strcmp(txt, "Pedal") == 0)
      settings->show_pedal = checked;
    else if (strcmp(txt, "WG Pos") == 0)
      settings->show_wg_pos = checked;
    else if (strcmp(txt, "BOV") == 0)
      settings->show_bov = checked;
    else if (strcmp(txt, "TCU Req") == 0)
      settings->show_tcu_req = checked;
    else if (strcmp(txt, "TCU Act") == 0)
      settings->show_tcu_act = checked;
    else if (strcmp(txt, "Eng Req") == 0)
      settings->show_eng_req = checked;

    else if (strcmp(txt, "Eng Act") == 0)
      settings->show_eng_act = checked;
    else if (strcmp(txt, "Limit TQ") == 0)
      settings->show_limit_tq = checked;

    // Update ordered list
    gauge_id_t id = get_gauge_id_from_text(txt);
    if (checked) {
      add_gauge_to_list(settings, id);
    } else {
      remove_gauge_from_list(settings, id);
    }
  }

  // Force global layout update
  // We need to declare this function or include header
  extern void ui_update_global_layout(void);
  ui_update_global_layout();
}

static void lua_run_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    ESP_LOGI("SCREEN6", "Run ESP-Claw Lua Rule");
    if(ui_TextArea_Lua) {
      const char * code = lv_textarea_get_text(ui_TextArea_Lua);
      ESP_LOGI("SCREEN6", "Lua Code:\n%s", code);
      // Run the code via Lua Manager
      extern esp_err_t lua_manager_execute(const char *script);
      lua_manager_execute(code);
    }
  }
}

static void lua_save_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    ESP_LOGI("SCREEN6", "Save ESP-Claw Rule");
    if(ui_TextArea_Lua) {
      const char * code = lv_textarea_get_text(ui_TextArea_Lua);
      extern esp_err_t lua_manager_save_background_script(const char *script);
      lua_manager_save_background_script(code);
      ui_Screen6_set_ai_info("✅ Rule Saved! Running in background...");
    }
  }
}

static void lua_help_close_cb(lv_event_t *e) {
  lv_obj_t *popup = lv_event_get_user_data(e);
  if (popup) {
    lv_obj_del(popup);
  }
}

static void lua_help_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    ESP_LOGI("SCREEN6", "Show Lua Help");
    
    // Create popup background
    lv_obj_t *popup = lv_obj_create(ui_Screen6);
    lv_obj_set_size(popup, 600, 400);
    lv_obj_center(popup);
    lv_obj_set_style_bg_color(popup, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(popup, 2, 0);
    lv_obj_set_style_border_color(popup, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_pad_all(popup, 20, 0);
    
    // Create help text label
    lv_obj_t *help_text = lv_label_create(popup);
    extern const char* lua_manager_get_help_text(void);
    lv_label_set_text(help_text, lua_manager_get_help_text());
    lv_obj_set_style_text_color(help_text, lv_color_white(), 0);
    lv_obj_set_style_text_font(help_text, &montserrat_20_en_ru, 0);
    lv_label_set_long_mode(help_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(help_text, 560);
    lv_obj_align(help_text, LV_ALIGN_TOP_LEFT, 0, 0);
    
    // Create close button
    lv_obj_t *close_btn = lv_btn_create(popup);
    lv_obj_set_size(close_btn, 120, 40);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xFF3333), 0);
    lv_obj_add_event_cb(close_btn, lua_help_close_cb, LV_EVENT_CLICKED, popup);
    
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "Close");
    lv_obj_center(close_lbl);
    
    // Bring to front
    lv_obj_move_foreground(popup);
  }
}

static lv_obj_t *ui_Keyboard_Gemini = NULL;

static void gemini_ta_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = lv_event_get_target(e);
  
  if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
    if (ui_Keyboard_Gemini == NULL) {
      ui_Keyboard_Gemini = lv_keyboard_create(ui_Screen6);
      lv_obj_set_size(ui_Keyboard_Gemini, 720, 450);
      lv_obj_align(ui_Keyboard_Gemini, LV_ALIGN_BOTTOM_MID, 0, -50); 
      lv_keyboard_set_textarea(ui_Keyboard_Gemini, ta);
    }
    lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, -520); // Move up
    lv_obj_move_foreground(ta);
    if (ui_Keyboard_Gemini) lv_obj_move_foreground(ui_Keyboard_Gemini);
  } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_CANCEL) {
    if (ui_Keyboard_Gemini != NULL) {
      lv_obj_del(ui_Keyboard_Gemini);
      ui_Keyboard_Gemini = NULL;
    }
    lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, -80); // Restore position
    lv_textarea_clear_selection(ta);
    if (code == LV_EVENT_CANCEL) {
      lv_obj_clear_state(ta, LV_STATE_FOCUSED);
    }
  } else if (code == LV_EVENT_READY) {
    // Send to Gemini
    const char *text = lv_textarea_get_text(ta);
    if (strlen(text) > 0) {
      extern void ai_manager_send_text_query(const char *text);
      ai_manager_send_text_query(text);
      lv_textarea_set_text(ta, ""); // Clear after send
    }
    if (ui_Keyboard_Gemini != NULL) {
      lv_obj_del(ui_Keyboard_Gemini);
      ui_Keyboard_Gemini = NULL;
    }
    lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, -80);
    lv_obj_clear_state(ta, LV_STATE_FOCUSED);
  }
}

static void gpio_map_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    ESP_LOGI("SCREEN6", "Show GPIO Map");
    
    lv_obj_t *popup = lv_obj_create(ui_Screen6);
    lv_obj_set_size(popup, 600, 450);
    lv_obj_center(popup);
    lv_obj_set_style_bg_color(popup, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(popup, 2, 0);
    lv_obj_set_style_border_color(popup, lv_color_hex(0x00FF88), 0);
    lv_obj_set_style_pad_all(popup, 20, 0);
    
    lv_obj_t *map_text = lv_label_create(popup);
    lv_label_set_recolor(map_text, true);
    lv_label_set_text(map_text, 
      "ESP32-P4 GPIO Map:\n\n"
      "#FF0000 0-3: JTAG/System (DO NOT USE)#\n"
      "#FF0000 4,5,7,8: I2C Touch Panel#\n"
      "#FF0000 9-13, 53: Audio I2S#\n"
      "#FF0000 14-19, 54: Wi-Fi SDIO#\n"
      "#FF0000 20,21: CAN Bus#\n"
      "#FF0000 33: Display Reset#\n"
      "#FF0000 39-44: SD Card#\n\n"
      "#00FF00 FREE PINS (SAFE TO USE):#\n"
      "#00FF00 6, 22-32, 34-38, 45-52#\n"
    );
    lv_obj_set_style_text_color(map_text, lv_color_white(), 0);
    lv_obj_set_style_text_font(map_text, &montserrat_20_en_ru, 0);
    lv_label_set_long_mode(map_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(map_text, 560);
    lv_obj_align(map_text, LV_ALIGN_TOP_LEFT, 0, 0);
    
    lv_obj_t *close_btn = lv_btn_create(popup);
    lv_obj_set_size(close_btn, 120, 40);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xFF3333), 0);
    lv_obj_add_event_cb(close_btn, lua_help_close_cb, LV_EVENT_CLICKED, popup);
    
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "Close");
    lv_obj_center(close_lbl);
    
    lv_obj_move_foreground(popup);
  }
}

static void ta_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = lv_event_get_target(e);
  
  if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
    if (ui_Keyboard_Lua == NULL) {
      ui_Keyboard_Lua = lv_keyboard_create(ui_Screen6);
      lv_obj_set_size(ui_Keyboard_Lua, 720, 450);
      lv_obj_align(ui_Keyboard_Lua, LV_ALIGN_BOTTOM_MID, 0, -50); // Just above nav buttons
      lv_keyboard_set_textarea(ui_Keyboard_Lua, ta);
    }
    // Shift textarea up so it's not hidden by keyboard
    lv_obj_set_y(ta, 80);
    lv_obj_move_foreground(ta);
    if (ui_Keyboard_Lua) lv_obj_move_foreground(ui_Keyboard_Lua);
  } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    if (ui_Keyboard_Lua != NULL) {
      lv_obj_del(ui_Keyboard_Lua);
      ui_Keyboard_Lua = NULL;
    }
    // Restore textarea position
    lv_obj_set_y(ta, 545);
    lv_textarea_clear_selection(ta);
    
    // Explicitly clear focus if closed via keyboard so clicking it again works
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
      lv_obj_clear_state(ta, LV_STATE_FOCUSED);
    }
  }
}

// Platform checkbox event callback (Mutual Exclusion)
static void platform_checkbox_event_cb(lv_event_t *e) {
  lv_obj_t *cb = lv_event_get_target(e);
  const char *txt = lv_checkbox_get_text(cb);
  bool checked = lv_obj_has_state(cb, LV_STATE_CHECKED);

  // If unchecked, force reference back to checked (radio button behavior)
  if (!checked) {
    lv_obj_add_state(cb, LV_STATE_CHECKED);
    return;
  }

  // touch_settings_t *settings = &current_settings; // Removed direct access

  CanPlatform selected_platform = PLATFORM_VW_PQ35_46;
  if (strcmp(txt, "VW PQ35/46") == 0)
    selected_platform = PLATFORM_VW_PQ35_46;
  else if (strcmp(txt, "VW PQ25") == 0)
    selected_platform = PLATFORM_VW_PQ25;
  else if (strcmp(txt, "VW MQB") == 0)
    selected_platform = PLATFORM_VW_MQB;
  else if (strcmp(txt, "BMW Exx") == 0)
    selected_platform = PLATFORM_BMW_E9X;
  else if (strcmp(txt, "BMW E46") == 0)
    selected_platform = PLATFORM_BMW_E46;
  else if (strcmp(txt, "BMW Fxx") == 0)
    selected_platform = PLATFORM_BMW_F_SERIES;

  // Apply via setter (also updates parser)
  settings_set_can_platform(selected_platform);

  // Apply immediately to parser
  // #include "include/can_parser.h" // Moved to top of file
  can_parser_set_platform(selected_platform);

  settings_modified = 1;
  ESP_LOGI("SCREEN6", "Platform switched to: %s (%d)", txt, selected_platform);

  // Uncheck other checkboxes
  if (ui_Container_PlatformList) {
    uint32_t child_cnt = lv_obj_get_child_cnt(ui_Container_PlatformList);
    for (uint32_t i = 0; i < child_cnt; i++) {
      lv_obj_t *child = lv_obj_get_child(ui_Container_PlatformList, i);
      if (lv_obj_check_type(child, &lv_checkbox_class)) {
        if (child != cb) {
          lv_obj_clear_state(child, LV_STATE_CHECKED);
        }
      }
    }
  }
}

// Initialize Screen6
void ui_Screen6_screen_init(void) {
  ESP_LOGI("SCREEN6", "Starting Screen6 initialization");
  ui_Screen6 = lv_obj_create(NULL);
  lv_obj_set_size(ui_Screen6, 736, 1280);
  ESP_LOGI("SCREEN6", "ui_Screen6 created: %p", (void *)ui_Screen6);
  if (ui_Screen6 == NULL) {
    ESP_LOGE("SCREEN6", "FATAL: lv_obj_create(NULL) returned NULL!");
    return;
  }
  lv_obj_clear_flag(ui_Screen6, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(ui_Screen6, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(ui_Screen6, LV_OPA_COVER, 0);

  // Main title - Center for 720 width
  ui_Label_Device_Title = lv_label_create(ui_Screen6);
  lv_label_set_text((lv_obj_t *)ui_Label_Device_Title, "Settings");
  lv_obj_set_style_text_color((lv_obj_t *)ui_Label_Device_Title,
                              lv_color_hex(0x00D4FF), 0);
  lv_obj_set_style_text_font((lv_obj_t *)ui_Label_Device_Title,
                             &montserrat_20_en_ru, 0); // Cyrillic support
  lv_obj_align((lv_obj_t *)ui_Label_Device_Title, LV_ALIGN_TOP_MID, 0, 10);

  // Settings Buttons - Vertical column on the left side
  int btn_w = 180; // Smaller width
  int btn_h = 50;  // Smaller height
  int btn_x = 10;  // Left position
  int btn_start_y = 45;
  int btn_gap = 55; // Vertical gap between buttons

  // Button 1 - Demo Mode
  ui_Button_Demo_Mode = lv_btn_create(ui_Screen6);
  lv_obj_set_size((lv_obj_t *)ui_Button_Demo_Mode, btn_w, btn_h);
  lv_obj_align((lv_obj_t *)ui_Button_Demo_Mode, LV_ALIGN_TOP_LEFT, btn_x,
               btn_start_y);
  lv_obj_set_style_bg_color((lv_obj_t *)ui_Button_Demo_Mode,
                            lv_color_hex(0x333333), 0);
  lv_obj_add_event_cb((lv_obj_t *)ui_Button_Demo_Mode, demo_mode_event_cb,
                      LV_EVENT_CLICKED, NULL);
  ui_Label_Demo_Mode = lv_label_create((lv_obj_t *)ui_Button_Demo_Mode);
  lv_label_set_text(ui_Label_Demo_Mode, "Demo: OFF");
  lv_obj_center(ui_Label_Demo_Mode);

  // Button 2 - Terminal Screen
  ui_Button_Enable_Screen3 = lv_btn_create(ui_Screen6);
  lv_obj_set_size((lv_obj_t *)ui_Button_Enable_Screen3, btn_w, btn_h);
  lv_obj_align((lv_obj_t *)ui_Button_Enable_Screen3, LV_ALIGN_TOP_LEFT, btn_x,
               btn_start_y + btn_gap);
  lv_obj_add_event_cb((lv_obj_t *)ui_Button_Enable_Screen3,
                      enable_screen3_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *s3_label = lv_label_create((lv_obj_t *)ui_Button_Enable_Screen3);
  lv_label_set_text(s3_label, "Terminal");
  lv_obj_center(s3_label);

  // Button 3 - Nav Buttons
  ui_Button_Nav_Buttons = lv_btn_create(ui_Screen6);
  lv_obj_set_size((lv_obj_t *)ui_Button_Nav_Buttons, btn_w, btn_h);
  lv_obj_align((lv_obj_t *)ui_Button_Nav_Buttons, LV_ALIGN_TOP_LEFT, btn_x,
               btn_start_y + btn_gap * 2);
  lv_obj_add_event_cb((lv_obj_t *)ui_Button_Nav_Buttons, nav_buttons_event_cb,
                      LV_EVENT_CLICKED, NULL);
  lv_obj_t *nav_label = lv_label_create((lv_obj_t *)ui_Button_Nav_Buttons);
  lv_label_set_text(nav_label, "Nav Btns");
  lv_obj_center(nav_label);

  // Button 4 - Save Settings
  ui_Button_Save_Settings = lv_btn_create(ui_Screen6);
  lv_obj_set_size((lv_obj_t *)ui_Button_Save_Settings, btn_w, btn_h);
  lv_obj_align((lv_obj_t *)ui_Button_Save_Settings, LV_ALIGN_TOP_LEFT, btn_x,
               btn_start_y + btn_gap * 3);
  lv_obj_set_style_bg_color((lv_obj_t *)ui_Button_Save_Settings,
                            lv_color_hex(0x00FF88), 0);
  lv_obj_add_event_cb((lv_obj_t *)ui_Button_Save_Settings,
                      save_settings_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *save_lbl = lv_label_create((lv_obj_t *)ui_Button_Save_Settings);
  lv_label_set_text(save_lbl, "SAVE");
  lv_obj_set_style_text_color(save_lbl, lv_color_black(), 0);
  lv_obj_center(save_lbl);

  // Button 5 - AI Assistant
  ui_Button_AI = lv_btn_create(ui_Screen6);
  lv_obj_set_size((lv_obj_t *)ui_Button_AI, btn_w, btn_h);
  lv_obj_align((lv_obj_t *)ui_Button_AI, LV_ALIGN_TOP_LEFT, btn_x,
               btn_start_y + btn_gap * 4);
  lv_obj_set_style_bg_color((lv_obj_t *)ui_Button_AI, lv_color_hex(0x00D4FF),
                            0);
  lv_obj_add_event_cb((lv_obj_t *)ui_Button_AI, ai_button_event_cb,
                      LV_EVENT_CLICKED, NULL);
  lv_obj_t *ai_lbl = lv_label_create((lv_obj_t *)ui_Button_AI);
  lv_label_set_text(ai_lbl, "AI");
  lv_obj_set_style_text_color(ai_lbl, lv_color_black(), 0);
  lv_obj_center(ai_lbl);

  // Button 6 - Reset Settings
  ui_Button_Reset_Settings = lv_btn_create(ui_Screen6);
  lv_obj_set_size((lv_obj_t *)ui_Button_Reset_Settings, btn_w, btn_h);
  lv_obj_align((lv_obj_t *)ui_Button_Reset_Settings, LV_ALIGN_TOP_LEFT, btn_x,
               btn_start_y + btn_gap * 5);
  lv_obj_set_style_bg_color((lv_obj_t *)ui_Button_Reset_Settings,
                            lv_color_hex(0xFF3366), 0);
  lv_obj_add_event_cb((lv_obj_t *)ui_Button_Reset_Settings,
                      reset_settings_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *reset_lbl = lv_label_create((lv_obj_t *)ui_Button_Reset_Settings);
  lv_label_set_text(reset_lbl, "RESET");
  lv_obj_center(reset_lbl);

  // Button 7 - Intro Sound
  ui_Button_Intro_Sound = lv_btn_create(ui_Screen6);
  lv_obj_set_size((lv_obj_t *)ui_Button_Intro_Sound, btn_w, btn_h);
  lv_obj_align((lv_obj_t *)ui_Button_Intro_Sound, LV_ALIGN_TOP_LEFT, btn_x,
               btn_start_y + btn_gap * 6);
  lv_obj_set_style_bg_color((lv_obj_t *)ui_Button_Intro_Sound,
                            lv_color_hex(0xFFCC00), 0);
  lv_obj_add_event_cb((lv_obj_t *)ui_Button_Intro_Sound, intro_sound_event_cb,
                      LV_EVENT_CLICKED, NULL);
  ui_Label_Intro_Sound = lv_label_create((lv_obj_t *)ui_Button_Intro_Sound);
  lv_label_set_text(ui_Label_Intro_Sound, "Audio");
  lv_obj_set_style_text_color(ui_Label_Intro_Sound, lv_color_black(), 0);
  lv_obj_center(ui_Label_Intro_Sound);

  // Button 8 - WiFi Settings
  ui_Button_Wifi = lv_btn_create(ui_Screen6);
  lv_obj_set_size((lv_obj_t *)ui_Button_Wifi, btn_w, btn_h);
  lv_obj_align((lv_obj_t *)ui_Button_Wifi, LV_ALIGN_TOP_LEFT, btn_x,
               btn_start_y + btn_gap * 7);
  lv_obj_set_style_bg_color((lv_obj_t *)ui_Button_Wifi, lv_color_hex(0x00FF88),
                            0);
  lv_obj_add_event_cb((lv_obj_t *)ui_Button_Wifi, wifi_button_event_cb,
                      LV_EVENT_CLICKED, NULL);
  lv_obj_t *wifi_lbl = lv_label_create((lv_obj_t *)ui_Button_Wifi);
  lv_label_set_text(wifi_lbl, LV_SYMBOL_WIFI " WiFi");
  lv_obj_set_style_text_color(wifi_lbl, lv_color_black(), 0);
  lv_obj_center(wifi_lbl);

  // Button 9 - Voice AI Toggle
  ui_Button_Voice_AI = lv_btn_create(ui_Screen6);
  lv_obj_set_size((lv_obj_t *)ui_Button_Voice_AI, btn_w, btn_h);
  lv_obj_align((lv_obj_t *)ui_Button_Voice_AI, LV_ALIGN_TOP_LEFT, btn_x,
               btn_start_y + btn_gap * 8);
  lv_obj_add_event_cb((lv_obj_t *)ui_Button_Voice_AI, voice_ai_event_cb,
                      LV_EVENT_CLICKED, NULL);
  lv_obj_t *voice_lbl = lv_label_create((lv_obj_t *)ui_Button_Voice_AI);
  lv_label_set_text(voice_lbl, "Voice AI: ON");
  lv_obj_center(voice_lbl);

  // Single Box Container with border for Gauges and Platform (right of buttons)
  lv_obj_t *ui_Container_Combined = lv_obj_create(ui_Screen6);
  lv_obj_set_size(ui_Container_Combined, 520, 480);
  lv_obj_align(ui_Container_Combined, LV_ALIGN_TOP_LEFT, 200, 45);
  lv_obj_set_style_bg_color(ui_Container_Combined, lv_color_hex(0x1a1a1a), 0);
  lv_obj_set_style_border_width(ui_Container_Combined, 1, 0);
  lv_obj_set_style_border_color(ui_Container_Combined, lv_color_hex(0x00D4FF),
                                0);
  lv_obj_set_style_radius(ui_Container_Combined, 6, 0);
  lv_obj_set_style_pad_all(ui_Container_Combined, 5, 0);
  lv_obj_set_style_pad_gap(ui_Container_Combined, 5, 0);
  lv_obj_clear_flag(ui_Container_Combined, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(ui_Container_Combined, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ui_Container_Combined, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  // Left Column - Gauges (no border, transparent bg)
  ui_Container_GaugeList = lv_obj_create(ui_Container_Combined);
  lv_obj_set_size(ui_Container_GaugeList, 250, 465);
  lv_obj_set_scrollbar_mode(ui_Container_GaugeList, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_bg_opa(ui_Container_GaugeList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ui_Container_GaugeList, 0, 0);
  lv_obj_set_flex_flow(ui_Container_GaugeList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(ui_Container_GaugeList, 2, 0);
  lv_obj_set_style_pad_gap(ui_Container_GaugeList, 4, 0);

  // Gauge List Header
  lv_obj_t *gauge_header = lv_label_create(ui_Container_GaugeList);
  lv_label_set_text(gauge_header, "Visible Gauges");
  lv_obj_set_style_text_color(gauge_header, lv_color_hex(0x00D4FF), 0);
  lv_obj_set_style_text_font(gauge_header, &montserrat_20_en_ru, 0);

  // Populate Gauge List
  const char *gauges[] = {
      "Screen 1:",  "MAP",        "Wastegate", "TPS",       "RPM",
      "Boost",      "TCU",        "Screen 2:", "Oil Press", "Oil Temp",
      "Water Temp", "Fuel Press", "Battery",   "Screen 4:", "Pedal",
      "WG Pos",     "BOV",        "TCU Req",   "TCU Act",   "Eng Req",
      "Screen 5:",  "Eng Act",    "Limit TQ"};

  for (int i = 0; i < 23; i++) {
    if (strchr(gauges[i], ':')) {
      lv_obj_t *header = lv_label_create(ui_Container_GaugeList);
      lv_label_set_text(header, gauges[i]);
      lv_obj_set_style_text_color(header, lv_color_hex(0x00FF88), 0);
      lv_obj_set_style_text_font(header, &montserrat_20_en_ru, 0);
      lv_obj_set_style_pad_top(header, 6, 0);
    } else {
      lv_obj_t *cb = lv_checkbox_create(ui_Container_GaugeList);
      lv_checkbox_set_text(cb, gauges[i]);
      lv_obj_set_style_text_color(cb, lv_color_white(), 0);
      lv_obj_add_event_cb(cb, gauge_checkbox_event_cb, LV_EVENT_VALUE_CHANGED,
                          NULL);
    }
  }

  // Right Column - Platform (no border, transparent bg)
  ui_Container_PlatformList = lv_obj_create(ui_Container_Combined);
  lv_obj_set_size(ui_Container_PlatformList, 250, 465);
  lv_obj_set_scrollbar_mode(ui_Container_PlatformList, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_bg_opa(ui_Container_PlatformList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ui_Container_PlatformList, 0, 0);
  lv_obj_set_flex_flow(ui_Container_PlatformList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(ui_Container_PlatformList, 2, 0);
  lv_obj_set_style_pad_gap(ui_Container_PlatformList, 4, 0);

  // Platform List Header
  lv_obj_t *platform_header = lv_label_create(ui_Container_PlatformList);
  lv_label_set_text(platform_header, "Vehicle Platform");
  lv_obj_set_style_text_color(platform_header, lv_color_hex(0x00FF88), 0);
  lv_obj_set_style_text_font(platform_header, &montserrat_20_en_ru, 0);

  const char *platforms[] = {"VW PQ35/46", "VW PQ25", "VW MQB",
                             "BMW Exx",    "BMW E46", "BMW Fxx"};

  for (int i = 0; i < 6; i++) {
    lv_obj_t *cb = lv_checkbox_create(ui_Container_PlatformList);
    lv_checkbox_set_text(cb, platforms[i]);
    lv_obj_set_style_text_color(cb, lv_color_white(), 0);
    lv_obj_add_event_cb(cb, platform_checkbox_event_cb, LV_EVENT_VALUE_CHANGED,
                        NULL);
  }

  // --- ESP-Claw Terminal ---
  ui_Label_AIInfo = lv_label_create(ui_Screen6);
  lv_label_set_long_mode(ui_Label_AIInfo, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(ui_Label_AIInfo, 500); // Fit between nav buttons
  lv_label_set_text(ui_Label_AIInfo, "AI: Idle | Waiting for trigger...");
  lv_obj_set_style_text_align(ui_Label_AIInfo, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(ui_Label_AIInfo, lv_color_white(), 0);
  lv_obj_set_style_text_font(ui_Label_AIInfo, &montserrat_20_en_ru, 0);
  lv_obj_align(ui_Label_AIInfo, LV_ALIGN_BOTTOM_MID, 0, -35);

  // Gemini Text Input Area
  ui_TextArea_Gemini = lv_textarea_create(ui_Screen6);
  lv_obj_set_size(ui_TextArea_Gemini, 600, 50); // Single line
  lv_textarea_set_one_line(ui_TextArea_Gemini, true);
  lv_textarea_set_placeholder_text(ui_TextArea_Gemini, "Введите запрос для ИИ...");
  lv_obj_align(ui_TextArea_Gemini, LV_ALIGN_BOTTOM_MID, 0, -80);
  lv_obj_set_style_text_font(ui_TextArea_Gemini, &montserrat_20_en_ru, 0);
  lv_obj_set_style_bg_color(ui_TextArea_Gemini, lv_color_hex(0x1a1a1a), 0);
  lv_obj_set_style_text_color(ui_TextArea_Gemini, lv_color_white(), 0);
  lv_obj_set_style_border_color(ui_TextArea_Gemini, lv_color_hex(0x00D4FF), 0);
  lv_obj_set_style_border_width(ui_TextArea_Gemini, 2, 0);
  lv_obj_add_event_cb(ui_TextArea_Gemini, gemini_ta_event_cb, LV_EVENT_ALL, NULL);

  // Lua Terminal Text Area
  ui_TextArea_Lua = lv_textarea_create(ui_Screen6);
  lv_obj_set_size(ui_TextArea_Lua, 690, 350); // Increased size
  lv_obj_align(ui_TextArea_Lua, LV_ALIGN_TOP_MID, 0, 545);
  lv_obj_set_style_bg_color(ui_TextArea_Lua, lv_color_hex(0x000000), 0);
  lv_obj_set_style_text_color(ui_TextArea_Lua, lv_color_hex(0x00FF88), 0); // Hackers green text
  lv_obj_set_style_text_font(ui_TextArea_Lua, &montserrat_20_en_ru, 0);
  // Add padding to make it look like a real terminal
  lv_obj_set_style_pad_all(ui_TextArea_Lua, 15, 0);
  
  // Make cursor white and clearly visible
  lv_obj_set_style_bg_color(ui_TextArea_Lua, lv_color_hex(0xFFFFFF), LV_PART_CURSOR);
  lv_obj_set_style_bg_opa(ui_TextArea_Lua, LV_OPA_COVER, LV_PART_CURSOR);
  
  lv_textarea_set_text(ui_TextArea_Lua, 
    "setTickRate(2)\n"
    "local counter = 0\n"
    "canRxAdd(0x123)\n"
    "function onTick()\n"
    "    counter = counter + 1\n"
    "    log(\"onTick work! Counter: \" .. counter)\n"
    "    txCan(1, 0x600, 0, {counter, 0xAA, 0xBB})\n"
    "    if counter % 10 == 0 then\n"
    "        show_warning(\"Lua Tick: \" .. counter)\n"
    "    end\n"
    "end\n"
    "function onCanRx(bus, id, dlc, data)\n"
    "    log(\"Wow, got CAN frame: \" .. id)\n"
    "end\n"
    "log(\"Background script loaded successfully!\")");
  lv_textarea_set_cursor_click_pos(ui_TextArea_Lua, true);
  lv_obj_add_event_cb(ui_TextArea_Lua, ta_event_cb, LV_EVENT_ALL, NULL);

  // Action Buttons
  ui_Button_Run_Rule = lv_btn_create(ui_Screen6);
  lv_obj_set_size(ui_Button_Run_Rule, 150, 40);
  lv_obj_align(ui_Button_Run_Rule, LV_ALIGN_TOP_LEFT, 15, 915); // Shifted down due to larger terminal
  lv_obj_set_style_bg_color(ui_Button_Run_Rule, lv_color_hex(0x00D4FF), 0);
  lv_obj_add_event_cb(ui_Button_Run_Rule, lua_run_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *run_lbl = lv_label_create(ui_Button_Run_Rule);
  lv_label_set_text(run_lbl, "Run Rule");
  lv_obj_center(run_lbl);

  ui_Button_Save_Rule = lv_btn_create(ui_Screen6);
  lv_obj_set_size(ui_Button_Save_Rule, 180, 40);
  lv_obj_align(ui_Button_Save_Rule, LV_ALIGN_TOP_LEFT, 180, 915); // Shifted down due to larger terminal
  lv_obj_set_style_bg_color(ui_Button_Save_Rule, lv_color_hex(0x00FF88), 0);
  lv_obj_add_event_cb(ui_Button_Save_Rule, lua_save_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *save_rule_lbl = lv_label_create(ui_Button_Save_Rule);
  lv_label_set_text(save_rule_lbl, "Save to ESP-Claw");
  lv_obj_set_style_text_color(save_rule_lbl, lv_color_black(), 0);
  lv_obj_center(save_rule_lbl);

  ui_Button_Help_Rule = lv_btn_create(ui_Screen6);
  lv_obj_set_size(ui_Button_Help_Rule, 100, 40);
  lv_obj_align(ui_Button_Help_Rule, LV_ALIGN_TOP_LEFT, 375, 915); // Next to Save button
  lv_obj_set_style_bg_color(ui_Button_Help_Rule, lv_color_hex(0xFFCC00), 0);
  lv_obj_add_event_cb(ui_Button_Help_Rule, lua_help_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *help_rule_lbl = lv_label_create(ui_Button_Help_Rule);
  lv_label_set_text(help_rule_lbl, "Help");
  lv_obj_set_style_text_color(help_rule_lbl, lv_color_black(), 0);
  lv_obj_center(help_rule_lbl);

  ui_Button_GPIO_Map = lv_btn_create(ui_Screen6);
  lv_obj_set_size(ui_Button_GPIO_Map, 130, 40);
  lv_obj_align(ui_Button_GPIO_Map, LV_ALIGN_TOP_LEFT, 490, 915); // Next to Help button
  lv_obj_set_style_bg_color(ui_Button_GPIO_Map, lv_color_hex(0xFF00FF), 0);
  lv_obj_add_event_cb(ui_Button_GPIO_Map, gpio_map_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *gpio_map_lbl = lv_label_create(ui_Button_GPIO_Map);
  lv_label_set_text(gpio_map_lbl, "GPIO Map");
  lv_obj_set_style_text_color(gpio_map_lbl, lv_color_white(), 0);
  lv_obj_center(gpio_map_lbl);

  // Note: Virtual keyboard removed to save memory.
  // Lua code is injected by the AI or edited via text input.

  // Load saved settings and update button states
  ui_Screen6_load_settings();
  ui_Screen6_update_button_states();

  // Add navigation buttons if enabled in settings
  ui_create_standard_navigation_buttons(ui_Screen6);
}

// WiFi status update function removed - WiFi status box no longer on Screen6

// Update AI status text
void ui_Screen6_set_ai_info(const char *text) {
  if (ui_Label_AIInfo) {
    lv_label_set_text(ui_Label_AIInfo, text);
  }
}

// Update Lua Terminal text
void ui_Screen6_set_lua_terminal_text(const char *text) {
  if (ui_TextArea_Lua) {
    lv_textarea_set_text(ui_TextArea_Lua, text);
  }
}

// Destroy Screen6
void ui_Screen6_screen_destroy(void) {
  if (ui_Screen6) {
    lv_obj_del(ui_Screen6);
    ui_Screen6 = NULL;
  }
}

// Load Screen6 settings from the configuration system (which uses SD card)
void ui_Screen6_load_settings(void) {
  demo_mode_enabled = demo_mode_get_enabled();
  screen3_enabled = screen3_get_enabled();
  nav_buttons_enabled = nav_buttons_get_enabled();

  // Load gauge settings (assuming getters exist or adding them)
  system_settings_t *settings = system_settings_get();
  if (settings) {
    show_map = settings->show_map;
    show_wastegate = settings->show_wastegate;
    show_tps = settings->show_tps;
    show_rpm = settings->show_rpm;
    show_boost = settings->show_boost;
    show_tcu = settings->show_tcu;

    show_oil_press = settings->show_oil_press;
    show_oil_temp = settings->show_oil_temp;
    show_water_temp = settings->show_water_temp;
    show_fuel_press = settings->show_fuel_press;
    show_battery = settings->show_battery;

    show_pedal = settings->show_pedal;
    show_wg_pos = settings->show_wg_pos;
    show_bov = settings->show_bov;
    show_tcu_req = settings->show_tcu_req;
    show_tcu_act = settings->show_tcu_act;
    show_eng_req = settings->show_eng_req;

    show_eng_act = settings->show_eng_act;
    show_limit_tq = settings->show_limit_tq;

    // Legacy support: If count is 0 but booleans are true, populate list in
    // default order
    if (settings->active_gauge_count == 0) {
      if (show_map)
        add_gauge_to_list(settings, GAUGE_MAP);
      if (show_wastegate)
        add_gauge_to_list(settings, GAUGE_WASTEGATE);
      if (show_tps)
        add_gauge_to_list(settings, GAUGE_TPS);
      if (show_rpm)
        add_gauge_to_list(settings, GAUGE_RPM);
      if (show_boost)
        add_gauge_to_list(settings, GAUGE_BOOST);
      if (show_tcu)
        add_gauge_to_list(settings, GAUGE_TCU);

      if (show_oil_press)
        add_gauge_to_list(settings, GAUGE_OIL_PRESS);
      if (show_oil_temp)
        add_gauge_to_list(settings, GAUGE_OIL_TEMP);
      if (show_water_temp)
        add_gauge_to_list(settings, GAUGE_WATER_TEMP);
      if (show_fuel_press)
        add_gauge_to_list(settings, GAUGE_FUEL_PRESS);
      if (show_battery)
        add_gauge_to_list(settings, GAUGE_BATTERY);

      if (show_pedal)
        add_gauge_to_list(settings, GAUGE_PEDAL);
      if (show_wg_pos)
        add_gauge_to_list(settings, GAUGE_WG_POS);
      if (show_bov)
        add_gauge_to_list(settings, GAUGE_BOV);
      if (show_tcu_req)
        add_gauge_to_list(settings, GAUGE_TCU_REQ);
      if (show_tcu_act)
        add_gauge_to_list(settings, GAUGE_TCU_ACT);
      if (show_eng_req)
        add_gauge_to_list(settings, GAUGE_ENG_REQ);

      if (show_eng_act)
        add_gauge_to_list(settings, GAUGE_ENG_ACT);
      if (show_limit_tq)
        add_gauge_to_list(settings, GAUGE_LIMIT_TQ);
    }
  }

  ESP_LOGI("SCREEN6", "Loaded settings - Demo: %s, Screen3: %s",
           demo_mode_enabled ? "ON" : "OFF", screen3_enabled ? "ON" : "OFF");
}

// Save Screen6 settings to the SD Card via the configuration system
void ui_Screen6_save_settings(void) {
  demo_mode_set_enabled(demo_mode_enabled);
  screen3_set_enabled(screen3_enabled);
  nav_buttons_set_enabled(nav_buttons_enabled);

  system_settings_t *settings = system_settings_get();
  if (settings) {
    settings->show_map = show_map;
    settings->show_wastegate = show_wastegate;
    settings->show_tps = show_tps;
    settings->show_rpm = show_rpm;
    settings->show_boost = show_boost;
    settings->show_tcu = show_tcu;

    settings->show_oil_press = show_oil_press;
    settings->show_oil_temp = show_oil_temp;
    settings->show_water_temp = show_water_temp;
    settings->show_fuel_press = show_fuel_press;
    settings->show_battery = show_battery;

    settings->show_pedal = show_pedal;
    settings->show_wg_pos = show_wg_pos;
    settings->show_bov = show_bov;
    settings->show_tcu_req = show_tcu_req;
    settings->show_tcu_act = show_tcu_act;
    settings->show_eng_req = show_eng_req;

    settings->show_eng_act = show_eng_act;
    settings->show_limit_tq = show_limit_tq;

    // system_settings_save(settings); // Removed redundant call
  }

  trigger_settings_save(); // Use the non-blocking trigger

  ESP_LOGI("SCREEN6", "Triggered save settings - Demo: %s, Screen3: %s",
           demo_mode_enabled ? "ON" : "OFF", screen3_enabled ? "ON" : "OFF");
}

// Update button states based on loaded settings
void ui_Screen6_update_button_states(void) {
  lv_obj_t *btn;
  lv_obj_t *label;

  // Update Demo Mode button
  btn = (lv_obj_t *)ui_Button_Demo_Mode;
  if (btn) {
    label = lv_obj_get_child(btn, 0);
    if (demo_mode_enabled) {
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x00FF88), 0);
      if (label)
        lv_label_set_text(label, "Demo Mode: ON");
    } else {
      lv_obj_set_style_bg_color(btn, lv_color_hex(0xFF3366), 0);
      if (label)
        lv_label_set_text(label, "Demo Mode: OFF");
    }
  }

  // Update Screen3 button
  btn = (lv_obj_t *)ui_Button_Enable_Screen3;
  if (btn) {
    label = lv_obj_get_child(btn, 0);
    if (screen3_enabled) {
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x00FF88), 0);
      if (label)
        lv_label_set_text(label, "Screen3: ON");
    } else {
      lv_obj_set_style_bg_color(btn, lv_color_hex(0xFF3366), 0);
      if (label)
        lv_label_set_text(label, "Screen3: OFF");
    }
  }

  // Update Nav Buttons button
  btn = (lv_obj_t *)ui_Button_Nav_Buttons;
  if (btn) {
    label = lv_obj_get_child(btn, 0);
    if (nav_buttons_enabled) {
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x00FF88), 0);
      if (label)
        lv_label_set_text(label, "Nav Buttons: ON");
    } else {
      lv_obj_set_style_bg_color(btn, lv_color_hex(0xFF3366), 0);
      if (label)
        lv_label_set_text(label, "Nav Buttons: OFF");
    }
  }

  // Update Gauge Checkboxes
  if (ui_Container_GaugeList) {
    uint32_t child_cnt = lv_obj_get_child_cnt(ui_Container_GaugeList);
    for (uint32_t i = 0; i < child_cnt; i++) {
      lv_obj_t *child = lv_obj_get_child(ui_Container_GaugeList, i);
      if (lv_obj_check_type(child, &lv_checkbox_class)) {
        // ... (Existing Gauge logic remains) ...
        const char *txt = lv_checkbox_get_text(child);
        bool should_check = false;

        if (strcmp(txt, "MAP") == 0)
          should_check = show_map;
        // ... (abbreviated for brevity, tool handles context) ...
        else if (strcmp(txt, "Limit TQ") == 0)
          should_check = show_limit_tq;
        else {
          // Fallback to check full list if abbreviated
          if (strcmp(txt, "Wastegate") == 0)
            should_check = show_wastegate;
          else if (strcmp(txt, "TPS") == 0)
            should_check = show_tps;
          else if (strcmp(txt, "RPM") == 0)
            should_check = show_rpm;
          else if (strcmp(txt, "Boost") == 0)
            should_check = show_boost;
          else if (strcmp(txt, "TCU") == 0)
            should_check = show_tcu;
          else if (strcmp(txt, "Oil Press") == 0)
            should_check = show_oil_press;
          else if (strcmp(txt, "Oil Temp") == 0)
            should_check = show_oil_temp;
          else if (strcmp(txt, "Water Temp") == 0)
            should_check = show_water_temp;
          else if (strcmp(txt, "Fuel Press") == 0)
            should_check = show_fuel_press;
          else if (strcmp(txt, "Battery") == 0)
            should_check = show_battery;
          else if (strcmp(txt, "Pedal") == 0)
            should_check = show_pedal;
          else if (strcmp(txt, "WG Pos") == 0)
            should_check = show_wg_pos;
          else if (strcmp(txt, "BOV") == 0)
            should_check = show_bov;
          else if (strcmp(txt, "TCU Req") == 0)
            should_check = show_tcu_req;
          else if (strcmp(txt, "TCU Act") == 0)
            should_check = show_tcu_act;
          else if (strcmp(txt, "Eng Req") == 0)
            should_check = show_eng_req;
          else if (strcmp(txt, "Eng Act") == 0)
            should_check = show_eng_act;
        }

        if (should_check)
          lv_obj_add_state(child, LV_STATE_CHECKED);
        else
          lv_obj_clear_state(child, LV_STATE_CHECKED);
      }
    }
  }

  // Update Platform Checkboxes
  if (ui_Container_PlatformList) {
    // Use getter
    CanPlatform current_plat = settings_get_can_platform();

    uint32_t child_cnt = lv_obj_get_child_cnt(ui_Container_PlatformList);
    for (uint32_t i = 0; i < child_cnt; i++) {
      lv_obj_t *child = lv_obj_get_child(ui_Container_PlatformList, i);
      if (lv_obj_check_type(child, &lv_checkbox_class)) {
        const char *txt = lv_checkbox_get_text(child);
        bool should_check = false;

        if (strcmp(txt, "VW PQ35/46") == 0 &&
            current_plat == PLATFORM_VW_PQ35_46)
          should_check = true;
        else if (strcmp(txt, "VW PQ25") == 0 &&
                 current_plat == PLATFORM_VW_PQ25)
          should_check = true;
        else if (strcmp(txt, "VW MQB") == 0 && current_plat == PLATFORM_VW_MQB)
          should_check = true;
        else if (strcmp(txt, "BMW Exx") == 0 &&
                 current_plat == PLATFORM_BMW_E9X)
          should_check = true;
        else if (strcmp(txt, "BMW E46") == 0 &&
                 current_plat == PLATFORM_BMW_E46)
          should_check = true;
        else if (strcmp(txt, "BMW Fxx") == 0 &&
                 current_plat == PLATFORM_BMW_F_SERIES)
          should_check = true;

        if (should_check)
          lv_obj_add_state(child, LV_STATE_CHECKED);
        else
          lv_obj_clear_state(child, LV_STATE_CHECKED);
      }
    }
  }

  // Update Voice AI button
  btn = (lv_obj_t *)ui_Button_Voice_AI;
  if (btn) {
    label = lv_obj_get_child(btn, 0);
    if (voice_ai_active) {
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x00FF88), 0);
      if (label)
        lv_label_set_text(label, "Voice AI: ON");
    } else {
      lv_obj_set_style_bg_color(btn, lv_color_hex(0xFF3366), 0);
      if (label)
        lv_label_set_text(label, "Voice AI: OFF");
    }
  }
}

// Save device settings (wrapper)
void ui_save_device_settings(void) {
  ESP_LOGI("SCREEN6", "Saving device settings to SD Card");
  ui_Screen6_save_settings();
}

// =================================================================
// ИСПРАВЛЕННАЯ ФУНКЦИЯ / FIXED FUNCTION
// =================================================================
void ui_reset_device_settings(void) {
  ESP_LOGI("SCREEN6", "Resetting device settings to defaults");

  // 1. Устанавливаем локальные переменные в значения по умолчанию
  // Вместо вызова settings_reset_to_defaults(), который может вызывать
  // блокирующую запись, мы просто меняем состояние здесь и затем вызываем нашу
  // асинхронную функцию сохранения.
  demo_mode_enabled = DEFAULT_DEMO_MODE_ENABLED;
  screen3_enabled = DEFAULT_SCREEN3_ENABLED;
  nav_buttons_enabled = DEFAULT_NAV_BUTTONS_ENABLED;

  // 2. Обновляем внешний вид кнопок на экране, чтобы отразить сброс
  ui_Screen6_update_button_states();

  // 3. Сохраняем эти новые значения по умолчанию на SD-карту асинхронно
  // Эта функция уже вызывает trigger_settings_save() и не блокирует UI.
  ui_Screen6_save_settings();
}

// Update touch cursor position for Screen6
void ui_update_touch_cursor_screen6(void *point) {
  if (ui_Touch_Cursor_Screen6 && point) {
    lv_point_t *p = (lv_point_t *)point;
    lv_obj_set_pos((lv_obj_t *)ui_Touch_Cursor_Screen6, p->x - 15, p->y - 15);
    lv_obj_clear_flag((lv_obj_t *)ui_Touch_Cursor_Screen6, LV_OBJ_FLAG_HIDDEN);
  }
}
