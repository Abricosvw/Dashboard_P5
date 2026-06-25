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
lv_obj_t *ui_Screen10_Boost_Val = NULL;
lv_obj_t *ui_Screen10_RPM_Val = NULL;

// Wastegate VGT elements
lv_obj_t *ui_Wg_Actual_Bar = NULL;
lv_obj_t *ui_Wg_Actual_Label = NULL;
lv_obj_t *ui_Wg_Mode_Auto_Btn = NULL;
lv_obj_t *ui_Wg_Mode_Man_Btn = NULL;
lv_obj_t *ui_Wg_Slider = NULL;
lv_obj_t *ui_Wg_Slider_Label = NULL;

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
#define CLR_CYAN 0x00D4FF   // Wastegate (Cyan)
#define CLR_GOLD 0xFFD700   // Blow-off (Gold)
#define CLR_BTN_BG 0x1E293B
#define CLR_BTN_ACTIVE 0x0F172A
#define CLR_TEXT_DIM 0x94A3B8
#define CLR_TEXT_WHITE 0xF1F5F9
#define CLR_LED_OFF 0x3A2E00
#define CLR_LED_ON 0xFFD700

// Fonts declared in ui.h
LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(montserrat_20_en_ru);

// Wastegate Map Point Type
typedef struct {
  int rpm;   // Engine RPM
  int pos;   // Position in %
} wg_map_point_t;

// Persistent states
static bool wg_is_auto = true;
static int wg_manual_pos = 50;

// Default VGT map (closed at low/mid RPM, opens at high RPM to control boost)
static wg_map_point_t wg_map[10] = {
  {1000, 100}, {1500, 100}, {2000, 95}, {2500, 90},
  {3000, 80},  {3500, 70},  {4000, 60}, {5000, 50},
  {6000, 40},  {7000, 30}
};

static bool bov_is_auto = true;
static bool bov_manual_open = false;

// Default ME7.1 values
static int bov_tps_threshold = 25;       // GWPLDU: 25% drop per 100ms
static int bov_press_threshold = 35;     // SDLDSUA: 35 kPa (350 hPa)
static int bov_open_duration = 20;       // THLDUVD: 2.0s (stored as 20 for tenths of seconds, range 0.5s to 3.0s)
static bool bov_stat_enabled = true;     // Stationary path enabled
static int bov_stat_ratio = 120;         // SVDLDUVS: 1.20 (stored as 120, range 1.05 to 1.50)

// Computed outputs for background logic / Lua
static int s_actual_wg_pos = 0;
static int s_actual_bov_state = 0;       // 0 = closed, 100 = open (flashing)

// Dynamic labels for Wastegate Map
static lv_obj_t *wg_rpm_labels[10];
static lv_obj_t *wg_pos_labels[10];

// ---------- Forward declarations for UI Event Handlers ----------
static void update_wg_ui_state(void);
static void update_bov_ui_state(void);
static void wg_mode_auto_cb(lv_event_t *e);
static void wg_mode_man_cb(lv_event_t *e);
static void bov_mode_auto_cb(lv_event_t *e);
static void bov_mode_man_cb(lv_event_t *e);
static void wg_slider_cb(lv_event_t *e);
static void bov_switch_cb(lv_event_t *e);
static void wg_map_btn_cb(lv_event_t *e);

static void bov_tps_slider_cb(lv_event_t *e);
static void bov_press_slider_cb(lv_event_t *e);
static void bov_dur_slider_cb(lv_event_t *e);
static void bov_stat_switch_cb(lv_event_t *e);
static void bov_stat_ratio_slider_cb(lv_event_t *e);

static void help_btn_cb(lv_event_t *e);
static void close_help_cb(lv_event_t *e);

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
    int step = 5;
    int curr = wg_map[idx].pos;
    curr += is_plus ? step : -step;
    if (curr < 0)   curr = 0;
    if (curr > 100) curr = 100;
    wg_map[idx].pos = curr;
    lv_label_set_text_fmt(wg_pos_labels[idx], "%d%%", curr);
  } else {
    int step = 100;
    int curr = wg_map[idx].rpm;
    curr += is_plus ? step : -step;

    // Boundary checks to maintain strictly sorted RPMs
    int min_val = (idx > 0) ? wg_map[idx - 1].rpm + 100 : 500;
    int max_val = (idx < 9) ? wg_map[idx + 1].rpm - 100 : 9000;

    if (curr < min_val) curr = min_val;
    if (curr > max_val) curr = max_val;

    wg_map[idx].rpm = curr;
    lv_label_set_text_fmt(wg_rpm_labels[idx], "%d", curr);
  }
}

// Update Wastegate VGT UI State (locks manual slider if in auto mode)
static void update_wg_ui_state(void) {
  if (!ui_Screen10) return;
  if (wg_is_auto) {
    lv_obj_set_style_bg_color(ui_Wg_Mode_Auto_Btn, lv_color_hex(CLR_CYAN), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Wg_Mode_Auto_Btn, 0), lv_color_black(), 0);

    lv_obj_set_style_bg_color(ui_Wg_Mode_Man_Btn, lv_color_hex(CLR_BTN_BG), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Wg_Mode_Man_Btn, 0), lv_color_hex(CLR_TEXT_DIM), 0);

    lv_obj_set_style_opa(ui_Wg_Slider, LV_OPA_40, 0);
  } else {
    lv_obj_set_style_bg_color(ui_Wg_Mode_Auto_Btn, lv_color_hex(CLR_BTN_BG), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Wg_Mode_Auto_Btn, 0), lv_color_hex(CLR_TEXT_DIM), 0);

    lv_obj_set_style_bg_color(ui_Wg_Mode_Man_Btn, lv_color_hex(CLR_CYAN), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Wg_Mode_Man_Btn, 0), lv_color_black(), 0);

    lv_obj_set_style_opa(ui_Wg_Slider, LV_OPA_COVER, 0);
  }

  if (ui_Wg_Slider) {
    lv_slider_set_value(ui_Wg_Slider, wg_manual_pos, LV_ANIM_OFF);
    if (ui_Wg_Slider_Label) {
      lv_label_set_text_fmt(ui_Wg_Slider_Label, "%d%%", wg_manual_pos);
    }
  }
}

// Update Blow-off Solenoid UI State
static void update_bov_ui_state(void) {
  if (!ui_Screen10) return;
  if (bov_is_auto) {
    lv_obj_set_style_bg_color(ui_Bov_Mode_Auto_Btn, lv_color_hex(CLR_GOLD), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Bov_Mode_Auto_Btn, 0), lv_color_black(), 0);

    lv_obj_set_style_bg_color(ui_Bov_Mode_Man_Btn, lv_color_hex(CLR_BTN_BG), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Bov_Mode_Man_Btn, 0), lv_color_hex(CLR_TEXT_DIM), 0);

    lv_obj_set_style_opa(ui_Bov_Switch, LV_OPA_40, 0);
  } else {
    lv_obj_set_style_bg_color(ui_Bov_Mode_Auto_Btn, lv_color_hex(CLR_BTN_BG), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Bov_Mode_Auto_Btn, 0), lv_color_hex(CLR_TEXT_DIM), 0);

    lv_obj_set_style_bg_color(ui_Bov_Mode_Man_Btn, lv_color_hex(CLR_GOLD), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ui_Bov_Mode_Man_Btn, 0), lv_color_black(), 0);

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
    lv_label_set_text_fmt(ui_Bov_Tps_Slider_Label, "%d%% / 100ms", bov_tps_threshold);
  }
  if (ui_Bov_Press_Slider) {
    lv_slider_set_value(ui_Bov_Press_Slider, bov_press_threshold, LV_ANIM_OFF);
    lv_label_set_text_fmt(ui_Bov_Press_Slider_Label, "%d kPa", bov_press_threshold);
  }
  if (ui_Bov_Dur_Slider) {
    lv_slider_set_value(ui_Bov_Dur_Slider, bov_open_duration, LV_ANIM_OFF);
    lv_label_set_text_fmt(ui_Bov_Dur_Slider_Label, "%.1fs", (float)bov_open_duration / 10.0f);
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
    lv_label_set_text_fmt(ui_Bov_Stat_Ratio_Label, "%.2f", (float)bov_stat_ratio / 100.0f);
  }
}

// Mode callbacks
static void wg_mode_auto_cb(lv_event_t *e) {
  wg_is_auto = true;
  update_wg_ui_state();
  ESP_LOGI(TAG, "Wastegate VGT Mode: AUTO");
}

static void wg_mode_man_cb(lv_event_t *e) {
  wg_is_auto = false;
  update_wg_ui_state();
  ESP_LOGI(TAG, "Wastegate VGT Mode: MANUAL");
}

static void bov_mode_auto_cb(lv_event_t *e) {
  bov_is_auto = true;
  update_bov_ui_state();
  ESP_LOGI(TAG, "Blow-off Mode: AUTO");
}

static void bov_mode_man_cb(lv_event_t *e) {
  bov_is_auto = false;
  update_bov_ui_state();
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
  ESP_LOGI(TAG, "Blow-off Manual State: %s", bov_manual_open ? "OPEN" : "CLOSED");
}

static void bov_tps_slider_cb(lv_event_t *e) {
  bov_tps_threshold = lv_slider_get_value(ui_Bov_Tps_Slider);
  lv_label_set_text_fmt(ui_Bov_Tps_Slider_Label, "%d%% / 100ms", bov_tps_threshold);
}

static void bov_press_slider_cb(lv_event_t *e) {
  bov_press_threshold = lv_slider_get_value(ui_Bov_Press_Slider);
  lv_label_set_text_fmt(ui_Bov_Press_Slider_Label, "%d kPa", bov_press_threshold);
}

static void bov_dur_slider_cb(lv_event_t *e) {
  bov_open_duration = lv_slider_get_value(ui_Bov_Dur_Slider);
  lv_label_set_text_fmt(ui_Bov_Dur_Slider_Label, "%.1fs", (float)bov_open_duration / 10.0f);
}

static void bov_stat_switch_cb(lv_event_t *e) {
  bov_stat_enabled = lv_obj_has_state(ui_Bov_Stat_Switch, LV_STATE_CHECKED);
  if (bov_stat_enabled) {
    lv_obj_set_style_opa(ui_Bov_Stat_Ratio_Slider, LV_OPA_COVER, 0);
  } else {
    lv_obj_set_style_opa(ui_Bov_Stat_Ratio_Slider, LV_OPA_40, 0);
  }
  ESP_LOGI(TAG, "Blow-off Stationary Path: %s", bov_stat_enabled ? "ENABLED" : "DISABLED");
}

static void bov_stat_ratio_slider_cb(lv_event_t *e) {
  if (!bov_stat_enabled) {
    lv_slider_set_value(ui_Bov_Stat_Ratio_Slider, bov_stat_ratio, LV_ANIM_OFF);
    return;
  }
  bov_stat_ratio = lv_slider_get_value(ui_Bov_Stat_Ratio_Slider);
  lv_label_set_text_fmt(ui_Bov_Stat_Ratio_Label, "%.2f", (float)bov_stat_ratio / 100.0f);
}

// Help Modal implementation
static void help_btn_cb(lv_event_t *e) {
  if (help_popup_overlay) return;

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
    "This screen implements boost and charge air bypass control based on factory "
    "Bosch Motronic ME7.1 strategies (LDR and LDUVST) adapted for Speed-Density / MPI setups.\n\n"
    "1. SERVO WASTEGATE (LDR-Servo):\n"
    "The variable geometry wastegate is positioned directly via a rotary electronic "
    "servo motor. In AUTO mode, the target position (0-100%) is interpolated using a "
    "10-point RPM map.\n"
    "• Factory Bosch Logic:\n"
    "At low/mid RPM (under 2500 RPM), the wastegate is held fully closed (100%) to "
    "maximize turbine speed and spool. As RPM rises, it gradually opens (down to "
    "30% at 7000 RPM) to regulate target boost pressure and control backpressure.");
  lv_obj_set_style_text_color(lbl_desc, lv_color_hex(CLR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(lbl_desc, &lv_font_montserrat_14, 0);

  lv_obj_t *lbl_bov_desc = lv_label_create(text_cont);
  lv_obj_set_width(lbl_bov_desc, 530);
  lv_label_set_text(lbl_bov_desc,
    "2. BLOW-OFF SOLENOID (LDUVST):\n"
    "The N249 recirculating diverter valve is electrically operated to bypass the "
    "compressor during deceleration or overboost. It operates via two paths:\n\n"
    "A) Dynamic Path (Dynamischer Pfad):\n"
    "Triggers during rapid throttle lift-off. Parameters:\n"
    "• TPS Drop Limit (GWPLDU): Throttle position drop rate over 100ms. "
    "Factory default: 25%.\n"
    "• Boost Delta (SDLDSUA): Overshoot difference between actual and target boost. "
    "Factory default: 35 kPa (350 hPa).\n"
    "• Hold Duration (THLDUVD): Opening hold time for the solenoid. "
    "Factory default: 2.0 seconds.\n\n"
    "B) Stationary Path (Stationärer Pfad):\n"
    "• Pressure Ratio (SVDLDUVS): If compressor pressure ratio (MAP / Ambient) "
    "drops below this threshold (factory default: 1.20) during engine braking, "
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

// Screen initialization
void ui_Screen10_screen_init(void) {
  ESP_LOGI(TAG, "Initializing Screen 10 (Wastegate/Blow-off)...");

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
  lv_obj_align(title, LV_ALIGN_TOP_MID, -45, 8);

  ui_Screen10_RPM_Val = lv_label_create(header_panel);
  lv_label_set_text(ui_Screen10_RPM_Val, "RPM: -- rpm");
  lv_obj_set_style_text_color(ui_Screen10_RPM_Val, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(ui_Screen10_RPM_Val, &lv_font_montserrat_14, 0);
  lv_obj_align(ui_Screen10_RPM_Val, LV_ALIGN_BOTTOM_LEFT, 20, -8);

  ui_Screen10_Boost_Val = lv_label_create(header_panel);
  lv_label_set_text(ui_Screen10_Boost_Val, "Boost: -- kPa");
  lv_obj_set_style_text_color(ui_Screen10_Boost_Val, lv_color_hex(CLR_GOLD), 0);
  lv_obj_set_style_text_font(ui_Screen10_Boost_Val, &lv_font_montserrat_14, 0);
  lv_obj_align(ui_Screen10_Boost_Val, LV_ALIGN_BOTTOM_MID, -45, -8);

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
  lv_obj_t *wg_actual_title = lv_label_create(wg_panel);
  lv_label_set_text(wg_actual_title, "Actual Geometry:");
  lv_obj_set_style_text_color(wg_actual_title, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(wg_actual_title, &lv_font_montserrat_14, 0);
  lv_obj_align(wg_actual_title, LV_ALIGN_TOP_LEFT, 15, 45);

  ui_Wg_Actual_Label = lv_label_create(wg_panel);
  lv_label_set_text(ui_Wg_Actual_Label, "0%");
  lv_obj_set_style_text_color(ui_Wg_Actual_Label, lv_color_hex(CLR_CYAN), 0);
  lv_obj_set_style_text_font(ui_Wg_Actual_Label, &montserrat_20_en_ru, 0);
  lv_obj_align(ui_Wg_Actual_Label, LV_ALIGN_TOP_RIGHT, -15, 40);

  ui_Wg_Actual_Bar = lv_bar_create(wg_panel);
  lv_obj_set_size(ui_Wg_Actual_Bar, 310, 15);
  lv_obj_align(ui_Wg_Actual_Bar, LV_ALIGN_TOP_MID, 0, 75);
  lv_bar_set_range(ui_Wg_Actual_Bar, 0, 100);
  lv_obj_set_style_bg_color(ui_Wg_Actual_Bar, lv_color_hex(0x1F293B), LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui_Wg_Actual_Bar, lv_color_hex(CLR_CYAN), LV_PART_INDICATOR);

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
  lv_obj_add_event_cb(ui_Wg_Mode_Auto_Btn, wg_mode_auto_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *btn_wg1_lbl = lv_label_create(ui_Wg_Mode_Auto_Btn);
  lv_label_set_text(btn_wg1_lbl, "AUTO");
  lv_obj_set_style_text_font(btn_wg1_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(btn_wg1_lbl);

  ui_Wg_Mode_Man_Btn = lv_btn_create(wg_panel);
  lv_obj_set_size(ui_Wg_Mode_Man_Btn, 150, 40);
  lv_obj_set_pos(ui_Wg_Mode_Man_Btn, 180, 120);
  lv_obj_set_style_radius(ui_Wg_Mode_Man_Btn, 6, 0);
  lv_obj_set_style_shadow_width(ui_Wg_Mode_Man_Btn, 0, 0);
  lv_obj_add_event_cb(ui_Wg_Mode_Man_Btn, wg_mode_man_cb, LV_EVENT_CLICKED, NULL);
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
  lv_obj_set_style_text_color(ui_Wg_Slider_Label, lv_color_hex(CLR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(ui_Wg_Slider_Label, &lv_font_montserrat_14, 0);
  lv_obj_align(ui_Wg_Slider_Label, LV_ALIGN_TOP_RIGHT, -15, 185);

  ui_Wg_Slider = lv_slider_create(wg_panel);
  lv_obj_set_size(ui_Wg_Slider, 310, 12);
  lv_obj_align(ui_Wg_Slider, LV_ALIGN_TOP_MID, 0, 215);
  lv_slider_set_value(ui_Wg_Slider, wg_manual_pos, LV_ANIM_OFF);
  lv_obj_add_event_cb(ui_Wg_Slider, wg_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_set_style_bg_color(ui_Wg_Slider, lv_color_hex(0x1F293B), LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui_Wg_Slider, lv_color_hex(CLR_CYAN), LV_PART_INDICATOR);

  // Divider
  lv_obj_t *div_wg2 = lv_obj_create(wg_panel);
  lv_obj_set_size(div_wg2, 310, 1);
  lv_obj_set_pos(div_wg2, 15, 245);
  lv_obj_set_style_bg_color(div_wg2, lv_color_hex(CLR_BORDER), 0);

  // Table header
  lv_obj_t *wg_map_title = lv_label_create(wg_panel);
  lv_label_set_text(wg_map_title, "VGT AUTO POSITION MAP");
  lv_obj_set_style_text_color(wg_map_title, lv_color_hex(CLR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(wg_map_title, &lv_font_montserrat_14, 0);
  lv_obj_align(wg_map_title, LV_ALIGN_TOP_MID, 0, 260);

  // Scrollable container for map points
  lv_obj_t *wg_map_cont = lv_obj_create(wg_panel);
  lv_obj_set_size(wg_map_cont, 320, 780);
  lv_obj_set_pos(wg_map_cont, 10, 290);
  lv_obj_set_style_bg_opa(wg_map_cont, 0, 0);
  lv_obj_set_style_border_width(wg_map_cont, 0, 0);
  lv_obj_set_style_pad_all(wg_map_cont, 0, 0);
  lv_obj_set_scrollbar_mode(wg_map_cont, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_flex_flow(wg_map_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(wg_map_cont, 6, 0);

  // Build map rows
  for (int i = 0; i < 10; i++) {
    lv_obj_t *row = lv_obj_create(wg_map_cont);
    lv_obj_set_size(row, 300, 52);
    lv_obj_set_style_bg_color(row, lv_color_hex(CLR_BTN_ACTIVE), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Index
    lv_obj_t *idx_lbl = lv_label_create(row);
    lv_label_set_text_fmt(idx_lbl, "#%d", i + 1);
    lv_obj_set_style_text_color(idx_lbl, lv_color_hex(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(idx_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(idx_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    // RPM buttons
    uintptr_t data_minus_rpm = (0 << 6) | (i << 2) | (0 << 1) | 0;
    uintptr_t data_plus_rpm =  (0 << 6) | (i << 2) | (0 << 1) | 1;

    lv_obj_t *btn_r_min = lv_btn_create(row);
    lv_obj_set_size(btn_r_min, 30, 30);
    lv_obj_align(btn_r_min, LV_ALIGN_LEFT_MID, 28, 0);
    lv_obj_set_style_bg_color(btn_r_min, lv_color_hex(CLR_BTN_BG), 0);
    lv_obj_set_style_radius(btn_r_min, 4, 0);
    lv_obj_set_style_shadow_width(btn_r_min, 0, 0);
    lv_obj_add_event_cb(btn_r_min, wg_map_btn_cb, LV_EVENT_CLICKED, (void *)data_minus_rpm);
    lv_obj_t *lbl_rm = lv_label_create(btn_r_min);
    lv_label_set_text(lbl_rm, "-");
    lv_obj_center(lbl_rm);

    wg_rpm_labels[i] = lv_label_create(row);
    lv_label_set_text_fmt(wg_rpm_labels[i], "%d", wg_map[i].rpm);
    lv_obj_set_style_text_color(wg_rpm_labels[i], lv_color_hex(CLR_TEXT_WHITE), 0);
    lv_obj_set_style_text_font(wg_rpm_labels[i], &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(wg_rpm_labels[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(wg_rpm_labels[i], 52, 20);
    lv_obj_align(wg_rpm_labels[i], LV_ALIGN_LEFT_MID, 60, 0);

    lv_obj_t *btn_r_pls = lv_btn_create(row);
    lv_obj_set_size(btn_r_pls, 30, 30);
    lv_obj_align(btn_r_pls, LV_ALIGN_LEFT_MID, 114, 0);
    lv_obj_set_style_bg_color(btn_r_pls, lv_color_hex(CLR_BTN_BG), 0);
    lv_obj_set_style_radius(btn_r_pls, 4, 0);
    lv_obj_set_style_shadow_width(btn_r_pls, 0, 0);
    lv_obj_add_event_cb(btn_r_pls, wg_map_btn_cb, LV_EVENT_CLICKED, (void *)data_plus_rpm);
    lv_obj_t *lbl_rp = lv_label_create(btn_r_pls);
    lv_label_set_text(lbl_rp, "+");
    lv_obj_center(lbl_rp);

    // Separator
    lv_obj_t *arr = lv_label_create(row);
    lv_label_set_text(arr, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(arr, lv_color_hex(CLR_CYAN), 0);
    lv_obj_align(arr, LV_ALIGN_CENTER, 4, 0);

    // Position buttons
    uintptr_t data_minus_pos = (0 << 6) | (i << 2) | (1 << 1) | 0;
    uintptr_t data_plus_pos =  (0 << 6) | (i << 2) | (1 << 1) | 1;

    lv_obj_t *btn_p_min = lv_btn_create(row);
    lv_obj_set_size(btn_p_min, 30, 30);
    lv_obj_align(btn_p_min, LV_ALIGN_RIGHT_MID, -114, 0);
    lv_obj_set_style_bg_color(btn_p_min, lv_color_hex(CLR_BTN_BG), 0);
    lv_obj_set_style_radius(btn_p_min, 4, 0);
    lv_obj_set_style_shadow_width(btn_p_min, 0, 0);
    lv_obj_add_event_cb(btn_p_min, wg_map_btn_cb, LV_EVENT_CLICKED, (void *)data_minus_pos);
    lv_obj_t *lbl_pm = lv_label_create(btn_p_min);
    lv_label_set_text(lbl_pm, "-");
    lv_obj_center(lbl_pm);

    wg_pos_labels[i] = lv_label_create(row);
    lv_label_set_text_fmt(wg_pos_labels[i], "%d%%", wg_map[i].pos);
    lv_obj_set_style_text_color(wg_pos_labels[i], lv_color_hex(CLR_TEXT_WHITE), 0);
    lv_obj_set_style_text_font(wg_pos_labels[i], &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(wg_pos_labels[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(wg_pos_labels[i], 52, 20);
    lv_obj_align(wg_pos_labels[i], LV_ALIGN_RIGHT_MID, -60, 0);

    lv_obj_t *btn_p_pls = lv_btn_create(row);
    lv_obj_set_size(btn_p_pls, 30, 30);
    lv_obj_align(btn_p_pls, LV_ALIGN_RIGHT_MID, -28, 0);
    lv_obj_set_style_bg_color(btn_p_pls, lv_color_hex(CLR_BTN_BG), 0);
    lv_obj_set_style_radius(btn_p_pls, 4, 0);
    lv_obj_set_style_shadow_width(btn_p_pls, 0, 0);
    lv_obj_add_event_cb(btn_p_pls, wg_map_btn_cb, LV_EVENT_CLICKED, (void *)data_plus_pos);
    lv_obj_t *lbl_pp = lv_label_create(btn_p_pls);
    lv_label_set_text(lbl_pp, "+");
    lv_obj_center(lbl_pp);
  }

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
  lv_obj_set_style_text_color(ui_Bov_State_Label, lv_color_hex(CLR_TEXT_WHITE), 0);
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
  lv_obj_add_event_cb(ui_Bov_Mode_Auto_Btn, bov_mode_auto_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *btn_bov1_lbl = lv_label_create(ui_Bov_Mode_Auto_Btn);
  lv_label_set_text(btn_bov1_lbl, "AUTO");
  lv_obj_set_style_text_font(btn_bov1_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(btn_bov1_lbl);

  ui_Bov_Mode_Man_Btn = lv_btn_create(bov_panel);
  lv_obj_set_size(ui_Bov_Mode_Man_Btn, 150, 40);
  lv_obj_set_pos(ui_Bov_Mode_Man_Btn, 180, 120);
  lv_obj_set_style_radius(ui_Bov_Mode_Man_Btn, 6, 0);
  lv_obj_set_style_shadow_width(ui_Bov_Mode_Man_Btn, 0, 0);
  lv_obj_add_event_cb(ui_Bov_Mode_Man_Btn, bov_mode_man_cb, LV_EVENT_CLICKED, NULL);
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
  lv_obj_add_event_cb(ui_Bov_Switch, bov_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_set_style_bg_color(ui_Bov_Switch, lv_color_hex(CLR_GOLD), LV_PART_INDICATOR | LV_STATE_CHECKED);

  // Divider
  lv_obj_t *div_bov2 = lv_obj_create(bov_panel);
  lv_obj_set_size(div_bov2, 310, 1);
  lv_obj_set_pos(div_bov2, 15, 245);
  lv_obj_set_style_bg_color(div_bov2, lv_color_hex(CLR_BORDER), 0);

  // Automatic Strategy Parameters Configurator (ME7 LDUVST)
  lv_obj_t *bov_config_title = lv_label_create(bov_panel);
  lv_label_set_text(bov_config_title, "AUTOMATIC STRATEGY (ME7.1)");
  lv_obj_set_style_text_color(bov_config_title, lv_color_hex(CLR_TEXT_WHITE), 0);
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
  lv_obj_set_style_text_color(ui_Bov_Tps_Slider_Label, lv_color_hex(CLR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(ui_Bov_Tps_Slider_Label, &lv_font_montserrat_12, 0);
  lv_obj_align(ui_Bov_Tps_Slider_Label, LV_ALIGN_TOP_RIGHT, -15, 290);

  ui_Bov_Tps_Slider = lv_slider_create(bov_panel);
  lv_obj_set_size(ui_Bov_Tps_Slider, 310, 10);
  lv_obj_align(ui_Bov_Tps_Slider, LV_ALIGN_TOP_MID, 0, 310);
  lv_slider_set_range(ui_Bov_Tps_Slider, 10, 80);
  lv_slider_set_value(ui_Bov_Tps_Slider, bov_tps_threshold, LV_ANIM_OFF);
  lv_obj_add_event_cb(ui_Bov_Tps_Slider, bov_tps_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_set_style_bg_color(ui_Bov_Tps_Slider, lv_color_hex(0x1F293B), LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui_Bov_Tps_Slider, lv_color_hex(CLR_GOLD), LV_PART_INDICATOR);

  // Trigger 2: SDLDSUA (Pressure Delta)
  lv_obj_t *press_lbl = lv_label_create(bov_panel);
  lv_label_set_text(press_lbl, "SDLDSUA (Boost Delta):");
  lv_obj_set_style_text_color(press_lbl, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(press_lbl, &lv_font_montserrat_12, 0);
  lv_obj_align(press_lbl, LV_ALIGN_TOP_LEFT, 15, 340);

  ui_Bov_Press_Slider_Label = lv_label_create(bov_panel);
  lv_label_set_text(ui_Bov_Press_Slider_Label, "35 kPa");
  lv_obj_set_style_text_color(ui_Bov_Press_Slider_Label, lv_color_hex(CLR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(ui_Bov_Press_Slider_Label, &lv_font_montserrat_12, 0);
  lv_obj_align(ui_Bov_Press_Slider_Label, LV_ALIGN_TOP_RIGHT, -15, 340);

  ui_Bov_Press_Slider = lv_slider_create(bov_panel);
  lv_obj_set_size(ui_Bov_Press_Slider, 310, 10);
  lv_obj_align(ui_Bov_Press_Slider, LV_ALIGN_TOP_MID, 0, 360);
  lv_slider_set_range(ui_Bov_Press_Slider, 10, 100);
  lv_slider_set_value(ui_Bov_Press_Slider, bov_press_threshold, LV_ANIM_OFF);
  lv_obj_add_event_cb(ui_Bov_Press_Slider, bov_press_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_set_style_bg_color(ui_Bov_Press_Slider, lv_color_hex(0x1F293B), LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui_Bov_Press_Slider, lv_color_hex(CLR_GOLD), LV_PART_INDICATOR);

  // Trigger 3: THLDUVD (Solenoid open duration)
  lv_obj_t *dur_lbl = lv_label_create(bov_panel);
  lv_label_set_text(dur_lbl, "THLDUVD (Hold Duration):");
  lv_obj_set_style_text_color(dur_lbl, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(dur_lbl, &lv_font_montserrat_12, 0);
  lv_obj_align(dur_lbl, LV_ALIGN_TOP_LEFT, 15, 390);

  ui_Bov_Dur_Slider_Label = lv_label_create(bov_panel);
  lv_label_set_text(ui_Bov_Dur_Slider_Label, "2.0s");
  lv_obj_set_style_text_color(ui_Bov_Dur_Slider_Label, lv_color_hex(CLR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(ui_Bov_Dur_Slider_Label, &lv_font_montserrat_12, 0);
  lv_obj_align(ui_Bov_Dur_Slider_Label, LV_ALIGN_TOP_RIGHT, -15, 390);

  ui_Bov_Dur_Slider = lv_slider_create(bov_panel);
  lv_obj_set_size(ui_Bov_Dur_Slider, 310, 10);
  lv_obj_align(ui_Bov_Dur_Slider, LV_ALIGN_TOP_MID, 0, 410);
  lv_slider_set_range(ui_Bov_Dur_Slider, 5, 30); // 0.5s to 3.0s in tenths
  lv_slider_set_value(ui_Bov_Dur_Slider, bov_open_duration, LV_ANIM_OFF);
  lv_obj_add_event_cb(ui_Bov_Dur_Slider, bov_dur_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_set_style_bg_color(ui_Bov_Dur_Slider, lv_color_hex(0x1F293B), LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui_Bov_Dur_Slider, lv_color_hex(CLR_GOLD), LV_PART_INDICATOR);

  // Trigger 4: Stationary Path (SVDLDUVS)
  lv_obj_t *stat_title = lv_label_create(bov_panel);
  lv_label_set_text(stat_title, "Stationary Path Enable:");
  lv_obj_set_style_text_color(stat_title, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(stat_title, &lv_font_montserrat_12, 0);
  lv_obj_align(stat_title, LV_ALIGN_TOP_LEFT, 15, 450);

  ui_Bov_Stat_Switch = lv_switch_create(bov_panel);
  lv_obj_set_size(ui_Bov_Stat_Switch, 50, 24);
  lv_obj_align(ui_Bov_Stat_Switch, LV_ALIGN_TOP_RIGHT, -15, 445);
  lv_obj_add_event_cb(ui_Bov_Stat_Switch, bov_stat_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_set_style_bg_color(ui_Bov_Stat_Switch, lv_color_hex(CLR_GOLD), LV_PART_INDICATOR | LV_STATE_CHECKED);

  lv_obj_t *ratio_lbl = lv_label_create(bov_panel);
  lv_label_set_text(ratio_lbl, "SVDLDUVS (Pressure Ratio):");
  lv_obj_set_style_text_color(ratio_lbl, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(ratio_lbl, &lv_font_montserrat_12, 0);
  lv_obj_align(ratio_lbl, LV_ALIGN_TOP_LEFT, 15, 490);

  ui_Bov_Stat_Ratio_Label = lv_label_create(bov_panel);
  lv_label_set_text(ui_Bov_Stat_Ratio_Label, "1.20");
  lv_obj_set_style_text_color(ui_Bov_Stat_Ratio_Label, lv_color_hex(CLR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(ui_Bov_Stat_Ratio_Label, &lv_font_montserrat_12, 0);
  lv_obj_align(ui_Bov_Stat_Ratio_Label, LV_ALIGN_TOP_RIGHT, -15, 490);

  ui_Bov_Stat_Ratio_Slider = lv_slider_create(bov_panel);
  lv_obj_set_size(ui_Bov_Stat_Ratio_Slider, 310, 10);
  lv_obj_align(ui_Bov_Stat_Ratio_Slider, LV_ALIGN_TOP_MID, 0, 510);
  lv_slider_set_range(ui_Bov_Stat_Ratio_Slider, 105, 150); // 1.05 to 1.50
  lv_slider_set_value(ui_Bov_Stat_Ratio_Slider, bov_stat_ratio, LV_ANIM_OFF);
  lv_obj_add_event_cb(ui_Bov_Stat_Ratio_Slider, bov_stat_ratio_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_set_style_bg_color(ui_Bov_Stat_Ratio_Slider, lv_color_hex(0x1F293B), LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui_Bov_Stat_Ratio_Slider, lv_color_hex(CLR_GOLD), LV_PART_INDICATOR);

  update_bov_ui_state();

  // Navigation Buttons
  ui_create_standard_navigation_buttons(ui_Screen10);

  ESP_LOGI(TAG, "Screen 10 (Boost Control) successfully initialized.");
}

// Lifecycle screen destroy method
void ui_Screen10_screen_destroy(void) {
  ESP_LOGI(TAG, "Destroying ui_Screen10...");

  ui_Screen10_Boost_Val = NULL;
  ui_Screen10_RPM_Val = NULL;

  ui_Wg_Actual_Bar = NULL;
  ui_Wg_Actual_Label = NULL;
  ui_Wg_Mode_Auto_Btn = NULL;
  ui_Wg_Mode_Man_Btn = NULL;
  ui_Wg_Slider = NULL;
  ui_Wg_Slider_Label = NULL;

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
  if (!ui_Screen10) return;

  ecu_data_t data;
  if (demo_mode_get_enabled()) {
    ecu_data_simulate(&data);
  } else {
    ecu_data_get_copy(&data);
  }

  // Update header labels
  if (ui_Screen10_RPM_Val) {
    lv_label_set_text_fmt(ui_Screen10_RPM_Val, "RPM: %.0f rpm", data.engine_rpm);
  }
  if (ui_Screen10_Boost_Val) {
    lv_label_set_text_fmt(ui_Screen10_Boost_Val, "Boost: %.1f kPa", data.map_kpa);
  }

  // Compute Wastegate position
  int actual_wg_pos = 0;
  if (wg_is_auto) {
    actual_wg_pos = lookup_wg_position((int)data.engine_rpm);
  } else {
    actual_wg_pos = wg_manual_pos;
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
    lv_label_set_text(ui_Bov_State_Label, (actual_bov_state > 0) ? "OPEN" : "CLOSED");
    lv_obj_set_style_text_color(ui_Bov_State_Label, 
      (actual_bov_state > 0) ? lv_color_hex(CLR_GOLD) : lv_color_hex(CLR_TEXT_WHITE), 0);
  }

  if (ui_Bov_Led) {
    lv_obj_set_style_bg_color(ui_Bov_Led, 
      (actual_bov_state > 0) ? lv_color_hex(CLR_LED_ON) : lv_color_hex(CLR_LED_OFF), 0);
  }

  s_actual_wg_pos = actual_wg_pos;
  s_actual_bov_state = actual_bov_state;
}

// ---------- Bidirectional Getters & Setters for External / Lua Engine ----------
extern bool example_lvgl_lock(int timeout_ms);
extern void example_lvgl_unlock(void);

bool ui_Screen10_get_wg_is_auto(void) { return wg_is_auto; }
void ui_Screen10_set_wg_is_auto(bool is_auto) {
  wg_is_auto = is_auto;
  if (ui_Screen10) {
    if (example_lvgl_lock(500)) {
      update_wg_ui_state();
      example_lvgl_unlock();
    }
  }
}

int ui_Screen10_get_wg_manual_pos(void) { return wg_manual_pos; }
void ui_Screen10_set_wg_manual_pos(int pos) {
  if (pos < 0) pos = 0;
  if (pos > 100) pos = 100;
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
}

int ui_Screen10_get_wg_map_rpm(int idx) {
  if (idx < 0 || idx >= 10) return 0;
  return wg_map[idx].rpm;
}

void ui_Screen10_set_wg_map_rpm(int idx, int rpm) {
  if (idx < 0 || idx >= 10) return;
  wg_map[idx].rpm = rpm;
  if (ui_Screen10 && wg_rpm_labels[idx]) {
    if (example_lvgl_lock(500)) {
      lv_label_set_text_fmt(wg_rpm_labels[idx], "%d", rpm);
      example_lvgl_unlock();
    }
  }
}

int ui_Screen10_get_wg_map_pos(int idx) {
  if (idx < 0 || idx >= 10) return 0;
  return wg_map[idx].pos;
}

void ui_Screen10_set_wg_map_pos(int idx, int pos) {
  if (idx < 0 || idx >= 10) return;
  if (pos < 0) pos = 0;
  if (pos > 100) pos = 100;
  wg_map[idx].pos = pos;
  if (ui_Screen10 && wg_pos_labels[idx]) {
    if (example_lvgl_lock(500)) {
      lv_label_set_text_fmt(wg_pos_labels[idx], "%d%%", pos);
      example_lvgl_unlock();
    }
  }
}

int ui_Screen10_get_actual_wg_pos(void) { return s_actual_wg_pos; }

bool ui_Screen10_get_bov_is_auto(void) { return bov_is_auto; }
void ui_Screen10_set_bov_is_auto(bool is_auto) {
  bov_is_auto = is_auto;
  if (ui_Screen10) {
    if (example_lvgl_lock(500)) {
      update_bov_ui_state();
      example_lvgl_unlock();
    }
  }
}

bool ui_Screen10_get_bov_manual_open(void) { return bov_manual_open; }
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
}

int ui_Screen10_get_bov_tps_threshold(void) { return bov_tps_threshold; }
void ui_Screen10_set_bov_tps_threshold(int val) {
  if (val < 10) val = 10;
  if (val > 80) val = 80;
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
}

int ui_Screen10_get_bov_press_threshold(void) { return bov_press_threshold; }
void ui_Screen10_set_bov_press_threshold(int val) {
  if (val < 10) val = 10;
  if (val > 100) val = 100;
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
}

int ui_Screen10_get_bov_open_duration(void) { return bov_open_duration; }
void ui_Screen10_set_bov_open_duration(int val_ms) {
  if (val_ms < 5) val_ms = 5;
  if (val_ms > 30) val_ms = 30;
  bov_open_duration = val_ms;
  if (ui_Screen10 && ui_Bov_Dur_Slider) {
    if (example_lvgl_lock(500)) {
      lv_slider_set_value(ui_Bov_Dur_Slider, val_ms, LV_ANIM_OFF);
      if (ui_Bov_Dur_Slider_Label) {
        lv_label_set_text_fmt(ui_Bov_Dur_Slider_Label, "%.1fs", (float)val_ms / 10.0f);
      }
      example_lvgl_unlock();
    }
  }
}

bool ui_Screen10_get_bov_stat_enabled(void) { return bov_stat_enabled; }
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
}

int ui_Screen10_get_bov_stat_ratio(void) { return bov_stat_ratio; }
void ui_Screen10_set_bov_stat_ratio(int val_x100) {
  if (val_x100 < 105) val_x100 = 105;
  if (val_x100 > 150) val_x100 = 150;
  bov_stat_ratio = val_x100;
  if (ui_Screen10 && ui_Bov_Stat_Ratio_Slider) {
    if (example_lvgl_lock(500)) {
      lv_slider_set_value(ui_Bov_Stat_Ratio_Slider, val_x100, LV_ANIM_OFF);
      if (ui_Bov_Stat_Ratio_Label) {
        lv_label_set_text_fmt(ui_Bov_Stat_Ratio_Label, "%.2f", (float)val_x100 / 100.0f);
      }
      example_lvgl_unlock();
    }
  }
}

int ui_Screen10_get_actual_bov_state(void) { return s_actual_bov_state; }
