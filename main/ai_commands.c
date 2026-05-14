// AI Commands - Voice command handlers for Gemini Function Calling
#include "ai_commands.h"
#include "cJSON.h"
#include "esp_log.h"
#include "settings_config.h"
#include "ui/ui_screen_manager.h"
#include "ecu_data.h"
#include <stdio.h>
#include <string.h>

// External declarations
extern void board_set_backlight(int percent);
extern void wifi_controller_get_info(void *info);

// Manual LVGL lock declarations
bool example_lvgl_lock(int timeout_ms);
void example_lvgl_unlock(void);

static const char *TAG = "AI_CMD";

// Gauge name to settings mapping
// arc_index matches the order in settings_config (screen1: 0-5, screen2: 0-4)
static const struct {
  const char *name;
  const char *alt_name;
  int screen;    // 1 or 2 for screens with arcs
  int arc_index; // Index in the screen's arc array (-1 if not controllable)
} gauge_map[] = {
    {"MAP", "map", 1, 0},
    {"Wastegate", "wastegate", 1, 1},
    {"TPS", "tps", 1, 2},
    {"RPM", "rpm", 1, 3},
    {"Boost", "boost", 1, 4},
    {"TCU", "tcu", 1, 5},
    {"Oil Press", "oil_press", 2, 0},
    {"Oil Temp", "oil_temp", 2, 1},
    {"Water Temp", "water_temp", 2, 2},
    {"Fuel Press", "fuel_press", 2, 3},
    {"Battery", "battery", 2, 4},
    // Screen 4 and 5 gauges (not controllable via arc API)
    {"Pedal", "pedal", 4, -1},
    {"WG Pos", "wg_pos", 4, -1},
    {"BOV", "bov", 4, -1},
    {"TCU Req", "tcu_req", 4, -1},
    {"TCU Act", "tcu_act", 4, -1},
    {"Eng Req", "eng_req", 4, -1},
    {"Eng Act", "eng_act", 5, -1},
    {"Limit TQ", "limit_tq", 5, -1},
};

ai_cmd_result_t ai_cmd_switch_screen(int screen_number) {
  ai_cmd_result_t result = {.success = false, .message = ""};

  if (screen_number < 1 || screen_number > 6) {
    snprintf(result.message, sizeof(result.message),
             "Ошибка: экран %d не существует. Доступны экраны 1-6.",
             screen_number);
    return result;
  }

  // Convert to screen_id_t (0-indexed internally)
  screen_id_t target = (screen_id_t)(screen_number - 1);

  if (example_lvgl_lock(100)) {
    ui_switch_to_screen(target);
    example_lvgl_unlock();
    result.success = true;
    snprintf(result.message, sizeof(result.message), "Переключено на экран %d",
             screen_number);
    ESP_LOGI(TAG, "Switched to screen %d", screen_number);
  } else {
    snprintf(result.message, sizeof(result.message),
             "Ошибка: не удалось заблокировать UI");
  }

  return result;
}

ai_cmd_result_t ai_cmd_toggle_gauge(const char *gauge_name, bool enable) {
  ai_cmd_result_t result = {.success = false, .message = ""};

  if (!gauge_name) {
    snprintf(result.message, sizeof(result.message),
             "Ошибка: не указано имя датчика");
    return result;
  }

  // Find gauge in map
  bool found = false;
  for (size_t i = 0; i < sizeof(gauge_map) / sizeof(gauge_map[0]); i++) {
    if (strcasecmp(gauge_name, gauge_map[i].name) == 0 ||
        strcasecmp(gauge_name, gauge_map[i].alt_name) == 0) {
      found = true;

      // Check if this gauge is controllable
      if (gauge_map[i].arc_index < 0) {
        snprintf(result.message, sizeof(result.message),
                 "Датчик %s на экране %d не поддерживает управление",
                 gauge_map[i].name, gauge_map[i].screen);
        return result;
      }

      // Actually toggle the gauge using settings API
      if (gauge_map[i].screen == 1) {
        screen1_arc_set_enabled(gauge_map[i].arc_index, enable);
      } else if (gauge_map[i].screen == 2) {
        screen2_arc_set_enabled(gauge_map[i].arc_index, enable);
      }

      // Trigger async settings save
      trigger_settings_save();

      result.success = true;
      snprintf(result.message, sizeof(result.message),
               "Датчик %s %s на экране %d", gauge_map[i].name,
               enable ? "включен" : "выключен", gauge_map[i].screen);
      ESP_LOGI(TAG, "Gauge %s (screen %d, arc %d) set to %s", gauge_map[i].name,
               gauge_map[i].screen, gauge_map[i].arc_index,
               enable ? "ON" : "OFF");
      break;
    }
  }

  if (!found) {
    snprintf(result.message, sizeof(result.message),
             "Ошибка: датчик '%s' не найден. Доступные: MAP, Wastegate, TPS, "
             "RPM, Boost, TCU, Oil Press/Temp, Water Temp, Fuel Press, Battery",
             gauge_name);
  }

  return result;
}

ai_cmd_result_t ai_cmd_search_can_id(uint32_t can_id) {
  ai_cmd_result_t result = {.success = false, .message = ""};

  // This would set a filter in the CAN terminal (Screen 3)
  // For now, just acknowledge the command
  result.success = true;
  snprintf(result.message, sizeof(result.message),
           "Поиск CAN ID 0x%03lX. Перейдите на экран 3 (терминал) для "
           "просмотра результатов.",
           (unsigned long)can_id);
  ESP_LOGI(TAG, "CAN ID search requested: 0x%03lX", (unsigned long)can_id);

  return result;
}

ai_cmd_result_t ai_cmd_set_brightness(int percent) {
  ai_cmd_result_t result = {.success = false, .message = ""};

  if (percent < 0 || percent > 100) {
    snprintf(result.message, sizeof(result.message),
             "Ошибка: яркость должна быть от 0 до 100 процентов");
    return result;
  }

  board_set_backlight(percent);
  result.success = true;
  snprintf(result.message, sizeof(result.message),
           "Яркость установлена на %d%%", percent);
  ESP_LOGI(TAG, "Brightness set to %d%%", percent);

  return result;
}

ai_cmd_result_t ai_cmd_get_status(void) {
  ai_cmd_result_t result = {.success = true, .message = ""};

  // Get WiFi status
  bool demo = demo_mode_get_enabled();
  bool screen3 = screen3_get_enabled();
  bool nav = nav_buttons_get_enabled();

  snprintf(result.message, sizeof(result.message),
           "Статус системы:\n"
           "• Demo режим: %s\n"
           "• Терминал: %s\n"
           "• Кнопки навигации: %s",
           demo ? "ВКЛ" : "ВЫКЛ", screen3 ? "ВКЛ" : "ВЫКЛ",
           nav ? "ВКЛ" : "ВЫКЛ");

  return result;
}

ai_cmd_result_t ai_cmd_toggle_demo_mode(bool enable) {
  ai_cmd_result_t result = {.success = false, .message = ""};

  demo_mode_set_enabled(enable);
  result.success = true;
  snprintf(result.message, sizeof(result.message), "Demo режим %s",
           enable ? "включен" : "выключен");
  ESP_LOGI(TAG, "Demo mode set to %s", enable ? "ON" : "OFF");

  return result;
}

ai_cmd_result_t ai_cmd_save_settings(void) {
  ai_cmd_result_t result = {.success = false, .message = ""};

  trigger_settings_save(); // Async save via background task
  result.success = true;
  snprintf(result.message, sizeof(result.message),
           "Настройки сохраняются на SD карту");

  return result;
}

ai_cmd_result_t ai_cmd_get_ecu_data(void) {
  ai_cmd_result_t result = {.success = true, .message = ""};
  ecu_data_t data;
  ecu_data_get_copy(&data);
  
  snprintf(result.message, sizeof(result.message),
           "Текущие данные автомобиля:\n"
           "Обороты: %.0f об/мин\n"
           "Температура ОЖ: %.0f°C\n"
           "Давление во впуске: %.2f кПа\n"
           "Батарея: %.1f В\n"
           "Скорость: %.0f км/ч",
           data.engine_rpm, data.clt_temp, data.map_kpa, 
           data.battery_voltage, data.vehicle_speed);
  return result;
}

ai_cmd_result_t ai_cmd_click_ui_button(const char *button_name) {
  ai_cmd_result_t result = {.success = false, .message = ""};
  if (!button_name) {
    snprintf(result.message, sizeof(result.message), "Имя кнопки не указано");
    return result;
  }

  screen_id_t target_screen = SCREEN_6; // Default to Settings Screen
  if (strcmp(button_name, "sniffer") == 0 || 
      strcmp(button_name, "clear") == 0 || 
      strcmp(button_name, "record") == 0) {
      target_screen = SCREEN_3; // CAN Terminal
  }

  if (example_lvgl_lock(500)) {
    ui_switch_to_screen(target_screen);
    example_lvgl_unlock();
  }

  // Screen 6 Buttons
  extern lv_obj_t *ui_Button_Help_Rule, *ui_Button_Telegram, *ui_Button_GPIO_Map;
  extern lv_obj_t *ui_Button_Run_Rule, *ui_Button_Save_Rule;
  extern lv_obj_t *ui_Button_Demo_Mode, *ui_Button_Enable_Screen3, *ui_Button_Nav_Buttons;
  extern lv_obj_t *ui_Button_Save_Settings, *ui_Button_Reset_Settings, *ui_Button_Intro_Sound;
  extern lv_obj_t *ui_Button_Wifi, *ui_Button_Voice_AI, *ui_Button_AI;

  // Screen 3 Buttons
  extern lv_obj_t *ui_Button_Sniffer, *ui_Button_Clear, *ui_Button_Record;

  lv_obj_t *target_btn = NULL;
  
  // Screen 6 Mapping
  if (strcmp(button_name, "help") == 0) target_btn = ui_Button_Help_Rule;
  else if (strcmp(button_name, "telegram") == 0) target_btn = ui_Button_Telegram;
  else if (strcmp(button_name, "gpio") == 0) target_btn = ui_Button_GPIO_Map;
  else if (strcmp(button_name, "run_lua") == 0) target_btn = ui_Button_Run_Rule;
  else if (strcmp(button_name, "save_lua") == 0) target_btn = ui_Button_Save_Rule;
  else if (strcmp(button_name, "demo_mode") == 0) target_btn = ui_Button_Demo_Mode;
  else if (strcmp(button_name, "enable_screen3") == 0) target_btn = ui_Button_Enable_Screen3;
  else if (strcmp(button_name, "nav_buttons") == 0) target_btn = ui_Button_Nav_Buttons;
  else if (strcmp(button_name, "save_settings") == 0) target_btn = ui_Button_Save_Settings;
  else if (strcmp(button_name, "reset_settings") == 0) target_btn = ui_Button_Reset_Settings;
  else if (strcmp(button_name, "intro_sound") == 0) target_btn = ui_Button_Intro_Sound;
  else if (strcmp(button_name, "wifi") == 0) target_btn = ui_Button_Wifi;
  else if (strcmp(button_name, "voice_ai") == 0) target_btn = ui_Button_Voice_AI;
  else if (strcmp(button_name, "ai") == 0) target_btn = ui_Button_AI;
  // Screen 3 Mapping
  else if (strcmp(button_name, "sniffer") == 0) target_btn = ui_Button_Sniffer;
  else if (strcmp(button_name, "clear") == 0) target_btn = ui_Button_Clear;
  else if (strcmp(button_name, "record") == 0) target_btn = ui_Button_Record;

  // Special case: clear_lua clears the Lua terminal text (not a physical button)
  if (strcmp(button_name, "clear_lua") == 0) {
      extern void ui_Screen6_set_lua_text(const char *text);
      vTaskDelay(pdMS_TO_TICKS(100));
      if (example_lvgl_lock(500)) {
          ui_Screen6_set_lua_text("");
          example_lvgl_unlock();
          result.success = true;
          snprintf(result.message, sizeof(result.message), "Lua терминал очищен");
      } else {
          snprintf(result.message, sizeof(result.message), "Ошибка UI: не удалось очистить терминал");
      }
      return result;
  }

  if (target_btn) {
      // Delay slightly to let the screen transition finish
      vTaskDelay(pdMS_TO_TICKS(100));
      if (example_lvgl_lock(500)) {
          lv_event_send(target_btn, LV_EVENT_CLICKED, NULL);
          example_lvgl_unlock();
          result.success = true;
          snprintf(result.message, sizeof(result.message), "Кнопка %s нажата", button_name);
      } else {
          snprintf(result.message, sizeof(result.message), "Ошибка UI: не удалось нажать кнопку");
      }
  } else {
      snprintf(result.message, sizeof(result.message), "Кнопка %s не найдена", button_name);
  }

  return result;
}

// Execute function call from Gemini response
ai_cmd_result_t ai_execute_function_call(const char *function_name,
                                         const char *args_json) {
  ai_cmd_result_t result = {.success = false, .message = ""};

  if (!function_name) {
    snprintf(result.message, sizeof(result.message),
             "Ошибка: нет имени функции");
    return result;
  }

  ESP_LOGI(TAG, "Executing function: %s with args: %s", function_name,
           args_json ? args_json : "null");

  cJSON *args = args_json ? cJSON_Parse(args_json) : NULL;

  if (strcmp(function_name, "switch_screen") == 0) {
    int screen_num = 1;
    if (args) {
      cJSON *sn = cJSON_GetObjectItem(args, "screen_number");
      if (sn && cJSON_IsNumber(sn)) {
        screen_num = sn->valueint;
      }
    }
    result = ai_cmd_switch_screen(screen_num);

  } else if (strcmp(function_name, "toggle_gauge") == 0) {
    const char *gauge_name = NULL;
    bool enable = true;
    if (args) {
      cJSON *gn = cJSON_GetObjectItem(args, "gauge_name");
      cJSON *en = cJSON_GetObjectItem(args, "enable");
      if (gn && cJSON_IsString(gn))
        gauge_name = gn->valuestring;
      if (en)
        enable = cJSON_IsTrue(en);
    }
    result = ai_cmd_toggle_gauge(gauge_name, enable);

  } else if (strcmp(function_name, "search_can_id") == 0) {
    uint32_t can_id = 0;
    if (args) {
      cJSON *id = cJSON_GetObjectItem(args, "can_id");
      if (id && cJSON_IsNumber(id)) {
        can_id = (uint32_t)id->valueint;
      } else if (id && cJSON_IsString(id)) {
        can_id = strtoul(id->valuestring, NULL, 0);
      }
    }
    result = ai_cmd_search_can_id(can_id);

  } else if (strcmp(function_name, "set_brightness") == 0) {
    int percent = 50;
    if (args) {
      cJSON *p = cJSON_GetObjectItem(args, "percent");
      if (p && cJSON_IsNumber(p)) {
        percent = p->valueint;
      }
    }
    result = ai_cmd_set_brightness(percent);

  } else if (strcmp(function_name, "get_status") == 0) {
    result = ai_cmd_get_status();

  } else if (strcmp(function_name, "toggle_demo_mode") == 0) {
    bool enable = true;
    if (args) {
      cJSON *en = cJSON_GetObjectItem(args, "enable");
      if (en)
        enable = cJSON_IsTrue(en);
    }
    result = ai_cmd_toggle_demo_mode(enable);

  } else if (strcmp(function_name, "save_settings") == 0) {
    result = ai_cmd_save_settings();

  } else if (strcmp(function_name, "get_ecu_data") == 0) {
    result = ai_cmd_get_ecu_data();

  } else if (strcmp(function_name, "click_ui_button") == 0) {
    const char *btn = NULL;
    if (args) {
      cJSON *b = cJSON_GetObjectItem(args, "button_name");
      if (b && cJSON_IsString(b)) btn = b->valuestring;
    }
    result = ai_cmd_click_ui_button(btn);

  } else {
    snprintf(result.message, sizeof(result.message), "Неизвестная команда: %s",
             function_name);
  }

  if (args)
    cJSON_Delete(args);

  return result;
}
