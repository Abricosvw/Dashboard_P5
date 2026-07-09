// Settings Configuration Implementation
#include "settings_config.h"
#include "can_parser.h" // For can_parser_set_platform
#include "ecu_data.h"
#include "sd_card_manager.h" // Use P4 SD manager
#include <esp_log.h>
#include <nvs_flash.h>
#include <stdio.h>

#include "../background_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <nvs.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "SETTINGS_CONFIG";
#define NVS_NAMESPACE "settings"
static touch_settings_t current_settings;

// SD card access protection (Thread safety for file I/O)
static SemaphoreHandle_t sd_card_mutex = NULL;

// Helper to serialize settings to a JSON string
static void settings_to_json(const touch_settings_t *settings, char *buffer,
                             size_t buffer_size) {
  system_settings_t *sys_settings = system_settings_get();

  // Format the base fields
  int len = snprintf(buffer, buffer_size,
           "{\"sensitivity\":%d,\"demo_mode\":%s,\"screen3_enabled\":%s,\"nav_"
           "buttons_enabled\":%s,"
           "\"show_map\":%s,\"show_wastegate\":%s,\"show_tps\":%s,\"show_rpm\":"
           "%s,\"show_boost\":%s,\"show_tcu\":%s,"
           "\"show_oil_press\":%s,\"show_oil_temp\":%s,\"show_water_temp\":%s,"
           "\"show_fuel_press\":%s,\"show_battery\":%s,"
           "\"show_pedal\":%s,\"show_wg_pos\":%s,\"show_bov\":%s,\"show_tcu_"
           "req\":%s,\"show_tcu_act\":%s,\"show_eng_req\":%s,"
           "\"show_eng_act\":%s,\"show_limit_tq\":%s,"
           "\"show_iat\":%s,\"show_speed\":%s,\"show_trans_temp\":%s,"
           "\"show_afr\":%s,\"show_egt\":%s,\"show_knock_retard\":%s,\"show_boost_act\":%s,"
           "\"show_ambient_temp\":%s,\"show_mre_map\":%s,\"show_mre_wastegate\":%s,\"mre_parallel\":%s,\"send_ambient_temp_to_can\":%s,\"ambient_can_temp\":%.1f,"
           "\"can_platform\":%d,"
           "\"boot_sound_path\":\"%s\","
           "\"diag_address\":%d,\"diag_protocol\":%d,"
           "\"diag_part_number\":\"%s\",\"diag_comp_name\":\"%s\","
           "\"diag_hw_number\":\"%s\",\"diag_sw_version\":\"%s\","
           "\"diag_vin\":\"%s\",\"diag_coding\":%lu,",
           settings->touch_sensitivity_level,
           settings->demo_mode_enabled ? "true" : "false",
           settings->screen3_enabled ? "true" : "false",
           settings->nav_buttons_enabled ? "true" : "false",
           sys_settings->show_map ? "true" : "false",
           sys_settings->show_wastegate ? "true" : "false",
           sys_settings->show_tps ? "true" : "false",
           sys_settings->show_rpm ? "true" : "false",
           sys_settings->show_boost ? "true" : "false",
           sys_settings->show_tcu ? "true" : "false",
           sys_settings->show_oil_press ? "true" : "false",
           sys_settings->show_oil_temp ? "true" : "false",
           sys_settings->show_water_temp ? "true" : "false",
           sys_settings->show_fuel_press ? "true" : "false",
           sys_settings->show_battery ? "true" : "false",
           sys_settings->show_pedal ? "true" : "false",
           sys_settings->show_wg_pos ? "true" : "false",
           sys_settings->show_bov ? "true" : "false",
           sys_settings->show_tcu_req ? "true" : "false",
           sys_settings->show_tcu_act ? "true" : "false",
           sys_settings->show_eng_req ? "true" : "false",
           sys_settings->show_eng_act ? "true" : "false",
           sys_settings->show_limit_tq ? "true" : "false",
           sys_settings->show_iat ? "true" : "false",
           sys_settings->show_speed ? "true" : "false",
           sys_settings->show_trans_temp ? "true" : "false",
           sys_settings->show_afr ? "true" : "false",
           sys_settings->show_egt ? "true" : "false",
           sys_settings->show_knock_retard ? "true" : "false",
           sys_settings->show_boost_act ? "true" : "false",
           sys_settings->show_ambient_temp ? "true" : "false",
           sys_settings->show_mre_map ? "true" : "false",
           sys_settings->show_mre_wastegate ? "true" : "false",
           settings->mre_parallel ? "true" : "false",
           sys_settings->send_ambient_temp_to_can ? "true" : "false",
           sys_settings->ambient_can_temp,
           settings->can_platform, settings->boot_sound_path,
           settings->diag_address, settings->diag_protocol,
           settings->diag_part_number, settings->diag_comp_name,
           settings->diag_hw_number, settings->diag_sw_version,
           settings->diag_vin, (unsigned long)settings->diag_coding);

  // Append Screen 9 and Screen 10 settings and maps
  if (len > 0 && (size_t)len < buffer_size) {
    char *p = buffer + len;
    size_t rem = buffer_size - len;
    int w = snprintf(p, rem,
      "\"pump_is_auto\":%s,\"pump_manual_on\":%s,\"pump_manual_speed\":%d,"
      "\"fan_is_auto\":%s,\"fan_manual_on\":%s,\"fan_manual_speed\":%d,"
      "\"wg_is_auto\":%s,\"wg_manual_pos\":%d,\"wg_is_inverted\":%s,"
      "\"bov_is_auto\":%s,\"bov_manual_open\":%s,\"bov_tps_threshold\":%d,"
      "\"bov_press_threshold\":%d,\"bov_open_duration\":%d,\"bov_stat_enabled\":%s,"
      "\"bov_stat_ratio\":%d,",
      settings->pump_is_auto ? "true" : "false",
      settings->pump_manual_on ? "true" : "false",
      settings->pump_manual_speed,
      settings->fan_is_auto ? "true" : "false",
      settings->fan_manual_on ? "true" : "false",
      settings->fan_manual_speed,
      settings->wg_is_auto ? "true" : "false",
      settings->wg_manual_pos,
      settings->wg_is_inverted ? "true" : "false",
      settings->bov_is_auto ? "true" : "false",
      settings->bov_manual_open ? "true" : "false",
      settings->bov_tps_threshold,
      settings->bov_press_threshold,
      settings->bov_open_duration,
      settings->bov_stat_enabled ? "true" : "false",
      settings->bov_stat_ratio
    );
    if (w > 0 && (size_t)w < rem) {
      len += w;
    }
  }

  if (len > 0 && (size_t)len < buffer_size) {
    char *p = buffer + len;
    size_t rem = buffer_size - len;
    int w = snprintf(p, rem, "\"pump_map_temp\":[");
    if (w > 0 && (size_t)w < rem) {
      p += w; rem -= w; len += w;
      for (int i = 0; i < 10; i++) {
        w = snprintf(p, rem, "%d%s", settings->pump_map_temp[i], (i == 9) ? "" : ",");
        if (w > 0 && (size_t)w < rem) { p += w; rem -= w; len += w; } else { break; }
      }
      w = snprintf(p, rem, "],\"pump_map_speed\":[");
      if (w > 0 && (size_t)w < rem) {
        p += w; rem -= w; len += w;
        for (int i = 0; i < 10; i++) {
          w = snprintf(p, rem, "%d%s", settings->pump_map_speed[i], (i == 9) ? "" : ",");
          if (w > 0 && (size_t)w < rem) { p += w; rem -= w; len += w; } else { break; }
        }
        w = snprintf(p, rem, "],\"fan_map_temp\":[");
        if (w > 0 && (size_t)w < rem) {
          p += w; rem -= w; len += w;
          for (int i = 0; i < 10; i++) {
            w = snprintf(p, rem, "%d%s", settings->fan_map_temp[i], (i == 9) ? "" : ",");
            if (w > 0 && (size_t)w < rem) { p += w; rem -= w; len += w; } else { break; }
          }
          w = snprintf(p, rem, "],\"fan_map_speed\":[");
          if (w > 0 && (size_t)w < rem) {
            p += w; rem -= w; len += w;
            for (int i = 0; i < 10; i++) {
              w = snprintf(p, rem, "%d%s", settings->fan_map_speed[i], (i == 9) ? "" : ",");
              if (w > 0 && (size_t)w < rem) { p += w; rem -= w; len += w; } else { break; }
            }
            w = snprintf(p, rem, "],");
            if (w > 0 && (size_t)w < rem) { len += w; }
          }
        }
      }
    }
  }

  // Now append units and sources arrays: "units":[...],"sources":[...]}
  if (len > 0 && (size_t)len < buffer_size) {
    char *p = buffer + len;
    size_t rem = buffer_size - len;
    int w = snprintf(p, rem, "\"units\":[");
    if (w > 0 && (size_t)w < rem) {
      p += w;
      rem -= w;
      for (int i = 0; i < GAUGE_MAX; i++) {
        w = snprintf(p, rem, "%d%s", settings->gauge_units[i], (i == GAUGE_MAX - 1) ? "" : ",");
        if (w > 0 && (size_t)w < rem) {
          p += w;
          rem -= w;
        } else {
          break;
        }
      }
      w = snprintf(p, rem, "],\"sources\":[");
      if (w > 0 && (size_t)w < rem) {
        p += w;
        rem -= w;
        for (int i = 0; i < GAUGE_MAX; i++) {
          w = snprintf(p, rem, "%d%s", settings->gauge_sources[i], (i == GAUGE_MAX - 1) ? "" : ",");
          if (w > 0 && (size_t)w < rem) {
            p += w;
            rem -= w;
          } else {
            break;
          }
        }
        snprintf(p, rem, "]}");
      }
    }
  }
}

static void parse_int_array(const char *json_str, const char *key, int *target, int size) {
  char *p = strstr(json_str, key);
  if (p) {
    p = strchr(p, '[');
    if (p) {
      p++;
      for (int i = 0; i < size; i++) {
        target[i] = atoi(p);
        p = strchr(p, ',');
        if (!p) break;
        p++;
      }
    }
  }
}

// Helper to deserialize settings from a JSON string
static bool settings_from_json(const char *json_str,
                               touch_settings_t *settings) {
  system_settings_t *sys_settings = system_settings_get();

  // This is a very basic parser. A real implementation should use a JSON
  // library like cJSON.
  const char *sens_key = "\"sensitivity\":";
  const char *demo_key = "\"demo_mode\":";
  const char *s3_key = "\"screen3_enabled\":";
  const char *nav_key = "\"nav_buttons_enabled\":";
  const char *platform_key = "\"can_platform\":";

  char *sens_ptr = strstr(json_str, sens_key);
  char *demo_ptr = strstr(json_str, demo_key);
  char *s3_ptr = strstr(json_str, s3_key);
  char *nav_ptr = strstr(json_str, nav_key);
  char *platform_ptr = strstr(json_str, platform_key);

  if (sens_ptr && demo_ptr && s3_ptr) {
    // Parse sensitivity
    settings->touch_sensitivity_level = atoi(sens_ptr + strlen(sens_key));

    // Parse demo_mode
    char *demo_val_ptr = demo_ptr + strlen(demo_key);
    settings->demo_mode_enabled = (strncmp(demo_val_ptr, "true", 4) == 0);

    // Parse screen3_enabled
    char *s3_val_ptr = s3_ptr + strlen(s3_key);
    settings->screen3_enabled = (strncmp(s3_val_ptr, "true", 4) == 0);

    // Parse nav_buttons_enabled
    if (nav_ptr) {
      char *nav_val_ptr = nav_ptr + strlen(nav_key);
      settings->nav_buttons_enabled = (strncmp(nav_val_ptr, "true", 4) == 0);
    } else {
      settings->nav_buttons_enabled = DEFAULT_NAV_BUTTONS_ENABLED;
    }

    // Parse can_platform
    if (platform_ptr) {
      settings->can_platform = atoi(platform_ptr + strlen(platform_key));
    } else {
      settings->can_platform = DEFAULT_CAN_PLATFORM;
    }

// Parse Gauge Visibility Settings
// Helper macro for boolean parsing
#define PARSE_BOOL(key, target)                                                \
  do {                                                                         \
    const char *k = "\"" key "\":";                                            \
    char *p = strstr(json_str, k);                                             \
    if (p) {                                                                   \
      char *v = p + strlen(k);                                                 \
      target = (strncmp(v, "true", 4) == 0);                                   \
    }                                                                          \
  } while (0)

#define PARSE_INT(key, target)                                                 \
  do {                                                                         \
    const char *k = "\"" key "\":";                                            \
    char *p = strstr(json_str, k);                                             \
    if (p) {                                                                   \
      target = atoi(p + strlen(k));                                            \
    }                                                                          \
  } while (0)

    PARSE_BOOL("show_map", sys_settings->show_map);
    PARSE_BOOL("show_wastegate", sys_settings->show_wastegate);
    PARSE_BOOL("show_tps", sys_settings->show_tps);
    PARSE_BOOL("show_rpm", sys_settings->show_rpm);
    PARSE_BOOL("show_boost", sys_settings->show_boost);
    PARSE_BOOL("show_tcu", sys_settings->show_tcu);

    PARSE_BOOL("show_oil_press", sys_settings->show_oil_press);
    PARSE_BOOL("show_oil_temp", sys_settings->show_oil_temp);
    PARSE_BOOL("show_water_temp", sys_settings->show_water_temp);
    PARSE_BOOL("show_fuel_press", sys_settings->show_fuel_press);
    PARSE_BOOL("show_battery", sys_settings->show_battery);

    PARSE_BOOL("show_pedal", sys_settings->show_pedal);
    PARSE_BOOL("show_wg_pos", sys_settings->show_wg_pos);
    PARSE_BOOL("show_bov", sys_settings->show_bov);
    PARSE_BOOL("show_tcu_req", sys_settings->show_tcu_req);
    PARSE_BOOL("show_tcu_act", sys_settings->show_tcu_act);
    PARSE_BOOL("show_eng_req", sys_settings->show_eng_req);

    PARSE_BOOL("show_eng_act", sys_settings->show_eng_act);
    PARSE_BOOL("show_limit_tq", sys_settings->show_limit_tq);
    PARSE_BOOL("show_iat", sys_settings->show_iat);
    PARSE_BOOL("show_speed", sys_settings->show_speed);
    PARSE_BOOL("show_trans_temp", sys_settings->show_trans_temp);
    PARSE_BOOL("show_afr", sys_settings->show_afr);
    PARSE_BOOL("show_egt", sys_settings->show_egt);
    PARSE_BOOL("show_boost_act", sys_settings->show_boost_act);
    PARSE_BOOL("show_ambient_temp", sys_settings->show_ambient_temp);
    PARSE_BOOL("show_mre_map", sys_settings->show_mre_map);
    PARSE_BOOL("show_mre_wastegate", sys_settings->show_mre_wastegate);
    PARSE_BOOL("mre_parallel", sys_settings->mre_parallel);
    PARSE_BOOL("send_ambient_temp_to_can", sys_settings->send_ambient_temp_to_can);

    const char *amb_temp_key = "\"ambient_can_temp\":";
    char *amb_temp_ptr = strstr(json_str, amb_temp_key);
    if (amb_temp_ptr) {
      sys_settings->ambient_can_temp = atof(amb_temp_ptr + strlen(amb_temp_key));
    } else {
      sys_settings->ambient_can_temp = 20.0f; // default 20C
    }

    // Sync to touch_settings_t
    settings->show_iat = sys_settings->show_iat;
    settings->show_speed = sys_settings->show_speed;
    settings->show_trans_temp = sys_settings->show_trans_temp;
    settings->show_afr = sys_settings->show_afr;
    settings->show_egt = sys_settings->show_egt;
    settings->show_knock_retard = sys_settings->show_knock_retard;
    settings->show_boost_act = sys_settings->show_boost_act;
    settings->show_ambient_temp = sys_settings->show_ambient_temp;
    settings->show_mre_map = sys_settings->show_mre_map;
    settings->show_mre_wastegate = sys_settings->show_mre_wastegate;
    settings->mre_parallel = sys_settings->mre_parallel;
    settings->send_ambient_temp_to_can = sys_settings->send_ambient_temp_to_can;
    settings->ambient_can_temp = sys_settings->ambient_can_temp;

    // Parse Screen 9 (Pump & Fan) Settings
    PARSE_BOOL("pump_is_auto", settings->pump_is_auto);
    PARSE_BOOL("pump_manual_on", settings->pump_manual_on);
    PARSE_INT("pump_manual_speed", settings->pump_manual_speed);
    PARSE_BOOL("fan_is_auto", settings->fan_is_auto);
    PARSE_BOOL("fan_manual_on", settings->fan_manual_on);
    PARSE_INT("fan_manual_speed", settings->fan_manual_speed);
    parse_int_array(json_str, "\"pump_map_temp\":", settings->pump_map_temp, 10);
    parse_int_array(json_str, "\"pump_map_speed\":", settings->pump_map_speed, 10);
    parse_int_array(json_str, "\"fan_map_temp\":", settings->fan_map_temp, 10);
    parse_int_array(json_str, "\"fan_map_speed\":", settings->fan_map_speed, 10);

    // Parse Screen 10 (Wastegate & BOV) Settings
    PARSE_BOOL("wg_is_auto", settings->wg_is_auto);
    PARSE_INT("wg_manual_pos", settings->wg_manual_pos);
    PARSE_BOOL("wg_is_inverted", settings->wg_is_inverted);
    PARSE_BOOL("bov_is_auto", settings->bov_is_auto);
    PARSE_BOOL("bov_manual_open", settings->bov_manual_open);
    PARSE_INT("bov_tps_threshold", settings->bov_tps_threshold);
    PARSE_INT("bov_press_threshold", settings->bov_press_threshold);
    PARSE_INT("bov_open_duration", settings->bov_open_duration);
    PARSE_BOOL("bov_stat_enabled", settings->bov_stat_enabled);
    PARSE_INT("bov_stat_ratio", settings->bov_stat_ratio);

#undef PARSE_BOOL
#undef PARSE_INT

    // Parse units array
    const char *units_key = "\"units\":[";
    char *units_ptr = strstr(json_str, units_key);
    if (units_ptr) {
      char *p = units_ptr + strlen(units_key);
      for (int i = 0; i < GAUGE_MAX; i++) {
        settings->gauge_units[i] = atoi(p);
        sys_settings->gauge_units[i] = settings->gauge_units[i];
        p = strchr(p, ',');
        if (p) p++;
        else break;
      }
    }

    // Parse sources array
    const char *sources_key = "\"sources\":[";
    char *sources_ptr = strstr(json_str, sources_key);
    if (sources_ptr) {
      char *p = sources_ptr + strlen(sources_key);
      for (int i = 0; i < GAUGE_MAX; i++) {
        settings->gauge_sources[i] = atoi(p);
        sys_settings->gauge_sources[i] = settings->gauge_sources[i];
        p = strchr(p, ',');
        if (p) p++;
        else break;
      }
    } else {
      // Default to AUTO (0)
      for (int i = 0; i < GAUGE_MAX; i++) {
        settings->gauge_sources[i] = 0;
        sys_settings->gauge_sources[i] = 0;
      }
    }

    // Parse boot_sound_path
    const char *sound_key = "\"boot_sound_path\":\"";
    char *sound_ptr = strstr(json_str, sound_key);
    if (sound_ptr) {
      char *val_start = sound_ptr + strlen(sound_key);
      char *val_end = strchr(val_start, '\"');
      if (val_end) {
        size_t len = val_end - val_start;
        if (len < sizeof(settings->boot_sound_path)) {
          strncpy(settings->boot_sound_path, val_start, len);
          settings->boot_sound_path[len] = '\0';
        }
      }
    } else {
      strncpy(settings->boot_sound_path, DEFAULT_BOOT_SOUND_PATH,
              sizeof(settings->boot_sound_path));
    }

    // Parse VAG Diagnostic Emulation Settings
    const char *diag_addr_key = "\"diag_address\":";
    char *diag_addr_ptr = strstr(json_str, diag_addr_key);
    if (diag_addr_ptr) {
      settings->diag_address = atoi(diag_addr_ptr + strlen(diag_addr_key));
    } else {
      settings->diag_address = 0x13; // default Address 13 (ACC / Auto Distance Regulation)
    }
    sys_settings->diag_address = settings->diag_address;

    const char *diag_proto_key = "\"diag_protocol\":";
    char *diag_proto_ptr = strstr(json_str, diag_proto_key);
    if (diag_proto_ptr) {
      settings->diag_protocol = atoi(diag_proto_ptr + strlen(diag_proto_key));
    } else {
      settings->diag_protocol = 3; // default Both
    }
    sys_settings->diag_protocol = settings->diag_protocol;

    const char *part_key = "\"diag_part_number\":\"";
    char *part_ptr = strstr(json_str, part_key);
    if (part_ptr) {
      char *val_start = part_ptr + strlen(part_key);
      char *val_end = strchr(val_start, '\"');
      if (val_end) {
        size_t len = val_end - val_start;
        if (len < sizeof(settings->diag_part_number)) {
          strncpy(settings->diag_part_number, val_start, len);
          settings->diag_part_number[len] = '\0';
        }
      }
    } else {
      strcpy(settings->diag_part_number, "P5-DASHBOARD");
    }
    strcpy(sys_settings->diag_part_number, settings->diag_part_number);

    const char *comp_key = "\"diag_comp_name\":\"";
    char *comp_ptr = strstr(json_str, comp_key);
    if (comp_ptr) {
      char *val_start = comp_ptr + strlen(comp_key);
      char *val_end = strchr(val_start, '\"');
      if (val_end) {
        size_t len = val_end - val_start;
        if (len < sizeof(settings->diag_comp_name)) {
          strncpy(settings->diag_comp_name, val_start, len);
          settings->diag_comp_name[len] = '\0';
        }
      }
    } else {
      strcpy(settings->diag_comp_name, "Dashboard P5  H01 0100");
    }
    strcpy(sys_settings->diag_comp_name, settings->diag_comp_name);

    const char *hw_key = "\"diag_hw_number\":\"";
    char *hw_ptr = strstr(json_str, hw_key);
    if (hw_ptr) {
      char *val_start = hw_ptr + strlen(hw_key);
      char *val_end = strchr(val_start, '\"');
      if (val_end) {
        size_t len = val_end - val_start;
        if (len < sizeof(settings->diag_hw_number)) {
          strncpy(settings->diag_hw_number, val_start, len);
          settings->diag_hw_number[len] = '\0';
        }
      }
    } else {
      strcpy(settings->diag_hw_number, "P5-DASHBOARD");
    }
    strcpy(sys_settings->diag_hw_number, settings->diag_hw_number);

    const char *sw_ver_key = "\"diag_sw_version\":\"";
    char *sw_ver_ptr = strstr(json_str, sw_ver_key);
    if (sw_ver_ptr) {
      char *val_start = sw_ver_ptr + strlen(sw_ver_key);
      char *val_end = strchr(val_start, '\"');
      if (val_end) {
        size_t len = val_end - val_start;
        if (len < sizeof(settings->diag_sw_version)) {
          strncpy(settings->diag_sw_version, val_start, len);
          settings->diag_sw_version[len] = '\0';
        }
      }
    } else {
      strcpy(settings->diag_sw_version, "0100");
    }
    strcpy(sys_settings->diag_sw_version, settings->diag_sw_version);

    const char *vin_key = "\"diag_vin\":\"";
    char *vin_ptr = strstr(json_str, vin_key);
    if (vin_ptr) {
      char *val_start = vin_ptr + strlen(vin_key);
      char *val_end = strchr(val_start, '\"');
      if (val_end) {
        size_t len = val_end - val_start;
        if (len < sizeof(settings->diag_vin)) {
          strncpy(settings->diag_vin, val_start, len);
          settings->diag_vin[len] = '\0';
        }
      }
    } else {
      strcpy(settings->diag_vin, "1VWBP7A39DC091177");
    }
    strcpy(sys_settings->diag_vin, settings->diag_vin);

    const char *coding_key = "\"diag_coding\":";
    char *coding_ptr = strstr(json_str, coding_key);
    if (coding_ptr) {
      settings->diag_coding = strtoul(coding_ptr + strlen(coding_key), NULL, 10);
    } else {
      settings->diag_coding = 1; // default coding 1
    }
    sys_settings->diag_coding = settings->diag_coding;

    return true;
  }
  return false;
}

void settings_init_defaults(touch_settings_t *settings) {
  if (settings == NULL)
    settings = &current_settings;
  settings->touch_sensitivity_level = DEFAULT_TOUCH_SENSITIVITY;
  settings->demo_mode_enabled = DEFAULT_DEMO_MODE_ENABLED;
  settings->screen3_enabled = DEFAULT_SCREEN3_ENABLED;
  settings->nav_buttons_enabled = true; // FORCE ON FOR DEBUGGING
  settings->can_platform = DEFAULT_CAN_PLATFORM;
  strncpy(settings->boot_sound_path, DEFAULT_BOOT_SOUND_PATH,
          sizeof(settings->boot_sound_path));
  for (int i = 0; i < SCREEN1_ARCS_COUNT; i++)
    settings->screen1_arcs_enabled[i] = true;
  for (int i = 0; i < SCREEN2_ARCS_COUNT; i++)
    settings->screen2_arcs_enabled[i] = true;

  // New gauges visibility defaults
  settings->show_iat = true;
  settings->show_speed = true;
  settings->show_trans_temp = true;
  settings->show_afr = true;
  settings->show_egt = true;
  settings->show_boost_act = true;
  settings->show_ambient_temp = true;
  settings->show_mre_map = true;
  settings->show_mre_wastegate = true;
  settings->mre_parallel = false;
  settings->send_ambient_temp_to_can = false;
  settings->ambient_can_temp = 20.0f;

  // Screen 9 Pump & Fan defaults
  settings->pump_is_auto = true;
  settings->pump_manual_on = false;
  settings->pump_manual_speed = 50;
  settings->fan_is_auto = true;
  settings->fan_manual_on = false;
  settings->fan_manual_speed = 50;
  int def_pump_map_temp[10] = {20, 30, 40, 50, 60, 70, 80, 90, 100, 110};
  int def_pump_map_speed[10] = {0, 10, 30, 50, 70, 90, 100, 100, 100, 100};
  int def_fan_map_temp[10] = {30, 40, 50, 60, 70, 80, 90, 100, 110, 120};
  int def_fan_map_speed[10] = {0, 0, 20, 40, 60, 80, 100, 100, 100, 100};
  memcpy(settings->pump_map_temp, def_pump_map_temp, sizeof(settings->pump_map_temp));
  memcpy(settings->pump_map_speed, def_pump_map_speed, sizeof(settings->pump_map_speed));
  memcpy(settings->fan_map_temp, def_fan_map_temp, sizeof(settings->fan_map_temp));
  memcpy(settings->fan_map_speed, def_fan_map_speed, sizeof(settings->fan_map_speed));

  // Screen 10 Wastegate & BOV defaults
  settings->wg_is_auto = true;
  settings->wg_manual_pos = 50;
  settings->wg_is_inverted = false;
  settings->bov_is_auto = true;
  settings->bov_manual_open = false;
  settings->bov_tps_threshold = 25;
  settings->bov_press_threshold = 35;
  settings->bov_open_duration = 20;
  settings->bov_stat_enabled = true;
  settings->bov_stat_ratio = 120;

  // Initialize gauge units defaults
  for (int i = 0; i < 32; i++) {
    settings->gauge_units[i] = 0;
    settings->gauge_sources[i] = 0; // default to AUTO (0)
  }
  settings->gauge_units[GAUGE_MAP] = UNIT_KPA;
  settings->gauge_units[GAUGE_BOOST] = UNIT_KPA;
  settings->gauge_units[GAUGE_BOOST_ACT] = UNIT_KPA;
  settings->gauge_units[GAUGE_MRE_MAP] = UNIT_KPA;
  settings->gauge_units[GAUGE_MRE_WASTEGATE] = UNIT_PCT;
  settings->gauge_units[GAUGE_OIL_PRESS] = UNIT_KPA;
  settings->gauge_units[GAUGE_FUEL_PRESS] = UNIT_KPA;
  settings->gauge_units[GAUGE_AFR] = UNIT_LAMBDA;
  settings->gauge_units[GAUGE_OIL_TEMP] = UNIT_CELSIUS;
  settings->gauge_units[GAUGE_WATER_TEMP] = UNIT_CELSIUS;
  settings->gauge_units[GAUGE_IAT] = UNIT_CELSIUS;
  settings->gauge_units[GAUGE_TRANS_TEMP] = UNIT_CELSIUS;
  settings->gauge_units[GAUGE_EGT] = UNIT_CELSIUS;
  settings->gauge_units[GAUGE_SPEED] = UNIT_KMH;
  settings->gauge_units[GAUGE_TCU_REQ] = UNIT_NM;
  settings->gauge_units[GAUGE_TCU_ACT] = UNIT_NM;
  settings->gauge_units[GAUGE_ENG_REQ] = UNIT_NM;
  settings->gauge_units[GAUGE_ENG_ACT] = UNIT_NM;
  settings->gauge_units[GAUGE_LIMIT_TQ] = UNIT_NM;
  settings->gauge_units[GAUGE_AMBIENT_TEMP] = UNIT_CELSIUS;

  // Default diagnostic settings (VAG ACC Emulation)
  settings->diag_address = 0x13; // Address 13 (ACC / Auto Distance Regulation Control Module)
  settings->diag_protocol = 3;   // Both UDS and KWP2000 over TP2.0
  strcpy(settings->diag_part_number, "P5-DASHBOARD");
  strcpy(settings->diag_comp_name, "Dashboard P5  H01 0100");
  strcpy(settings->diag_hw_number, "P5-DASHBOARD");
  strcpy(settings->diag_sw_version, "0100");
  strcpy(settings->diag_vin, "1VWBP7A39DC091177");
  settings->diag_coding = 1;      // default coding 1

  // Sync with sys_settings
  system_settings_t *sys_settings = system_settings_get();
  if (sys_settings) {
    sys_settings->show_iat = true;
    sys_settings->show_speed = true;
    sys_settings->show_trans_temp = true;
    sys_settings->show_afr = true;
    sys_settings->show_egt = true;
    sys_settings->show_knock_retard = true;
    sys_settings->show_boost_act = true;
    sys_settings->show_ambient_temp = true;
    sys_settings->show_mre_map = true;
    sys_settings->show_mre_wastegate = true;
    sys_settings->mre_parallel = false;
    sys_settings->send_ambient_temp_to_can = false;
    sys_settings->ambient_can_temp = 20.0f;
    for (int i = 0; i < 32; i++) {
      sys_settings->gauge_units[i] = settings->gauge_units[i];
      sys_settings->gauge_sources[i] = settings->gauge_sources[i];
    }
    sys_settings->diag_address = settings->diag_address;
    sys_settings->diag_protocol = settings->diag_protocol;
    strcpy(sys_settings->diag_part_number, settings->diag_part_number);
    strcpy(sys_settings->diag_comp_name, settings->diag_comp_name);
    strcpy(sys_settings->diag_hw_number, settings->diag_hw_number);
    strcpy(sys_settings->diag_sw_version, settings->diag_sw_version);
    strcpy(sys_settings->diag_vin, settings->diag_vin);
    sys_settings->diag_coding = settings->diag_coding;
  }

  ESP_LOGI(TAG,
           "Initialized default settings: Demo=%s, Screen3=%s, NavButtons=%s, "
           "Sensitivity=%d",
           settings->demo_mode_enabled ? "ON" : "OFF",
           settings->screen3_enabled ? "ON" : "OFF",
           settings->nav_buttons_enabled ? "ON" : "OFF",
           settings->touch_sensitivity_level);
}

/**
 * @brief Saves the provided settings struct to the SD card.
 * This is a slow, blocking function and should only be called from a background
 * task or during initial setup.
 * @param settings_to_save A pointer to the settings struct to save.
 */
void settings_save(const touch_settings_t *settings_to_save) {
  if (settings_to_save == NULL) {
    ESP_LOGE(TAG, "settings_save called with NULL data!");
    return;
  }

  // Check if SD card is mounted
  if (!sd_card_is_mounted()) {
    ESP_LOGW(TAG, "SD card not mounted, skipping save.");
    return;
  }

  // Save to SD Card as JSON
  char *json_buffer = malloc(2048);
  if (!json_buffer) {
    ESP_LOGE(TAG, "Failed to allocate memory for JSON serialization");
    return;
  }
  settings_to_json(settings_to_save, json_buffer, 2048);

  ESP_LOGI(TAG, "Attempting to save settings to SD card...");

  // Take mutex before SD card access
  if (sd_card_mutex == NULL ||
      xSemaphoreTake(sd_card_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
    ESP_LOGE(TAG,
             "Failed to take SD card mutex for writing, operation aborted");
    free(json_buffer);
    return;
  }

  // Save settings as .txt file with 8.3 filename format
  // Use standard C file I/O operations
  FILE *f = fopen(SD_MOUNT_POINT "/settings.cfg", "w");
  esp_err_t result = ESP_FAIL;

  if (f != NULL) {
    size_t written = fwrite(json_buffer, 1, strlen(json_buffer), f);
    fclose(f);
    if (written == strlen(json_buffer)) {
      result = ESP_OK;
    } else {
      ESP_LOGE(TAG, "Incomplete write");
    }
  } else {
    ESP_LOGE(TAG, "Failed to open settings file for writing");
  }

  // Release mutex after file operations
  xSemaphoreGive(sd_card_mutex);
  free(json_buffer);

  if (result == ESP_OK) {
    ESP_LOGI(TAG, "Settings saved to SD card successfully.");
  } else {
    ESP_LOGE(TAG, "Failed to save settings to SD card.");
  }
}

/**
 * @brief Queues a request to save the current settings in a background task.
 * This function makes a copy of the current settings to pass to the background
 * task.
 */
void trigger_settings_save(void) {
  // Allocate memory for a copy of the settings to ensure thread safety.
  touch_settings_t *settings_copy = malloc(sizeof(touch_settings_t));
  if (settings_copy == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for settings copy!");
    return;
  }

  // Copy the current settings to the new memory block
  memcpy(settings_copy, &current_settings, sizeof(touch_settings_t));

  background_task_t task = {.type = BG_TASK_SETTINGS_SAVE,
                            .data =
                                settings_copy, // Pass the pointer to the copy
                            .data_size = sizeof(touch_settings_t),
                            .callback = NULL};

  if (background_task_add(&task) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to queue settings save task. Queue might be full.");
    free(settings_copy); // Free memory if task queuing fails
  } else {
    ESP_LOGI(TAG, "Settings save queued for background processing.");
  }
}

/**
 * @brief Loads settings from the SD card. If it fails, loads defaults.
 */
esp_err_t settings_load(void) {
  // Initialize SD card mutex if not already done
  if (sd_card_mutex == NULL) {
    sd_card_mutex = xSemaphoreCreateMutex();
    if (sd_card_mutex == NULL) {
      ESP_LOGE(TAG, "Failed to create SD card mutex!");
      settings_init_defaults(&current_settings);
      return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "SD card access mutex created successfully");
  }

  // Check if SD card is mounted
  if (!sd_card_is_mounted()) {
    ESP_LOGW(TAG, "SD card not mounted. Using default settings.");
    settings_init_defaults(&current_settings);
    return ESP_ERR_INVALID_STATE;
  }

  // Try to load from SD card with mutex protection
  ESP_LOGI(TAG, "Attempting to load settings from SD card...");

  // Take mutex before SD card access
  if (xSemaphoreTake(sd_card_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGW(TAG, "Failed to take SD card mutex for reading, using defaults");
    settings_init_defaults(&current_settings);
    return ESP_ERR_TIMEOUT;
  }

  FILE *f = fopen(SD_MOUNT_POINT "/settings.cfg", "r");
  if (f != NULL) {
    char *buffer = malloc(2048);
    if (!buffer) {
      ESP_LOGE(TAG, "Failed to allocate memory for reading settings");
      fclose(f);
      xSemaphoreGive(sd_card_mutex);
      return ESP_ERR_NO_MEM;
    }
    memset(buffer, 0, 2048);
    size_t bytes_read = fread(buffer, 1, 2048, f);

    fclose(f);

    // Release mutex after file operations
    xSemaphoreGive(sd_card_mutex);

    ESP_LOGI(TAG, "Read %d bytes from settings.cfg: %s", (int)bytes_read, buffer);

    // Initialize default values first, so missing fields in old settings.cfg retain defaults
    settings_init_defaults(&current_settings);

    if (settings_from_json(buffer, &current_settings)) {
      ESP_LOGI(TAG, "Settings loaded from settings.cfg successfully.");

      ESP_LOGI(
          TAG,
          "Loaded settings: Demo=%s, Screen3=%s, NavButtons=%s, Sensitivity=%d",
          current_settings.demo_mode_enabled ? "ON" : "OFF",
          current_settings.screen3_enabled ? "ON" : "OFF",
          current_settings.nav_buttons_enabled ? "ON" : "OFF",
          current_settings.touch_sensitivity_level);

      // Apply platform setting
      can_parser_set_platform(current_settings.can_platform);

      free(buffer);
      return ESP_OK;
    } else {
      ESP_LOGW(TAG, "Failed to parse settings.cfg, using defaults.");
      free(buffer);
      settings_init_defaults(&current_settings);
      return ESP_FAIL;
    }
  }

  // Release mutex if file couldn't be opened
  xSemaphoreGive(sd_card_mutex);

  // If file doesn't exist or can't be opened, use defaults and try to create
  // the file.
  ESP_LOGI(TAG,
           "settings.cfg not found on SD card, initializing with defaults.");
  settings_init_defaults(&current_settings);

  // Attempt to save the new default settings to the SD card.
  // This is a blocking call, but it only happens once on the very first boot.
  ESP_LOGI(TAG, "Attempting to create default settings file...");
  settings_save(&current_settings);

  return ESP_ERR_NOT_FOUND; // Return NOT_FOUND to indicate that defaults were
                            // loaded.
}

// ... other functions like settings_validate, getters/setters, etc. remain the
// same ...

bool settings_validate(touch_settings_t *settings) {
  if (settings == NULL)
    return false;
  if (settings->touch_sensitivity_level < MIN_TOUCH_SENSITIVITY ||
      settings->touch_sensitivity_level > MAX_TOUCH_SENSITIVITY)
    return false;
  return true;
}
void settings_print_debug(touch_settings_t *settings) {
  if (settings == NULL)
    return;
  ESP_LOGI(TAG, "Settings Debug: Touch=%d, Demo=%s, Screen3=%s",
           settings->touch_sensitivity_level,
           settings->demo_mode_enabled ? "ON" : "OFF",
           settings->screen3_enabled ? "ON" : "OFF");
}
bool demo_mode_get_enabled(void) { return current_settings.demo_mode_enabled; }
void demo_mode_set_enabled(bool enabled) {
  current_settings.demo_mode_enabled = enabled;
}
bool screen3_get_enabled(void) { return current_settings.screen3_enabled; }
void screen3_set_enabled(bool enabled) {
  current_settings.screen3_enabled = enabled;
}
bool nav_buttons_get_enabled(void) {
  return current_settings.nav_buttons_enabled;
}
void nav_buttons_set_enabled(bool enabled) {
  current_settings.nav_buttons_enabled = enabled;
}

CanPlatform settings_get_can_platform(void) {
  return current_settings.can_platform;
}
void settings_set_can_platform(CanPlatform platform) {
  current_settings.can_platform = platform;
  // Apply immediately
  can_parser_set_platform(platform);
}
bool settings_get_mre_parallel(void) {
  return current_settings.mre_parallel;
}
void settings_set_mre_parallel(bool enabled) {
  current_settings.mre_parallel = enabled;
  system_settings_t *sys_settings = system_settings_get();
  if (sys_settings) {
    sys_settings->mre_parallel = enabled;
  }
}
void settings_apply_changes(void) {
  ESP_LOGI(TAG, "Applying settings changes...");

  // Forward call to layout manager to reflow gauges
  extern void ui_update_global_layout(void);
  ui_update_global_layout();
}
void settings_reset_to_defaults(void) {
  ESP_LOGI(TAG, "Resetting settings to defaults in memory");
  settings_init_defaults(&current_settings);
  settings_apply_changes();
}
bool screen1_arc_get_enabled(int arc_index) {
  if (arc_index < 0 || arc_index >= SCREEN1_ARCS_COUNT)
    return false;
  return current_settings.screen1_arcs_enabled[arc_index];
}
void screen1_arc_set_enabled(int arc_index, bool enabled) {
  if (arc_index < 0 || arc_index >= SCREEN1_ARCS_COUNT)
    return;
  current_settings.screen1_arcs_enabled[arc_index] = enabled;
}
bool screen2_arc_get_enabled(int arc_index) {
  if (arc_index < 0 || arc_index >= SCREEN2_ARCS_COUNT)
    return false;
  return current_settings.screen2_arcs_enabled[arc_index];
}
void screen2_arc_set_enabled(int arc_index, bool enabled) {
  if (arc_index < 0 || arc_index >= SCREEN2_ARCS_COUNT)
    return;
  current_settings.screen2_arcs_enabled[arc_index] = enabled;
}
void ui_Screen1_update_arcs_visibility(void) {
  ESP_LOGD(TAG, "Screen1 arcs visibility update requested");
}
void ui_Screen2_update_arcs_visibility(void) {
  ESP_LOGD(TAG, "Screen2 arcs visibility update requested");
}
void demo_mode_test_toggle(void) {
  current_settings.demo_mode_enabled = !current_settings.demo_mode_enabled;
}
void demo_mode_status_report(void) {
  ESP_LOGI(TAG, "Demo Mode Status: %s",
           current_settings.demo_mode_enabled ? "ENABLED" : "DISABLED");
}

const char *settings_get_boot_sound_path(void) {
  return current_settings.boot_sound_path;
}

void settings_set_boot_sound_path(const char *path) {
  if (path) {
    strncpy(current_settings.boot_sound_path, path,
            sizeof(current_settings.boot_sound_path) - 1);
    current_settings
        .boot_sound_path[sizeof(current_settings.boot_sound_path) - 1] = '\0';
  }
}

void settings_set_send_ambient_temp_to_can(bool enabled) {
  current_settings.send_ambient_temp_to_can = enabled;
  system_settings_t *sys_settings = system_settings_get();
  if (sys_settings) {
    sys_settings->send_ambient_temp_to_can = enabled;
  }
  trigger_settings_save();
}

bool settings_get_send_ambient_temp_to_can(void) {
  return current_settings.send_ambient_temp_to_can;
}

void settings_set_ambient_can_temp(float temp) {
  current_settings.ambient_can_temp = temp;
  system_settings_t *sys_settings = system_settings_get();
  if (sys_settings) {
    sys_settings->ambient_can_temp = temp;
  }
  trigger_settings_save();
}

float settings_get_ambient_can_temp(void) {
  return current_settings.ambient_can_temp;
}

// Screen 9 Persistent Getters/Setters
bool settings_get_pump_is_auto(void) { return current_settings.pump_is_auto; }
void settings_set_pump_is_auto(bool is_auto) { current_settings.pump_is_auto = is_auto; }
bool settings_get_pump_manual_on(void) { return current_settings.pump_manual_on; }
void settings_set_pump_manual_on(bool manual_on) { current_settings.pump_manual_on = manual_on; }
int settings_get_pump_manual_speed(void) { return current_settings.pump_manual_speed; }
void settings_set_pump_manual_speed(int speed) { current_settings.pump_manual_speed = speed; }
bool settings_get_fan_is_auto(void) { return current_settings.fan_is_auto; }
void settings_set_fan_is_auto(bool is_auto) { current_settings.fan_is_auto = is_auto; }
bool settings_get_fan_manual_on(void) { return current_settings.fan_manual_on; }
void settings_set_fan_manual_on(bool manual_on) { current_settings.fan_manual_on = manual_on; }
int settings_get_fan_manual_speed(void) { return current_settings.fan_manual_speed; }
void settings_set_fan_manual_speed(int speed) { current_settings.fan_manual_speed = speed; }
int settings_get_pump_map_temp(int idx) { return current_settings.pump_map_temp[idx]; }
void settings_set_pump_map_temp(int idx, int temp) { current_settings.pump_map_temp[idx] = temp; }
int settings_get_pump_map_speed(int idx) { return current_settings.pump_map_speed[idx]; }
void settings_set_pump_map_speed(int idx, int speed) { current_settings.pump_map_speed[idx] = speed; }
int settings_get_fan_map_temp(int idx) { return current_settings.fan_map_temp[idx]; }
void settings_set_fan_map_temp(int idx, int temp) { current_settings.fan_map_temp[idx] = temp; }
int settings_get_fan_map_speed(int idx) { return current_settings.fan_map_speed[idx]; }
void settings_set_fan_map_speed(int idx, int speed) { current_settings.fan_map_speed[idx] = speed; }

// Screen 10 Persistent Getters/Setters
bool settings_get_wg_is_auto(void) { return current_settings.wg_is_auto; }
void settings_set_wg_is_auto(bool is_auto) { current_settings.wg_is_auto = is_auto; }
int settings_get_wg_manual_pos(void) { return current_settings.wg_manual_pos; }
void settings_set_wg_manual_pos(int pos) { current_settings.wg_manual_pos = pos; }
bool settings_get_wg_is_inverted(void) { return current_settings.wg_is_inverted; }
void settings_set_wg_is_inverted(bool inverted) { current_settings.wg_is_inverted = inverted; }
bool settings_get_bov_is_auto(void) { return current_settings.bov_is_auto; }
void settings_set_bov_is_auto(bool is_auto) { current_settings.bov_is_auto = is_auto; }
bool settings_get_bov_manual_open(void) { return current_settings.bov_manual_open; }
void settings_set_bov_manual_open(bool open) { current_settings.bov_manual_open = open; }
int settings_get_bov_tps_threshold(void) { return current_settings.bov_tps_threshold; }
void settings_set_bov_tps_threshold(int val) { current_settings.bov_tps_threshold = val; }
int settings_get_bov_press_threshold(void) { return current_settings.bov_press_threshold; }
void settings_set_bov_press_threshold(int val) { current_settings.bov_press_threshold = val; }
int settings_get_bov_open_duration(void) { return current_settings.bov_open_duration; }
void settings_set_bov_open_duration(int val) { current_settings.bov_open_duration = val; }
bool settings_get_bov_stat_enabled(void) { return current_settings.bov_stat_enabled; }
void settings_set_bov_stat_enabled(bool enabled) { current_settings.bov_stat_enabled = enabled; }
int settings_get_bov_stat_ratio(void) { return current_settings.bov_stat_ratio; }
void settings_set_bov_stat_ratio(int val) { current_settings.bov_stat_ratio = val; }
