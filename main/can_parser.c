#include "can_parser.h"
#include "ecu_data.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>
#include <string.h>


static const char *TAG = "CAN_PARSER";

// --- Global State ---
CanPlatform g_current_platform = PLATFORM_VW_PQ35_46; // Default
const CanPlatformConfig *g_current_platform_config = NULL;
static float g_max_torque_nm = 500.0f;
static uint8_t s_brake_motor = 0;
static uint8_t s_brake_abs = 0;

// --- Helper Functions ---
static inline uint16_t get_u16_le(const uint8_t *data, int offset) {
  return (uint16_t)(data[offset + 1] << 8) | data[offset];
}

static inline uint16_t get_u16_be(const uint8_t *data, int offset) {
  return (uint16_t)(data[offset] << 8) | data[offset + 1];
}

void can_parser_set_max_torque(float max_torque) {
  if (max_torque > 0) {
    g_max_torque_nm = max_torque;
  }
}

// --- Platform Parsers ---

// 1. VW PQ35/PQ46 (Passat B6, Golf 5/6, etc.) - The original implementation
static void parse_vw_pq35_46(const twai_message_t *message,
                             ecu_data_t *ecu_data) {
  uint32_t now = esp_timer_get_time() / 1000;
  switch (message->identifier) {
  case 0x280: // Motor_1: RPM (0.25 scaling)
    ecu_data->engine_rpm_raw =
        ((uint16_t)message->data[3] << 8 | message->data[2]) * 0.25f;
    ecu_data->last_raw_update_ms[GAUGE_RPM] = now;
    ecu_data->eng_act_nm = (message->data[1] * 0.39f) * (g_max_torque_nm / 100.0f);  // Inneres_Motormoment in Nm
    ecu_data->tps_position_raw = message->data[5] * 0.4f; // Throttle
    ecu_data->last_raw_update_ms[GAUGE_TPS] = now;
    ecu_data->eng_trg_nm = (message->data[7] * 0.39f) * (g_max_torque_nm / 100.0f);  // Requested Torque in Nm
    break;

  case 0x288: // Motor_2: Coolant & Timing Retard (Knock)
    ecu_data->clt_temp_raw = (message->data[1] * 0.75f) - 48.0f;
    ecu_data->last_raw_update_ms[GAUGE_WATER_TEMP] = now;
    ecu_data->limit_tq_nm = (message->data[6] * 0.39f) * (g_max_torque_nm / 100.0f);
    ecu_data->knock_retard = message->data[7] * 0.375f; // ZW Ruecknahme in degrees
    s_brake_motor = (message->data[2] & 0x01) ? 3 : 0; // MO2_BLS (Brake Light Switch)
    ecu_data->brake_status = (s_brake_motor == 3 || s_brake_abs == 3) ? 3 : 0;
    break;

  case 0x380: // Motor_3: IAT
    ecu_data->iat_temp_raw = (message->data[1] * 0.75f) - 48.0f;
    ecu_data->last_raw_update_ms[GAUGE_IAT] = now;
    ecu_data->abs_pedal_pos_raw = message->data[2] * 0.4f;
    ecu_data->last_raw_update_ms[GAUGE_PEDAL] = now;
    ecu_data->pedal_position = message->data[2] * 0.4f;
    break;

  case 0x588: // Motor_7: Oil Temp & Boost
    ecu_data->map_kpa_raw = (message->data[4] * 0.01f) * 100.0f; // Bar -> kPa
    ecu_data->last_raw_update_ms[GAUGE_MAP] = now;
    ecu_data->last_raw_update_ms[GAUGE_BOOST_ACT] = now;
    ecu_data->oil_temp_raw = (message->data[7] * 1.0f) - 60.0f;
    ecu_data->last_raw_update_ms[GAUGE_OIL_TEMP] = now;
    break;

  case 0x372: // Battery
    ecu_data->battery_voltage_raw = (message->data[5] * 0.05f) + 5.0f;
    ecu_data->last_raw_update_ms[GAUGE_BATTERY] = now;
    break;

  case 0x540: // Gear (Getriebe_2)
    {
      uint8_t mux = message->data[7] & 0x0F;        // Low nibble: Multiplexer
      uint8_t val = (message->data[7] >> 4) & 0x0F; // High nibble: Value

      if (mux == 6) { // Selector position info (GE2_PRNDS)
        ecu_data->selector_position_raw = val;
        ecu_data->last_raw_update_ms[GAUGE_TCU] = now;
        if (val == 6 || val == 7) { // Sport or Manual/Tiptronic
          ecu_data->gear_lever_val = 12; // Gbx_stGearLvr = 12 (Sport/Tiptronic)
        } else if (val == 5) { // Drive
          ecu_data->gear_lever_val = 11; // Gbx_stGearLvr = 11 (Drive)
        } else if (val == 4) { // Neutral
          ecu_data->gear_lever_val = 13; // Gbx_stGearLvr = 13 (Neutral)
        } else if (val == 2) { // Park
          ecu_data->gear_lever_val = 14; // Gbx_stGearLvr = 14 (Park)
        } else if (val == 3) { // Reverse
          ecu_data->gear_lever_val = 15; // Gbx_stGearLvr = 15 (Reverse)
        } else {
          ecu_data->gear_lever_val = val;
        }
      } else if (mux == 15) { // Actual gear info (GE2_akt_Gang)
        // 2 = 1st gear, 3 = 2nd gear, etc.
        ecu_data->gear_raw = val; 
        ecu_data->last_raw_update_ms[GAUGE_TCU] = now;
      }
    }
    break;

  case 0x440: // Getriebe_1: TCU Torque Request
    ecu_data->tcu_tq_req_nm = (message->data[3] * 0.39f) * (g_max_torque_nm / 100.0f);
    // 0xFE (254) and 0xFF (255) represent "no request/no intervention" in VAG PQ35 CAN
    ecu_data->tcu_torque_intervention = (message->data[3] > 0) && (message->data[3] < 0xFE);
    // GE1_LaunchControl is in byte 6 (0-indexed), bit 0
    ecu_data->tcu_launch_ready = (message->data[6] & 0x01);
    break;

  case 0x488: // Motor_6: TCU Torque Actual & EGT
    ecu_data->tcu_tq_act_nm = (message->data[2] * 0.39f) * (g_max_torque_nm / 100.0f); // Istmoment_Getriebe in Nm
    ecu_data->egt_temp = ((message->data[4] << 8) | message->data[5]) * 0.1f - 100.0f; // EGT in C
    break;

  case 0x2C6: // DSG Oil Temp
    ecu_data->trans_temp = message->data[0] - 40.0f;
    break;

  case 0x480: // Motor_5: Lambda Target / Actual
    ecu_data->afr_val_raw = message->data[0] * 0.0048f; // Actual Lambda
    ecu_data->afr_target_raw = message->data[4] * 0.0048f; // Target Lambda
    ecu_data->last_raw_update_ms[GAUGE_AFR] = now;
    break;

  case 0x1A0: // Speed source (Bremse_1) - ABS Wheel Speed
  {
    uint16_t raw_speed = ((uint16_t)message->data[3] << 8 | message->data[2]);
    raw_speed = (raw_speed >> 1) & 0x7FFF;
    ecu_data->vehicle_speed_raw = raw_speed * 0.01f;
    ecu_data->last_raw_update_ms[GAUGE_SPEED] = now;
    s_brake_abs = (message->data[1] & 0x08) ? 3 : 0; // BR1_Lichtschalt (Brake Light Switch)
    ecu_data->brake_status = (s_brake_motor == 3 || s_brake_abs == 3) ? 3 : 0;
  } break;

    // Custom / Other
  case 0x390: // Wastegate (Custom)
    ecu_data->wg_set_percent = message->data[1] / 2.0f;
    ecu_data->wg_pos_percent = message->data[2] / 2.0f;
    break;

  case 0x394: // BOV (Custom)
    ecu_data->bov_percent = (message->data[0] * 50.0f) / 255.0f;
    break;

  case 0x420: // Kombi_2: Outside Temperature
  {
    system_settings_t *settings = system_settings_get();
    if (settings && !settings->send_ambient_temp_to_can) {
      if (message->data[2] != 0xFF) {
        ecu_data->ambient_temp_raw = message->data[2] * 0.5f - 50.0f;
        ecu_data->last_raw_update_ms[GAUGE_AMBIENT_TEMP] = now;
      }
    }
  } break;

  default:
    break;
  }

  // Calculate Launch Control Active status locally
  ecu_data->launch_control_active = (ecu_data->gear_lever_val == 12) && 
                                    (ecu_data->vehicle_speed_raw < 1.0f) && 
                                    (ecu_data->pedal_position > 80.0f) && 
                                    (ecu_data->brake_status == 3) && 
                                    (ecu_data->tcu_torque_intervention);
}

// 2. VW PQ25 (Polo 6R, Fabia 2)
static void parse_vw_pq25(const twai_message_t *message, ecu_data_t *ecu_data) {
  uint32_t now = esp_timer_get_time() / 1000;
  switch (message->identifier) {
  case 0x280: // Same as PQ35
    ecu_data->engine_rpm_raw =
        ((uint16_t)message->data[3] << 8 | message->data[2]) * 0.25f;
    ecu_data->last_raw_update_ms[GAUGE_RPM] = now;
    break;

  case 0x5A0: // Speed_1 (Dash Speed)
  {
    uint16_t raw_speed = get_u16_le(message->data, 1);
    ecu_data->vehicle_speed_raw = raw_speed * 0.01f;
    ecu_data->last_raw_update_ms[GAUGE_SPEED] = now;
  } break;

  case 0x1A0: // ABS Speed (Fallback)
  {
    uint16_t raw_speed = ((uint16_t)message->data[3] << 8 | message->data[2]);
    raw_speed = (raw_speed >> 1) & 0x7FFF;
    ecu_data->vehicle_speed_raw = raw_speed * 0.01f;
    ecu_data->last_raw_update_ms[GAUGE_SPEED] = now;
  } break;

  case 0x288:
    ecu_data->clt_temp_raw = (message->data[1] * 0.75f) - 48.0f;
    ecu_data->last_raw_update_ms[GAUGE_WATER_TEMP] = now;
    break;

  default:
    break;
  }
}

// 3. BMW E-Series (E90/E60)
static void parse_bmw_e_series(const twai_message_t *message,
                               ecu_data_t *ecu_data) {
  uint32_t now = esp_timer_get_time() / 1000;
  switch (message->identifier) {
  case 0x0AA: // RPM (DME1)
  {
    uint16_t raw = get_u16_le(message->data, 4);
    ecu_data->engine_rpm_raw = raw / 4.0f;
    ecu_data->last_raw_update_ms[GAUGE_RPM] = now;
  } break;

  case 0x1D0: // Engine Temp
    ecu_data->clt_temp_raw = message->data[0] - 48.0f;
    ecu_data->last_raw_update_ms[GAUGE_WATER_TEMP] = now;
    break;

  case 0x1A6: // Speed (Cluster Speed)
  {
    uint16_t raw = get_u16_le(message->data, 0);
    ecu_data->vehicle_speed_raw = raw / 2.0f;
    ecu_data->last_raw_update_ms[GAUGE_SPEED] = now;
  } break;

  case 0x1D2: // Gear
    ecu_data->gear_raw = message->data[0];
    ecu_data->last_raw_update_ms[GAUGE_TCU] = now;
    break;

  default:
    break;
  }
}

// 4. BMW F-Series (F10/F30)
static void parse_bmw_f_series(const twai_message_t *message,
                               ecu_data_t *ecu_data) {
  switch (message->identifier) {
  // Placeholder - BN2020 needs CRC validation for production
  default:
    break;
  }
}

// 5. VW MQB (Golf 7, Octavia A7)
static void parse_vw_mqb(const twai_message_t *message, ecu_data_t *ecu_data) {
  uint32_t now = esp_timer_get_time() / 1000;
  switch (message->identifier) {
  case 0x280: // RPM
    ecu_data->engine_rpm_raw =
        ((uint16_t)message->data[3] << 8 | message->data[2]) * 0.25f;
    ecu_data->last_raw_update_ms[GAUGE_RPM] = now;
    break;

  case 0x0FD: // ESP_21 : Speed
  {
    uint16_t raw_speed = get_u16_le(message->data, 1);
    ecu_data->vehicle_speed_raw = raw_speed * 0.01f;
    ecu_data->last_raw_update_ms[GAUGE_SPEED] = now;
  } break;

  case 0x288: // Coolant
    ecu_data->clt_temp_raw = (message->data[1] * 0.75f) - 48.0f;
    ecu_data->last_raw_update_ms[GAUGE_WATER_TEMP] = now;
    break;

  default:
    break;
  }
}

// 6. rusEFI microRusEFI (MRE)
static void parse_rusefi_mre(const twai_message_t *message, ecu_data_t *ecu_data) {
  uint32_t now = esp_timer_get_time() / 1000;
  switch (message->identifier) {
  case 0x200: // BASE0: Status, Gear, Warnings
    ecu_data->gear_raw = (int8_t)message->data[5]; // CurrentGear
    ecu_data->last_raw_update_ms[GAUGE_TCU] = now;
    break;

  case 0x201: // BASE1: RPM, Speed
    ecu_data->engine_rpm_raw = (float)(message->data[0] | (message->data[1] << 8)); // scale=1
    ecu_data->last_raw_update_ms[GAUGE_RPM] = now;
    ecu_data->vehicle_speed_raw = (float)message->data[6]; // scale=1
    ecu_data->last_raw_update_ms[GAUGE_SPEED] = now;
    break;

  case 0x202: // BASE2: PPS, TPS1, TPS2, Wastegate
    ecu_data->abs_pedal_pos_raw = (float)((int16_t)(message->data[0] | (message->data[1] << 8))) * 0.01f;
    ecu_data->last_raw_update_ms[GAUGE_PEDAL] = now;
    ecu_data->tps_position_raw  = (float)((int16_t)(message->data[2] | (message->data[3] << 8))) * 0.01f;
    ecu_data->last_raw_update_ms[GAUGE_TPS] = now;
    ecu_data->mre_wg_pos_percent = (float)((int16_t)(message->data[6] | (message->data[7] << 8))) * 0.01f;
    break;

  case 0x203: // BASE3: MAP, CLT, IAT, OilTemp
    ecu_data->mre_map_kpa = (float)(message->data[0] | (message->data[1] << 8)) * 0.03333f; // scale=0.03333
    if (g_current_platform == PLATFORM_RUSEFI_MRE) {
      ecu_data->map_kpa_raw = ecu_data->mre_map_kpa;
      ecu_data->last_raw_update_ms[GAUGE_MAP] = now;
      ecu_data->last_raw_update_ms[GAUGE_BOOST_ACT] = now;
    }
    ecu_data->clt_temp_raw = (float)message->data[2] - 40.0f; // offset=-40
    ecu_data->last_raw_update_ms[GAUGE_WATER_TEMP] = now;
    ecu_data->iat_temp_raw = (float)message->data[3] - 40.0f; // offset=-40
    ecu_data->last_raw_update_ms[GAUGE_IAT] = now;
    ecu_data->oil_temp_raw = (float)message->data[4] - 40.0f; // AuxTemp1 offset=-40 (mapped to oil_temp)
    ecu_data->last_raw_update_ms[GAUGE_OIL_TEMP] = now;
    break;

  default:
    break;
  }
}

// --- Main Dispatcher ---

void can_parser_set_platform(CanPlatform platform) {
  if (platform < PLATFORM_MAX) {
    g_current_platform = platform;
    ESP_LOGI(TAG, "Switched to CAN Platform: %d", platform);
  }
}

CanPlatform can_parser_get_platform(void) { return g_current_platform; }

typedef struct {
  const twai_message_t *msg;
  void (*parse_fn)(const twai_message_t *, ecu_data_t *);
} parse_tx_ctx_t;

static void parse_transaction_wrapper(ecu_data_t *state, void *ctx) {
  parse_tx_ctx_t *tx = (parse_tx_ctx_t *)ctx;
  tx->parse_fn(tx->msg, state);
}

void parse_can_message(const twai_message_t *message) {
  if (!message)
    return;

  void (*parse_fn)(const twai_message_t *, ecu_data_t *) = NULL;

  switch (g_current_platform) {
  case PLATFORM_VW_PQ35_46:
    parse_fn = parse_vw_pq35_46;
    break;
  case PLATFORM_VW_PQ25:
    parse_fn = parse_vw_pq25;
    break;
  case PLATFORM_BMW_E9X:
  case PLATFORM_BMW_E46:
    parse_fn = parse_bmw_e_series;
    break;
  case PLATFORM_BMW_F_SERIES:
    parse_fn = parse_bmw_f_series;
    break;
  case PLATFORM_VW_MQB:
    parse_fn = parse_vw_mqb;
    break;
  case PLATFORM_RUSEFI_MRE:
    parse_fn = parse_rusefi_mre;
    break;
  default:
    parse_fn = parse_vw_pq35_46;
    break;
  }

  if (parse_fn) {
    parse_tx_ctx_t ctx = { .msg = message, .parse_fn = parse_fn };
    ecu_data_update_transaction(parse_transaction_wrapper, &ctx);
  }

  // Parse rusEFI MRE messages in parallel if they arrive, unless MRE is the main platform
  system_settings_t *settings = system_settings_get();
  if (settings && settings->mre_parallel && g_current_platform != PLATFORM_RUSEFI_MRE &&
      message->identifier >= 0x200 && message->identifier <= 0x203) {
    parse_tx_ctx_t ctx_mre = { .msg = message, .parse_fn = parse_rusefi_mre };
    ecu_data_update_transaction(parse_transaction_wrapper, &ctx_mre);
  }
}
