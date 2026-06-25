// ECU Dashboard Screen 5 - ECU Data Gauges (Page 2)
#include "ui_Screen5.h"
#include "../ui.h"
#include "ecu_data.h"
#include "ui_helpers.h"
#include "ui_screen_manager.h"
#include <esp_log.h>
#include <stdio.h>

// 1 BPP font for performance
LV_FONT_DECLARE(lv_font_unscii_16);

// Screen object
lv_obj_t *ui_Screen5;

// Gauge Objects
lv_obj_t *ui_Arc_Eng_TQ_Act;
lv_obj_t *ui_Label_Eng_TQ_Act_Value;
lv_obj_t *ui_Arc_Limit_TQ;
lv_obj_t *ui_Label_Limit_TQ_Value;

// New Gauges
lv_obj_t *ui_Arc_IAT = NULL;
lv_obj_t *ui_Label_IAT_Value = NULL;
lv_obj_t *ui_Arc_Speed = NULL;
lv_obj_t *ui_Label_Speed_Value = NULL;
lv_obj_t *ui_Arc_Trans_Temp = NULL;
lv_obj_t *ui_Label_Trans_Temp_Value = NULL;
lv_obj_t *ui_Arc_AFR = NULL;
lv_obj_t *ui_Label_AFR_Value = NULL;
lv_obj_t *ui_Arc_EGT = NULL;
lv_obj_t *ui_Label_EGT_Value = NULL;
lv_obj_t *ui_Arc_Knock_Retard = NULL;
lv_obj_t *ui_Label_Knock_Retard_Value = NULL;
lv_obj_t *ui_Arc_Boost_Act = NULL;
lv_obj_t *ui_Label_Boost_Act_Value = NULL;
lv_obj_t *ui_Arc_Ambient_Temp = NULL;
lv_obj_t *ui_Label_Ambient_Temp_Value = NULL;

// Animation variables
static lv_anim_t anim_eng_tq_act;
static lv_anim_t anim_limit_tq;
static lv_anim_t anim_iat;
static lv_anim_t anim_speed;
static lv_anim_t anim_trans_temp;
static lv_anim_t anim_afr;
static lv_anim_t anim_egt;
static lv_anim_t anim_knock_retard;
static lv_anim_t anim_boost_act;
static lv_anim_t anim_ambient_temp;

// Function prototypes
static void anim_value_cb_screen5(void *var, int32_t v);

// Helper to get container of a gauge
static lv_obj_t *get_gauge_container(lv_obj_t *gauge) __attribute__((unused));
static lv_obj_t *get_gauge_container(lv_obj_t *gauge) {
  if (!gauge)
    return NULL;
  return lv_obj_get_parent(gauge);
}

// Update layout based on visible gauges
void ui_Screen5_update_layout(void) {
  // Legacy function - now handled by ui_layout_manager.c
  ESP_LOGI("SCREEN5", "Legacy update_layout called - ignored");
}

// Helper function to create a gauge
static void create_gauge(lv_obj_t *parent, lv_obj_t **arc, lv_obj_t **label,
                         const char *title, const char *unit, lv_color_t color,
                         int32_t min_val, int32_t max_val, int x, int y, gauge_id_t gauge_id) {
  lv_obj_t *cont = lv_obj_create(parent);
  lv_obj_set_width(cont, 330);
  lv_obj_set_height(cont, 360);
  lv_obj_set_x(cont, x);
  lv_obj_set_y(cont, y);
  lv_obj_set_align(cont, LV_ALIGN_TOP_LEFT);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(cont, lv_color_hex(0x2a2a2a), 0);
  lv_obj_set_style_border_color(cont, color, 0);
  lv_obj_set_style_border_width(cont, 2, 0);
  lv_obj_set_style_radius(cont, 15, 0);
  lv_obj_set_style_pad_all(cont, 10, 0);
  lv_obj_set_style_shadow_width(cont, 0, 0); // Disable shadow for performance

  // Add click support for unit switching
  lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(cont, ui_gauge_click_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)gauge_id);

  lv_obj_t *label_title = lv_label_create(cont);
  lv_label_set_text(label_title, title);
  lv_obj_set_style_text_color(label_title, lv_color_white(), 0);
  lv_obj_align(label_title, LV_ALIGN_BOTTOM_MID, 0, -15);

  *arc = lv_arc_create(cont);
  lv_obj_set_size(*arc, 240, 240);
  lv_arc_set_rotation(*arc, 135);
  lv_arc_set_bg_angles(*arc, 0, 270);
  lv_arc_set_range(*arc, min_val, max_val);
  lv_arc_set_value(*arc, min_val);
  lv_obj_set_style_arc_color(*arc, color, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(*arc, 15, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(*arc, lv_color_hex(0x4a4a4a), LV_PART_MAIN);
  lv_obj_set_style_arc_width(*arc, 15, LV_PART_MAIN);
  lv_obj_center(*arc);
  lv_obj_remove_style(*arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(*arc, LV_OBJ_FLAG_CLICKABLE);

  *label = lv_label_create(cont);
  lv_label_set_text(*label, "0");
  lv_obj_set_style_text_color(*label, lv_color_white(), 0);
  lv_obj_set_style_text_font(*label, &lv_font_montserrat_24, 0);
  lv_obj_center(*label);
  lv_obj_align(*label, LV_ALIGN_CENTER, 0, -5);

  lv_obj_t *label_unit = lv_label_create(cont);
  lv_label_set_text(label_unit, unit);
  lv_obj_set_style_text_color(label_unit, lv_color_hex(0xcccccc), 0);
  lv_obj_align_to(label_unit, *label, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
}

// Main screen initialization
void ui_Screen5_screen_init(void) {
  ui_Screen5 = lv_obj_create(NULL);
  lv_obj_set_size(ui_Screen5, 736, 1280);
  lv_obj_set_pos(ui_Screen5, 0, 0);
  lv_obj_clear_flag(ui_Screen5, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(ui_Screen5, lv_color_hex(0x1a1a1a), 0);

  // Title removed as per user request to provide more space.

  // Base Gauges
  create_gauge(ui_Screen5, &ui_Arc_Eng_TQ_Act, &ui_Label_Eng_TQ_Act_Value,
               "Eng Tq Act", "Nm", lv_color_hex(0x00D4FF), 0, 500, 20, 60, GAUGE_ENG_ACT);
  create_gauge(ui_Screen5, &ui_Arc_Limit_TQ, &ui_Label_Limit_TQ_Value,
               "Torque Limit", "Nm", lv_color_hex(0x00FF88), 0, 500, 370, 60, GAUGE_LIMIT_TQ);

  // New Gauges instantiated on Screen 5 (will reflow dynamically)
  create_gauge(ui_Screen5, &ui_Arc_IAT, &ui_Label_IAT_Value, "Intake Temp",
               "°C", lv_color_hex(0xFF8800), 0, 100, 20, 440, GAUGE_IAT);
  create_gauge(ui_Screen5, &ui_Arc_Speed, &ui_Label_Speed_Value, "Vehicle Speed",
               "km/h", lv_color_hex(0x00FFD4), 0, 260, 370, 440, GAUGE_SPEED);

  create_gauge(ui_Screen5, &ui_Arc_Trans_Temp, &ui_Label_Trans_Temp_Value, "Trans Temp",
               "°C", lv_color_hex(0xFF5500), 60, 140, 20, 820, GAUGE_TRANS_TEMP);
  create_gauge(ui_Screen5, &ui_Arc_AFR, &ui_Label_AFR_Value, "Lambda / AFR",
               "λ", lv_color_hex(0x00FF88), 60, 140, 370, 820, GAUGE_AFR);

  create_gauge(ui_Screen5, &ui_Arc_EGT, &ui_Label_EGT_Value, "EGT",
               "°C", lv_color_hex(0xFF3300), 200, 1000, 20, 1200, GAUGE_EGT);
  create_gauge(ui_Screen5, &ui_Arc_Knock_Retard, &ui_Label_Knock_Retard_Value, "Knock Retard",
               "°", lv_color_hex(0xFFCC00), 0, 120, 370, 1200, GAUGE_KNOCK_RETARD);
  create_gauge(ui_Screen5, &ui_Arc_Boost_Act, &ui_Label_Boost_Act_Value, "Actual Boost",
               "kPa", lv_color_hex(0x00D4FF), 100, 250, 20, 1580, GAUGE_BOOST_ACT);
  create_gauge(ui_Screen5, &ui_Arc_Ambient_Temp, &ui_Label_Ambient_Temp_Value, "Ambient Temp",
               "°C", lv_color_hex(0x00FF88), -20, 60, 370, 1580, GAUGE_AMBIENT_TEMP);

  // Apply initial layout
  ui_Screen5_update_layout();

  // Initialize animations
  lv_anim_init(&anim_eng_tq_act);
  lv_anim_set_var(&anim_eng_tq_act, ui_Arc_Eng_TQ_Act);
  lv_anim_set_values(&anim_eng_tq_act, 0, 500);
  lv_anim_set_time(&anim_eng_tq_act, 3000);
  lv_anim_set_playback_time(&anim_eng_tq_act, 3000);
  lv_anim_set_repeat_count(&anim_eng_tq_act, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_exec_cb(&anim_eng_tq_act, anim_value_cb_screen5);

  lv_anim_init(&anim_limit_tq);
  lv_anim_set_var(&anim_limit_tq, ui_Arc_Limit_TQ);
  lv_anim_set_values(&anim_limit_tq, 0, 500);
  lv_anim_set_time(&anim_limit_tq, 4000);
  lv_anim_set_playback_time(&anim_limit_tq, 4000);
  lv_anim_set_repeat_count(&anim_limit_tq, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_exec_cb(&anim_limit_tq, anim_value_cb_screen5);

  lv_anim_init(&anim_iat);
  lv_anim_set_var(&anim_iat, ui_Arc_IAT);
  lv_anim_set_values(&anim_iat, 0, 100);
  lv_anim_set_time(&anim_iat, 3000);
  lv_anim_set_playback_time(&anim_iat, 3000);
  lv_anim_set_repeat_count(&anim_iat, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_exec_cb(&anim_iat, anim_value_cb_screen5);

  lv_anim_init(&anim_speed);
  lv_anim_set_var(&anim_speed, ui_Arc_Speed);
  lv_anim_set_values(&anim_speed, 0, 260);
  lv_anim_set_time(&anim_speed, 4000);
  lv_anim_set_playback_time(&anim_speed, 4000);
  lv_anim_set_repeat_count(&anim_speed, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_exec_cb(&anim_speed, anim_value_cb_screen5);

  lv_anim_init(&anim_trans_temp);
  lv_anim_set_var(&anim_trans_temp, ui_Arc_Trans_Temp);
  lv_anim_set_values(&anim_trans_temp, 60, 140);
  lv_anim_set_time(&anim_trans_temp, 5000);
  lv_anim_set_playback_time(&anim_trans_temp, 5000);
  lv_anim_set_repeat_count(&anim_trans_temp, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_exec_cb(&anim_trans_temp, anim_value_cb_screen5);

  lv_anim_init(&anim_afr);
  lv_anim_set_var(&anim_afr, ui_Arc_AFR);
  lv_anim_set_values(&anim_afr, 60, 140);
  lv_anim_set_time(&anim_afr, 2500);
  lv_anim_set_playback_time(&anim_afr, 2500);
  lv_anim_set_repeat_count(&anim_afr, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_exec_cb(&anim_afr, anim_value_cb_screen5);

  lv_anim_init(&anim_egt);
  lv_anim_set_var(&anim_egt, ui_Arc_EGT);
  lv_anim_set_values(&anim_egt, 200, 1000);
  lv_anim_set_time(&anim_egt, 6000);
  lv_anim_set_playback_time(&anim_egt, 6000);
  lv_anim_set_repeat_count(&anim_egt, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_exec_cb(&anim_egt, anim_value_cb_screen5);

  lv_anim_init(&anim_knock_retard);
  lv_anim_set_var(&anim_knock_retard, ui_Arc_Knock_Retard);
  lv_anim_set_values(&anim_knock_retard, 0, 120);
  lv_anim_set_time(&anim_knock_retard, 3000);
  lv_anim_set_playback_time(&anim_knock_retard, 3000);
  lv_anim_set_repeat_count(&anim_knock_retard, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_exec_cb(&anim_knock_retard, anim_value_cb_screen5);

  lv_anim_init(&anim_boost_act);
  lv_anim_set_var(&anim_boost_act, ui_Arc_Boost_Act);
  lv_anim_set_values(&anim_boost_act, 100, 250);
  lv_anim_set_time(&anim_boost_act, 3500);
  lv_anim_set_playback_time(&anim_boost_act, 3500);
  lv_anim_set_repeat_count(&anim_boost_act, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_exec_cb(&anim_boost_act, anim_value_cb_screen5);

  lv_anim_init(&anim_ambient_temp);
  lv_anim_set_var(&anim_ambient_temp, ui_Arc_Ambient_Temp);
  lv_anim_set_values(&anim_ambient_temp, -20, 60);
  lv_anim_set_time(&anim_ambient_temp, 3500);
  lv_anim_set_playback_time(&anim_ambient_temp, 3500);
  lv_anim_set_repeat_count(&anim_ambient_temp, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_exec_cb(&anim_ambient_temp, anim_value_cb_screen5);

  if (demo_mode_get_enabled()) {
    ui_Screen5_update_animations(true);
  }

  // Add navigation buttons if enabled in settings
  ui_create_standard_navigation_buttons(ui_Screen5);

  ESP_LOGI("SCREEN5", "Screen 5 initialized");
}

static void anim_value_cb_screen5(void *var, int32_t v) {
  if (var == ui_Arc_Eng_TQ_Act)
    update_gauge(GAUGE_ENG_ACT, ui_Arc_Eng_TQ_Act, ui_Label_Eng_TQ_Act_Value, v, "%.0f", 450, 500, false, lv_color_hex(0x00D4FF));
  else if (var == ui_Arc_Limit_TQ)
    update_gauge(GAUGE_LIMIT_TQ, ui_Arc_Limit_TQ, ui_Label_Limit_TQ_Value, v, "%.0f", 450, 500, false, lv_color_hex(0x00FF88));
  else if (var == ui_Arc_IAT)
    update_gauge(GAUGE_IAT, ui_Arc_IAT, ui_Label_IAT_Value, v, "%.0f", 70, 90, false, lv_color_hex(0xFF8800));
  else if (var == ui_Arc_Speed)
    update_gauge(GAUGE_SPEED, ui_Arc_Speed, ui_Label_Speed_Value, v, "%.0f", 200, 240, false, lv_color_hex(0x00FFD4));
  else if (var == ui_Arc_Trans_Temp)
    update_gauge(GAUGE_TRANS_TEMP, ui_Arc_Trans_Temp, ui_Label_Trans_Temp_Value, v, "%.0f", 100, 115, false, lv_color_hex(0xFF5500));
  else if (var == ui_Arc_AFR)
    update_gauge(GAUGE_AFR, ui_Arc_AFR, ui_Label_AFR_Value, v / 100.0f, "%.2f", 1.15f, 1.25f, false, lv_color_hex(0x00FF88));
  else if (var == ui_Arc_EGT)
    update_gauge(GAUGE_EGT, ui_Arc_EGT, ui_Label_EGT_Value, v, "%.0f", 850, 950, false, lv_color_hex(0xFF3300));
  else if (var == ui_Arc_Knock_Retard)
    update_gauge(GAUGE_KNOCK_RETARD, ui_Arc_Knock_Retard, ui_Label_Knock_Retard_Value, v / 10.0f, "%.1f", 3.0f, 6.0f, false, lv_color_hex(0xFFCC00));
  else if (var == ui_Arc_Boost_Act)
    update_gauge(GAUGE_BOOST_ACT, ui_Arc_Boost_Act, ui_Label_Boost_Act_Value, v, "%.0f", 200, 230, false, lv_color_hex(0x00D4FF));
  else if (var == ui_Arc_Ambient_Temp)
    update_gauge(GAUGE_AMBIENT_TEMP, ui_Arc_Ambient_Temp, ui_Label_Ambient_Temp_Value, v, "%.0f", 45, 55, false, lv_color_hex(0x00FF88));
}

void ui_Screen5_update_animations(bool demo_enabled) {
  if (demo_enabled) {
    lv_anim_start(&anim_eng_tq_act);
    lv_anim_start(&anim_limit_tq);
    lv_anim_start(&anim_iat);
    lv_anim_start(&anim_speed);
    lv_anim_start(&anim_trans_temp);
    lv_anim_start(&anim_afr);
    lv_anim_start(&anim_egt);
    lv_anim_start(&anim_knock_retard);
    lv_anim_start(&anim_boost_act);
    lv_anim_start(&anim_ambient_temp);
  } else {
    lv_anim_del(ui_Arc_Eng_TQ_Act, anim_value_cb_screen5);
    lv_anim_del(ui_Arc_Limit_TQ, anim_value_cb_screen5);
    lv_anim_del(ui_Arc_IAT, anim_value_cb_screen5);
    lv_anim_del(ui_Arc_Speed, anim_value_cb_screen5);
    lv_anim_del(ui_Arc_Trans_Temp, anim_value_cb_screen5);
    lv_anim_del(ui_Arc_AFR, anim_value_cb_screen5);
    lv_anim_del(ui_Arc_EGT, anim_value_cb_screen5);
    lv_anim_del(ui_Arc_Knock_Retard, anim_value_cb_screen5);
    lv_anim_del(ui_Arc_Boost_Act, anim_value_cb_screen5);
    lv_anim_del(ui_Arc_Ambient_Temp, anim_value_cb_screen5);
  }
}

// Function to control individual arc visibility
void ui_Screen5_update_arc_visibility(int arc_index, bool visible) {
  lv_obj_t *arc_container = NULL;
  const char *arc_name = NULL;

  switch (arc_index) {
  case 0:
    arc_container = lv_obj_get_parent(ui_Arc_Eng_TQ_Act);
    arc_name = "Eng TQ Act";
    break;
  case 1:
    arc_container = lv_obj_get_parent(ui_Arc_Limit_TQ);
    arc_name = "Limit TQ";
    break;
  case 2:
    arc_container = lv_obj_get_parent(ui_Arc_IAT);
    arc_name = "Intake Temp";
    break;
  case 3:
    arc_container = lv_obj_get_parent(ui_Arc_Speed);
    arc_name = "Vehicle Speed";
    break;
  case 4:
    arc_container = lv_obj_get_parent(ui_Arc_Trans_Temp);
    arc_name = "Trans Temp";
    break;
  case 5:
    arc_container = lv_obj_get_parent(ui_Arc_AFR);
    arc_name = "Lambda / AFR";
    break;
  case 6:
    arc_container = lv_obj_get_parent(ui_Arc_EGT);
    arc_name = "EGT";
    break;
  case 7:
    arc_container = lv_obj_get_parent(ui_Arc_Knock_Retard);
    arc_name = "Knock Retard";
    break;
  case 8:
    arc_container = lv_obj_get_parent(ui_Arc_Boost_Act);
    arc_name = "Actual Boost";
    break;
  case 9:
    arc_container = lv_obj_get_parent(ui_Arc_Ambient_Temp);
    arc_name = "Ambient Temp";
    break;
  default:
    ESP_LOGW("SCREEN5", "Invalid arc index: %d", arc_index);
    return;
  }

  if (!arc_container) {
    ESP_LOGW("SCREEN5", "Arc container not found for index %d", arc_index);
    return;
  }

  if (visible) {
    lv_obj_set_style_opa(arc_container, LV_OPA_COVER, 0);
    ESP_LOGI("SCREEN5", "%s gauge is now VISIBLE", arc_name);
  } else {
    lv_obj_set_style_opa(arc_container, LV_OPA_TRANSP, 0);
    ESP_LOGI("SCREEN5", "%s gauge is now HIDDEN", arc_name);
  }
}

void ui_Screen5_screen_destroy(void) {
  if (ui_Screen5) {
    lv_obj_del(ui_Screen5);
    ui_Screen5 = NULL;
  }
}
