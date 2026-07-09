/*
 * ECU Data Management for ECU Dashboard
 * Handles ECU data storage, updates, and data stream logging
 */

#include "ecu_data.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


static const char *TAG = "ECU_DATA";

// Global ECU data
static ecu_data_t g_ecu_data = {0};
static SemaphoreHandle_t ecu_data_mutex = NULL;

// System settings
static system_settings_t g_system_settings = {
    .max_boost_limit = 250.0f,
    .max_rpm_limit = 7000.0f,
    .audio_alerts_enabled = true,
    .ecu_address = "192.168.4.1",
    .screen_brightness = 80, // Default brightness
    
    // Gauge Visibility Settings (Screen 1)
    .show_map = true,
    .show_wastegate = true,
    .show_tps = true,
    .show_rpm = true,
    .show_boost = true,
    .show_tcu = true,

    // Screen 2
    .show_oil_press = true,
    .show_oil_temp = true,
    .show_water_temp = true,
    .show_fuel_press = true,
    .show_battery = true,

    // Screen 4
    .show_pedal = true,
    .show_wg_pos = true,
    .show_bov = true,
    .show_tcu_req = true,
    .show_tcu_act = true,
    .show_eng_req = true,

    // Screen 5
    .show_eng_act = true,
    .show_limit_tq = true,
    .show_mre_map = true,
    .show_mre_wastegate = true,
    .mre_parallel = false
};

// Data stream (simple circular buffer)
#define DATA_STREAM_SIZE 50
static data_stream_entry_t data_stream[DATA_STREAM_SIZE] = {0};
static int data_stream_index = 0;
static bool data_stream_initialized = false;

// Initialize ECU data system
void ecu_data_init(void) {
  if (ecu_data_mutex == NULL) {
    ecu_data_mutex = xSemaphoreCreateMutex();
  }

  // Initialize ECU data with default values
  memset(&g_ecu_data, 0, sizeof(ecu_data_t));
  g_ecu_data.timestamp = esp_timer_get_time() / 1000; // milliseconds

  // Initialize data stream
  memset(data_stream, 0, sizeof(data_stream));
  data_stream_initialized = true;

  ESP_LOGI(TAG, "ECU data system initialized");
}

static float resolve_value(uint8_t source, float raw_val, float diag_val, uint32_t raw_ts, uint32_t diag_ts, float default_val) {
  uint32_t now = esp_timer_get_time() / 1000; // ms
  if (source == 1) { // Force Raw
    return raw_val;
  } else if (source == 2) { // Force Diag
    return diag_val;
  } else { // AUTO
    // If raw was updated in the last 3000ms, use raw. Otherwise fallback to diag (if diag was updated in the last 10000ms).
    if (raw_ts > 0 && (now - raw_ts) < 3000) {
      return raw_val;
    } else if (diag_ts > 0 && (now - diag_ts) < 10000) {
      return diag_val;
    } else {
      // Fallback to raw if available, otherwise diag
      return raw_ts >= diag_ts ? raw_val : diag_val;
    }
  }
}

static int8_t resolve_value_int8(uint8_t source, int8_t raw_val, int8_t diag_val, uint32_t raw_ts, uint32_t diag_ts, int8_t default_val) {
  uint32_t now = esp_timer_get_time() / 1000;
  if (source == 1) { // Force Raw
    return raw_val;
  } else if (source == 2) { // Force Diag
    return diag_val;
  } else { // AUTO
    if (raw_ts > 0 && (now - raw_ts) < 3000) {
      return raw_val;
    } else if (diag_ts > 0 && (now - diag_ts) < 10000) {
      return diag_val;
    } else {
      return raw_ts >= diag_ts ? raw_val : diag_val;
    }
  }
}

static void resolve_all_active_values_in_place(ecu_data_t *data) {
  data->engine_rpm = resolve_value(g_system_settings.gauge_sources[GAUGE_RPM],
                                    data->engine_rpm_raw, data->engine_rpm_diag,
                                    data->last_raw_update_ms[GAUGE_RPM], data->last_diag_update_ms[GAUGE_RPM],
                                    0.0f);
                                    
  data->tps_position = resolve_value(g_system_settings.gauge_sources[GAUGE_TPS],
                                      data->tps_position_raw, data->tps_position_diag,
                                      data->last_raw_update_ms[GAUGE_TPS], data->last_diag_update_ms[GAUGE_TPS],
                                      0.0f);

  data->abs_pedal_pos = resolve_value(g_system_settings.gauge_sources[GAUGE_PEDAL],
                                       data->abs_pedal_pos_raw, data->abs_pedal_pos_diag,
                                       data->last_raw_update_ms[GAUGE_PEDAL], data->last_diag_update_ms[GAUGE_PEDAL],
                                       0.0f);
                                       
  data->map_kpa = resolve_value(g_system_settings.gauge_sources[GAUGE_MAP],
                                 data->map_kpa_raw, data->map_kpa_diag,
                                 data->last_raw_update_ms[GAUGE_MAP], data->last_diag_update_ms[GAUGE_MAP],
                                 100.0f);
                                 
  data->clt_temp = resolve_value(g_system_settings.gauge_sources[GAUGE_WATER_TEMP],
                                  data->clt_temp_raw, data->clt_temp_diag,
                                  data->last_raw_update_ms[GAUGE_WATER_TEMP], data->last_diag_update_ms[GAUGE_WATER_TEMP],
                                  0.0f);
                                  
  data->iat_temp = resolve_value(g_system_settings.gauge_sources[GAUGE_IAT],
                                  data->iat_temp_raw, data->iat_temp_diag,
                                  data->last_raw_update_ms[GAUGE_IAT], data->last_diag_update_ms[GAUGE_IAT],
                                  0.0f);
                                  
  data->oil_temp = resolve_value(g_system_settings.gauge_sources[GAUGE_OIL_TEMP],
                                  data->oil_temp_raw, data->oil_temp_diag,
                                  data->last_raw_update_ms[GAUGE_OIL_TEMP], data->last_diag_update_ms[GAUGE_OIL_TEMP],
                                  0.0f);
                                  
  data->vehicle_speed = resolve_value(g_system_settings.gauge_sources[GAUGE_SPEED],
                                       data->vehicle_speed_raw, data->vehicle_speed_diag,
                                       data->last_raw_update_ms[GAUGE_SPEED], data->last_diag_update_ms[GAUGE_SPEED],
                                       0.0f);
                                       
  data->battery_voltage = resolve_value(g_system_settings.gauge_sources[GAUGE_BATTERY],
                                         data->battery_voltage_raw, data->battery_voltage_diag,
                                         data->last_raw_update_ms[GAUGE_BATTERY], data->last_diag_update_ms[GAUGE_BATTERY],
                                         0.0f);
                                         
  data->afr_val = resolve_value(g_system_settings.gauge_sources[GAUGE_AFR],
                                 data->afr_val_raw, data->afr_val_diag,
                                 data->last_raw_update_ms[GAUGE_AFR], data->last_diag_update_ms[GAUGE_AFR],
                                 1.0f);

  data->afr_target = resolve_value(g_system_settings.gauge_sources[GAUGE_AFR],
                                    data->afr_target_raw, data->afr_target_diag,
                                    data->last_raw_update_ms[GAUGE_AFR], data->last_diag_update_ms[GAUGE_AFR],
                                    1.0f);
                                    
  data->gear = resolve_value_int8(g_system_settings.gauge_sources[GAUGE_TCU],
                                   data->gear_raw, data->gear_diag,
                                   data->last_raw_update_ms[GAUGE_TCU], data->last_diag_update_ms[GAUGE_TCU],
                                   0);
                                   
  data->selector_position = resolve_value_int8(g_system_settings.gauge_sources[GAUGE_TCU],
                                                data->selector_position_raw, data->selector_position_diag,
                                                data->last_raw_update_ms[GAUGE_TCU], data->last_diag_update_ms[GAUGE_TCU],
                                                0);

  // Dynamically map gear_lever_val from resolved selector_position
  if (data->selector_position == 2) {
    data->gear_lever_val = 14; // Park
  } else if (data->selector_position == 3) {
    data->gear_lever_val = 15; // Reverse
  } else if (data->selector_position == 4) {
    data->gear_lever_val = 13; // Neutral
  } else if (data->selector_position == 5) {
    data->gear_lever_val = 11; // Drive
  } else if (data->selector_position == 6 || data->selector_position == 7) {
    data->gear_lever_val = 12; // Sport/Manual
  } else {
    data->gear_lever_val = 0;
  }

  data->ambient_temp = resolve_value(g_system_settings.gauge_sources[GAUGE_AMBIENT_TEMP],
                                      data->ambient_temp_raw, data->ambient_temp_diag,
                                      data->last_raw_update_ms[GAUGE_AMBIENT_TEMP], data->last_diag_update_ms[GAUGE_AMBIENT_TEMP],
                                      0.0f);
}

// Update ECU data (thread-safe)
void ecu_data_update(ecu_data_t *data) {
  if (!data || !ecu_data_mutex)
    return;

  if (xSemaphoreTake(ecu_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    memcpy(&g_ecu_data, data, sizeof(ecu_data_t));
    resolve_all_active_values_in_place(&g_ecu_data);
    g_ecu_data.timestamp = esp_timer_get_time() / 1000; // milliseconds

    xSemaphoreGive(ecu_data_mutex);
  }
}

// Update ECU data transaction (thread-safe and protects against read-modify-write overwrite race conditions)
void ecu_data_update_transaction(ecu_data_update_fn_t update_fn, void *ctx) {
  if (!update_fn || !ecu_data_mutex)
    return;

  if (xSemaphoreTake(ecu_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    update_fn(&g_ecu_data, ctx);
    resolve_all_active_values_in_place(&g_ecu_data);
    g_ecu_data.timestamp = esp_timer_get_time() / 1000; // milliseconds

    xSemaphoreGive(ecu_data_mutex);
  }
}

// Get current ECU data (thread-safe)
ecu_data_t *ecu_data_get(void) {
  return &g_ecu_data; // For now, return direct pointer (should be protected by
                      // mutex in caller)
}

// Get a thread-safe copy of the current ECU data
void ecu_data_get_copy(ecu_data_t *data_copy) {
  if (!data_copy || !ecu_data_mutex)
    return;

  if (xSemaphoreTake(ecu_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    memcpy(data_copy, &g_ecu_data, sizeof(ecu_data_t));
    xSemaphoreGive(ecu_data_mutex);
  }
}

// Convert ECU data to JSON string
char *ecu_data_to_json(const ecu_data_t *data) {
  // TODO: Update this function to serialize the new ecu_data_t struct if needed
  // for the web server.
  static char json_buffer[20] = "{}";
  return json_buffer;
}

// Parse ECU data from JSON string
bool ecu_data_from_json(const char *json_str, ecu_data_t *data) {
  // TODO: Update this function if needed.
  return false;
}

// Simulate ECU data for testing
void ecu_data_simulate(ecu_data_t *data) {
  if (!data)
    return;

  static float sim_time = 0;
  static int sim_ticks = 0;
  sim_time += 0.1f;
  sim_ticks++;

  float phase_time = fmod(sim_time, 15.0f);
  if (phase_time < 4.0f) {
    // Phase 1: Normal driving
    data->vehicle_speed = 40.0f + 20.0f * sin(sim_time);
    data->gear_lever_val = 11; // D
    data->brake_status = 0;
    data->pedal_position = 20.0f + 10.0f * sin(sim_time);
    data->tcu_torque_intervention = false;
    data->engine_rpm = 1500.0f + data->vehicle_speed * 30.0f;
    data->tps_position = data->pedal_position;
    data->abs_pedal_pos = data->pedal_position;
  } else if (phase_time < 7.0f) {
    // Phase 2: Stopping / preparing
    data->vehicle_speed = 0.0f;
    data->gear_lever_val = 12; // S
    data->brake_status = 3; // Pressed
    data->pedal_position = 0.0f;
    data->tcu_torque_intervention = false;
    data->engine_rpm = 800.0f; // Idle
    data->tps_position = 0.0f;
    data->abs_pedal_pos = 0.0f;
  } else if (phase_time < 11.0f) {
    // Phase 3: Launching (floor it on the brake!)
    data->vehicle_speed = 0.0f;
    data->gear_lever_val = 12; // S
    data->brake_status = 3; // Pressed
    data->pedal_position = 95.0f; // Floored
    data->tcu_torque_intervention = true;
    // Bouncing RPM at launch limit (3500 RPM)
    data->engine_rpm = 3450.0f + sin(sim_time * 50.0f) * 50.0f;
    data->tps_position = 90.0f;
    data->abs_pedal_pos = 95.0f;
  } else {
    // Phase 4: Launched! (Go!)
    data->vehicle_speed = (phase_time - 11.0f) * 25.0f; // Accelerating
    data->gear_lever_val = 12; // S
    data->brake_status = 0; // Released
    data->pedal_position = 90.0f;
    data->tcu_torque_intervention = false;
    data->engine_rpm = 3500.0f + (data->vehicle_speed * 40.0f);
    data->tps_position = 90.0f;
    data->abs_pedal_pos = 90.0f;
  }

  // Map selector_position from gear_lever_val for demo mode compatibility
  if (data->gear_lever_val == 11) data->selector_position = 5; // D
  else if (data->gear_lever_val == 12) data->selector_position = 6; // S
  else if (data->gear_lever_val == 13) data->selector_position = 4; // N
  else if (data->gear_lever_val == 14) data->selector_position = 2; // P
  else if (data->gear_lever_val == 15) data->selector_position = 3; // R
  else data->selector_position = 5; // Default to D

  // Evaluate Launch Control Active from the 5 conditions (same logic as CAN parser)
  data->launch_control_active = (data->gear_lever_val == 12) &&
                                (data->vehicle_speed < 1.0f) &&
                                (data->pedal_position > 80.0f) &&
                                (data->brake_status == 3) &&
                                (data->tcu_torque_intervention);
  data->tcu_launch_ready = data->launch_control_active;

  data->map_kpa = 100.0f + (data->engine_rpm / 1000.0f) * 30.0f + (data->launch_control_active ? 80.0f : 0.0f);
  data->target_boost = (data->selector_position == 5) ? 160.0f : 250.0f;
  data->oil_pressure = 20.0f + (data->engine_rpm / 100.0f) * 4.0f; // Varies with RPM
  data->oil_temp = 90.0f + 5.0f * sin(sim_time * 0.1f);
  data->clt_temp = 85.0f + 3.0f * sin(sim_time * 0.15f);
  data->iat_temp = 30.0f + 10.0f * sin(sim_time * 0.2f);

  // Simulate new powertrain parameters
  data->trans_temp = 80.0f + 5.0f * sin(sim_time * 0.1f);
  data->afr_target = 1.0f - 0.12f * (data->tps_position / 100.0f);
  data->afr_val = data->afr_target + 0.02f * sin(sim_time * 2.0f);
  data->egt_temp = 350.0f + 250.0f * (data->engine_rpm / 3800.0f) + 50.0f * sin(sim_time * 0.5f);
  data->knock_retard = (data->engine_rpm > 3000.0f) ? (float)(fmod(sim_time, 3.0f) > 2.0f ? 2.5f : 0.0f) : 0.0f;
  data->dsg_shift_active = (sim_ticks % 150 >= 120 && sim_ticks % 150 < 135);
  // Blip active during part of downshift
  data->dsg_blip_active = (data->dsg_shift_active && (sim_ticks % 150 >= 125) && ((sim_ticks / 150) % 2 == 1));
  
  // ESP / ASR active event occurs every 25 seconds (250 ticks) and lasts 2.0 seconds (20 ticks)
  data->esp_active = (sim_ticks % 250 >= 210 && sim_ticks % 250 < 230);
  data->asr_active = data->esp_active;
  
  // Set torque values accordingly (Nm)
  float max_eng_torque = 350.0f;
  data->eng_trg_nm = (data->tps_position / 100.0f) * max_eng_torque;
  
  if (data->dsg_shift_active) {
    // During DSG shift: DSG cuts torque target, ECU pulls ignition and closes throttle
    data->tcu_tq_req_nm = 50.0f;
    data->eng_act_nm = 40.0f;
    data->limit_tq_nm = 50.0f;
    data->tps_position = 10.0f; 
  } else if (data->esp_active) {
    // During ESP intervention: ESP requests low torque, actual torque drops
    data->tcu_tq_req_nm = max_eng_torque;
    data->eng_act_nm = 80.0f;
    data->limit_tq_nm = 80.0f;
    data->tps_position = 15.0f;
  } else {
    // Normal operation
    data->tcu_tq_req_nm = max_eng_torque;
    data->eng_act_nm = data->eng_trg_nm;
    data->limit_tq_nm = max_eng_torque;
  }

  data->timestamp = esp_timer_get_time() / 1000;
}

// ============================================================================
// SYSTEM SETTINGS FUNCTIONS
// ============================================================================

void system_settings_init(void) {
  // Initialize with defaults (already done at declaration)
  ESP_LOGI(TAG, "System settings initialized");
}

system_settings_t *system_settings_get(void) { return &g_system_settings; }

void system_settings_save(const system_settings_t *settings) {
  if (settings) {
    memcpy(&g_system_settings, settings, sizeof(system_settings_t));
    ESP_LOGI(TAG, "System settings saved");
  }
}

// ============================================================================
// DATA STREAM FUNCTIONS
// ============================================================================

void data_stream_add_entry(const char *message, log_type_t type) {
  if (!message || !data_stream_initialized)
    return;

  // Add new entry
  data_stream[data_stream_index].timestamp = esp_timer_get_time() / 1000;
  data_stream[data_stream_index].type = type;
  snprintf(data_stream[data_stream_index].message,
           sizeof(data_stream[data_stream_index].message), "%s", message);

  // Move to next index (circular buffer)
  data_stream_index = (data_stream_index + 1) % DATA_STREAM_SIZE;
}

void data_stream_clear(void) {
  memset(data_stream, 0, sizeof(data_stream));
  data_stream_index = 0;
}

char *data_stream_to_json(void) {
  static char json_buffer[4096];
  char *ptr = json_buffer;

  ptr += sprintf(ptr, "[");

  for (int i = 0; i < DATA_STREAM_SIZE; i++) {
    int index =
        (data_stream_index - 1 - i + DATA_STREAM_SIZE) % DATA_STREAM_SIZE;

    if (data_stream[index].timestamp == 0)
      continue; // Skip empty entries

    if (ptr > json_buffer + 1) {
      ptr += sprintf(ptr, ",");
    }

    const char *type_str;
    switch (data_stream[index].type) {
    case LOG_INFO:
      type_str = "info";
      break;
    case LOG_WARNING:
      type_str = "warning";
      break;
    case LOG_SUCCESS:
      type_str = "success";
      break;
    case LOG_ERROR:
      type_str = "error";
      break;
    default:
      type_str = "info";
      break;
    }

    ptr += sprintf(
        ptr, "{\"timestamp\":%llu,\"message\":\"%s\",\"type\":\"%s\"}",
        data_stream[index].timestamp, data_stream[index].message, type_str);
  }

  ptr += sprintf(ptr, "]");
  return json_buffer;
}

// ============================================================================
// SIMPLE DATA FUNCTIONS FOR WIFI SERVER
// ============================================================================

char *ecu_data_to_string(const ecu_data_t *data) {
  // TODO: Update this function if needed for the web server.
  static char buffer[32] = "No data";
  return buffer;
}

char *data_stream_to_string(void) {
  static char buffer[1024];
  char *ptr = buffer;

  for (int i = 0; i < DATA_STREAM_SIZE && ptr < buffer + sizeof(buffer) - 100;
       i++) {
    int index =
        (data_stream_index - 1 - i + DATA_STREAM_SIZE) % DATA_STREAM_SIZE;

    if (data_stream[index].timestamp == 0)
      continue;

    ptr += sprintf(ptr, "[%llu] %s\n", data_stream[index].timestamp,
                   data_stream[index].message);
  }

  return buffer;
}
