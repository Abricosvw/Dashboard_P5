#include "ui_Screen9.h"
#include "../ui.h"
#include "../ui_screen_manager.h"
#include "ecu_data.h"
#include "esp_log.h"
#include "settings_config.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "SCREEN9";

// Root screen pointer
lv_obj_t *ui_Screen9 = NULL;

// Coolant Pump UI elements
lv_obj_t *ui_Pump_Speed_Bar = NULL;
lv_obj_t *ui_Pump_Speed_Label = NULL;
lv_obj_t *ui_Pump_Mode_Auto_Btn = NULL;
lv_obj_t *ui_Pump_Mode_Man_Btn = NULL;
lv_obj_t *ui_Pump_Switch = NULL;
lv_obj_t *ui_Pump_Slider = NULL;
lv_obj_t *ui_Pump_Slider_Label = NULL;

// Electric Fan UI elements
lv_obj_t *ui_Fan_Speed_Bar = NULL;
lv_obj_t *ui_Fan_Speed_Label = NULL;
lv_obj_t *ui_Fan_Mode_Auto_Btn = NULL;
lv_obj_t *ui_Fan_Mode_Man_Btn = NULL;
lv_obj_t *ui_Fan_Switch = NULL;
lv_obj_t *ui_Fan_Slider = NULL;
lv_obj_t *ui_Fan_Slider_Label = NULL;

// Header Labels
lv_obj_t *ui_Screen9_CLT_Val = NULL;
lv_obj_t *ui_Screen9_IAT_Val = NULL;

// Colors matching the existing premium design system
#define CLR_BG 0x0A0F1A
#define CLR_PANEL 0x111827
#define CLR_BORDER 0x334155
#define CLR_CYAN 0x00D4FF  // Pump Color
#define CLR_GREEN 0x00FF88 // Fan Color
#define CLR_BTN_BG 0x1E293B
#define CLR_BTN_ACTIVE 0x0F172A
#define CLR_TEXT_DIM 0x94A3B8
#define CLR_TEXT_WHITE 0xF1F5F9

// Fonts declared in ui.h
LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(montserrat_20_en_ru);

// Temperature-Speed Lookup Point Type
typedef struct {
  int temp;  // Temperature in °C
  int speed; // Speed in %
} map_point_t;

// Persistent states
static bool pump_is_auto = true;
static bool pump_manual_on = false;
static int pump_manual_speed = 50;

// Actual computed output speeds (shared with Lua engine via getters)
static int s_actual_pump_speed = 0;
static int s_actual_fan_speed = 0;

// Public getters for Lua bindings
int ui_Screen9_get_actual_pump_speed(void) { return s_actual_pump_speed; }
int ui_Screen9_get_actual_fan_speed(void) { return s_actual_fan_speed; }
static map_point_t pump_map[10] = {{20, 0},    {30, 10},  {40, 30},  {50, 50},
                                   {60, 70},   {70, 90},  {80, 100}, {90, 100},
                                   {100, 100}, {110, 100}};

static bool fan_is_auto = true;
static bool fan_manual_on = false;
static int fan_manual_speed = 50;
static map_point_t fan_map[10] = {{30, 0},    {40, 0},   {50, 20},  {60, 40},
                                  {70, 60},   {80, 80},  {90, 100}, {100, 100},
                                  {110, 100}, {120, 100}};

// UI Label pointers to dynamically update text in cells without rebuilds
static lv_obj_t *pump_temp_labels[10];
static lv_obj_t *pump_speed_labels[10];
static lv_obj_t *fan_temp_labels[10];
static lv_obj_t *fan_speed_labels[10];

// ---------- Forward declarations for UI Event Handlers ----------
static void update_pump_ui_state(void);
static void update_fan_ui_state(void);
static void pump_mode_auto_cb(lv_event_t *e);
static void pump_mode_man_cb(lv_event_t *e);
static void fan_mode_auto_cb(lv_event_t *e);
static void fan_mode_man_cb(lv_event_t *e);
static void pump_switch_cb(lv_event_t *e);
static void pump_slider_cb(lv_event_t *e);
static void fan_switch_cb(lv_event_t *e);
static void fan_slider_cb(lv_event_t *e);
static void map_btn_cb(lv_event_t *e);

// Linear interpolation lookup logic
static int lookup_speed(map_point_t *map, int size, float current_temp) {
  if (current_temp <= map[0].temp) {
    return map[0].speed;
  }
  if (current_temp >= map[size - 1].temp) {
    return map[size - 1].speed;
  }
  for (int i = 0; i < size - 1; i++) {
    if (current_temp >= map[i].temp && current_temp <= map[i + 1].temp) {
      float t0 = map[i].temp;
      float t1 = map[i + 1].temp;
      float s0 = map[i].speed;
      float s1 = map[i + 1].speed;
      float ratio = (current_temp - t0) / (t1 - t0);
      return (int)(s0 + ratio * (s1 - s0));
    }
  }
  return 0;
}

// Map Adjust Event Handler
static void map_btn_cb(lv_event_t *e) {
  uintptr_t val = (uintptr_t)lv_event_get_user_data(e);
  bool is_plus = val & 1;
  bool is_speed = (val >> 1) & 1;
  int idx = (val >> 2) & 0x0F;
  bool is_fan = (val >> 6) & 1;

  map_point_t *map = is_fan ? fan_map : pump_map;
  lv_obj_t **labels = is_fan
                          ? (is_speed ? fan_speed_labels : fan_temp_labels)
                          : (is_speed ? pump_speed_labels : pump_temp_labels);

  if (is_speed) {
    int step = 5;
    int curr = map[idx].speed;
    curr += is_plus ? step : -step;
    if (curr < 0)
      curr = 0;
    if (curr > 100)
      curr = 100;
    map[idx].speed = curr;
    lv_label_set_text_fmt(labels[idx], "%d%%", curr);
  } else {
    int step = 5;
    int curr = map[idx].temp;
    curr += is_plus ? step : -step;

    // Boundary checks to maintain strictly sorted temperatures
    int min_val = (idx > 0) ? map[idx - 1].temp + 1 : -40;
    int max_val = (idx < 9) ? map[idx + 1].temp - 1 : 150;

    if (curr < min_val)
      curr = min_val;
    if (curr > max_val)
      curr = max_val;

    map[idx].temp = curr;
    lv_label_set_text_fmt(labels[idx], "%d°C", curr);
  }
}

// Update PumP UI state (locks manual components if in auto mode)
static void update_pump_ui_state(void) {
  if (!ui_Screen9) return;
  if (pump_is_auto) {
    lv_obj_set_style_bg_color(ui_Pump_Mode_Auto_Btn, lv_color_hex(CLR_CYAN), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Pump_Mode_Auto_Btn, 0),
                                lv_color_black(), 0);

    lv_obj_set_style_bg_color(ui_Pump_Mode_Man_Btn, lv_color_hex(CLR_BTN_BG),
                              0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Pump_Mode_Man_Btn, 0),
                                lv_color_hex(CLR_TEXT_DIM), 0);

    // Set translucent to visually represent the disabled state
    lv_obj_set_style_opa(ui_Pump_Switch, LV_OPA_40, 0);
    lv_obj_set_style_opa(ui_Pump_Slider, LV_OPA_40, 0);
  } else {
    lv_obj_set_style_bg_color(ui_Pump_Mode_Auto_Btn, lv_color_hex(CLR_BTN_BG),
                              0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Pump_Mode_Auto_Btn, 0),
                                lv_color_hex(CLR_TEXT_DIM), 0);

    lv_obj_set_style_bg_color(ui_Pump_Mode_Man_Btn, lv_color_hex(CLR_CYAN), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Pump_Mode_Man_Btn, 0),
                                lv_color_black(), 0);

    // Restore full opacity for active manual controls
    lv_obj_set_style_opa(ui_Pump_Switch, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(ui_Pump_Slider, LV_OPA_COVER, 0);
  }

  // Also make sure state checked matches pump_manual_on
  if (ui_Pump_Switch) {
    if (pump_manual_on) {
      lv_obj_add_state(ui_Pump_Switch, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(ui_Pump_Switch, LV_STATE_CHECKED);
    }
  }
  if (ui_Pump_Slider) {
    lv_slider_set_value(ui_Pump_Slider, pump_manual_speed, LV_ANIM_OFF);
    if (ui_Pump_Slider_Label) {
      lv_label_set_text_fmt(ui_Pump_Slider_Label, "%d%%", pump_manual_speed);
    }
  }
}

// Update Fan UI state (locks manual components if in auto mode)
static void update_fan_ui_state(void) {
  if (!ui_Screen9) return;
  if (fan_is_auto) {
    lv_obj_set_style_bg_color(ui_Fan_Mode_Auto_Btn, lv_color_hex(CLR_GREEN), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Fan_Mode_Auto_Btn, 0),
                                lv_color_black(), 0);

    lv_obj_set_style_bg_color(ui_Fan_Mode_Man_Btn, lv_color_hex(CLR_BTN_BG), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Fan_Mode_Man_Btn, 0),
                                lv_color_hex(CLR_TEXT_DIM), 0);

    // Set translucent to visually represent the disabled state
    lv_obj_set_style_opa(ui_Fan_Switch, LV_OPA_40, 0);
    lv_obj_set_style_opa(ui_Fan_Slider, LV_OPA_40, 0);
  } else {
    lv_obj_set_style_bg_color(ui_Fan_Mode_Auto_Btn, lv_color_hex(CLR_BTN_BG),
                              0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Fan_Mode_Auto_Btn, 0),
                                lv_color_hex(CLR_TEXT_DIM), 0);

    lv_obj_set_style_bg_color(ui_Fan_Mode_Man_Btn, lv_color_hex(CLR_GREEN), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Fan_Mode_Man_Btn, 0),
                                lv_color_black(), 0);

    // Restore full opacity for active manual controls
    lv_obj_set_style_opa(ui_Fan_Switch, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(ui_Fan_Slider, LV_OPA_COVER, 0);
  }

  // Also make sure state checked matches fan_manual_on
  if (ui_Fan_Switch) {
    if (fan_manual_on) {
      lv_obj_add_state(ui_Fan_Switch, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(ui_Fan_Switch, LV_STATE_CHECKED);
    }
  }
  if (ui_Fan_Slider) {
    lv_slider_set_value(ui_Fan_Slider, fan_manual_speed, LV_ANIM_OFF);
    if (ui_Fan_Slider_Label) {
      lv_label_set_text_fmt(ui_Fan_Slider_Label, "%d%%", fan_manual_speed);
    }
  }
}

static void pump_mode_auto_cb(lv_event_t *e) {
  pump_is_auto = true;
  update_pump_ui_state();
  ESP_LOGI(TAG, "Pump Mode set to AUTO");
}

static void pump_mode_man_cb(lv_event_t *e) {
  pump_is_auto = false;
  update_pump_ui_state();
  ESP_LOGI(TAG, "Pump Mode set to MANUAL");
}

static void fan_mode_auto_cb(lv_event_t *e) {
  fan_is_auto = true;
  update_fan_ui_state();
  ESP_LOGI(TAG, "Fan Mode set to AUTO");
}

static void fan_mode_man_cb(lv_event_t *e) {
  fan_is_auto = false;
  update_fan_ui_state();
  ESP_LOGI(TAG, "Fan Mode set to MANUAL");
}

static void pump_switch_cb(lv_event_t *e) {
  if (pump_is_auto) {
    // If in Auto mode, reject any toggle and force the state back
    if (pump_manual_on) {
      lv_obj_add_state(ui_Pump_Switch, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(ui_Pump_Switch, LV_STATE_CHECKED);
    }
    return;
  }
  pump_manual_on = lv_obj_has_state(ui_Pump_Switch, LV_STATE_CHECKED);
  ESP_LOGI(TAG, "Pump Switch toggled: %s", pump_manual_on ? "ON" : "OFF");
}

static void pump_slider_cb(lv_event_t *e) {
  if (pump_is_auto) {
    // If in Auto mode, reject slider movements by resetting to last manual
    // value
    lv_slider_set_value(ui_Pump_Slider, pump_manual_speed, LV_ANIM_OFF);
    return;
  }
  pump_manual_speed = lv_slider_get_value(ui_Pump_Slider);
  lv_label_set_text_fmt(ui_Pump_Slider_Label, "%d%%", pump_manual_speed);
  ESP_LOGI(TAG, "Pump Slider speed updated: %d%%", pump_manual_speed);
}

static void fan_switch_cb(lv_event_t *e) {
  if (fan_is_auto) {
    // If in Auto mode, reject any toggle and force the state back
    if (fan_manual_on) {
      lv_obj_add_state(ui_Fan_Switch, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(ui_Fan_Switch, LV_STATE_CHECKED);
    }
    return;
  }
  fan_manual_on = lv_obj_has_state(ui_Fan_Switch, LV_STATE_CHECKED);
  ESP_LOGI(TAG, "Fan Switch toggled: %s", fan_manual_on ? "ON" : "OFF");
}

static void fan_slider_cb(lv_event_t *e) {
  if (fan_is_auto) {
    // If in Auto mode, reject slider movements by resetting to last manual
    // value
    lv_slider_set_value(ui_Fan_Slider, fan_manual_speed, LV_ANIM_OFF);
    return;
  }
  fan_manual_speed = lv_slider_get_value(ui_Fan_Slider);
  lv_label_set_text_fmt(ui_Fan_Slider_Label, "%d%%", fan_manual_speed);
  ESP_LOGI(TAG, "Fan Slider speed updated: %d%%", fan_manual_speed);
}

// Main screen creation function
void ui_Screen9_screen_init(void) {
  ESP_LOGI(TAG, "Lazy-initializing ui_Screen9");

  // Reset local lists to prevent stray pointers
  memset(pump_temp_labels, 0, sizeof(pump_temp_labels));
  memset(pump_speed_labels, 0, sizeof(pump_speed_labels));
  memset(fan_temp_labels, 0, sizeof(fan_temp_labels));
  memset(fan_speed_labels, 0, sizeof(fan_speed_labels));

  // --- ROOT SCREEN ---
  ui_Screen9 = lv_obj_create(NULL);
  lv_obj_set_size(ui_Screen9, 720, 1280);
  lv_obj_clear_flag(ui_Screen9, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(ui_Screen9, lv_color_hex(CLR_BG), 0);
  lv_obj_set_style_bg_opa(ui_Screen9, LV_OPA_COVER, 0);

  // --- HEADER PANEL ---
  lv_obj_t *header_panel = lv_obj_create(ui_Screen9);
  lv_obj_set_size(header_panel, 700, 80);
  lv_obj_set_pos(header_panel, 10, 10);
  lv_obj_set_style_bg_color(header_panel, lv_color_hex(CLR_PANEL), 0);
  lv_obj_set_style_bg_opa(header_panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(header_panel, lv_color_hex(CLR_BORDER), 0);
  lv_obj_set_style_border_width(header_panel, 1, 0);
  lv_obj_set_style_radius(header_panel, 10, 0);
  lv_obj_set_style_shadow_width(header_panel, 0, 0);
  lv_obj_clear_flag(header_panel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(header_panel);
  lv_label_set_text(title, "AIR TO WATER INTERCOOLER");
  lv_obj_set_style_text_color(title, lv_color_hex(CLR_CYAN), 0);
  lv_obj_set_style_text_font(title, &montserrat_20_en_ru, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

  ui_Screen9_CLT_Val = lv_label_create(header_panel);
  lv_label_set_text(ui_Screen9_CLT_Val, "CLT: -- °C");
  lv_obj_set_style_text_color(ui_Screen9_CLT_Val, lv_color_hex(CLR_TEXT_DIM),
                              0);
  lv_obj_set_style_text_font(ui_Screen9_CLT_Val, &lv_font_montserrat_14, 0);
  lv_obj_align(ui_Screen9_CLT_Val, LV_ALIGN_BOTTOM_LEFT, 20, -8);

  ui_Screen9_IAT_Val = lv_label_create(header_panel);
  lv_label_set_text(ui_Screen9_IAT_Val, "IAT: -- °C");
  lv_obj_set_style_text_color(ui_Screen9_IAT_Val, lv_color_hex(CLR_GREEN), 0);
  lv_obj_set_style_text_font(ui_Screen9_IAT_Val, &lv_font_montserrat_14, 0);
  lv_obj_align(ui_Screen9_IAT_Val, LV_ALIGN_BOTTOM_RIGHT, -20, -8);

  // ==========================================
  // LEFT COLUMN: COOLANT PUMP (CYAN)
  // ==========================================
  lv_obj_t *pump_panel = lv_obj_create(ui_Screen9);
  lv_obj_set_size(pump_panel, 340, 1080);
  lv_obj_set_pos(pump_panel, 10, 100);
  lv_obj_set_style_bg_color(pump_panel, lv_color_hex(CLR_PANEL), 0);
  lv_obj_set_style_bg_opa(pump_panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(pump_panel, lv_color_hex(CLR_BORDER), 0);
  lv_obj_set_style_border_width(pump_panel, 1, 0);
  lv_obj_set_style_radius(pump_panel, 12, 0);
  lv_obj_set_style_shadow_width(pump_panel, 0, 0);
  lv_obj_clear_flag(pump_panel, LV_OBJ_FLAG_SCROLLABLE);

  // Column Header Title
  lv_obj_t *pump_title = lv_label_create(pump_panel);
  lv_label_set_text(pump_title, "COOLANT PUMP");
  lv_obj_set_style_text_color(pump_title, lv_color_hex(CLR_CYAN), 0);
  lv_obj_set_style_text_font(pump_title, &montserrat_20_en_ru, 0);
  lv_obj_align(pump_title, LV_ALIGN_TOP_MID, 0, 10);

  // Active Output row
  lv_obj_t *pump_actual_lbl = lv_label_create(pump_panel);
  lv_label_set_text(pump_actual_lbl, "Actual Speed:");
  lv_obj_set_style_text_color(pump_actual_lbl, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(pump_actual_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(pump_actual_lbl, LV_ALIGN_TOP_LEFT, 15, 45);

  ui_Pump_Speed_Label = lv_label_create(pump_panel);
  lv_label_set_text(ui_Pump_Speed_Label, "0%");
  lv_obj_set_style_text_color(ui_Pump_Speed_Label, lv_color_hex(CLR_CYAN), 0);
  lv_obj_set_style_text_font(ui_Pump_Speed_Label, &montserrat_20_en_ru, 0);
  lv_obj_align(ui_Pump_Speed_Label, LV_ALIGN_TOP_RIGHT, -15, 40);

  // Speed progress Bar
  ui_Pump_Speed_Bar = lv_bar_create(pump_panel);
  lv_obj_set_size(ui_Pump_Speed_Bar, 310, 15);
  lv_obj_align(ui_Pump_Speed_Bar, LV_ALIGN_TOP_MID, 0, 75);
  lv_bar_set_range(ui_Pump_Speed_Bar, 0, 100);
  lv_obj_set_style_bg_color(ui_Pump_Speed_Bar, lv_color_hex(0x1F293B),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui_Pump_Speed_Bar, lv_color_hex(CLR_CYAN),
                            LV_PART_INDICATOR);

  // Divider
  lv_obj_t *div_pump1 = lv_obj_create(pump_panel);
  lv_obj_set_size(div_pump1, 310, 1);
  lv_obj_set_pos(div_pump1, 15, 105);
  lv_obj_set_style_bg_color(div_pump1, lv_color_hex(CLR_BORDER), 0);

  // Mode Auto/Manual tab controls
  ui_Pump_Mode_Auto_Btn = lv_btn_create(pump_panel);
  lv_obj_set_size(ui_Pump_Mode_Auto_Btn, 150, 40);
  lv_obj_set_pos(ui_Pump_Mode_Auto_Btn, 15, 120);
  lv_obj_set_style_radius(ui_Pump_Mode_Auto_Btn, 6, 0);
  lv_obj_set_style_shadow_width(ui_Pump_Mode_Auto_Btn, 0, 0);
  lv_obj_add_event_cb(ui_Pump_Mode_Auto_Btn, pump_mode_auto_cb,
                      LV_EVENT_CLICKED, NULL);
  lv_obj_t *btn1_lbl = lv_label_create(ui_Pump_Mode_Auto_Btn);
  lv_label_set_text(btn1_lbl, "AUTO");
  lv_obj_set_style_text_font(btn1_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(btn1_lbl);

  ui_Pump_Mode_Man_Btn = lv_btn_create(pump_panel);
  lv_obj_set_size(ui_Pump_Mode_Man_Btn, 150, 40);
  lv_obj_set_pos(ui_Pump_Mode_Man_Btn, 180, 120);
  lv_obj_set_style_radius(ui_Pump_Mode_Man_Btn, 6, 0);
  lv_obj_set_style_shadow_width(ui_Pump_Mode_Man_Btn, 0, 0);
  lv_obj_add_event_cb(ui_Pump_Mode_Man_Btn, pump_mode_man_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_t *btn2_lbl = lv_label_create(ui_Pump_Mode_Man_Btn);
  lv_label_set_text(btn2_lbl, "MANUAL");
  lv_obj_set_style_text_font(btn2_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(btn2_lbl);

  // Manual Control Area
  lv_obj_t *pump_sw_lbl = lv_label_create(pump_panel);
  lv_label_set_text(pump_sw_lbl, "Manual ON/OFF:");
  lv_obj_set_style_text_color(pump_sw_lbl, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(pump_sw_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(pump_sw_lbl, LV_ALIGN_TOP_LEFT, 15, 185);

  ui_Pump_Switch = lv_switch_create(pump_panel);
  lv_obj_set_size(ui_Pump_Switch, 60, 30);
  lv_obj_align(ui_Pump_Switch, LV_ALIGN_TOP_RIGHT, -15, 178);
  lv_obj_add_event_cb(ui_Pump_Switch, pump_switch_cb, LV_EVENT_VALUE_CHANGED,
                      NULL);
  lv_obj_set_style_bg_color(ui_Pump_Switch, lv_color_hex(CLR_CYAN),
                            LV_PART_INDICATOR | LV_STATE_CHECKED);

  lv_obj_t *pump_slider_lbl = lv_label_create(pump_panel);
  lv_label_set_text(pump_slider_lbl, "Manual Speed:");
  lv_obj_set_style_text_color(pump_slider_lbl, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(pump_slider_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(pump_slider_lbl, LV_ALIGN_TOP_LEFT, 15, 230);

  ui_Pump_Slider_Label = lv_label_create(pump_panel);
  lv_label_set_text(ui_Pump_Slider_Label, "50%");
  lv_obj_set_style_text_color(ui_Pump_Slider_Label,
                              lv_color_hex(CLR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(ui_Pump_Slider_Label, &lv_font_montserrat_14, 0);
  lv_obj_align(ui_Pump_Slider_Label, LV_ALIGN_TOP_RIGHT, -15, 230);

  ui_Pump_Slider = lv_slider_create(pump_panel);
  lv_obj_set_size(ui_Pump_Slider, 310, 12);
  lv_obj_align(ui_Pump_Slider, LV_ALIGN_TOP_MID, 0, 260);
  lv_slider_set_value(ui_Pump_Slider, pump_manual_speed, LV_ANIM_OFF);
  lv_obj_add_event_cb(ui_Pump_Slider, pump_slider_cb, LV_EVENT_VALUE_CHANGED,
                      NULL);
  lv_obj_set_style_bg_color(ui_Pump_Slider, lv_color_hex(0x1F293B),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui_Pump_Slider, lv_color_hex(CLR_CYAN),
                            LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(ui_Pump_Slider, lv_color_hex(CLR_TEXT_WHITE),
                            LV_PART_KNOB);

  // Divider
  lv_obj_t *div_pump2 = lv_obj_create(pump_panel);
  lv_obj_set_size(div_pump2, 310, 1);
  lv_obj_set_pos(div_pump2, 15, 290);
  lv_obj_set_style_bg_color(div_pump2, lv_color_hex(CLR_BORDER), 0);

  // Auto Curve Table Header
  lv_obj_t *pump_map_title = lv_label_create(pump_panel);
  lv_label_set_text(pump_map_title, "AUTOMATIC CONFIGURE MAP");
  lv_obj_set_style_text_color(pump_map_title, lv_color_hex(CLR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(pump_map_title, &lv_font_montserrat_14, 0);
  lv_obj_align(pump_map_title, LV_ALIGN_TOP_MID, 0, 305);

  // Scrollable container for map cells
  lv_obj_t *pump_map_cont = lv_obj_create(pump_panel);
  lv_obj_set_size(pump_map_cont, 320, 730);
  lv_obj_set_pos(pump_map_cont, 10, 335);
  lv_obj_set_style_bg_opa(pump_map_cont, 0, 0); // transparent background
  lv_obj_set_style_border_width(pump_map_cont, 0, 0);
  lv_obj_set_style_pad_all(pump_map_cont, 0, 0);
  lv_obj_set_scrollbar_mode(pump_map_cont, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_flex_flow(pump_map_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(pump_map_cont, 6, 0);

  // Build the 10 rows for Coolant Pump
  for (int i = 0; i < 10; i++) {
    lv_obj_t *row = lv_obj_create(pump_map_cont);
    lv_obj_set_size(row, 300, 52);
    lv_obj_set_style_bg_color(row, lv_color_hex(CLR_BTN_ACTIVE), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Index Label
    lv_obj_t *idx_lbl = lv_label_create(row);
    lv_label_set_text_fmt(idx_lbl, "#%d", i + 1);
    lv_obj_set_style_text_color(idx_lbl, lv_color_hex(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(idx_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(idx_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    // Temp adjust buttons
    uintptr_t data_minus_temp =
        (0 << 6) | (i << 2) | (0 << 1) | 0; // Pump, index i, Temp, Minus
    uintptr_t data_plus_temp =
        (0 << 6) | (i << 2) | (0 << 1) | 1; // Pump, index i, Temp, Plus

    lv_obj_t *btn_t_min = lv_btn_create(row);
    lv_obj_set_size(btn_t_min, 30, 30);
    lv_obj_align(btn_t_min, LV_ALIGN_LEFT_MID, 28, 0);
    lv_obj_set_style_bg_color(btn_t_min, lv_color_hex(CLR_BTN_BG), 0);
    lv_obj_set_style_radius(btn_t_min, 4, 0);
    lv_obj_set_style_shadow_width(btn_t_min, 0, 0);
    lv_obj_add_event_cb(btn_t_min, map_btn_cb, LV_EVENT_CLICKED,
                        (void *)data_minus_temp);
    lv_obj_t *lbl_tm = lv_label_create(btn_t_min);
    lv_label_set_text(lbl_tm, "-");
    lv_obj_set_style_text_font(lbl_tm, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_tm);

    pump_temp_labels[i] = lv_label_create(row);
    lv_label_set_text_fmt(pump_temp_labels[i], "%d°C", pump_map[i].temp);
    lv_obj_set_style_text_color(pump_temp_labels[i],
                                lv_color_hex(CLR_TEXT_WHITE), 0);
    lv_obj_set_style_text_font(pump_temp_labels[i], &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(pump_temp_labels[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(pump_temp_labels[i], 52, 20);
    lv_obj_align(pump_temp_labels[i], LV_ALIGN_LEFT_MID, 60, 0);

    lv_obj_t *btn_t_pls = lv_btn_create(row);
    lv_obj_set_size(btn_t_pls, 30, 30);
    lv_obj_align(btn_t_pls, LV_ALIGN_LEFT_MID, 114, 0);
    lv_obj_set_style_bg_color(btn_t_pls, lv_color_hex(CLR_BTN_BG), 0);
    lv_obj_set_style_radius(btn_t_pls, 4, 0);
    lv_obj_set_style_shadow_width(btn_t_pls, 0, 0);
    lv_obj_add_event_cb(btn_t_pls, map_btn_cb, LV_EVENT_CLICKED,
                        (void *)data_plus_temp);
    lv_obj_t *lbl_tp = lv_label_create(btn_t_pls);
    lv_label_set_text(lbl_tp, "+");
    lv_obj_set_style_text_font(lbl_tp, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_tp);

    // Arrow icon -> separator
    lv_obj_t *arr = lv_label_create(row);
    lv_label_set_text(arr, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(arr, lv_color_hex(CLR_CYAN), 0);
    lv_obj_set_style_text_font(arr, &lv_font_montserrat_12, 0);
    lv_obj_align(arr, LV_ALIGN_CENTER, 4, 0);

    // Speed adjust buttons
    uintptr_t data_minus_speed =
        (0 << 6) | (i << 2) | (1 << 1) | 0; // Pump, index i, Speed, Minus
    uintptr_t data_plus_speed =
        (0 << 6) | (i << 2) | (1 << 1) | 1; // Pump, index i, Speed, Plus

    lv_obj_t *btn_s_min = lv_btn_create(row);
    lv_obj_set_size(btn_s_min, 30, 30);
    lv_obj_align(btn_s_min, LV_ALIGN_RIGHT_MID, -114, 0);
    lv_obj_set_style_bg_color(btn_s_min, lv_color_hex(CLR_BTN_BG), 0);
    lv_obj_set_style_radius(btn_s_min, 4, 0);
    lv_obj_set_style_shadow_width(btn_s_min, 0, 0);
    lv_obj_add_event_cb(btn_s_min, map_btn_cb, LV_EVENT_CLICKED,
                        (void *)data_minus_speed);
    lv_obj_t *lbl_sm = lv_label_create(btn_s_min);
    lv_label_set_text(lbl_sm, "-");
    lv_obj_set_style_text_font(lbl_sm, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_sm);

    pump_speed_labels[i] = lv_label_create(row);
    lv_label_set_text_fmt(pump_speed_labels[i], "%d%%", pump_map[i].speed);
    lv_obj_set_style_text_color(pump_speed_labels[i],
                                lv_color_hex(CLR_TEXT_WHITE), 0);
    lv_obj_set_style_text_font(pump_speed_labels[i], &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(pump_speed_labels[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(pump_speed_labels[i], 52, 20);
    lv_obj_align(pump_speed_labels[i], LV_ALIGN_RIGHT_MID, -60, 0);

    lv_obj_t *btn_s_pls = lv_btn_create(row);
    lv_obj_set_size(btn_s_pls, 30, 30);
    lv_obj_align(btn_s_pls, LV_ALIGN_RIGHT_MID, -28, 0);
    lv_obj_set_style_bg_color(btn_s_pls, lv_color_hex(CLR_BTN_BG), 0);
    lv_obj_set_style_radius(btn_s_pls, 4, 0);
    lv_obj_set_style_shadow_width(btn_s_pls, 0, 0);
    lv_obj_add_event_cb(btn_s_pls, map_btn_cb, LV_EVENT_CLICKED,
                        (void *)data_plus_speed);
    lv_obj_t *lbl_sp = lv_label_create(btn_s_pls);
    lv_label_set_text(lbl_sp, "+");
    lv_obj_set_style_text_font(lbl_sp, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_sp);
  }

  update_pump_ui_state();

  // ==========================================
  // RIGHT COLUMN: ELECTRIC FAN (GREEN)
  // ==========================================
  lv_obj_t *fan_panel = lv_obj_create(ui_Screen9);
  lv_obj_set_size(fan_panel, 340, 1080);
  lv_obj_set_pos(fan_panel, 370, 100);
  lv_obj_set_style_bg_color(fan_panel, lv_color_hex(CLR_PANEL), 0);
  lv_obj_set_style_bg_opa(fan_panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(fan_panel, lv_color_hex(CLR_BORDER), 0);
  lv_obj_set_style_border_width(fan_panel, 1, 0);
  lv_obj_set_style_radius(fan_panel, 12, 0);
  lv_obj_set_style_shadow_width(fan_panel, 0, 0);
  lv_obj_clear_flag(fan_panel, LV_OBJ_FLAG_SCROLLABLE);

  // Column Header Title
  lv_obj_t *fan_title = lv_label_create(fan_panel);
  lv_label_set_text(fan_title, "ELECTRIC FAN");
  lv_obj_set_style_text_color(fan_title, lv_color_hex(CLR_GREEN), 0);
  lv_obj_set_style_text_font(fan_title, &montserrat_20_en_ru, 0);
  lv_obj_align(fan_title, LV_ALIGN_TOP_MID, 0, 10);

  // Active Output row
  lv_obj_t *fan_actual_lbl = lv_label_create(fan_panel);
  lv_label_set_text(fan_actual_lbl, "Actual Speed:");
  lv_obj_set_style_text_color(fan_actual_lbl, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(fan_actual_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(fan_actual_lbl, LV_ALIGN_TOP_LEFT, 15, 45);

  ui_Fan_Speed_Label = lv_label_create(fan_panel);
  lv_label_set_text(ui_Fan_Speed_Label, "0%");
  lv_obj_set_style_text_color(ui_Fan_Speed_Label, lv_color_hex(CLR_GREEN), 0);
  lv_obj_set_style_text_font(ui_Fan_Speed_Label, &montserrat_20_en_ru, 0);
  lv_obj_align(ui_Fan_Speed_Label, LV_ALIGN_TOP_RIGHT, -15, 40);

  // Speed progress Bar
  ui_Fan_Speed_Bar = lv_bar_create(fan_panel);
  lv_obj_set_size(ui_Fan_Speed_Bar, 310, 15);
  lv_obj_align(ui_Fan_Speed_Bar, LV_ALIGN_TOP_MID, 0, 75);
  lv_bar_set_range(ui_Fan_Speed_Bar, 0, 100);
  lv_obj_set_style_bg_color(ui_Fan_Speed_Bar, lv_color_hex(0x1F293B),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui_Fan_Speed_Bar, lv_color_hex(CLR_GREEN),
                            LV_PART_INDICATOR);

  // Divider
  lv_obj_t *div_fan1 = lv_obj_create(fan_panel);
  lv_obj_set_size(div_fan1, 310, 1);
  lv_obj_set_pos(div_fan1, 15, 105);
  lv_obj_set_style_bg_color(div_fan1, lv_color_hex(CLR_BORDER), 0);

  // Mode Auto/Manual tab controls
  ui_Fan_Mode_Auto_Btn = lv_btn_create(fan_panel);
  lv_obj_set_size(ui_Fan_Mode_Auto_Btn, 150, 40);
  lv_obj_set_pos(ui_Fan_Mode_Auto_Btn, 15, 120);
  lv_obj_set_style_radius(ui_Fan_Mode_Auto_Btn, 6, 0);
  lv_obj_set_style_shadow_width(ui_Fan_Mode_Auto_Btn, 0, 0);
  lv_obj_add_event_cb(ui_Fan_Mode_Auto_Btn, fan_mode_auto_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_t *btn3_lbl = lv_label_create(ui_Fan_Mode_Auto_Btn);
  lv_label_set_text(btn3_lbl, "AUTO");
  lv_obj_set_style_text_font(btn3_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(btn3_lbl);

  ui_Fan_Mode_Man_Btn = lv_btn_create(fan_panel);
  lv_obj_set_size(ui_Fan_Mode_Man_Btn, 150, 40);
  lv_obj_set_pos(ui_Fan_Mode_Man_Btn, 180, 120);
  lv_obj_set_style_radius(ui_Fan_Mode_Man_Btn, 6, 0);
  lv_obj_set_style_shadow_width(ui_Fan_Mode_Man_Btn, 0, 0);
  lv_obj_add_event_cb(ui_Fan_Mode_Man_Btn, fan_mode_man_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_t *btn4_lbl = lv_label_create(ui_Fan_Mode_Man_Btn);
  lv_label_set_text(btn4_lbl, "MANUAL");
  lv_obj_set_style_text_font(btn4_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(btn4_lbl);

  // Manual Control Area
  lv_obj_t *fan_sw_lbl = lv_label_create(fan_panel);
  lv_label_set_text(fan_sw_lbl, "Manual ON/OFF:");
  lv_obj_set_style_text_color(fan_sw_lbl, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(fan_sw_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(fan_sw_lbl, LV_ALIGN_TOP_LEFT, 15, 185);

  ui_Fan_Switch = lv_switch_create(fan_panel);
  lv_obj_set_size(ui_Fan_Switch, 60, 30);
  lv_obj_align(ui_Fan_Switch, LV_ALIGN_TOP_RIGHT, -15, 178);
  lv_obj_add_event_cb(ui_Fan_Switch, fan_switch_cb, LV_EVENT_VALUE_CHANGED,
                      NULL);
  lv_obj_set_style_bg_color(ui_Fan_Switch, lv_color_hex(CLR_GREEN),
                            LV_PART_INDICATOR | LV_STATE_CHECKED);

  lv_obj_t *fan_slider_lbl = lv_label_create(fan_panel);
  lv_label_set_text(fan_slider_lbl, "Manual Speed:");
  lv_obj_set_style_text_color(fan_slider_lbl, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(fan_slider_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(fan_slider_lbl, LV_ALIGN_TOP_LEFT, 15, 230);

  ui_Fan_Slider_Label = lv_label_create(fan_panel);
  lv_label_set_text(ui_Fan_Slider_Label, "50%");
  lv_obj_set_style_text_color(ui_Fan_Slider_Label, lv_color_hex(CLR_TEXT_WHITE),
                              0);
  lv_obj_set_style_text_font(ui_Fan_Slider_Label, &lv_font_montserrat_14, 0);
  lv_obj_align(ui_Fan_Slider_Label, LV_ALIGN_TOP_RIGHT, -15, 230);

  ui_Fan_Slider = lv_slider_create(fan_panel);
  lv_obj_set_size(ui_Fan_Slider, 310, 12);
  lv_obj_align(ui_Fan_Slider, LV_ALIGN_TOP_MID, 0, 260);
  lv_slider_set_value(ui_Fan_Slider, fan_manual_speed, LV_ANIM_OFF);
  lv_obj_add_event_cb(ui_Fan_Slider, fan_slider_cb, LV_EVENT_VALUE_CHANGED,
                      NULL);
  lv_obj_set_style_bg_color(ui_Fan_Slider, lv_color_hex(0x1F293B),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui_Fan_Slider, lv_color_hex(CLR_GREEN),
                            LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(ui_Fan_Slider, lv_color_hex(CLR_TEXT_WHITE),
                            LV_PART_KNOB);

  // Divider
  lv_obj_t *div_fan2 = lv_obj_create(fan_panel);
  lv_obj_set_size(div_fan2, 310, 1);
  lv_obj_set_pos(div_fan2, 15, 290);
  lv_obj_set_style_bg_color(div_fan2, lv_color_hex(CLR_BORDER), 0);

  // Auto Curve Table Header
  lv_obj_t *fan_map_title = lv_label_create(fan_panel);
  lv_label_set_text(fan_map_title, "AUTOMATIC CONFIGURE MAP");
  lv_obj_set_style_text_color(fan_map_title, lv_color_hex(CLR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(fan_map_title, &lv_font_montserrat_14, 0);
  lv_obj_align(fan_map_title, LV_ALIGN_TOP_MID, 0, 305);

  // Scrollable container for map cells
  lv_obj_t *fan_map_cont = lv_obj_create(fan_panel);
  lv_obj_set_size(fan_map_cont, 320, 730);
  lv_obj_set_pos(fan_map_cont, 10, 335);
  lv_obj_set_style_bg_opa(fan_map_cont, 0, 0); // transparent background
  lv_obj_set_style_border_width(fan_map_cont, 0, 0);
  lv_obj_set_style_pad_all(fan_map_cont, 0, 0);
  lv_obj_set_scrollbar_mode(fan_map_cont, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_flex_flow(fan_map_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(fan_map_cont, 6, 0);

  // Build the 10 rows for Electric Fan
  for (int i = 0; i < 10; i++) {
    lv_obj_t *row = lv_obj_create(fan_map_cont);
    lv_obj_set_size(row, 300, 52);
    lv_obj_set_style_bg_color(row, lv_color_hex(CLR_BTN_ACTIVE), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Index Label
    lv_obj_t *idx_lbl = lv_label_create(row);
    lv_label_set_text_fmt(idx_lbl, "#%d", i + 1);
    lv_obj_set_style_text_color(idx_lbl, lv_color_hex(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(idx_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(idx_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    // Temp adjust buttons
    uintptr_t data_minus_temp =
        (1 << 6) | (i << 2) | (0 << 1) | 0; // Fan, index i, Temp, Minus
    uintptr_t data_plus_temp =
        (1 << 6) | (i << 2) | (0 << 1) | 1; // Fan, index i, Temp, Plus

    lv_obj_t *btn_t_min = lv_btn_create(row);
    lv_obj_set_size(btn_t_min, 30, 30);
    lv_obj_align(btn_t_min, LV_ALIGN_LEFT_MID, 28, 0);
    lv_obj_set_style_bg_color(btn_t_min, lv_color_hex(CLR_BTN_BG), 0);
    lv_obj_set_style_radius(btn_t_min, 4, 0);
    lv_obj_set_style_shadow_width(btn_t_min, 0, 0);
    lv_obj_add_event_cb(btn_t_min, map_btn_cb, LV_EVENT_CLICKED,
                        (void *)data_minus_temp);
    lv_obj_t *lbl_tm = lv_label_create(btn_t_min);
    lv_label_set_text(lbl_tm, "-");
    lv_obj_set_style_text_font(lbl_tm, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_tm);

    fan_temp_labels[i] = lv_label_create(row);
    lv_label_set_text_fmt(fan_temp_labels[i], "%d°C", fan_map[i].temp);
    lv_obj_set_style_text_color(fan_temp_labels[i],
                                lv_color_hex(CLR_TEXT_WHITE), 0);
    lv_obj_set_style_text_font(fan_temp_labels[i], &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(fan_temp_labels[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(fan_temp_labels[i], 52, 20);
    lv_obj_align(fan_temp_labels[i], LV_ALIGN_LEFT_MID, 60, 0);

    lv_obj_t *btn_t_pls = lv_btn_create(row);
    lv_obj_set_size(btn_t_pls, 30, 30);
    lv_obj_align(btn_t_pls, LV_ALIGN_LEFT_MID, 114, 0);
    lv_obj_set_style_bg_color(btn_t_pls, lv_color_hex(CLR_BTN_BG), 0);
    lv_obj_set_style_radius(btn_t_pls, 4, 0);
    lv_obj_set_style_shadow_width(btn_t_pls, 0, 0);
    lv_obj_add_event_cb(btn_t_pls, map_btn_cb, LV_EVENT_CLICKED,
                        (void *)data_plus_temp);
    lv_obj_t *lbl_tp = lv_label_create(btn_t_pls);
    lv_label_set_text(lbl_tp, "+");
    lv_obj_set_style_text_font(lbl_tp, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_tp);

    // Arrow icon -> separator
    lv_obj_t *arr = lv_label_create(row);
    lv_label_set_text(arr, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(arr, lv_color_hex(CLR_GREEN), 0);
    lv_obj_set_style_text_font(arr, &lv_font_montserrat_12, 0);
    lv_obj_align(arr, LV_ALIGN_CENTER, 4, 0);

    // Speed adjust buttons
    uintptr_t data_minus_speed =
        (1 << 6) | (i << 2) | (1 << 1) | 0; // Fan, index i, Speed, Minus
    uintptr_t data_plus_speed =
        (1 << 6) | (i << 2) | (1 << 1) | 1; // Fan, index i, Speed, Plus

    lv_obj_t *btn_s_min = lv_btn_create(row);
    lv_obj_set_size(btn_s_min, 30, 30);
    lv_obj_align(btn_s_min, LV_ALIGN_RIGHT_MID, -114, 0);
    lv_obj_set_style_bg_color(btn_s_min, lv_color_hex(CLR_BTN_BG), 0);
    lv_obj_set_style_radius(btn_s_min, 4, 0);
    lv_obj_set_style_shadow_width(btn_s_min, 0, 0);
    lv_obj_add_event_cb(btn_s_min, map_btn_cb, LV_EVENT_CLICKED,
                        (void *)data_minus_speed);
    lv_obj_t *lbl_sm = lv_label_create(btn_s_min);
    lv_label_set_text(lbl_sm, "-");
    lv_obj_set_style_text_font(lbl_sm, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_sm);

    fan_speed_labels[i] = lv_label_create(row);
    lv_label_set_text_fmt(fan_speed_labels[i], "%d%%", fan_map[i].speed);
    lv_obj_set_style_text_color(fan_speed_labels[i],
                                lv_color_hex(CLR_TEXT_WHITE), 0);
    lv_obj_set_style_text_font(fan_speed_labels[i], &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(fan_speed_labels[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(fan_speed_labels[i], 52, 20);
    lv_obj_align(fan_speed_labels[i], LV_ALIGN_RIGHT_MID, -60, 0);

    lv_obj_t *btn_s_pls = lv_btn_create(row);
    lv_obj_set_size(btn_s_pls, 30, 30);
    lv_obj_align(btn_s_pls, LV_ALIGN_RIGHT_MID, -28, 0);
    lv_obj_set_style_bg_color(btn_s_pls, lv_color_hex(CLR_BTN_BG), 0);
    lv_obj_set_style_radius(btn_s_pls, 4, 0);
    lv_obj_set_style_shadow_width(btn_s_pls, 0, 0);
    lv_obj_add_event_cb(btn_s_pls, map_btn_cb, LV_EVENT_CLICKED,
                        (void *)data_plus_speed);
    lv_obj_t *lbl_sp = lv_label_create(btn_s_pls);
    lv_label_set_text(lbl_sp, "+");
    lv_obj_set_style_text_font(lbl_sp, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_sp);
  }

  update_fan_ui_state();

  // --- SWIPE GESTURE SUPPORT (DISABLED - causes crashes when dragging sliders) ---
  // lv_obj_add_event_cb(ui_Screen9, ui_screen_swipe_event_cb, LV_EVENT_GESTURE,
  //                     NULL);

  // --- NAVIGATION BUTTONS ---
  ui_create_standard_navigation_buttons(ui_Screen9);

  ESP_LOGI(TAG, "Screen 9 - Intercooler Controls successfully initialized");
}

// Lifecycle screen destroy method
void ui_Screen9_screen_destroy(void) {
  ESP_LOGI(TAG, "Destroying ui_Screen9");

  ui_Pump_Speed_Bar = NULL;
  ui_Pump_Speed_Label = NULL;
  ui_Pump_Mode_Auto_Btn = NULL;
  ui_Pump_Mode_Man_Btn = NULL;
  ui_Pump_Switch = NULL;
  ui_Pump_Slider = NULL;
  ui_Pump_Slider_Label = NULL;

  ui_Fan_Speed_Bar = NULL;
  ui_Fan_Speed_Label = NULL;
  ui_Fan_Mode_Auto_Btn = NULL;
  ui_Fan_Mode_Man_Btn = NULL;
  ui_Fan_Switch = NULL;
  ui_Fan_Slider = NULL;
  ui_Fan_Slider_Label = NULL;

  ui_Screen9_CLT_Val = NULL;
  ui_Screen9_IAT_Val = NULL;

  memset(pump_temp_labels, 0, sizeof(pump_temp_labels));
  memset(pump_speed_labels, 0, sizeof(pump_speed_labels));
  memset(fan_temp_labels, 0, sizeof(fan_temp_labels));
  memset(fan_speed_labels, 0, sizeof(fan_speed_labels));

  if (ui_Screen9) {
    lv_obj_del(ui_Screen9);
    ui_Screen9 = NULL;
  }
}

// Periodic update method (called from update_all_gauges)
void ui_Screen9_update(void) {
  if (!ui_Screen9)
    return;

  ecu_data_t data;
  if (demo_mode_get_enabled()) {
    ecu_data_simulate(&data);
  } else {
    ecu_data_get_copy(&data);
  }

  // Update live temp labels
  if (ui_Screen9_CLT_Val) {
    lv_obj_set_style_text_color(ui_Screen9_CLT_Val,
                                (data.clt_temp >= 105)
                                    ? lv_color_hex(0xFF0000)
                                    : lv_color_hex(CLR_TEXT_WHITE),
                                0);
    lv_label_set_text_fmt(ui_Screen9_CLT_Val, "CLT: %.0f °C", data.clt_temp);
  }
  if (ui_Screen9_IAT_Val) {
    lv_obj_set_style_text_color(ui_Screen9_IAT_Val,
                                (data.iat_temp >= 60) ? lv_color_hex(0xFF0000)
                                                      : lv_color_hex(CLR_GREEN),
                                0);
    lv_label_set_text_fmt(ui_Screen9_IAT_Val, "IAT: %.0f °C", data.iat_temp);
  }

  // Lookup speeds in Auto mode, or read manual controls
  int actual_pump_speed = 0;
  if (pump_is_auto) {
    // Intercooler coolant pump speed responds directly to Intake Air Temp (IAT)
    actual_pump_speed = lookup_speed(pump_map, 10, data.iat_temp);
  } else {
    actual_pump_speed = pump_manual_on ? pump_manual_speed : 0;
  }

  int actual_fan_speed = 0;
  if (fan_is_auto) {
    // Electric radiator / intercooler fan speed responds directly to IAT (or
    // CLT if it shares radiator)
    actual_fan_speed = lookup_speed(fan_map, 10, data.iat_temp);
  } else {
    actual_fan_speed = fan_manual_on ? fan_manual_speed : 0;
  }

  // Set Speed Bars and Labels
  if (ui_Pump_Speed_Bar) {
    lv_bar_set_value(ui_Pump_Speed_Bar, actual_pump_speed, LV_ANIM_OFF);
  }
  if (ui_Pump_Speed_Label) {
    lv_label_set_text_fmt(ui_Pump_Speed_Label, "%d%%", actual_pump_speed);
  }

  if (ui_Fan_Speed_Bar) {
    lv_bar_set_value(ui_Fan_Speed_Bar, actual_fan_speed, LV_ANIM_OFF);
  }
  if (ui_Fan_Speed_Label) {
    lv_label_set_text_fmt(ui_Fan_Speed_Label, "%d%%", actual_fan_speed);
  }

  // Store for Lua engine access
  s_actual_pump_speed = actual_pump_speed;
  s_actual_fan_speed = actual_fan_speed;
}

// --- Bidirectional Getters & Setters for Lua/External Engine ---
extern bool example_lvgl_lock(int timeout_ms);
extern void example_lvgl_unlock(void);

bool ui_Screen9_get_pump_is_auto(void) { return pump_is_auto; }
void ui_Screen9_set_pump_is_auto(bool is_auto) {
  pump_is_auto = is_auto;
  if (ui_Screen9) {
    if (example_lvgl_lock(500)) {
      update_pump_ui_state();
      example_lvgl_unlock();
    }
  }
}

bool ui_Screen9_get_pump_manual_on(void) { return pump_manual_on; }
void ui_Screen9_set_pump_manual_on(bool manual_on) {
  pump_manual_on = manual_on;
  if (ui_Screen9 && ui_Pump_Switch) {
    if (example_lvgl_lock(500)) {
      if (manual_on) {
        lv_obj_add_state(ui_Pump_Switch, LV_STATE_CHECKED);
      } else {
        lv_obj_clear_state(ui_Pump_Switch, LV_STATE_CHECKED);
      }
      example_lvgl_unlock();
    }
  }
}

int ui_Screen9_get_pump_manual_speed(void) { return pump_manual_speed; }
void ui_Screen9_set_pump_manual_speed(int speed) {
  if (speed < 0) speed = 0;
  if (speed > 100) speed = 100;
  pump_manual_speed = speed;
  if (ui_Screen9 && ui_Pump_Slider) {
    if (example_lvgl_lock(500)) {
      lv_slider_set_value(ui_Pump_Slider, speed, LV_ANIM_OFF);
      if (ui_Pump_Slider_Label) {
        lv_label_set_text_fmt(ui_Pump_Slider_Label, "%d%%", speed);
      }
      example_lvgl_unlock();
    }
  }
}

bool ui_Screen9_get_fan_is_auto(void) { return fan_is_auto; }
void ui_Screen9_set_fan_is_auto(bool is_auto) {
  fan_is_auto = is_auto;
  if (ui_Screen9) {
    if (example_lvgl_lock(500)) {
      update_fan_ui_state();
      example_lvgl_unlock();
    }
  }
}

bool ui_Screen9_get_fan_manual_on(void) { return fan_manual_on; }
void ui_Screen9_set_fan_manual_on(bool manual_on) {
  fan_manual_on = manual_on;
  if (ui_Screen9 && ui_Fan_Switch) {
    if (example_lvgl_lock(500)) {
      if (manual_on) {
        lv_obj_add_state(ui_Fan_Switch, LV_STATE_CHECKED);
      } else {
        lv_obj_clear_state(ui_Fan_Switch, LV_STATE_CHECKED);
      }
      example_lvgl_unlock();
    }
  }
}

int ui_Screen9_get_fan_manual_speed(void) { return fan_manual_speed; }
void ui_Screen9_set_fan_manual_speed(int speed) {
  if (speed < 0) speed = 0;
  if (speed > 100) speed = 100;
  fan_manual_speed = speed;
  if (ui_Screen9 && ui_Fan_Slider) {
    if (example_lvgl_lock(500)) {
      lv_slider_set_value(ui_Fan_Slider, speed, LV_ANIM_OFF);
      if (ui_Fan_Slider_Label) {
        lv_label_set_text_fmt(ui_Fan_Slider_Label, "%d%%", speed);
      }
      example_lvgl_unlock();
    }
  }
}

int ui_Screen9_get_pump_map_temp(int idx) {
  if (idx < 0 || idx >= 10) return 0;
  return pump_map[idx].temp;
}

void ui_Screen9_set_pump_map_temp(int idx, int temp) {
  if (idx < 0 || idx >= 10) return;
  pump_map[idx].temp = temp;
  if (ui_Screen9 && pump_temp_labels[idx]) {
    if (example_lvgl_lock(500)) {
      lv_label_set_text_fmt(pump_temp_labels[idx], "%d°C", temp);
      example_lvgl_unlock();
    }
  }
}

int ui_Screen9_get_pump_map_speed(int idx) {
  if (idx < 0 || idx >= 10) return 0;
  return pump_map[idx].speed;
}

void ui_Screen9_set_pump_map_speed(int idx, int speed) {
  if (idx < 0 || idx >= 10) return;
  if (speed < 0) speed = 0;
  if (speed > 100) speed = 100;
  pump_map[idx].speed = speed;
  if (ui_Screen9 && pump_speed_labels[idx]) {
    if (example_lvgl_lock(500)) {
      lv_label_set_text_fmt(pump_speed_labels[idx], "%d%%", speed);
      example_lvgl_unlock();
    }
  }
}

int ui_Screen9_get_fan_map_temp(int idx) {
  if (idx < 0 || idx >= 10) return 0;
  return fan_map[idx].temp;
}

void ui_Screen9_set_fan_map_temp(int idx, int temp) {
  if (idx < 0 || idx >= 10) return;
  fan_map[idx].temp = temp;
  if (ui_Screen9 && fan_temp_labels[idx]) {
    if (example_lvgl_lock(500)) {
      lv_label_set_text_fmt(fan_temp_labels[idx], "%d°C", temp);
      example_lvgl_unlock();
    }
  }
}

int ui_Screen9_get_fan_map_speed(int idx) {
  if (idx < 0 || idx >= 10) return 0;
  return fan_map[idx].speed;
}

void ui_Screen9_set_fan_map_speed(int idx, int speed) {
  if (idx < 0 || idx >= 10) return;
  if (speed < 0) speed = 0;
  if (speed > 100) speed = 100;
  fan_map[idx].speed = speed;
  if (ui_Screen9 && fan_speed_labels[idx]) {
    if (example_lvgl_lock(500)) {
      lv_label_set_text_fmt(fan_speed_labels[idx], "%d%%", speed);
      example_lvgl_unlock();
    }
  }
}
