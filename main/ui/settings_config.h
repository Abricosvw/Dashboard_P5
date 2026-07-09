// Settings Configuration
// Defines default values and configuration for touch screen settings

#ifndef SETTINGS_CONFIG_H
#define SETTINGS_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "can_definitions.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>


// Default settings values
#define DEFAULT_TOUCH_SENSITIVITY 5
#define DEFAULT_DEMO_MODE_ENABLED true
#define DEFAULT_SCREEN3_ENABLED true
#define DEFAULT_NAV_BUTTONS_ENABLED true
#define DEFAULT_CAN_PLATFORM PLATFORM_VW_PQ35_46
#define DEFAULT_BOOT_SOUND_PATH "/sdcard/SYSTEM/SOUND/startup.wav"

// Touch sensitivity range
#define MIN_TOUCH_SENSITIVITY 1
#define MAX_TOUCH_SENSITIVITY 10

// Screen arcs count
#define SCREEN1_ARCS_COUNT 5
#define SCREEN2_ARCS_COUNT 4

// Gauge Units Enum
typedef enum {
  UNIT_KPA = 0,
  UNIT_BAR,
  UNIT_PSI,
  UNIT_LAMBDA,
  UNIT_AFR,
  UNIT_VOLTS,
  UNIT_CELSIUS,
  UNIT_FAHRENHEIT,
  UNIT_KMH,
  UNIT_MPH,
  UNIT_NM,
  UNIT_PCT
} gauge_unit_t;

// Settings structure
typedef struct {
  uint8_t touch_sensitivity_level;
  bool demo_mode_enabled;
  bool screen3_enabled;
  bool nav_buttons_enabled;
  bool screen1_arcs_enabled[SCREEN1_ARCS_COUNT];
  bool screen2_arcs_enabled[SCREEN2_ARCS_COUNT];
  CanPlatform can_platform;
  char boot_sound_path[128];

  // New Gauges Visibility
  bool show_iat;
  bool show_speed;
  bool show_trans_temp;
  bool show_afr;
  bool show_egt;
  bool show_knock_retard;
  bool show_boost_act;
  bool show_ambient_temp;
  bool show_mre_map;
  bool show_mre_wastegate;
  bool mre_parallel;
  bool send_ambient_temp_to_can;
  float ambient_can_temp;

  // Unit settings for each gauge
  uint8_t gauge_units[32];

  // Source settings for each gauge (0=AUTO, 1=RAW, 2=DIAG)
  uint8_t gauge_sources[32];

  // VAG Diagnostic Emulation Settings
  uint8_t diag_address;
  uint8_t diag_protocol;
  char diag_part_number[16];
  char diag_comp_name[32];
  char diag_hw_number[16];
  char diag_sw_version[8];
  char diag_vin[20];
  uint32_t diag_coding;

  // Screen 9 (Pump & Fan) Settings
  bool pump_is_auto;
  bool pump_manual_on;
  uint8_t pump_manual_speed;
  bool fan_is_auto;
  bool fan_manual_on;
  uint8_t fan_manual_speed;
  int pump_map_temp[10];
  int pump_map_speed[10];
  int fan_map_temp[10];
  int fan_map_speed[10];

  // Screen 10 (Wastegate & BOV) Settings
  bool wg_is_auto;
  uint8_t wg_manual_pos;
  bool wg_is_inverted;
  bool bov_is_auto;
  bool bov_manual_open;
  uint8_t bov_tps_threshold;
  uint8_t bov_press_threshold;
  uint8_t bov_open_duration;
  bool bov_stat_enabled;
  uint8_t bov_stat_ratio;
} touch_settings_t;


// Function declarations
void settings_init_defaults(touch_settings_t *settings);
bool settings_validate(touch_settings_t *settings);
void settings_print_debug(touch_settings_t *settings);

// Demo mode control functions
bool demo_mode_get_enabled(void);
void demo_mode_set_enabled(bool enabled);
void demo_mode_test_toggle(void);   // For testing purposes
void demo_mode_status_report(void); // Report current status

// Screen3 control functions
bool screen3_get_enabled(void);
void screen3_set_enabled(bool enabled);

// Navigation buttons control functions
bool nav_buttons_get_enabled(void);
void nav_buttons_set_enabled(bool enabled);

// Screen arcs control functions
bool screen1_arc_get_enabled(int arc_index);
void screen1_arc_set_enabled(int arc_index, bool enabled);
bool screen2_arc_get_enabled(int arc_index);
void screen2_arc_set_enabled(int arc_index, bool enabled);

// Screen arcs update functions
void ui_Screen1_update_arcs_visibility(void);
void ui_Screen2_update_arcs_visibility(void);

// CAN Platform control
CanPlatform settings_get_can_platform(void);
void settings_set_can_platform(CanPlatform platform);
bool settings_get_mre_parallel(void);
void settings_set_mre_parallel(bool enabled);

// Boot sound path control
const char *settings_get_boot_sound_path(void);
void settings_set_boot_sound_path(const char *path);

// Settings persistence functions
void settings_save(const touch_settings_t *settings_to_save);
void trigger_settings_save(void); // Асинхронное сохранение с фоновой задачей
void settings_apply_changes(void);
void settings_reset_to_defaults(void);
esp_err_t settings_load(void);

// Ambient Temp CAN transmit toggle helpers
void settings_set_send_ambient_temp_to_can(bool enabled);
bool settings_get_send_ambient_temp_to_can(void);
void settings_set_ambient_can_temp(float temp);
float settings_get_ambient_can_temp(void);

// Screen 9 Persistent Getters/Setters
bool settings_get_pump_is_auto(void);
void settings_set_pump_is_auto(bool is_auto);
bool settings_get_pump_manual_on(void);
void settings_set_pump_manual_on(bool manual_on);
int settings_get_pump_manual_speed(void);
void settings_set_pump_manual_speed(int speed);
bool settings_get_fan_is_auto(void);
void settings_set_fan_is_auto(bool is_auto);
bool settings_get_fan_manual_on(void);
void settings_set_fan_manual_on(bool manual_on);
int settings_get_fan_manual_speed(void);
void settings_set_fan_manual_speed(int speed);
int settings_get_pump_map_temp(int idx);
void settings_set_pump_map_temp(int idx, int temp);
int settings_get_pump_map_speed(int idx);
void settings_set_pump_map_speed(int idx, int speed);
int settings_get_fan_map_temp(int idx);
void settings_set_fan_map_temp(int idx, int temp);
int settings_get_fan_map_speed(int idx);
void settings_set_fan_map_speed(int idx, int speed);

// Screen 10 Persistent Getters/Setters
bool settings_get_wg_is_auto(void);
void settings_set_wg_is_auto(bool is_auto);
int settings_get_wg_manual_pos(void);
void settings_set_wg_manual_pos(int pos);
bool settings_get_wg_is_inverted(void);
void settings_set_wg_is_inverted(bool inverted);
bool settings_get_bov_is_auto(void);
void settings_set_bov_is_auto(bool is_auto);
bool settings_get_bov_manual_open(void);
void settings_set_bov_manual_open(bool open);
int settings_get_bov_tps_threshold(void);
void settings_set_bov_tps_threshold(int val);
int settings_get_bov_press_threshold(void);
void settings_set_bov_press_threshold(int val);
int settings_get_bov_open_duration(void);
void settings_set_bov_open_duration(int val);
bool settings_get_bov_stat_enabled(void);
void settings_set_bov_stat_enabled(bool enabled);
int settings_get_bov_stat_ratio(void);
void settings_set_bov_stat_ratio(int val);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
