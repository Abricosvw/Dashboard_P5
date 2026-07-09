#include "ui_updates.h"
#include "ai_manager.h"
#include "ecu_data.h"
#include "screens/ui_Screen6.h"
#include "screens/ui_Screen8.h"
#include "screens/ui_Screen9.h"
#include "screens/ui_Screen10.h"
#include "screens/ui_Screen11.h"
#include "screens/ui_Screen12.h"
#include "ui.h"
#include "wifi_controller.h"
#include <stdio.h>

static bool global_demo_mode = false;

void ui_updates_set_demo_mode(bool enabled) { global_demo_mode = enabled; }

// This function is called periodically by the LVGL task.
// It reads the latest data from the global ECU data struct
// and updates all the gauge widgets on all screens.
// Helper function to update gauge value, text, and color
void update_gauge(gauge_id_t id, lv_obj_t *arc, lv_obj_t *label, float value,
                  const char *default_fmt, float warn_thr, float crit_thr,
                  bool invert_logic, lv_color_t normal_color) {
  if (arc == NULL && label == NULL)
    return;

  // Dynamic Title Override for rusEFI MRE Platform
  lv_obj_t *parent = arc ? lv_obj_get_parent(arc) : NULL;
  if (parent) {
    lv_obj_t *title_label = lv_obj_get_child(parent, 0);
    if (title_label && title_label != label) {
      if (settings_get_can_platform() == PLATFORM_RUSEFI_MRE) {
        if (id == GAUGE_MAP || id == GAUGE_BOOST_ACT) {
          lv_label_set_text(title_label, "MAP sensor MRE");
        } else if (id == GAUGE_BOOST) {
          lv_label_set_text(title_label, "MAP Target MRE");
        } else if (id == GAUGE_WASTEGATE || id == GAUGE_WG_POS) {
          lv_label_set_text(title_label, "Westgate MRE");
        }
      } else {
        // Restore default titles for non-MRE platforms
        if (id == GAUGE_MAP) {
          lv_label_set_text(title_label, "MAP Pressure");
        } else if (id == GAUGE_BOOST_ACT) {
          lv_label_set_text(title_label, "Actual Boost");
        } else if (id == GAUGE_BOOST) {
          lv_label_set_text(title_label, "Target Boost");
        } else if (id == GAUGE_WASTEGATE) {
          lv_label_set_text(title_label, "WG Target");
        } else if (id == GAUGE_WG_POS) {
          lv_label_set_text(title_label, "WG Position");
        }
      }
    }
  }

  float converted_value = value;
  float converted_warn = warn_thr;
  float converted_crit = crit_thr;
  const char *unit_str = NULL;
  const char *fmt = default_fmt;

  int16_t arc_val = (int16_t)value;
  int16_t arc_min = 0;
  int16_t arc_max = 100;
  bool use_custom_arc_range = false;

  // Retrieve unit settings
  system_settings_t *settings = system_settings_get();
  uint8_t unit = (id != GAUGE_NONE && settings) ? settings->gauge_units[id] : 0;

  // Set default ranges in base unit
  float base_min = 0.0f;
  float base_max = 100.0f;

  if (id == GAUGE_MAP || id == GAUGE_BOOST || id == GAUGE_BOOST_ACT) {
    base_min = 100.0f;
    base_max = 250.0f;
  } else if (id == GAUGE_OIL_PRESS) {
    base_min = 0.0f;
    base_max = 1000.0f; // 0-10 bar in kPa
  } else if (id == GAUGE_FUEL_PRESS) {
    base_min = 0.0f;
    base_max = 800.0f; // 0-8 bar in kPa
  } else if (id == GAUGE_OIL_TEMP || id == GAUGE_TRANS_TEMP) {
    base_min = 60.0f;
    base_max = 140.0f;
  } else if (id == GAUGE_WATER_TEMP) {
    base_min = 60.0f;
    base_max = 120.0f;
  } else if (id == GAUGE_IAT) {
    base_min = 0.0f;
    base_max = 100.0f;
  } else if (id == GAUGE_AMBIENT_TEMP) {
    base_min = -20.0f;
    base_max = 60.0f;
  } else if (id == GAUGE_EGT) {
    base_min = 200.0f;
    base_max = 1000.0f;
  } else if (id == GAUGE_SPEED) {
    base_min = 0.0f;
    base_max = 260.0f;
  } else if (id == GAUGE_AFR) {
    base_min = 0.60f;
    base_max = 1.40f;
  } else if (id == GAUGE_TCU_REQ || id == GAUGE_TCU_ACT || id == GAUGE_ENG_REQ || id == GAUGE_ENG_ACT || id == GAUGE_LIMIT_TQ) {
    base_min = 0.0f;
    base_max = 500.0f;
  }

  // Handle Conversions
  if (id == GAUGE_MAP || id == GAUGE_BOOST || id == GAUGE_OIL_PRESS || id == GAUGE_FUEL_PRESS || id == GAUGE_BOOST_ACT) {
    if (unit == UNIT_BAR) {
      converted_value = value / 100.0f;
      converted_warn = warn_thr / 100.0f;
      converted_crit = crit_thr / 100.0f;
      unit_str = "Bar";
      fmt = "%.2f";
      
      arc_val = (int16_t)value;
      arc_min = (int16_t)base_min;
      arc_max = (int16_t)base_max;
      use_custom_arc_range = true;
    } else if (unit == UNIT_PSI) {
      converted_value = value * 0.1450377f;
      converted_warn = warn_thr * 0.1450377f;
      converted_crit = crit_thr * 0.1450377f;
      unit_str = "PSI";
      fmt = "%.1f";
      
      arc_val = (int16_t)(converted_value * 10.0f);
      arc_min = (int16_t)(base_min * 0.1450377f * 10.0f);
      arc_max = (int16_t)(base_max * 0.1450377f * 10.0f);
      use_custom_arc_range = true;
    } else { // UNIT_KPA
      converted_value = value;
      converted_warn = warn_thr;
      converted_crit = crit_thr;
      unit_str = "kPa";
      fmt = "%.0f";
      
      arc_val = (int16_t)value;
      arc_min = (int16_t)base_min;
      arc_max = (int16_t)base_max;
      use_custom_arc_range = true;
    }
  } else if (id == GAUGE_OIL_TEMP || id == GAUGE_WATER_TEMP || id == GAUGE_IAT || id == GAUGE_TRANS_TEMP || id == GAUGE_EGT || id == GAUGE_AMBIENT_TEMP) {
    if (unit == UNIT_FAHRENHEIT) {
      converted_value = value * 1.8f + 32.0f;
      converted_warn = warn_thr * 1.8f + 32.0f;
      converted_crit = crit_thr * 1.8f + 32.0f;
      unit_str = "°F";
      fmt = "%.0f";
      
      arc_val = (int16_t)converted_value;
      arc_min = (int16_t)(base_min * 1.8f + 32.0f);
      arc_max = (int16_t)(base_max * 1.8f + 32.0f);
      use_custom_arc_range = true;
    } else { // UNIT_CELSIUS
      converted_value = value;
      converted_warn = warn_thr;
      converted_crit = crit_thr;
      unit_str = "°C";
      fmt = "%.0f";
      
      arc_val = (int16_t)value;
      arc_min = (int16_t)base_min;
      arc_max = (int16_t)base_max;
      use_custom_arc_range = true;
    }
  } else if (id == GAUGE_SPEED) {
    if (unit == UNIT_MPH) {
      converted_value = value * 0.621371f;
      converted_warn = warn_thr * 0.621371f;
      converted_crit = crit_thr * 0.621371f;
      unit_str = "mph";
      fmt = "%.0f";
      
      arc_val = (int16_t)converted_value;
      arc_min = (int16_t)(base_min * 0.621371f);
      arc_max = (int16_t)(base_max * 0.621371f);
      use_custom_arc_range = true;
    } else { // UNIT_KMH
      converted_value = value;
      converted_warn = warn_thr;
      converted_crit = crit_thr;
      unit_str = "km/h";
      fmt = "%.0f";
      
      arc_val = (int16_t)value;
      arc_min = (int16_t)base_min;
      arc_max = (int16_t)base_max;
      use_custom_arc_range = true;
    }
  } else if (id == GAUGE_TCU_REQ || id == GAUGE_TCU_ACT || id == GAUGE_ENG_REQ || id == GAUGE_ENG_ACT || id == GAUGE_LIMIT_TQ) {
    if (unit == UNIT_PCT) {
      converted_value = (value / 500.0f) * 100.0f;
      converted_warn = (warn_thr / 500.0f) * 100.0f;
      converted_crit = (crit_thr / 500.0f) * 100.0f;
      unit_str = "%";
      fmt = "%.0f";
      
      arc_val = (int16_t)converted_value;
      arc_min = (int16_t)((base_min / 500.0f) * 100.0f);
      arc_max = (int16_t)((base_max / 500.0f) * 100.0f);
      use_custom_arc_range = true;
    } else { // UNIT_NM
      converted_value = value;
      converted_warn = warn_thr;
      converted_crit = crit_thr;
      unit_str = "Nm";
      fmt = "%.0f";
      
      arc_val = (int16_t)value;
      arc_min = (int16_t)base_min;
      arc_max = (int16_t)base_max;
      use_custom_arc_range = true;
    }
  } else if (id == GAUGE_AFR) {
    if (unit == UNIT_AFR) {
      converted_value = value * 14.7f;
      converted_warn = warn_thr * 14.7f;
      converted_crit = crit_thr * 14.7f;
      unit_str = "AFR";
      fmt = "%.1f";
      
      arc_val = (int16_t)(converted_value * 10.0f);
      arc_min = (int16_t)(base_min * 14.7f * 10.0f);
      arc_max = (int16_t)(base_max * 14.7f * 10.0f);
      use_custom_arc_range = true;
    } else if (unit == UNIT_VOLTS) {
      float volts = (value - 0.5f) * 5.0f;
      if (volts < 0.0f) volts = 0.0f;
      if (volts > 5.0f) volts = 5.0f;
      converted_value = volts;
      
      float warn_volts = (warn_thr - 0.5f) * 5.0f;
      if (warn_volts < 0.0f) warn_volts = 0.0f;
      if (warn_volts > 5.0f) warn_volts = 5.0f;
      converted_warn = warn_volts;
      
      float crit_volts = (crit_thr - 0.5f) * 5.0f;
      if (crit_volts < 0.0f) crit_volts = 0.0f;
      if (crit_volts > 5.0f) crit_volts = 5.0f;
      converted_crit = crit_volts;
      
      unit_str = "V";
      fmt = "%.2f";
      
      arc_val = (int16_t)(converted_value * 100.0f);
      arc_min = 0;
      arc_max = 500;
      use_custom_arc_range = true;
    } else { // UNIT_LAMBDA
      converted_value = value;
      converted_warn = warn_thr;
      converted_crit = crit_thr;
      unit_str = "λ";
      fmt = "%.2f";
      
      arc_val = (int16_t)(value * 100.0f);
      arc_min = (int16_t)(base_min * 100.0f);
      arc_max = (int16_t)(base_max * 100.0f);
      use_custom_arc_range = true;
    }
  }

  // Determine Color
  lv_color_t text_color = lv_color_white();
  lv_color_t arc_color = normal_color;

  // Check thresholds (using converted values)
  bool is_warn = invert_logic ? (converted_value <= converted_warn && converted_value > converted_crit)
                              : (converted_value >= converted_warn && converted_value < converted_crit);
  bool is_crit = invert_logic ? (converted_value <= converted_crit) : (converted_value >= converted_crit);

  if (is_crit) {
    text_color = lv_color_hex(0xFF0000); // Red
    arc_color = lv_color_hex(0xFF0000);
  } else if (is_warn) {
    text_color = lv_color_hex(0xFFD700); // Yellow
    arc_color = lv_color_hex(0xFFD700);
  }

  // Update Arc
  if (arc != NULL) {
    if (use_custom_arc_range) {
      lv_arc_set_range(arc, arc_min, arc_max);
      lv_arc_set_value(arc, arc_val);
    } else {
      lv_arc_set_value(arc, (int16_t)value);
    }
    lv_obj_set_style_arc_color(arc, arc_color, LV_PART_INDICATOR);
  }

  // Update Label
  if (label != NULL && fmt != NULL) {
    char buf[16];
    snprintf(buf, sizeof(buf), fmt, converted_value);
    lv_label_set_text(label, buf);
    lv_obj_set_style_text_color(label, text_color, 0);
  }

  // Update Unit Label dynamically if unit_str is set and arc parent exists
  if (unit_str != NULL && arc != NULL) {
    lv_obj_t *container = lv_obj_get_parent(arc);
    if (container != NULL) {
      lv_obj_t *unit_label = lv_obj_get_child(container, 3);
      if (unit_label != NULL) {
        lv_label_set_text(unit_label, unit_str);
      }
    }
  }
}

void update_all_gauges(void) {

  ecu_data_t data;

  if (global_demo_mode) {
    ecu_data_simulate(&data);
  } else {
    ecu_data_get_copy(&data);
  }

  // Override ambient temp if we are transmitting it
  system_settings_t *settings = system_settings_get();
  if (settings && settings->send_ambient_temp_to_can) {
    data.ambient_temp = settings->ambient_can_temp;
  }

  // --- Screen 1 ---
  // MAP: Cyan. Warn: 200, Crit: 230
  update_gauge(GAUGE_MAP, ui_Arc_MAP, ui_Label_MAP_Value, data.map_kpa, "%.0f", 200, 230,
               false, lv_color_hex(0x00D4FF));

  // RPM: Cyan. Warn: 7500, Crit: 9000
  update_gauge(GAUGE_RPM, ui_Arc_RPM, ui_Label_RPM_Value, data.engine_rpm, "%.0f", 7500,
               9000, false, lv_color_hex(0x00D4FF));

  // TPS: Cyan. Warn: 80, Crit: 90
  update_gauge(GAUGE_TPS, ui_Arc_TPS, ui_Label_TPS_Value, data.tps_position, "%.1f", 80,
               90, false, lv_color_hex(0x00D4FF));

  // Wastegate: Cyan. Warn: 110, Crit: 120
  update_gauge(GAUGE_WASTEGATE, ui_Arc_Wastegate, ui_Label_Wastegate_Value, data.wg_pos_percent,
               "%.1f", 110, 120, false, lv_color_hex(0x00D4FF));

  // Boost (Target): Cyan.
  if (ui_Arc_Boost || ui_Label_Boost_Value) {
    update_gauge(GAUGE_BOOST, ui_Arc_Boost, ui_Label_Boost_Value, data.target_boost, "%.0f", 200,
                 230, false, lv_color_hex(0x00FF88));
  }

  // --- Screen 2 ---
  // Oil Pressure: Orange (0xFF6B35). Warn: < 2.0 bar (200 kPa), Crit: < 1.0 bar (100 kPa)
  update_gauge(GAUGE_OIL_PRESS, ui_Arc_Oil_Pressure, ui_Label_Oil_Pressure_Value, data.oil_pressure, "%.1f", 200,
               100, true, lv_color_hex(0xFF6B35));

  // Oil Temp: Gold (0xFFD700). Warn: 110, Crit: 120
  update_gauge(GAUGE_OIL_TEMP, ui_Arc_Oil_Temp, ui_Label_Oil_Temp_Value, data.oil_temp, "%.0f",
               110, 120, false, lv_color_hex(0xFFD700));

  // Water Temp: Cyan (0x00D4FF). Warn: 105, Crit: 115
  update_gauge(GAUGE_WATER_TEMP, ui_Arc_Water_Temp, ui_Label_Water_Temp_Value, data.clt_temp,
               "%.0f", 105, 115, false, lv_color_hex(0x00D4FF));

  // Fuel Pressure: Green (0x00FF88). Warn: < 3.0 bar (300 kPa), Crit: < 2.0 bar (200 kPa)
  // Simulate from oil_pressure to show dynamic value
  update_gauge(GAUGE_FUEL_PRESS, ui_Arc_Fuel_Pressure, ui_Label_Fuel_Pressure_Value, data.oil_pressure * 0.8f, "%.1f",
               300, 200, true, lv_color_hex(0x00FF88));

  // Battery: Gold (0xFFD700). Warn: < 12.0, Crit: < 11.5
  update_gauge(GAUGE_BATTERY, ui_Arc_Battery_Voltage, ui_Label_Battery_Voltage_Value,
               data.battery_voltage, "%.1f", 12.0, 11.5, true,
               lv_color_hex(0xFFD700));

  // --- Screen 4 ---
  // Abs Pedal: Cyan
  update_gauge(GAUGE_PEDAL, ui_Arc_Abs_Pedal, ui_Label_Abs_Pedal_Value, data.abs_pedal_pos,
               "%.1f", 110, 120, false, lv_color_hex(0x00D4FF));

  // WG Pos: Green
  update_gauge(GAUGE_WG_POS, ui_Arc_WG_Pos, ui_Label_WG_Pos_Value, data.wg_pos_percent,
               "%.1f", 110, 120, false, lv_color_hex(0x00FF88));

  // BOV: Gold
  update_gauge(GAUGE_BOV, ui_Arc_BOV, ui_Label_BOV_Value, data.bov_percent, "%.1f", 110,
               120, false, lv_color_hex(0xFFD700));

  // TCU TQ: Orange
  update_gauge(GAUGE_TCU_REQ, ui_Arc_TCU_TQ_Req, ui_Label_TCU_TQ_Req_Value, data.tcu_tq_req_nm,
               "%.0f", 450, 500, false, lv_color_hex(0xFF6B35));
  update_gauge(GAUGE_TCU_ACT, ui_Arc_TCU_TQ_Act, ui_Label_TCU_TQ_Act_Value, data.tcu_tq_act_nm,
               "%.0f", 450, 500, false, lv_color_hex(0xFF3366));
  update_gauge(GAUGE_ENG_REQ, ui_Arc_Eng_TQ_Req, ui_Label_Eng_TQ_Req_Value, data.eng_trg_nm,
               "%.0f", 450, 500, false, lv_color_hex(0x8A2BE2));

  // --- Screen 5 ---
  // Eng Tq Act: Cyan
  update_gauge(GAUGE_ENG_ACT, ui_Arc_Eng_TQ_Act, ui_Label_Eng_TQ_Act_Value, data.eng_act_nm,
               "%.0f", 450, 500, false, lv_color_hex(0x00D4FF));
  // Limit Tq: Green
  update_gauge(GAUGE_LIMIT_TQ, ui_Arc_Limit_TQ, ui_Label_Limit_TQ_Value, data.limit_tq_nm,
               "%.0f", 450, 500, false, lv_color_hex(0x00FF88));

  // --- 7 New Powertrain Gauges (Screen 5/Dynamic) ---
  update_gauge(GAUGE_IAT, ui_Arc_IAT, ui_Label_IAT_Value, data.iat_temp, "%.0f",
               70, 90, false, lv_color_hex(0xFF8800));
  update_gauge(GAUGE_SPEED, ui_Arc_Speed, ui_Label_Speed_Value, data.vehicle_speed, "%.0f",
               200, 240, false, lv_color_hex(0x00FFD4));
  update_gauge(GAUGE_TRANS_TEMP, ui_Arc_Trans_Temp, ui_Label_Trans_Temp_Value, data.trans_temp, "%.0f",
               100, 115, false, lv_color_hex(0xFF5500));
  update_gauge(GAUGE_AFR, ui_Arc_AFR, ui_Label_AFR_Value, data.afr_val, "%.2f",
               1.15f, 1.25f, false, lv_color_hex(0x00FF88));
  update_gauge(GAUGE_EGT, ui_Arc_EGT, ui_Label_EGT_Value, data.egt_temp, "%.0f",
               850, 950, false, lv_color_hex(0xFF3300));
  update_gauge(GAUGE_KNOCK_RETARD, ui_Arc_Knock_Retard, ui_Label_Knock_Retard_Value, data.knock_retard, "%.1f",
               3.0f, 6.0f, false, lv_color_hex(0xFFCC00));
  update_gauge(GAUGE_BOOST_ACT, ui_Arc_Boost_Act, ui_Label_Boost_Act_Value, data.map_kpa, "%.0f",
               200, 230, false, lv_color_hex(0x00D4FF));
  // Ambient Temp: Purple
  update_gauge(GAUGE_AMBIENT_TEMP, ui_Arc_Ambient_Temp, ui_Label_Ambient_Temp_Value, data.ambient_temp, "%.0f",
               45.0f, 55.0f, false, lv_color_hex(0x8A2BE2));

  // MRE MAP: Cyan
  update_gauge(GAUGE_MRE_MAP, ui_Arc_MRE_MAP, ui_Label_MRE_MAP_Value, data.mre_map_kpa, "%.1f",
               200, 230, false, lv_color_hex(0x00D4FF));

  // MRE Wastegate: Green
  update_gauge(GAUGE_MRE_WASTEGATE, ui_Arc_MRE_Wastegate, ui_Label_MRE_Wastegate_Value, data.mre_wg_pos_percent, "%.1f",
               110, 120, false, lv_color_hex(0x00FF88));

  // Periodic send of ambient temp if enabled (every 1.0s = 10 calls of 100ms update loop)
  static int send_can_counter = 0;
  send_can_counter++;
  if (send_can_counter >= 10) {
    send_can_counter = 0;
    system_settings_t *settings = system_settings_get();
    if (settings && settings->send_ambient_temp_to_can) {
      extern void can_send_ambient_temp(float temp);
      can_send_ambient_temp(data.ambient_temp);
    }
  }

  // --- Gear Display (Screen 4) ---
  char gear_str[16] = "-";
  if (data.selector_position == 2) {
    strcpy(gear_str, "P");
  } else if (data.selector_position == 3) {
    strcpy(gear_str, "R");
  } else if (data.selector_position == 4) {
    strcpy(gear_str, "N");
  } else if (data.selector_position == 5) {
    if (data.gear >= 2 && data.gear <= 10) {
      snprintf(gear_str, sizeof(gear_str), "D%d", data.gear - 1);
    } else {
      strcpy(gear_str, "D");
    }
  } else if (data.selector_position == 6) {
    if (data.gear >= 2 && data.gear <= 10) {
      snprintf(gear_str, sizeof(gear_str), "S%d", data.gear - 1);
    } else {
      strcpy(gear_str, "S");
    }
  } else if (data.selector_position == 7) {
    if (data.gear >= 2 && data.gear <= 10) {
      snprintf(gear_str, sizeof(gear_str), "M%d", data.gear - 1);
    } else {
      strcpy(gear_str, "M");
    }
  } else {
    if (data.gear == 12) {
      strcpy(gear_str, "R");
    } else if (data.gear == 13) {
      strcpy(gear_str, "N");
    } else if (data.gear == 14) {
      strcpy(gear_str, "P");
    } else if (data.gear >= 2 && data.gear <= 10) {
      snprintf(gear_str, sizeof(gear_str), "%d", data.gear - 1);
    } else if (data.gear == 0) {
      strcpy(gear_str, "-");
    } else {
      snprintf(gear_str, sizeof(gear_str), "%d", data.gear);
    }
  }

  if (ui_Label_Gear) {
    char gear_buf[32];
    snprintf(gear_buf, sizeof(gear_buf), "Gear: %s", gear_str);
    lv_label_set_text(ui_Label_Gear, gear_buf);
  }

  // --- Screen 1 TCU Box ---
  if (ui_Label_Gear_S1) {
    char gear_buf[32];
    snprintf(gear_buf, sizeof(gear_buf), "Gear: %s", gear_str);
    lv_label_set_text(ui_Label_Gear_S1, gear_buf);
  }

  if (ui_Label_Selector_S1) {
    char sel_str[16] = "-";
    if (data.selector_position == 2) {
      strcpy(sel_str, "P");
    } else if (data.selector_position == 3) {
      strcpy(sel_str, "R");
    } else if (data.selector_position == 4) {
      strcpy(sel_str, "N");
    } else if (data.selector_position == 5) {
      strcpy(sel_str, "D");
    } else if (data.selector_position == 6) {
      strcpy(sel_str, "S");
    } else if (data.selector_position == 7) {
      strcpy(sel_str, "M");
    } else {
      snprintf(sel_str, sizeof(sel_str), "%d", data.selector_position);
    }

    char sel_buf[32];
    snprintf(sel_buf, sizeof(sel_buf), "Sel: %s", sel_str);
    lv_label_set_text(ui_Label_Selector_S1, sel_buf);
  }

  // --- Screen 8 (Classic Sports) ---
  // RPM (Left) - Red
  update_gauge(GAUGE_RPM, ui_Gauge_RPM_S8, ui_Label_RPM_Val_S8, data.engine_rpm, "%.0f",
               7500, 9000, false, lv_color_hex(0xFF0000));

  // Speed (Right) - White
  // Assuming data.vehicle_speed exists (or derive from RPM/Gear)
  // If vehicle_speed not in struct, use RPM * ratio for demo or default 0
  // float simul_speed = data.engine_rpm * 0.04; // Approximated
  update_gauge(GAUGE_SPEED, ui_Gauge_Speed_S8, ui_Label_Speed_Val_S8, data.vehicle_speed,
               "%.0f", 250, 280, false, lv_color_white());

  // Boost (Bar)
  if (ui_Bar_Boost_S8) {
    lv_bar_set_value(ui_Bar_Boost_S8, (int32_t)data.map_kpa, LV_ANIM_OFF);
    // Color Change based on threshold
    lv_color_t bar_col = lv_color_hex(0xFF0000); // Default Red
    if (data.map_kpa < 100)
      bar_col = lv_color_hex(0x555555);
    lv_obj_set_style_bg_color(ui_Bar_Boost_S8, bar_col, LV_PART_INDICATOR);
  }
  if (ui_Label_Boost_Val_S8) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f", data.map_kpa);
    lv_label_set_text(ui_Label_Boost_Val_S8, buf);
  }

  // Temperatures & Pressures (Center Panel)
  if (ui_Label_OilTemp_Val_S8) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f", data.oil_temp);
    lv_label_set_text(ui_Label_OilTemp_Val_S8, buf);
  }
  if (ui_Label_OilPress_Val_S8) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f",
             data.oil_pressure); // Assuming oil_pressure exists
    lv_label_set_text(ui_Label_OilPress_Val_S8, buf);
  }
  if (ui_Label_WaterTemp_Val_S8) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f", data.clt_temp);
    lv_label_set_text(ui_Label_WaterTemp_Val_S8, buf);
  }
  if (ui_Label_AirTemp_Val_S8) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f",
             data.iat_temp); // Assuming iat_temp exists
    lv_label_set_text(ui_Label_AirTemp_Val_S8, buf);
  }

  // Gear (Footer) - Simplified Text Update?
  // The Footer has 3 labels (P, N, D). This logic needs to highlight the active
  // one. However, recreating that logic here is complex since we don't have
  // pointers to the individual footer labels in 'ui_Screen8.h', only a single
  // 'ui_Label_Gear_S8' which we repurposed or didn't fully expose. In
  // ui_Screen8.c we made local variables for P/N/D. We should probably rely on
  // a separate update function in ui_Screen8.c for complex UI state, or just
  // stick to the simple logical label if exposed. Ideally, ui_Screen8_update()
  // should handle local widget logic. But for now, we'll leave it as is.
  if (ui_Label_Gear_S8) {
    lv_label_set_text(ui_Label_Gear_S8, gear_str);
  }

  // AI status is now handled via ui_Screen7_set_status() called from ai_manager

  // --- Screen 9 (Intercooler Controls) ---
  ui_Screen9_update();

  // --- Screen 10 (Boost & Blow-off Controls) ---
  ui_Screen10_update();

  // --- Screen 11 (VAG Diagnostic Scanner) ---
  ui_Screen11_update();

  // --- Screen 12 (VAG Launch Control Diagnostics) ---
  ui_Screen12_update();
}
