#include "ui_Screen10.h"
#include "../ui.h"
#include "../ui_screen_manager.h"
#include "ecu_data.h"
#include "esp_log.h"
#include "settings_config.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "SCREEN10";

// Root screen pointer
lv_obj_t *ui_Screen10 = NULL;

// Header live parameters
lv_obj_t *ui_Screen10_MAP_Val = NULL;
lv_obj_t *ui_Screen10_IAT_Val = NULL;

// MRE Gauges in Blow-Off Panel
lv_obj_t *ui_Arc_ECU_MAP_Screen10 = NULL;
lv_obj_t *ui_Label_ECU_MAP_Value_Screen10 = NULL;
lv_obj_t *ui_Arc_MRE_MAP_Screen10 = NULL;
lv_obj_t *ui_Label_MRE_MAP_Value_Screen10 = NULL;
lv_obj_t *ui_Arc_MRE_Wastegate_Screen10 = NULL;
lv_obj_t *ui_Label_MRE_Wastegate_Value_Screen10 = NULL;

// Title pointer to override dynamically
static lv_obj_t *ui_Wg_Actual_Title = NULL;

// Wastegate VGT elements
lv_obj_t *ui_Wg_Actual_Bar = NULL;
lv_obj_t *ui_Wg_Actual_Label = NULL;
lv_obj_t *ui_Wg_Mode_Auto_Btn = NULL;
lv_obj_t *ui_Wg_Mode_Man_Btn = NULL;
lv_obj_t *ui_Wg_Slider = NULL;
lv_obj_t *ui_Wg_Slider_Label = NULL;
lv_obj_t *ui_Wg_Invert_Switch = NULL;

// DSG Mode Indicator elements
lv_obj_t *ui_Mode_Panel = NULL;
lv_obj_t *ui_Mode_Val_Label = NULL;
lv_obj_t *ui_Mode_Info_Boost_Val = NULL;
lv_obj_t *ui_Mode_Info_Tps_Val = NULL;
lv_obj_t *ui_Mode_Info_Delta_Val = NULL;
lv_obj_t *ui_Mode_Info_Hold_Val = NULL;
lv_obj_t *ui_Mode_Info_Bypass_Val = NULL;

// Blow-off Solenoid elements
lv_obj_t *ui_Bov_State_Label = NULL;
lv_obj_t *ui_Bov_Led = NULL;
lv_obj_t *ui_Bov_Mode_Auto_Btn = NULL;
lv_obj_t *ui_Bov_Mode_Man_Btn = NULL;
lv_obj_t *ui_Bov_Switch = NULL;

// Auto Triggers (ME7 LDUVST parameters)
lv_obj_t *ui_Bov_Tps_Slider = NULL;
lv_obj_t *ui_Bov_Tps_Slider_Label = NULL;
lv_obj_t *ui_Bov_Press_Slider = NULL;
lv_obj_t *ui_Bov_Press_Slider_Label = NULL;
lv_obj_t *ui_Bov_Dur_Slider = NULL;
lv_obj_t *ui_Bov_Dur_Slider_Label = NULL;
lv_obj_t *ui_Bov_Stat_Switch = NULL;
lv_obj_t *ui_Bov_Stat_Ratio_Slider = NULL;
lv_obj_t *ui_Bov_Stat_Ratio_Label = NULL;

// Help Popup pointer
static lv_obj_t *help_popup_overlay = NULL;

// Color palette
#define CLR_BG 0x0A0F1A
#define CLR_PANEL 0x111827
#define CLR_BORDER 0x334155
#define CLR_CYAN 0x00D4FF  // Wastegate (Cyan)
#define CLR_GOLD 0xFFD700  // Blow-off (Gold)
#define CLR_GREEN 0x00FF88 // IAT Green
#define CLR_BTN_BG 0x1E293B
#define CLR_BTN_ACTIVE 0x0F172A
#define CLR_TEXT_DIM 0x94A3B8
#define CLR_TEXT_WHITE 0xF1F5F9
#define CLR_LED_OFF 0x3A2E00
#define CLR_LED_ON 0xFFD700

// Fonts declared in ui.h
LV_FONT_DECLARE(lv_font_montserrat_10);
LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(montserrat_20_en_ru);

// Wastegate Map Point Type
typedef struct {
  int rpm; // Engine RPM
  int pos; // Position in %
} wg_map_point_t;

// Persistent states
static bool wg_is_auto = true;
static int wg_manual_pos = 50;
static bool wg_is_inverted = false;

// Default VGT target boost map (in kPa absolute)
static wg_map_point_t wg_map[10] = {
    {1000, 100}, {1500, 120}, {2000, 150}, {2500, 180}, {3000, 200},
    {3500, 220}, {4000, 220}, {5000, 200}, {6000, 180}, {7000, 160}};

static bool bov_is_auto = true;
static bool bov_manual_open = false;

// Default ME7.1 values
static int bov_tps_threshold = 25;   // GWPLDU: 25% drop per 100ms
static int bov_press_threshold = 35; // SDLDSUA: 35 kPa (350 hPa)
static int bov_open_duration = 20; // THLDUVD: 2.0s (stored as 20 for tenths of
                                   // seconds, range 0.5s to 3.0s)
static bool bov_stat_enabled = true; // Stationary path enabled
static int bov_stat_ratio =
    120; // SVDLDUVS: 1.20 (stored as 120, range 1.05 to 1.50)

// Computed outputs for background logic / Lua
static int s_actual_wg_pos = 0;
static int s_actual_bov_state = 0; // 0 = closed, 100 = open (flashing)

// Dynamic labels for Wastegate Map
static lv_obj_t *wg_rpm_labels[10];
static lv_obj_t *wg_pos_labels[10];

static void sync_local_to_settings(void) {
  settings_set_wg_is_auto(wg_is_auto);
  settings_set_wg_manual_pos(wg_manual_pos);
  settings_set_wg_is_inverted(wg_is_inverted);
  settings_set_bov_is_auto(bov_is_auto);
  settings_set_bov_manual_open(bov_manual_open);
  settings_set_bov_tps_threshold(bov_tps_threshold);
  settings_set_bov_press_threshold(bov_press_threshold);
  settings_set_bov_open_duration(bov_open_duration);
  settings_set_bov_stat_enabled(bov_stat_enabled);
  settings_set_bov_stat_ratio(bov_stat_ratio);
}

// ---------- Forward declarations for UI Event Handlers ----------
static void update_wg_ui_state(void);
static void update_bov_ui_state(void);
static void wg_mode_auto_cb(lv_event_t *e);
static void wg_mode_man_cb(lv_event_t *e);
static void bov_mode_auto_cb(lv_event_t *e);
static void bov_mode_man_cb(lv_event_t *e);
static void wg_slider_cb(lv_event_t *e);
static void wg_invert_switch_cb(lv_event_t *e);
static void bov_switch_cb(lv_event_t *e);
// static void wg_map_btn_cb(lv_event_t *e);

static void bov_tps_slider_cb(lv_event_t *e);
static void bov_press_slider_cb(lv_event_t *e);
static void bov_dur_slider_cb(lv_event_t *e);
static void bov_stat_switch_cb(lv_event_t *e);
static void bov_stat_ratio_slider_cb(lv_event_t *e);

static void help_btn_cb(lv_event_t *e);
static void close_help_cb(lv_event_t *e);

/*
// Linear interpolation lookup logic for Wastegate
static int lookup_wg_position(int current_rpm) {
  if (current_rpm <= wg_map[0].rpm) {
    return wg_map[0].pos;
  }
  if (current_rpm >= wg_map[9].rpm) {
    return wg_map[9].pos;
  }
  for (int i = 0; i < 9; i++) {
    if (current_rpm >= wg_map[i].rpm && current_rpm <= wg_map[i + 1].rpm) {
      float r0 = wg_map[i].rpm;
      float r1 = wg_map[i + 1].rpm;
      float p0 = wg_map[i].pos;
      float p1 = wg_map[i + 1].pos;
      float ratio = (current_rpm - r0) / (r1 - r0);
      return (int)(p0 + ratio * (p1 - p0));
    }
  }
  return 0;
}

// Wastegate Map Adjust Button Event Handler
static void wg_map_btn_cb(lv_event_t *e) {
  uintptr_t val = (uintptr_t)lv_event_get_user_data(e);
  bool is_plus = val & 1;
  bool is_pos = (val >> 1) & 1;
  int idx = (val >> 2) & 0x0F;

  if (is_pos) {
    int step = 10; // 10 kPa steps
    int curr = wg_map[idx].pos;
    curr += is_plus ? step : -step;
    if (curr < 100)
      curr = 100; // atmospheric pressure limit
    if (curr > 300)
      curr = 300; // 2.0 bar gauge boost limit
    wg_map[idx].pos = curr;
    lv_label_set_text_fmt(wg_pos_labels[idx], "%d kPa", curr);
  } else {
    int step = 100;
    int curr = wg_map[idx].rpm;
    curr += is_plus ? step : -step;

    // Boundary checks to maintain strictly sorted RPMs
    int min_val = (idx > 0) ? wg_map[idx - 1].rpm + 100 : 500;
    int max_val = (idx < 9) ? wg_map[idx + 1].rpm - 100 : 9000;

    if (curr < min_val)
      curr = min_val;
    if (curr > max_val)
      curr = max_val;

    wg_map[idx].rpm = curr;
    lv_label_set_text_fmt(wg_rpm_labels[idx], "%d", curr);
  }
}
*/

// Update Wastegate VGT UI State (locks manual slider if in auto mode)
static void update_wg_ui_state(void) {
  if (!ui_Screen10)
    return;
  if (wg_is_auto) {
    lv_obj_set_style_bg_color(ui_Wg_Mode_Auto_Btn, lv_color_hex(CLR_CYAN), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Wg_Mode_Auto_Btn, 0),
                                lv_color_black(), 0);

    lv_obj_set_style_bg_color(ui_Wg_Mode_Man_Btn, lv_color_hex(CLR_BTN_BG), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Wg_Mode_Man_Btn, 0),
                                lv_color_hex(CLR_TEXT_DIM), 0);

    lv_obj_set_style_opa(ui_Wg_Slider, LV_OPA_40, 0);
  } else {
    lv_obj_set_style_bg_color(ui_Wg_Mode_Auto_Btn, lv_color_hex(CLR_BTN_BG), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Wg_Mode_Auto_Btn, 0),
                                lv_color_hex(CLR_TEXT_DIM), 0);

    lv_obj_set_style_bg_color(ui_Wg_Mode_Man_Btn, lv_color_hex(CLR_CYAN), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Wg_Mode_Man_Btn, 0),
                                lv_color_black(), 0);

    lv_obj_set_style_opa(ui_Wg_Slider, LV_OPA_COVER, 0);
  }

  if (ui_Wg_Slider) {
    lv_slider_set_value(ui_Wg_Slider, wg_manual_pos, LV_ANIM_OFF);
    if (ui_Wg_Slider_Label) {
      lv_label_set_text_fmt(ui_Wg_Slider_Label, "%d%%", wg_manual_pos);
    }
  }

  if (ui_Wg_Invert_Switch) {
    if (wg_is_inverted) {
      lv_obj_add_state(ui_Wg_Invert_Switch, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(ui_Wg_Invert_Switch, LV_STATE_CHECKED);
    }
  }
}

// Update Blow-off Solenoid UI State
static void update_bov_ui_state(void) {
  if (!ui_Screen10)
    return;
  if (bov_is_auto) {
    lv_obj_set_style_bg_color(ui_Bov_Mode_Auto_Btn, lv_color_hex(CLR_GOLD), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Bov_Mode_Auto_Btn, 0),
                                lv_color_black(), 0);

    lv_obj_set_style_bg_color(ui_Bov_Mode_Man_Btn, lv_color_hex(CLR_BTN_BG), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Bov_Mode_Man_Btn, 0),
                                lv_color_hex(CLR_TEXT_DIM), 0);

    lv_obj_set_style_opa(ui_Bov_Switch, LV_OPA_40, 0);
  } else {
    lv_obj_set_style_bg_color(ui_Bov_Mode_Auto_Btn, lv_color_hex(CLR_BTN_BG),
                              0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Bov_Mode_Auto_Btn, 0),
                                lv_color_hex(CLR_TEXT_DIM), 0);

    lv_obj_set_style_bg_color(ui_Bov_Mode_Man_Btn, lv_color_hex(CLR_GOLD), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Bov_Mode_Man_Btn, 0),
                                lv_color_black(), 0);

    lv_obj_set_style_opa(ui_Bov_Switch, LV_OPA_COVER, 0);
  }

  if (ui_Bov_Switch) {
    if (bov_manual_open) {
      lv_obj_add_state(ui_Bov_Switch, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(ui_Bov_Switch, LV_STATE_CHECKED);
    }
  }

  // Auto parameters sliders
  if (ui_Bov_Tps_Slider) {
    lv_slider_set_value(ui_Bov_Tps_Slider, bov_tps_threshold, LV_ANIM_OFF);
    lv_label_set_text_fmt(ui_Bov_Tps_Slider_Label, "%d%% / 100ms",
                          bov_tps_threshold);
  }
  if (ui_Bov_Press_Slider) {
    lv_slider_set_value(ui_Bov_Press_Slider, bov_press_threshold, LV_ANIM_OFF);
    lv_label_set_text_fmt(ui_Bov_Press_Slider_Label, "%d kPa",
                          bov_press_threshold);
  }
  if (ui_Bov_Dur_Slider) {
    lv_slider_set_value(ui_Bov_Dur_Slider, bov_open_duration, LV_ANIM_OFF);
    lv_label_set_text_fmt(ui_Bov_Dur_Slider_Label, "%.1fs",
                          (float)bov_open_duration / 10.0f);
  }
  if (ui_Bov_Stat_Switch) {
    if (bov_stat_enabled) {
      lv_obj_add_state(ui_Bov_Stat_Switch, LV_STATE_CHECKED);
      lv_obj_set_style_opa(ui_Bov_Stat_Ratio_Slider, LV_OPA_COVER, 0);
    } else {
      lv_obj_clear_state(ui_Bov_Stat_Switch, LV_STATE_CHECKED);
      lv_obj_set_style_opa(ui_Bov_Stat_Ratio_Slider, LV_OPA_40, 0);
    }
  }
  if (ui_Bov_Stat_Ratio_Slider) {
    lv_slider_set_value(ui_Bov_Stat_Ratio_Slider, bov_stat_ratio, LV_ANIM_OFF);
    lv_label_set_text_fmt(ui_Bov_Stat_Ratio_Label, "%.2f",
                          (float)bov_stat_ratio / 100.0f);
  }
}

// Mode callbacks
static void wg_mode_auto_cb(lv_event_t *e) {
  wg_is_auto = true;
  update_wg_ui_state();
  sync_local_to_settings();
  ESP_LOGI(TAG, "Wastegate VGT Mode: AUTO");
}

static void wg_mode_man_cb(lv_event_t *e) {
  wg_is_auto = false;
  update_wg_ui_state();
  sync_local_to_settings();
  ESP_LOGI(TAG, "Wastegate VGT Mode: MANUAL");
}

static void bov_mode_auto_cb(lv_event_t *e) {
  bov_is_auto = true;
  update_bov_ui_state();
  sync_local_to_settings();
  ESP_LOGI(TAG, "Blow-off Mode: AUTO");
}

static void bov_mode_man_cb(lv_event_t *e) {
  bov_is_auto = false;
  update_bov_ui_state();
  sync_local_to_settings();
  ESP_LOGI(TAG, "Blow-off Mode: MANUAL");
}

// Sliders and Switches callbacks
static void wg_slider_cb(lv_event_t *e) {
  if (wg_is_auto) {
    lv_slider_set_value(ui_Wg_Slider, wg_manual_pos, LV_ANIM_OFF);
    return;
  }
  wg_manual_pos = lv_slider_get_value(ui_Wg_Slider);
  lv_label_set_text_fmt(ui_Wg_Slider_Label, "%d%%", wg_manual_pos);
  sync_local_to_settings();
}

static void wg_invert_switch_cb(lv_event_t *e) {
  wg_is_inverted = lv_obj_has_state(ui_Wg_Invert_Switch, LV_STATE_CHECKED);
  sync_local_to_settings();
  ESP_LOGI(TAG, "Wastegate Inversion State: %s",
           wg_is_inverted ? "INVERTED" : "NORMAL");
}

static void bov_switch_cb(lv_event_t *e) {
  if (bov_is_auto) {
    if (bov_manual_open) {
      lv_obj_add_state(ui_Bov_Switch, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(ui_Bov_Switch, LV_STATE_CHECKED);
    }
    return;
  }
  bov_manual_open = lv_obj_has_state(ui_Bov_Switch, LV_STATE_CHECKED);
  sync_local_to_settings();
  ESP_LOGI(TAG, "Blow-off Manual State: %s",
           bov_manual_open ? "OPEN" : "CLOSED");
}

static void bov_tps_slider_cb(lv_event_t *e) {
  bov_tps_threshold = lv_slider_get_value(ui_Bov_Tps_Slider);
  lv_label_set_text_fmt(ui_Bov_Tps_Slider_Label, "%d%% / 100ms",
                        bov_tps_threshold);
  sync_local_to_settings();
}

static void bov_press_slider_cb(lv_event_t *e) {
  bov_press_threshold = lv_slider_get_value(ui_Bov_Press_Slider);
  lv_label_set_text_fmt(ui_Bov_Press_Slider_Label, "%d kPa",
                        bov_press_threshold);
  sync_local_to_settings();
}

static void bov_dur_slider_cb(lv_event_t *e) {
  bov_open_duration = lv_slider_get_value(ui_Bov_Dur_Slider);
  lv_label_set_text_fmt(ui_Bov_Dur_Slider_Label, "%.1fs",
                        (float)bov_open_duration / 10.0f);
  sync_local_to_settings();
}

static void bov_stat_switch_cb(lv_event_t *e) {
  bov_stat_enabled = lv_obj_has_state(ui_Bov_Stat_Switch, LV_STATE_CHECKED);
  if (bov_stat_enabled) {
    lv_obj_set_style_opa(ui_Bov_Stat_Ratio_Slider, LV_OPA_COVER, 0);
  } else {
    lv_obj_set_style_opa(ui_Bov_Stat_Ratio_Slider, LV_OPA_40, 0);
  }
  sync_local_to_settings();
  ESP_LOGI(TAG, "Blow-off Stationary Path: %s",
           bov_stat_enabled ? "ENABLED" : "DISABLED");
}

static void bov_stat_ratio_slider_cb(lv_event_t *e) {
  if (!bov_stat_enabled) {
    lv_slider_set_value(ui_Bov_Stat_Ratio_Slider, bov_stat_ratio, LV_ANIM_OFF);
    return;
  }
  bov_stat_ratio = lv_slider_get_value(ui_Bov_Stat_Ratio_Slider);
  lv_label_set_text_fmt(ui_Bov_Stat_Ratio_Label, "%.2f",
                        (float)bov_stat_ratio / 100.0f);
  sync_local_to_settings();
}

// Help Modal implementation
static void help_btn_cb(lv_event_t *e) {
  if (help_popup_overlay)
    return;

  help_popup_overlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(help_popup_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(help_popup_overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(help_popup_overlay, LV_OPA_70, 0);
  lv_obj_clear_flag(help_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *popup = lv_obj_create(help_popup_overlay);
  lv_obj_set_size(popup, 600, 950);
  lv_obj_center(popup);
  lv_obj_set_style_bg_color(popup, lv_color_hex(CLR_PANEL), 0);
  lv_obj_set_style_border_color(popup, lv_color_hex(CLR_GOLD), 0);
  lv_obj_set_style_border_width(popup, 2, 0);
  lv_obj_set_style_radius(popup, 12, 0);
  lv_obj_set_style_pad_all(popup, 20, 0);
  lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(popup);
  lv_label_set_text(title, "HELP: BOSCH ME7.1 BOOST ALGORITHMS");
  lv_obj_set_style_text_color(title, lv_color_hex(CLR_GOLD), 0);
  lv_obj_set_style_text_font(title, &montserrat_20_en_ru, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

  // Scrollable container for text explanation
  lv_obj_t *text_cont = lv_obj_create(popup);
  lv_obj_set_size(text_cont, 560, 780);
  lv_obj_align(text_cont, LV_ALIGN_TOP_MID, 0, 45);
  lv_obj_set_style_bg_opa(text_cont, 0, 0);
  lv_obj_set_style_border_width(text_cont, 0, 0);
  lv_obj_set_style_pad_all(text_cont, 5, 0);
  lv_obj_set_scrollbar_mode(text_cont, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_flex_flow(text_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(text_cont, 15, 0);

  // Description elements
  lv_obj_t *lbl_desc = lv_label_create(text_cont);
  lv_obj_set_width(lbl_desc, 530);
  lv_label_set_text(lbl_desc,
                    "This screen implements boost and charge air bypass "
                    "control based on factory "
                    "Bosch Motronic ME7.1 strategies (LDR and LDUVST) adapted "
                    "for Speed-Density / MPI setups.\n\n"
                    "1. CLOSED-LOOP WASTEGATE CONTROL (LDR PID):\n"
                    "In AUTO mode, you set the desired Target Boost Pressure "
                    "in kPa absolute (100 to 300 kPa) "
                    "for each RPM step. A software PID feedback controller "
                    "compares this target against actual "
                    "manifold pressure (MAP) and commands the electronic "
                    "wastegate servo actuator accordingly.\n"
                    "• Feedforward & Spool-up logic:\n"
                    "A baseline position (Feedforward) is estimated from the "
                    "requested pressure ratio. During "
                    "rapid spool-up (pedal > 75% and actual pressure is far "
                    "below target), the controller overrides "
                    "the PID loop and forces the wastegate 100% closed to "
                    "minimize lag, mimicking factory Bosch logic.");
  lv_obj_set_style_text_color(lbl_desc, lv_color_hex(CLR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(lbl_desc, &lv_font_montserrat_14, 0);

  lv_obj_t *lbl_bov_desc = lv_label_create(text_cont);
  lv_obj_set_width(lbl_bov_desc, 530);
  lv_label_set_text(
      lbl_bov_desc,
      "2. BLOW-OFF SOLENOID (LDUVST):\n"
      "The N249 recirculating diverter valve is electrically operated to "
      "bypass the "
      "compressor during deceleration or overboost. It operates via two "
      "paths:\n\n"
      "A) Dynamic Path (Dynamischer Pfad):\n"
      "Triggers during rapid throttle lift-off. Parameters:\n"
      "• TPS Drop Limit (GWPLDU): Throttle position drop rate over 100ms. "
      "Factory default: 25%.\n"
      "• Boost Delta (SDLDSUA): Overshoot difference between actual and target "
      "boost. "
      "Factory default: 35 kPa (350 hPa).\n"
      "• Hold Duration (THLDUVD): Opening hold time for the solenoid. "
      "Factory default: 2.0 seconds.\n\n"
      "B) Stationary Path (Stationärer Pfad):\n"
      "• Pressure Ratio (SVDLDUVS): If compressor pressure ratio (MAP / "
      "Ambient) "
      "drops below this threshold (factory default: 1.20) during engine "
      "braking, "
      "the solenoid opens the bypass port to eliminate compressor surge.");
  lv_obj_set_style_text_color(lbl_bov_desc, lv_color_hex(CLR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(lbl_bov_desc, &lv_font_montserrat_14, 0);

  lv_obj_t *close_btn = lv_btn_create(popup);
  lv_obj_set_size(close_btn, 180, 45);
  lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_set_style_bg_color(close_btn, lv_color_hex(CLR_BTN_BG), 0);
  lv_obj_set_style_border_color(close_btn, lv_color_hex(CLR_BORDER), 0);
  lv_obj_set_style_border_width(close_btn, 1, 0);
  lv_obj_add_event_cb(close_btn, close_help_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *close_lbl = lv_label_create(close_btn);
  lv_label_set_text(close_lbl, "CLOSE");
  lv_obj_set_style_text_color(close_lbl, lv_color_hex(CLR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(close_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(close_lbl);
}

static void close_help_cb(lv_event_t *e) {
  if (help_popup_overlay) {
    lv_obj_del(help_popup_overlay);
    help_popup_overlay = NULL;
  }
}

// Helper function to create a smaller gauge (fits inside columns/panels)
static void create_small_gauge(lv_obj_t *parent, lv_obj_t **arc,
                               lv_obj_t **label, const char *title,
                               const char *unit, lv_color_t color,
                               int32_t min_val, int32_t max_val, int x, int y,
                               gauge_id_t gauge_id) {
  lv_obj_t *cont = lv_obj_create(parent);
  lv_obj_set_width(cont, 145);
  lv_obj_set_height(cont, 180);
  lv_obj_set_x(cont, x);
  lv_obj_set_y(cont, y);
  lv_obj_set_align(cont, LV_ALIGN_TOP_LEFT);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(cont, lv_color_hex(0x2a2a2a), 0);
  lv_obj_set_style_border_color(cont, color, 0);
  lv_obj_set_style_border_width(cont, 1, 0);
  lv_obj_set_style_radius(cont, 10, 0);
  lv_obj_set_style_pad_all(cont, 5, 0);
  lv_obj_set_style_shadow_width(cont, 0, 0);

  // Add click support for unit switching
  lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(cont, ui_gauge_click_event_cb, LV_EVENT_CLICKED,
                      (void *)(uintptr_t)gauge_id);

  lv_obj_t *label_title = lv_label_create(cont);
  lv_label_set_text(label_title, title);
  lv_obj_set_style_text_color(label_title, lv_color_white(), 0);
  lv_obj_set_style_text_font(label_title, &lv_font_montserrat_12, 0);
  lv_obj_align(label_title, LV_ALIGN_BOTTOM_MID, 0, -5);

  *arc = lv_arc_create(cont);
  lv_obj_set_size(*arc, 100, 100);
  lv_arc_set_rotation(*arc, 135);
  lv_arc_set_bg_angles(*arc, 0, 270);
  lv_arc_set_range(*arc, min_val, max_val);
  lv_arc_set_value(*arc, min_val);
  lv_obj_set_style_arc_color(*arc, color, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(*arc, 8, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(*arc, lv_color_hex(0x4a4a4a), LV_PART_MAIN);
  lv_obj_set_style_arc_width(*arc, 8, LV_PART_MAIN);
  lv_obj_center(*arc);
  lv_obj_align(*arc, LV_ALIGN_TOP_MID, 0, 5);
  lv_obj_remove_style(*arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(*arc, LV_OBJ_FLAG_CLICKABLE);

  *label = lv_label_create(cont);
  lv_label_set_text(*label, "0");
  lv_obj_set_style_text_color(*label, lv_color_white(), 0);
  lv_obj_set_style_text_font(*label, &lv_font_montserrat_14, 0);
  lv_obj_align_to(*label, *arc, LV_ALIGN_CENTER, 0, -5);

  lv_obj_t *label_unit = lv_label_create(cont);
  lv_label_set_text(label_unit, unit);
  lv_obj_set_style_text_color(label_unit, lv_color_hex(0xcccccc), 0);
  lv_obj_set_style_text_font(label_unit, &lv_font_montserrat_10, 0);
  lv_obj_align_to(label_unit, *label, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
}

// Screen initialization
void ui_Screen10_screen_init(void) {
  ESP_LOGI(TAG, "Initializing Screen 10 (Wastegate/Blow-off)...");

  // Load persistent configurations
  wg_is_auto = settings_get_wg_is_auto();
  wg_manual_pos = settings_get_wg_manual_pos();
  wg_is_inverted = settings_get_wg_is_inverted();
  bov_is_auto = settings_get_bov_is_auto();
  bov_manual_open = settings_get_bov_manual_open();
  bov_tps_threshold = settings_get_bov_tps_threshold();
  bov_press_threshold = settings_get_bov_press_threshold();
  bov_open_duration = settings_get_bov_open_duration();
  bov_stat_enabled = settings_get_bov_stat_enabled();
  bov_stat_ratio = settings_get_bov_stat_ratio();

  memset(wg_rpm_labels, 0, sizeof(wg_rpm_labels));
  memset(wg_pos_labels, 0, sizeof(wg_pos_labels));

  // --- ROOT SCREEN ---
  ui_Screen10 = lv_obj_create(NULL);
  lv_obj_set_size(ui_Screen10, 720, 1280);
  lv_obj_clear_flag(ui_Screen10, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(ui_Screen10, lv_color_hex(CLR_BG), 0);
  lv_obj_set_style_bg_opa(ui_Screen10, LV_OPA_COVER, 0);

  // --- HEADER PANEL ---
  lv_obj_t *header_panel = lv_obj_create(ui_Screen10);
  lv_obj_set_size(header_panel, 700, 80);
  lv_obj_set_pos(header_panel, 10, 10);
  lv_obj_set_style_bg_color(header_panel, lv_color_hex(CLR_PANEL), 0);
  lv_obj_set_style_bg_opa(header_panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(header_panel, lv_color_hex(CLR_BORDER), 0);
  lv_obj_set_style_border_width(header_panel, 1, 0);
  lv_obj_set_style_radius(header_panel, 10, 0);
  lv_obj_clear_flag(header_panel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(header_panel);
  lv_label_set_text(title, "WASTEGATE & BLOW-OFF");
  lv_obj_set_style_text_color(title, lv_color_hex(CLR_CYAN), 0);
  lv_obj_set_style_text_font(title, &montserrat_20_en_ru, 0);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 20, 0);

  // Help Button
  lv_obj_t *help_btn = lv_btn_create(header_panel);
  lv_obj_set_size(help_btn, 110, 42);
  lv_obj_align(help_btn, LV_ALIGN_RIGHT_MID, -10, 0);
  lv_obj_set_style_bg_color(help_btn, lv_color_hex(CLR_BTN_BG), 0);
  lv_obj_set_style_border_color(help_btn, lv_color_hex(CLR_BORDER), 0);
  lv_obj_set_style_border_width(help_btn, 1, 0);
  lv_obj_set_style_radius(help_btn, 6, 0);
  lv_obj_set_style_shadow_width(help_btn, 0, 0);
  lv_obj_add_event_cb(help_btn, help_btn_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *help_lbl = lv_label_create(help_btn);
  lv_label_set_text(help_lbl, "HELP");
  lv_obj_set_style_text_color(help_lbl, lv_color_hex(CLR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(help_lbl, &lv_font_montserrat_12, 0);
  lv_obj_center(help_lbl);

  // ==========================================
  // LEFT COLUMN: SERVO WASTEGATE (CYAN)
  // ==========================================
  lv_obj_t *wg_panel = lv_obj_create(ui_Screen10);
  lv_obj_set_size(wg_panel, 340, 1080);
  lv_obj_set_pos(wg_panel, 10, 100);
  lv_obj_set_style_bg_color(wg_panel, lv_color_hex(CLR_PANEL), 0);
  lv_obj_set_style_bg_opa(wg_panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(wg_panel, lv_color_hex(CLR_BORDER), 0);
  lv_obj_set_style_border_width(wg_panel, 1, 0);
  lv_obj_set_style_radius(wg_panel, 12, 0);
  lv_obj_clear_flag(wg_panel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *wg_title = lv_label_create(wg_panel);
  lv_label_set_text(wg_title, "SERVO WASTEGATE");
  lv_obj_set_style_text_color(wg_title, lv_color_hex(CLR_CYAN), 0);
  lv_obj_set_style_text_font(wg_title, &montserrat_20_en_ru, 0);
  lv_obj_align(wg_title, LV_ALIGN_TOP_MID, 0, 10);

  // Actual feedback
  ui_Wg_Actual_Title = lv_label_create(wg_panel);
  lv_label_set_text(ui_Wg_Actual_Title, "Actual Geometry:");
  lv_obj_set_style_text_color(ui_Wg_Actual_Title, lv_color_hex(CLR_TEXT_DIM),
                              0);
  lv_obj_set_style_text_font(ui_Wg_Actual_Title, &lv_font_montserrat_14, 0);
  lv_obj_align(ui_Wg_Actual_Title, LV_ALIGN_TOP_LEFT, 15, 45);

  ui_Wg_Actual_Label = lv_label_create(wg_panel);
  lv_label_set_text(ui_Wg_Actual_Label, "0%");
  lv_obj_set_style_text_color(ui_Wg_Actual_Label, lv_color_hex(CLR_CYAN), 0);
  lv_obj_set_style_text_font(ui_Wg_Actual_Label, &montserrat_20_en_ru, 0);
  lv_obj_align(ui_Wg_Actual_Label, LV_ALIGN_TOP_RIGHT, -15, 40);

  ui_Wg_Actual_Bar = lv_bar_create(wg_panel);
  lv_obj_set_size(ui_Wg_Actual_Bar, 310, 15);
  lv_obj_align(ui_Wg_Actual_Bar, LV_ALIGN_TOP_MID, 0, 75);
  lv_bar_set_range(ui_Wg_Actual_Bar, 0, 100);
  lv_obj_set_style_bg_color(ui_Wg_Actual_Bar, lv_color_hex(0x1F293B),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui_Wg_Actual_Bar, lv_color_hex(CLR_CYAN),
                            LV_PART_INDICATOR);

  // Divider
  lv_obj_t *div_wg1 = lv_obj_create(wg_panel);
  lv_obj_set_size(div_wg1, 310, 1);
  lv_obj_set_pos(div_wg1, 15, 105);
  lv_obj_set_style_bg_color(div_wg1, lv_color_hex(CLR_BORDER), 0);

  // Modes
  ui_Wg_Mode_Auto_Btn = lv_btn_create(wg_panel);
  lv_obj_set_size(ui_Wg_Mode_Auto_Btn, 150, 40);
  lv_obj_set_pos(ui_Wg_Mode_Auto_Btn, 15, 120);
  lv_obj_set_style_radius(ui_Wg_Mode_Auto_Btn, 6, 0);
  lv_obj_set_style_shadow_width(ui_Wg_Mode_Auto_Btn, 0, 0);
  lv_obj_add_event_cb(ui_Wg_Mode_Auto_Btn, wg_mode_auto_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_t *btn_wg1_lbl = lv_label_create(ui_Wg_Mode_Auto_Btn);
  lv_label_set_text(btn_wg1_lbl, "AUTO");
  lv_obj_set_style_text_font(btn_wg1_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(btn_wg1_lbl);

  ui_Wg_Mode_Man_Btn = lv_btn_create(wg_panel);
  lv_obj_set_size(ui_Wg_Mode_Man_Btn, 150, 40);
  lv_obj_set_pos(ui_Wg_Mode_Man_Btn, 180, 120);
  lv_obj_set_style_radius(ui_Wg_Mode_Man_Btn, 6, 0);
  lv_obj_set_style_shadow_width(ui_Wg_Mode_Man_Btn, 0, 0);
  lv_obj_add_event_cb(ui_Wg_Mode_Man_Btn, wg_mode_man_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_t *btn_wg2_lbl = lv_label_create(ui_Wg_Mode_Man_Btn);
  lv_label_set_text(btn_wg2_lbl, "MANUAL");
  lv_obj_set_style_text_font(btn_wg2_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(btn_wg2_lbl);

  // Manual Slider
  lv_obj_t *wg_man_title = lv_label_create(wg_panel);
  lv_label_set_text(wg_man_title, "Manual Override:");
  lv_obj_set_style_text_color(wg_man_title, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(wg_man_title, &lv_font_montserrat_14, 0);
  lv_obj_align(wg_man_title, LV_ALIGN_TOP_LEFT, 15, 185);

  ui_Wg_Slider_Label = lv_label_create(wg_panel);
  lv_label_set_text(ui_Wg_Slider_Label, "50%");
  lv_obj_set_style_text_color(ui_Wg_Slider_Label, lv_color_hex(CLR_TEXT_WHITE),
                              0);
  lv_obj_set_style_text_font(ui_Wg_Slider_Label, &lv_font_montserrat_14, 0);
  lv_obj_align(ui_Wg_Slider_Label, LV_ALIGN_TOP_RIGHT, -15, 185);

  ui_Wg_Slider = lv_slider_create(wg_panel);
  lv_obj_set_size(ui_Wg_Slider, 310, 12);
  lv_obj_align(ui_Wg_Slider, LV_ALIGN_TOP_MID, 0, 215);
  lv_slider_set_value(ui_Wg_Slider, wg_manual_pos, LV_ANIM_OFF);
  lv_obj_add_event_cb(ui_Wg_Slider, wg_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_set_style_bg_color(ui_Wg_Slider, lv_color_hex(0x1F293B), LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui_Wg_Slider, lv_color_hex(CLR_CYAN),
                            LV_PART_INDICATOR);

  // Invert Wastegate Direction Switch
  lv_obj_t *wg_invert_lbl = lv_label_create(wg_panel);
  lv_label_set_text(wg_invert_lbl, "Invert WG Direction:");
  lv_obj_set_style_text_color(wg_invert_lbl, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(wg_invert_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(wg_invert_lbl, LV_ALIGN_TOP_LEFT, 15, 250);

  ui_Wg_Invert_Switch = lv_switch_create(wg_panel);
  lv_obj_set_size(ui_Wg_Invert_Switch, 60, 30);
  lv_obj_align(ui_Wg_Invert_Switch, LV_ALIGN_TOP_RIGHT, -15, 243);
  lv_obj_add_event_cb(ui_Wg_Invert_Switch, wg_invert_switch_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_set_style_bg_color(ui_Wg_Invert_Switch, lv_color_hex(CLR_CYAN),
                            LV_PART_INDICATOR | LV_STATE_CHECKED);

  // Divider
  lv_obj_t *div_wg2 = lv_obj_create(wg_panel);
  lv_obj_set_size(div_wg2, 310, 1);
  lv_obj_set_pos(div_wg2, 15, 295);
  lv_obj_set_style_bg_color(div_wg2, lv_color_hex(CLR_BORDER), 0);

  // DSG Control Mode Indicator Panel
  ui_Mode_Panel = lv_obj_create(wg_panel);
  lv_obj_set_size(ui_Mode_Panel, 310, 710);
  lv_obj_set_pos(ui_Mode_Panel, 15, 315);
  lv_obj_set_style_bg_color(ui_Mode_Panel, lv_color_hex(CLR_PANEL), 0);
  lv_obj_set_style_bg_opa(ui_Mode_Panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(ui_Mode_Panel, lv_color_hex(CLR_BORDER), 0);
  lv_obj_set_style_border_width(ui_Mode_Panel, 1, 0);
  lv_obj_set_style_radius(ui_Mode_Panel, 8, 0);
  lv_obj_set_style_pad_all(ui_Mode_Panel, 12, 0);
  lv_obj_clear_flag(ui_Mode_Panel, LV_OBJ_FLAG_SCROLLABLE);

  // Title inside Mode Panel
  lv_obj_t *mode_title = lv_label_create(ui_Mode_Panel);
  lv_label_set_text(mode_title, "ACTIVE DSG CONTROL MODE");
  lv_obj_set_style_text_color(mode_title, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(mode_title, &lv_font_montserrat_12, 0);
  lv_obj_align(mode_title, LV_ALIGN_TOP_MID, 0, 5);

  // Large Mode Value Display
  ui_Mode_Val_Label = lv_label_create(ui_Mode_Panel);
  lv_label_set_text(ui_Mode_Val_Label, "N/A (P/R/N)");
  lv_obj_set_style_text_color(ui_Mode_Val_Label, lv_color_hex(CLR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(ui_Mode_Val_Label, &montserrat_20_en_ru, 0);
  lv_obj_align(ui_Mode_Val_Label, LV_ALIGN_TOP_MID, 0, 26);

  // Divider inside Mode Panel
  lv_obj_t *mode_div = lv_obj_create(ui_Mode_Panel);
  lv_obj_set_size(mode_div, 250, 1);
  lv_obj_set_pos(mode_div, 18, 62);
  lv_obj_set_style_bg_color(mode_div, lv_color_hex(CLR_BORDER), 0);

  // Flex list container for parameters
  lv_obj_t *param_list = lv_obj_create(ui_Mode_Panel);
  lv_obj_set_size(param_list, 286, 615);
  lv_obj_set_pos(param_list, 0, 72);
  lv_obj_set_style_bg_opa(param_list, 0, 0);
  lv_obj_set_style_border_width(param_list, 0, 0);
  lv_obj_set_style_pad_all(param_list, 0, 0);
  lv_obj_set_flex_flow(param_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(param_list, 10, 0);
  lv_obj_clear_flag(param_list, LV_OBJ_FLAG_SCROLLABLE);

  // Parameter helper rows creation helper macro:
  #define CREATE_PARAM_ROW(parent, name, label_ptr) \
    do { \
      lv_obj_t *row = lv_obj_create(parent); \
      lv_obj_set_size(row, 286, 75); \
      lv_obj_set_style_bg_color(row, lv_color_hex(CLR_BTN_ACTIVE), 0); \
      lv_obj_set_style_border_color(row, lv_color_hex(CLR_BORDER), 0); \
      lv_obj_set_style_border_width(row, 1, 0); \
      lv_obj_set_style_radius(row, 6, 0); \
      lv_obj_set_style_pad_all(row, 10, 0); \
      lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE); \
      lv_obj_t *name_lbl = lv_label_create(row); \
      lv_label_set_text(name_lbl, name); \
      lv_obj_set_style_text_color(name_lbl, lv_color_hex(CLR_TEXT_DIM), 0); \
      lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_12, 0); \
      lv_obj_align(name_lbl, LV_ALIGN_TOP_LEFT, 0, 0); \
      label_ptr = lv_label_create(row); \
      lv_label_set_text(label_ptr, "---"); \
      lv_obj_set_style_text_color(label_ptr, lv_color_hex(CLR_TEXT_WHITE), 0); \
      lv_obj_set_style_text_font(label_ptr, &lv_font_montserrat_14, 0); \
      lv_obj_align(label_ptr, LV_ALIGN_BOTTOM_LEFT, 0, 0); \
    } while(0)

  CREATE_PARAM_ROW(param_list, "Target Boost Limit", ui_Mode_Info_Boost_Val);
  CREATE_PARAM_ROW(param_list, "TPS Drop Limit (GWPLDU)", ui_Mode_Info_Tps_Val);
  CREATE_PARAM_ROW(param_list, "Boost Delta Limit (SDLDSUA)", ui_Mode_Info_Delta_Val);
  CREATE_PARAM_ROW(param_list, "Hold Duration (THLDUVD)", ui_Mode_Info_Hold_Val);
  CREATE_PARAM_ROW(param_list, "Stationary Bypass (SVDLDUVS)", ui_Mode_Info_Bypass_Val);

  #undef CREATE_PARAM_ROW

  update_wg_ui_state();

  // ==========================================
  // RIGHT COLUMN: BLOW-OFF SOLENOID (GOLD)
  // ==========================================
  lv_obj_t *bov_panel = lv_obj_create(ui_Screen10);
  lv_obj_set_size(bov_panel, 340, 1080);
  lv_obj_set_pos(bov_panel, 370, 100);
  lv_obj_set_style_bg_color(bov_panel, lv_color_hex(CLR_PANEL), 0);
  lv_obj_set_style_bg_opa(bov_panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(bov_panel, lv_color_hex(CLR_BORDER), 0);
  lv_obj_set_style_border_width(bov_panel, 1, 0);
  lv_obj_set_style_radius(bov_panel, 12, 0);
  lv_obj_clear_flag(bov_panel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *bov_title = lv_label_create(bov_panel);
  lv_label_set_text(bov_title, "BLOW-OFF SOLENOID");
  lv_obj_set_style_text_color(bov_title, lv_color_hex(CLR_GOLD), 0);
  lv_obj_set_style_text_font(bov_title, &montserrat_20_en_ru, 0);
  lv_obj_align(bov_title, LV_ALIGN_TOP_MID, 0, 10);

  // Glow LED representation and status
  ui_Bov_Led = lv_obj_create(bov_panel);
  lv_obj_set_size(ui_Bov_Led, 36, 36);
  lv_obj_set_pos(ui_Bov_Led, 25, 45);
  lv_obj_set_style_radius(ui_Bov_Led, 18, 0); // circular shape
  lv_obj_set_style_bg_color(ui_Bov_Led, lv_color_hex(CLR_LED_OFF), 0);
  lv_obj_set_style_bg_opa(ui_Bov_Led, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(ui_Bov_Led, lv_color_hex(CLR_BORDER), 0);
  lv_obj_set_style_border_width(ui_Bov_Led, 2, 0);
  lv_obj_clear_flag(ui_Bov_Led, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *bov_state_title = lv_label_create(bov_panel);
  lv_label_set_text(bov_state_title, "Solenoid State:");
  lv_obj_set_style_text_color(bov_state_title, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(bov_state_title, &lv_font_montserrat_14, 0);
  lv_obj_align(bov_state_title, LV_ALIGN_TOP_LEFT, 75, 52);

  ui_Bov_State_Label = lv_label_create(bov_panel);
  lv_label_set_text(ui_Bov_State_Label, "CLOSED");
  lv_obj_set_style_text_color(ui_Bov_State_Label, lv_color_hex(CLR_TEXT_WHITE),
                              0);
  lv_obj_set_style_text_font(ui_Bov_State_Label, &montserrat_20_en_ru, 0);
  lv_obj_align(ui_Bov_State_Label, LV_ALIGN_TOP_RIGHT, -20, 48);

  // Divider
  lv_obj_t *div_bov1 = lv_obj_create(bov_panel);
  lv_obj_set_size(div_bov1, 310, 1);
  lv_obj_set_pos(div_bov1, 15, 105);
  lv_obj_set_style_bg_color(div_bov1, lv_color_hex(CLR_BORDER), 0);

  // Modes
  ui_Bov_Mode_Auto_Btn = lv_btn_create(bov_panel);
  lv_obj_set_size(ui_Bov_Mode_Auto_Btn, 150, 40);
  lv_obj_set_pos(ui_Bov_Mode_Auto_Btn, 15, 120);
  lv_obj_set_style_radius(ui_Bov_Mode_Auto_Btn, 6, 0);
  lv_obj_set_style_shadow_width(ui_Bov_Mode_Auto_Btn, 0, 0);
  lv_obj_add_event_cb(ui_Bov_Mode_Auto_Btn, bov_mode_auto_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_t *btn_bov1_lbl = lv_label_create(ui_Bov_Mode_Auto_Btn);
  lv_label_set_text(btn_bov1_lbl, "AUTO");
  lv_obj_set_style_text_font(btn_bov1_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(btn_bov1_lbl);

  ui_Bov_Mode_Man_Btn = lv_btn_create(bov_panel);
  lv_obj_set_size(ui_Bov_Mode_Man_Btn, 150, 40);
  lv_obj_set_pos(ui_Bov_Mode_Man_Btn, 180, 120);
  lv_obj_set_style_radius(ui_Bov_Mode_Man_Btn, 6, 0);
  lv_obj_set_style_shadow_width(ui_Bov_Mode_Man_Btn, 0, 0);
  lv_obj_add_event_cb(ui_Bov_Mode_Man_Btn, bov_mode_man_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_t *btn_bov2_lbl = lv_label_create(ui_Bov_Mode_Man_Btn);
  lv_label_set_text(btn_bov2_lbl, "MANUAL");
  lv_obj_set_style_text_font(btn_bov2_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(btn_bov2_lbl);

  // Manual Direct Open Switch
  lv_obj_t *bov_man_title = lv_label_create(bov_panel);
  lv_label_set_text(bov_man_title, "Manual Open Valve:");
  lv_obj_set_style_text_color(bov_man_title, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(bov_man_title, &lv_font_montserrat_14, 0);
  lv_obj_align(bov_man_title, LV_ALIGN_TOP_LEFT, 15, 185);

  ui_Bov_Switch = lv_switch_create(bov_panel);
  lv_obj_set_size(ui_Bov_Switch, 60, 30);
  lv_obj_align(ui_Bov_Switch, LV_ALIGN_TOP_RIGHT, -15, 178);
  lv_obj_add_event_cb(ui_Bov_Switch, bov_switch_cb, LV_EVENT_VALUE_CHANGED,
                      NULL);
  lv_obj_set_style_bg_color(ui_Bov_Switch, lv_color_hex(CLR_GOLD),
                            LV_PART_INDICATOR | LV_STATE_CHECKED);

  // Divider
  lv_obj_t *div_bov2 = lv_obj_create(bov_panel);
  lv_obj_set_size(div_bov2, 310, 1);
  lv_obj_set_pos(div_bov2, 15, 245);
  lv_obj_set_style_bg_color(div_bov2, lv_color_hex(CLR_BORDER), 0);

  // Automatic Strategy Parameters Configurator (ME7 LDUVST)
  lv_obj_t *bov_config_title = lv_label_create(bov_panel);
  lv_label_set_text(bov_config_title, "AUTOMATIC STRATEGY (ME7.1)");
  lv_obj_set_style_text_color(bov_config_title, lv_color_hex(CLR_TEXT_WHITE),
                              0);
  lv_obj_set_style_text_font(bov_config_title, &lv_font_montserrat_14, 0);
  lv_obj_align(bov_config_title, LV_ALIGN_TOP_MID, 0, 260);

  // Trigger 1: GWPLDU (TPS Drop Rate)
  lv_obj_t *tps_lbl = lv_label_create(bov_panel);
  lv_label_set_text(tps_lbl, "GWPLDU (TPS Drop Limit):");
  lv_obj_set_style_text_color(tps_lbl, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(tps_lbl, &lv_font_montserrat_12, 0);
  lv_obj_align(tps_lbl, LV_ALIGN_TOP_LEFT, 15, 290);

  ui_Bov_Tps_Slider_Label = lv_label_create(bov_panel);
  lv_label_set_text(ui_Bov_Tps_Slider_Label, "25% / 100ms");
  lv_obj_set_style_text_color(ui_Bov_Tps_Slider_Label,
                              lv_color_hex(CLR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(ui_Bov_Tps_Slider_Label, &lv_font_montserrat_12,
                             0);
  lv_obj_align(ui_Bov_Tps_Slider_Label, LV_ALIGN_TOP_RIGHT, -15, 290);

  ui_Bov_Tps_Slider = lv_slider_create(bov_panel);
  lv_obj_set_size(ui_Bov_Tps_Slider, 310, 10);
  lv_obj_align(ui_Bov_Tps_Slider, LV_ALIGN_TOP_MID, 0, 310);
  lv_slider_set_range(ui_Bov_Tps_Slider, 10, 80);
  lv_slider_set_value(ui_Bov_Tps_Slider, bov_tps_threshold, LV_ANIM_OFF);
  lv_obj_add_event_cb(ui_Bov_Tps_Slider, bov_tps_slider_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_set_style_bg_color(ui_Bov_Tps_Slider, lv_color_hex(0x1F293B),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui_Bov_Tps_Slider, lv_color_hex(CLR_GOLD),
                            LV_PART_INDICATOR);

  // Trigger 2: SDLDSUA (Pressure Delta)
  lv_obj_t *press_lbl = lv_label_create(bov_panel);
  lv_label_set_text(press_lbl, "SDLDSUA (Boost Delta):");
  lv_obj_set_style_text_color(press_lbl, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(press_lbl, &lv_font_montserrat_12, 0);
  lv_obj_align(press_lbl, LV_ALIGN_TOP_LEFT, 15, 340);

  ui_Bov_Press_Slider_Label = lv_label_create(bov_panel);
  lv_label_set_text(ui_Bov_Press_Slider_Label, "35 kPa");
  lv_obj_set_style_text_color(ui_Bov_Press_Slider_Label,
                              lv_color_hex(CLR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(ui_Bov_Press_Slider_Label, &lv_font_montserrat_12,
                             0);
  lv_obj_align(ui_Bov_Press_Slider_Label, LV_ALIGN_TOP_RIGHT, -15, 340);

  ui_Bov_Press_Slider = lv_slider_create(bov_panel);
  lv_obj_set_size(ui_Bov_Press_Slider, 310, 10);
  lv_obj_align(ui_Bov_Press_Slider, LV_ALIGN_TOP_MID, 0, 360);
  lv_slider_set_range(ui_Bov_Press_Slider, 10, 100);
  lv_slider_set_value(ui_Bov_Press_Slider, bov_press_threshold, LV_ANIM_OFF);
  lv_obj_add_event_cb(ui_Bov_Press_Slider, bov_press_slider_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_set_style_bg_color(ui_Bov_Press_Slider, lv_color_hex(0x1F293B),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui_Bov_Press_Slider, lv_color_hex(CLR_GOLD),
                            LV_PART_INDICATOR);

  // Trigger 3: THLDUVD (Solenoid open duration)
  lv_obj_t *dur_lbl = lv_label_create(bov_panel);
  lv_label_set_text(dur_lbl, "THLDUVD (Hold Duration):");
  lv_obj_set_style_text_color(dur_lbl, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(dur_lbl, &lv_font_montserrat_12, 0);
  lv_obj_align(dur_lbl, LV_ALIGN_TOP_LEFT, 15, 390);

  ui_Bov_Dur_Slider_Label = lv_label_create(bov_panel);
  lv_label_set_text(ui_Bov_Dur_Slider_Label, "2.0s");
  lv_obj_set_style_text_color(ui_Bov_Dur_Slider_Label,
                              lv_color_hex(CLR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(ui_Bov_Dur_Slider_Label, &lv_font_montserrat_12,
                             0);
  lv_obj_align(ui_Bov_Dur_Slider_Label, LV_ALIGN_TOP_RIGHT, -15, 390);

  ui_Bov_Dur_Slider = lv_slider_create(bov_panel);
  lv_obj_set_size(ui_Bov_Dur_Slider, 310, 10);
  lv_obj_align(ui_Bov_Dur_Slider, LV_ALIGN_TOP_MID, 0, 410);
  lv_slider_set_range(ui_Bov_Dur_Slider, 5, 30); // 0.5s to 3.0s in tenths
  lv_slider_set_value(ui_Bov_Dur_Slider, bov_open_duration, LV_ANIM_OFF);
  lv_obj_add_event_cb(ui_Bov_Dur_Slider, bov_dur_slider_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_set_style_bg_color(ui_Bov_Dur_Slider, lv_color_hex(0x1F293B),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui_Bov_Dur_Slider, lv_color_hex(CLR_GOLD),
                            LV_PART_INDICATOR);

  // Trigger 4: Stationary Path (SVDLDUVS)
  lv_obj_t *stat_title = lv_label_create(bov_panel);
  lv_label_set_text(stat_title, "Stationary Path Enable:");
  lv_obj_set_style_text_color(stat_title, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(stat_title, &lv_font_montserrat_12, 0);
  lv_obj_align(stat_title, LV_ALIGN_TOP_LEFT, 15, 450);

  ui_Bov_Stat_Switch = lv_switch_create(bov_panel);
  lv_obj_set_size(ui_Bov_Stat_Switch, 50, 24);
  lv_obj_align(ui_Bov_Stat_Switch, LV_ALIGN_TOP_RIGHT, -15, 445);
  lv_obj_add_event_cb(ui_Bov_Stat_Switch, bov_stat_switch_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_set_style_bg_color(ui_Bov_Stat_Switch, lv_color_hex(CLR_GOLD),
                            LV_PART_INDICATOR | LV_STATE_CHECKED);

  lv_obj_t *ratio_lbl = lv_label_create(bov_panel);
  lv_label_set_text(ratio_lbl, "SVDLDUVS (Pressure Ratio):");
  lv_obj_set_style_text_color(ratio_lbl, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(ratio_lbl, &lv_font_montserrat_12, 0);
  lv_obj_align(ratio_lbl, LV_ALIGN_TOP_LEFT, 15, 490);

  ui_Bov_Stat_Ratio_Label = lv_label_create(bov_panel);
  lv_label_set_text(ui_Bov_Stat_Ratio_Label, "1.20");
  lv_obj_set_style_text_color(ui_Bov_Stat_Ratio_Label,
                              lv_color_hex(CLR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(ui_Bov_Stat_Ratio_Label, &lv_font_montserrat_12,
                             0);
  lv_obj_align(ui_Bov_Stat_Ratio_Label, LV_ALIGN_TOP_RIGHT, -15, 490);

  ui_Bov_Stat_Ratio_Slider = lv_slider_create(bov_panel);
  lv_obj_set_size(ui_Bov_Stat_Ratio_Slider, 310, 10);
  lv_obj_align(ui_Bov_Stat_Ratio_Slider, LV_ALIGN_TOP_MID, 0, 510);
  lv_slider_set_range(ui_Bov_Stat_Ratio_Slider, 105, 150); // 1.05 to 1.50
  lv_slider_set_value(ui_Bov_Stat_Ratio_Slider, bov_stat_ratio, LV_ANIM_OFF);
  lv_obj_add_event_cb(ui_Bov_Stat_Ratio_Slider, bov_stat_ratio_slider_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_set_style_bg_color(ui_Bov_Stat_Ratio_Slider, lv_color_hex(0x1F293B),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui_Bov_Stat_Ratio_Slider, lv_color_hex(CLR_GOLD),
                            LV_PART_INDICATOR);

  // Separator for MRE Monitor
  lv_obj_t *div_bov3 = lv_obj_create(bov_panel);
  lv_obj_set_size(div_bov3, 310, 1);
  lv_obj_set_pos(div_bov3, 15, 550);
  lv_obj_set_style_bg_color(div_bov3, lv_color_hex(CLR_BORDER), 0);

  lv_obj_t *mre_monitor_title = lv_label_create(bov_panel);
  lv_label_set_text(mre_monitor_title, "rusEFI MRE MONITOR");
  lv_obj_set_style_text_color(mre_monitor_title, lv_color_hex(CLR_TEXT_WHITE),
                              0);
  lv_obj_set_style_text_font(mre_monitor_title, &lv_font_montserrat_14, 0);
  lv_obj_align(mre_monitor_title, LV_ALIGN_TOP_MID, 0, 565);

  create_small_gauge(bov_panel, &ui_Arc_ECU_MAP_Screen10,
                     &ui_Label_ECU_MAP_Value_Screen10, "MAP ECU", "kPa",
                     lv_color_hex(0x00D4FF), 100, 250, 5, 595, GAUGE_MAP);

  create_small_gauge(bov_panel, &ui_Arc_MRE_MAP_Screen10,
                     &ui_Label_MRE_MAP_Value_Screen10, "MAP MRE", "kPa",
                     lv_color_hex(0x00D4FF), 100, 250, 170, 595, GAUGE_MRE_MAP);

  create_small_gauge(bov_panel, &ui_Arc_MRE_Wastegate_Screen10,
                     &ui_Label_MRE_Wastegate_Value_Screen10, "WG MRE", "%",
                     lv_color_hex(0x00FF88), 0, 100, 87, 785,
                     GAUGE_MRE_WASTEGATE);

  update_bov_ui_state();

  // Navigation Buttons
  ui_create_standard_navigation_buttons(ui_Screen10);

  ESP_LOGI(TAG, "Screen 10 (Boost Control) successfully initialized.");
}

// Lifecycle screen destroy method
void ui_Screen10_screen_destroy(void) {
  ESP_LOGI(TAG, "Destroying ui_Screen10...");

  ui_Screen10_MAP_Val = NULL;
  ui_Screen10_IAT_Val = NULL;
  ui_Wg_Actual_Title = NULL;

  ui_Wg_Actual_Bar = NULL;
  ui_Wg_Actual_Label = NULL;
  ui_Wg_Mode_Auto_Btn = NULL;
  ui_Wg_Mode_Man_Btn = NULL;
  ui_Wg_Slider = NULL;
  ui_Wg_Slider_Label = NULL;
  ui_Wg_Invert_Switch = NULL;

  ui_Mode_Panel = NULL;
  ui_Mode_Val_Label = NULL;
  ui_Mode_Info_Boost_Val = NULL;
  ui_Mode_Info_Tps_Val = NULL;
  ui_Mode_Info_Delta_Val = NULL;
  ui_Mode_Info_Hold_Val = NULL;
  ui_Mode_Info_Bypass_Val = NULL;

  ui_Bov_State_Label = NULL;
  ui_Bov_Led = NULL;
  ui_Bov_Mode_Auto_Btn = NULL;
  ui_Bov_Mode_Man_Btn = NULL;
  ui_Bov_Switch = NULL;

  ui_Bov_Tps_Slider = NULL;
  ui_Bov_Tps_Slider_Label = NULL;
  ui_Bov_Press_Slider = NULL;
  ui_Bov_Press_Slider_Label = NULL;
  ui_Bov_Dur_Slider = NULL;
  ui_Bov_Dur_Slider_Label = NULL;
  ui_Bov_Stat_Switch = NULL;
  ui_Bov_Stat_Ratio_Slider = NULL;
  ui_Bov_Stat_Ratio_Label = NULL;

  ui_Arc_ECU_MAP_Screen10 = NULL;
  ui_Label_ECU_MAP_Value_Screen10 = NULL;
  ui_Arc_MRE_MAP_Screen10 = NULL;
  ui_Label_MRE_MAP_Value_Screen10 = NULL;
  ui_Arc_MRE_Wastegate_Screen10 = NULL;
  ui_Label_MRE_Wastegate_Value_Screen10 = NULL;

  memset(wg_rpm_labels, 0, sizeof(wg_rpm_labels));
  memset(wg_pos_labels, 0, sizeof(wg_pos_labels));

  if (help_popup_overlay) {
    lv_obj_del(help_popup_overlay);
    help_popup_overlay = NULL;
  }

  if (ui_Screen10) {
    lv_obj_del(ui_Screen10);
    ui_Screen10 = NULL;
  }
}

// Periodic update method (called from update_all_gauges)
void ui_Screen10_update(void) {
  if (!ui_Screen10)
    return;

  ecu_data_t data;
  if (demo_mode_get_enabled()) {
    ecu_data_simulate(&data);
  } else {
    ecu_data_get_copy(&data);
  }

  // Update header labels
  if (ui_Screen10_IAT_Val) {
    lv_label_set_text_fmt(ui_Screen10_IAT_Val, "IAT: %.0f °C", data.iat_temp);
  }
  if (ui_Screen10_MAP_Val) {
    if (settings_get_can_platform() == PLATFORM_RUSEFI_MRE) {
      lv_label_set_text_fmt(ui_Screen10_MAP_Val, "MAP MRE: %.1f kPa",
                            data.map_kpa);
    } else {
      lv_label_set_text_fmt(ui_Screen10_MAP_Val, "MAP: %.1f kPa", data.map_kpa);
    }
  }

  // Compute Wastegate position
  int actual_wg_pos = 0;
  if (settings_get_can_platform() == PLATFORM_RUSEFI_MRE) {
    actual_wg_pos = (int)data.wg_pos_percent;
    if (ui_Wg_Actual_Title) {
      lv_label_set_text(ui_Wg_Actual_Title, "Westgate MRE:");
    }
  } else if (settings_get_mre_parallel()) {
    actual_wg_pos = (int)data.mre_wg_pos_percent;
    if (ui_Wg_Actual_Title) {
      lv_label_set_text(ui_Wg_Actual_Title, "Actual Geometry (MRE):");
    }
  } else {
    // Normal / Bosch PQ35 platform: use feedback if present, else demanded
    // target from Lua PID
    actual_wg_pos = (data.wg_pos_percent > 0.0f) ? (int)data.wg_pos_percent
                                                 : (int)data.wg_set_percent;
    if (ui_Wg_Actual_Title) {
      lv_label_set_text(ui_Wg_Actual_Title, "Actual Geometry:");
    }
  }

  // Clamp and invert if necessary
  if (actual_wg_pos < 0)
    actual_wg_pos = 0;
  if (actual_wg_pos > 100)
    actual_wg_pos = 100;
  if (wg_is_inverted) {
    actual_wg_pos = 100 - actual_wg_pos;
  }

  // Compute Blow-off state
  int actual_bov_state = 0;
  if (bov_is_auto) {
    // Dynamic Path simulation or direct state mapping
    // We check if engine is letting off load, or overboost
    // In demo / CAN data, bov_percent represents the target valve opening
    actual_bov_state = (data.bov_percent > 10.0f) ? 100 : 0;
  } else {
    actual_bov_state = bov_manual_open ? 100 : 0;
  }

  // Update UI values
  if (ui_Wg_Actual_Bar) {
    lv_bar_set_value(ui_Wg_Actual_Bar, actual_wg_pos, LV_ANIM_OFF);
  }
  if (ui_Wg_Actual_Label) {
    lv_label_set_text_fmt(ui_Wg_Actual_Label, "%d%%", actual_wg_pos);
  }

  if (ui_Bov_State_Label) {
    lv_label_set_text(ui_Bov_State_Label,
                      (actual_bov_state > 0) ? "OPEN" : "CLOSED");
    lv_obj_set_style_text_color(ui_Bov_State_Label,
                                (actual_bov_state > 0)
                                    ? lv_color_hex(CLR_GOLD)
                                    : lv_color_hex(CLR_TEXT_WHITE),
                                0);
  }

  if (ui_Bov_Led) {
    lv_obj_set_style_bg_color(ui_Bov_Led,
                              (actual_bov_state > 0)
                                  ? lv_color_hex(CLR_LED_ON)
                                  : lv_color_hex(CLR_LED_OFF),
                              0);
  }

  // Update MRE Gauges in parallel if they exist
  extern void update_gauge(gauge_id_t id, lv_obj_t * arc, lv_obj_t * label,
                           float value, const char *default_fmt, float warn_thr,
                           float crit_thr, bool invert_logic,
                           lv_color_t normal_color);

  if (ui_Arc_ECU_MAP_Screen10 || ui_Label_ECU_MAP_Value_Screen10) {
    update_gauge(GAUGE_MAP, ui_Arc_ECU_MAP_Screen10,
                 ui_Label_ECU_MAP_Value_Screen10, data.map_kpa, "%.1f", 200,
                 230, false, lv_color_hex(0x00D4FF));
  }
  if (ui_Arc_MRE_MAP_Screen10 || ui_Label_MRE_MAP_Value_Screen10) {
    update_gauge(GAUGE_MRE_MAP, ui_Arc_MRE_MAP_Screen10,
                 ui_Label_MRE_MAP_Value_Screen10, data.mre_map_kpa, "%.1f", 200,
                 230, false, lv_color_hex(0x00D4FF));
  }
  if (ui_Arc_MRE_Wastegate_Screen10 || ui_Label_MRE_Wastegate_Value_Screen10) {
    float mre_wg = data.mre_wg_pos_percent;
    if (wg_is_inverted) {
      mre_wg = 100.0f - mre_wg;
    }
    update_gauge(GAUGE_MRE_WASTEGATE, ui_Arc_MRE_Wastegate_Screen10,
                 ui_Label_MRE_Wastegate_Value_Screen10, mre_wg, "%.1f", 110,
                 120, false, lv_color_hex(0x00FF88));
  }

  // Update DSG Mode label and colors
  if (ui_Mode_Val_Label) {
    if (data.selector_position == 5 || data.selector_position == 0) {
      lv_label_set_text(ui_Mode_Val_Label, "DRIVE (D)");
      lv_obj_set_style_text_color(ui_Mode_Val_Label, lv_color_hex(0x00FFD4), 0); // Cyan-green
    } else if (data.selector_position == 6) {
      lv_label_set_text(ui_Mode_Val_Label, "SPORT (S)");
      lv_obj_set_style_text_color(ui_Mode_Val_Label, lv_color_hex(0xFF3B30), 0); // Red
    } else if (data.selector_position == 7) {
      lv_label_set_text(ui_Mode_Val_Label, "MANUAL (M)");
      lv_obj_set_style_text_color(ui_Mode_Val_Label, lv_color_hex(0xFF9500), 0); // Orange
    } else if (data.selector_position == 2) {
      lv_label_set_text(ui_Mode_Val_Label, "PARK (P)");
      lv_obj_set_style_text_color(ui_Mode_Val_Label, lv_color_hex(0x888888), 0); // Dim gray
    } else if (data.selector_position == 3) {
      lv_label_set_text(ui_Mode_Val_Label, "REVERSE (R)");
      lv_obj_set_style_text_color(ui_Mode_Val_Label, lv_color_hex(0x00D4FF), 0); // Cyan/Blue
    } else if (data.selector_position == 4) {
      lv_label_set_text(ui_Mode_Val_Label, "NEUTRAL (N)");
      lv_obj_set_style_text_color(ui_Mode_Val_Label, lv_color_hex(0x888888), 0); // Dim gray
    } else {
      lv_label_set_text(ui_Mode_Val_Label, "UNKNOWN");
      lv_obj_set_style_text_color(ui_Mode_Val_Label, lv_color_hex(CLR_TEXT_DIM), 0); // Dim text
    }
  }

  // Update parameters under Mode indicator
  if (data.selector_position == 5 || data.selector_position == 0) {
    if (ui_Mode_Info_Boost_Val) lv_label_set_text(ui_Mode_Info_Boost_Val, "0.6 bar (160 kPa) max");
    if (ui_Mode_Info_Tps_Val) lv_label_set_text(ui_Mode_Info_Tps_Val, "25% drop / 100ms");
    if (ui_Mode_Info_Delta_Val) lv_label_set_text(ui_Mode_Info_Delta_Val, "35 kPa overboost");
    if (ui_Mode_Info_Hold_Val) lv_label_set_text(ui_Mode_Info_Hold_Val, "2.0 seconds");
    if (ui_Mode_Info_Bypass_Val) lv_label_set_text(ui_Mode_Info_Bypass_Val, "Ratio 1.20 (Enabled)");
  } else if (data.selector_position == 6 || data.selector_position == 7) {
    if (ui_Mode_Info_Boost_Val) lv_label_set_text(ui_Mode_Info_Boost_Val, "1.5 bar (250 kPa) max");
    if (ui_Mode_Info_Tps_Val) lv_label_set_text(ui_Mode_Info_Tps_Val, "35% drop / 100ms");
    if (ui_Mode_Info_Delta_Val) lv_label_set_text(ui_Mode_Info_Delta_Val, "50 kPa overboost");
    if (ui_Mode_Info_Hold_Val) lv_label_set_text(ui_Mode_Info_Hold_Val, "1.2 seconds");
    if (ui_Mode_Info_Bypass_Val) lv_label_set_text(ui_Mode_Info_Bypass_Val, "Disabled (Ratio 1.35)");
  } else if (data.selector_position == 3) { // Reverse (R)
    if (ui_Mode_Info_Boost_Val) lv_label_set_text(ui_Mode_Info_Boost_Val, "0.2 bar (120 kPa) max");
    if (ui_Mode_Info_Tps_Val) lv_label_set_text(ui_Mode_Info_Tps_Val, "30% drop / 100ms");
    if (ui_Mode_Info_Delta_Val) lv_label_set_text(ui_Mode_Info_Delta_Val, "40 kPa overboost");
    if (ui_Mode_Info_Hold_Val) lv_label_set_text(ui_Mode_Info_Hold_Val, "1.5 seconds");
    if (ui_Mode_Info_Bypass_Val) lv_label_set_text(ui_Mode_Info_Bypass_Val, "Ratio 1.30 (Enabled)");
  } else { // Park (P) or Neutral (N) or others
    if (ui_Mode_Info_Boost_Val) lv_label_set_text(ui_Mode_Info_Boost_Val, "0.0 bar (100 kPa) max");
    if (ui_Mode_Info_Tps_Val) lv_label_set_text(ui_Mode_Info_Tps_Val, "40% drop / 100ms");
    if (ui_Mode_Info_Delta_Val) lv_label_set_text(ui_Mode_Info_Delta_Val, "60 kPa overboost");
    if (ui_Mode_Info_Hold_Val) lv_label_set_text(ui_Mode_Info_Hold_Val, "1.0 seconds");
    if (ui_Mode_Info_Bypass_Val) lv_label_set_text(ui_Mode_Info_Bypass_Val, "Disabled (Ratio 1.50)");
  }

  s_actual_wg_pos = actual_wg_pos;
  s_actual_bov_state = actual_bov_state;
}

// ---------- Bidirectional Getters & Setters for External / Lua Engine
// ----------
extern bool example_lvgl_lock(int timeout_ms);
extern void example_lvgl_unlock(void);

bool ui_Screen10_get_wg_is_auto(void) { return settings_get_wg_is_auto(); }
void ui_Screen10_set_wg_is_auto(bool is_auto) {
  wg_is_auto = is_auto;
  if (ui_Screen10) {
    if (example_lvgl_lock(500)) {
      update_wg_ui_state();
      example_lvgl_unlock();
    }
  }
  sync_local_to_settings();
}

int ui_Screen10_get_wg_manual_pos(void) { return settings_get_wg_manual_pos(); }
void ui_Screen10_set_wg_manual_pos(int pos) {
  if (pos < 0)
    pos = 0;
  if (pos > 100)
    pos = 100;
  wg_manual_pos = pos;
  if (ui_Screen10 && ui_Wg_Slider) {
    if (example_lvgl_lock(500)) {
      lv_slider_set_value(ui_Wg_Slider, pos, LV_ANIM_OFF);
      if (ui_Wg_Slider_Label) {
        lv_label_set_text_fmt(ui_Wg_Slider_Label, "%d%%", pos);
      }
      example_lvgl_unlock();
    }
  }
  sync_local_to_settings();
}

int ui_Screen10_get_wg_map_rpm(int idx) {
  if (idx < 0 || idx >= 10)
    return 0;
  return wg_map[idx].rpm;
}

void ui_Screen10_set_wg_map_rpm(int idx, int rpm) {
  if (idx < 0 || idx >= 10)
    return;
  wg_map[idx].rpm = rpm;
  if (ui_Screen10 && wg_rpm_labels[idx]) {
    if (example_lvgl_lock(500)) {
      lv_label_set_text_fmt(wg_rpm_labels[idx], "%d", rpm);
      example_lvgl_unlock();
    }
  }
}

int ui_Screen10_get_wg_map_pos(int idx) {
  if (idx < 0 || idx >= 10)
    return 0;
  return wg_map[idx].pos;
}

void ui_Screen10_set_wg_map_pos(int idx, int pos) {
  if (idx < 0 || idx >= 10)
    return;
  if (pos < 100)
    pos = 100;
  if (pos > 300)
    pos = 300;
  wg_map[idx].pos = pos;
  if (ui_Screen10 && wg_pos_labels[idx]) {
    if (example_lvgl_lock(500)) {
      lv_label_set_text_fmt(wg_pos_labels[idx], "%d kPa", pos);
      example_lvgl_unlock();
    }
  }
}

int ui_Screen10_get_actual_wg_pos(void) { return s_actual_wg_pos; }

bool ui_Screen10_get_bov_is_auto(void) { return settings_get_bov_is_auto(); }
void ui_Screen10_set_bov_is_auto(bool is_auto) {
  bov_is_auto = is_auto;
  if (ui_Screen10) {
    if (example_lvgl_lock(500)) {
      update_bov_ui_state();
      example_lvgl_unlock();
    }
  }
  sync_local_to_settings();
}

bool ui_Screen10_get_bov_manual_open(void) { return settings_get_bov_manual_open(); }
void ui_Screen10_set_bov_manual_open(bool open) {
  bov_manual_open = open;
  if (ui_Screen10 && ui_Bov_Switch) {
    if (example_lvgl_lock(500)) {
      if (open) {
        lv_obj_add_state(ui_Bov_Switch, LV_STATE_CHECKED);
      } else {
        lv_obj_clear_state(ui_Bov_Switch, LV_STATE_CHECKED);
      }
      example_lvgl_unlock();
    }
  }
  sync_local_to_settings();
}

int ui_Screen10_get_bov_tps_threshold(void) { return settings_get_bov_tps_threshold(); }
void ui_Screen10_set_bov_tps_threshold(int val) {
  if (val < 10)
    val = 10;
  if (val > 80)
    val = 80;
  bov_tps_threshold = val;
  if (ui_Screen10 && ui_Bov_Tps_Slider) {
    if (example_lvgl_lock(500)) {
      lv_slider_set_value(ui_Bov_Tps_Slider, val, LV_ANIM_OFF);
      if (ui_Bov_Tps_Slider_Label) {
        lv_label_set_text_fmt(ui_Bov_Tps_Slider_Label, "%d%% / 100ms", val);
      }
      example_lvgl_unlock();
    }
  }
  sync_local_to_settings();
}

int ui_Screen10_get_bov_press_threshold(void) { return settings_get_bov_press_threshold(); }
void ui_Screen10_set_bov_press_threshold(int val) {
  if (val < 10)
    val = 10;
  if (val > 100)
    val = 100;
  bov_press_threshold = val;
  if (ui_Screen10 && ui_Bov_Press_Slider) {
    if (example_lvgl_lock(500)) {
      lv_slider_set_value(ui_Bov_Press_Slider, val, LV_ANIM_OFF);
      if (ui_Bov_Press_Slider_Label) {
        lv_label_set_text_fmt(ui_Bov_Press_Slider_Label, "%d kPa", val);
      }
      example_lvgl_unlock();
    }
  }
  sync_local_to_settings();
}

int ui_Screen10_get_bov_open_duration(void) { return settings_get_bov_open_duration(); }
void ui_Screen10_set_bov_open_duration(int val_ms) {
  if (val_ms < 5)
    val_ms = 5;
  if (val_ms > 30)
    val_ms = 30;
  bov_open_duration = val_ms;
  if (ui_Screen10 && ui_Bov_Dur_Slider) {
    if (example_lvgl_lock(500)) {
      lv_slider_set_value(ui_Bov_Dur_Slider, val_ms, LV_ANIM_OFF);
      if (ui_Bov_Dur_Slider_Label) {
        lv_label_set_text_fmt(ui_Bov_Dur_Slider_Label, "%.1fs",
                              (float)val_ms / 10.0f);
      }
      example_lvgl_unlock();
    }
  }
  sync_local_to_settings();
}

bool ui_Screen10_get_bov_stat_enabled(void) { return settings_get_bov_stat_enabled(); }
void ui_Screen10_set_bov_stat_enabled(bool enabled) {
  bov_stat_enabled = enabled;
  if (ui_Screen10 && ui_Bov_Stat_Switch) {
    if (example_lvgl_lock(500)) {
      if (enabled) {
        lv_obj_add_state(ui_Bov_Stat_Switch, LV_STATE_CHECKED);
        lv_obj_set_style_opa(ui_Bov_Stat_Ratio_Slider, LV_OPA_COVER, 0);
      } else {
        lv_obj_clear_state(ui_Bov_Stat_Switch, LV_STATE_CHECKED);
        lv_obj_set_style_opa(ui_Bov_Stat_Ratio_Slider, LV_OPA_40, 0);
      }
      example_lvgl_unlock();
    }
  }
  sync_local_to_settings();
}

int ui_Screen10_get_bov_stat_ratio(void) { return settings_get_bov_stat_ratio(); }
void ui_Screen10_set_bov_stat_ratio(int val_x100) {
  if (val_x100 < 105)
    val_x100 = 105;
  if (val_x100 > 150)
    val_x100 = 150;
  bov_stat_ratio = val_x100;
  if (ui_Screen10 && ui_Bov_Stat_Ratio_Slider) {
    if (example_lvgl_lock(500)) {
      lv_slider_set_value(ui_Bov_Stat_Ratio_Slider, val_x100, LV_ANIM_OFF);
      if (ui_Bov_Stat_Ratio_Label) {
        lv_label_set_text_fmt(ui_Bov_Stat_Ratio_Label, "%.2f",
                              (float)val_x100 / 100.0f);
      }
      example_lvgl_unlock();
    }
  }
  sync_local_to_settings();
}

int ui_Screen10_get_actual_bov_state(void) { return s_actual_bov_state; }

bool ui_Screen10_get_wg_is_inverted(void) { return settings_get_wg_is_inverted(); }
void ui_Screen10_set_wg_is_inverted(bool inverted) {
  wg_is_inverted = inverted;
  if (ui_Screen10 && ui_Wg_Invert_Switch) {
    if (example_lvgl_lock(500)) {
      if (inverted) {
        lv_obj_add_state(ui_Wg_Invert_Switch, LV_STATE_CHECKED);
      } else {
        lv_obj_clear_state(ui_Wg_Invert_Switch, LV_STATE_CHECKED);
      }
      example_lvgl_unlock();
    }
  }
  sync_local_to_settings();
}
