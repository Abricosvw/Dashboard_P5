#include "diagnostic_manager.h"
#include "ecu_data.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ui/settings_config.h"
#include "can_manager.h"
#include <string.h>

static const char *TAG = "DIAG_MGR";

static twai_handle_t s_diag_bus_handle = NULL;

// --- Helper UDS IDs Mapping ---
static void get_uds_ids(uint8_t address, uint32_t *req_id, uint32_t *resp_id) {
  if (address == 0x01) {
    *req_id = 0x7E0;
    *resp_id = 0x7E8;
  } else if (address == 0x02) {
    *req_id = 0x7E1;
    *resp_id = 0x7E9;
  } else if (address == 0x17) { // Instruments
    *req_id = 0x714;
    *resp_id = 0x77E;
  } else if (address == 0x19) { // Gateway
    *req_id = 0x710;
    *resp_id = 0x77A;
  } else if (address == 0x3D) { // Special Function
    *req_id = 0x72B;
    *resp_id = 0x795;
  } else if (address == 0x13) { // ACC / Auto Distance Regulation
    *req_id = 0x757;
    *resp_id = 0x7C1;
  } else {
    // Standard body/chassis mapping fallback
    *req_id = 0x700 + address;
    *resp_id = *req_id + 0x6A;
  }
}

// --- ISO-TP State & Buffers ---
#define ISOTP_BUF_SIZE 256
static uint8_t isotp_rx_buf[ISOTP_BUF_SIZE];
static uint16_t isotp_rx_len = 0;
static uint16_t isotp_rx_idx = 0;
static uint8_t isotp_rx_seq = 0;
static bool isotp_rx_in_progress = false;

static uint8_t uds_tx_buf[ISOTP_BUF_SIZE];
static uint16_t uds_tx_len = 0;
static uint16_t uds_tx_idx = 0;
static uint8_t uds_tx_seq = 1;
static bool uds_tx_in_progress = false;

// --- TP 2.0 State ---
static bool tp20_connected = false;
static uint32_t tp20_tx_id = 0;          // Tester RX (Module TX)
static uint32_t tp20_rx_id = 0;          // Tester TX (Module RX)
static uint8_t tp20_rx_seq = 0;
static uint8_t tp20_tx_seq = 0;
static uint64_t tp20_last_activity = 0;   // millisecond timestamp

static void fill_kwp_value(uint8_t type, float val, uint8_t *out_type, uint8_t *out_a, uint8_t *out_b) {
  *out_type = type;
  switch (type) {
    case 1: // RPM
      *out_a = 200;
      *out_b = (uint8_t)(val / 40.0f);
      break;
    case 5: // Temperature
      *out_a = 10;
      *out_b = (uint8_t)(val + 50.0f);
      break;
    case 7: // Speed
      *out_a = 100;
      *out_b = (uint8_t)val;
      break;
    case 10: // Percent
      *out_a = 10;
      *out_b = (uint8_t)val;
      break;
    case 12: // Voltage
      *out_a = 100;
      *out_b = (uint8_t)(val * 10.0f);
      break;
    case 25: // Pressure (mbar)
      *out_a = 250;
      *out_b = (uint8_t)val; // val is MAP in kPa (scaled internally to mbar / 10)
      break;
    default:
      *out_a = 0;
      *out_b = 0;
      break;
  }
}

void diagnostic_manager_init(void) {
  s_diag_bus_handle = g_can2_handle;
  tp20_connected = false;
  tp20_tx_id = 0;
  tp20_rx_id = 0;
  tp20_rx_seq = 0;
  tp20_tx_seq = 0;
  tp20_last_activity = 0;

  isotp_rx_in_progress = false;
  uds_tx_in_progress = false;

  ESP_LOGI(TAG, "VAG Diagnostic Manager Initialized");
}

// --- ISO-TP Sender ---
static void isotp_send_flow_control(uint32_t tx_id) {
  twai_message_t reply;
  reply.identifier = tx_id;
  reply.extd = 0;
  reply.rtr = 0;
  reply.data_length_code = 8;
  memset(reply.data, 0, 8);
  reply.data[0] = 0x30; // Flow Control Continue-to-Send
  reply.data[1] = 0x00; // Block Size = 0 (send all)
  reply.data[2] = 0x00; // STmin = 0 (no delay)
  
  twai_transmit_v2(s_diag_bus_handle, &reply, pdMS_TO_TICKS(10));
}

static void uds_process_payload(const uint8_t *req, uint16_t req_len, uint32_t tx_id);

static void isotp_tx_continue(uint32_t tx_id) {
  if (!uds_tx_in_progress) return;

  twai_message_t msg;
  msg.identifier = tx_id;
  msg.extd = 0;
  msg.rtr = 0;

  while (uds_tx_idx < uds_tx_len) {
    msg.data_length_code = 8;
    memset(msg.data, 0, 8);

    msg.data[0] = 0x20 | uds_tx_seq;
    uint16_t chunk = uds_tx_len - uds_tx_idx;
    if (chunk > 7) chunk = 7;

    memcpy(&msg.data[1], &uds_tx_buf[uds_tx_idx], chunk);
    uds_tx_idx += chunk;
    uds_tx_seq = (uds_tx_seq + 1) & 0x0F;

    twai_transmit_v2(s_diag_bus_handle, &msg, pdMS_TO_TICKS(10));
    vTaskDelay(pdMS_TO_TICKS(2)); // Stagger frames slightly
  }

  uds_tx_in_progress = false;
  ESP_LOGD(TAG, "UDS multi-frame response transmission finished");
}

static void uds_send_response(const uint8_t *data, uint16_t len, uint32_t tx_id) {
  twai_message_t reply;
  reply.identifier = tx_id;
  reply.extd = 0;
  reply.rtr = 0;

  if (len <= 7) {
    // Single Frame (SF)
    reply.data_length_code = 8;
    memset(reply.data, 0, 8);
    reply.data[0] = len;
    memcpy(&reply.data[1], data, len);
    twai_transmit_v2(s_diag_bus_handle, &reply, pdMS_TO_TICKS(10));
    ESP_LOGD(TAG, "Sent UDS single-frame response of %d bytes", len);
  } else {
    // Multi-Frame: First Frame (FF)
    uds_tx_len = len;
    memcpy(uds_tx_buf, data, len);
    uds_tx_idx = 6;
    uds_tx_seq = 1;
    uds_tx_in_progress = true;

    reply.data_length_code = 8;
    memset(reply.data, 0, 8);
    reply.data[0] = 0x10 | ((len >> 8) & 0x0F);
    reply.data[1] = len & 0xFF;
    memcpy(&reply.data[2], data, 6);

    twai_transmit_v2(s_diag_bus_handle, &reply, pdMS_TO_TICKS(10));
    ESP_LOGD(TAG, "Sent UDS First Frame (total len=%d). Waiting for Flow Control...", len);
  }
}

// --- TP 2.0 Sender ---
static void tp20_send_ack(uint8_t seq) {
  twai_message_t reply;
  reply.identifier = tp20_tx_id;
  reply.extd = 0;
  reply.rtr = 0;
  reply.data_length_code = 1;
  reply.data[0] = 0xB0 | ((seq + 1) & 0x0F); // ACK opcode is 0xB
  
  twai_transmit_v2(s_diag_bus_handle, &reply, pdMS_TO_TICKS(10));
}

static void kwp2000_process_payload(const uint8_t *req, uint16_t req_len);

static void tp20_send_response(const uint8_t *data, uint16_t len) {
  // Wraps KWP2000 payload in TP2.0 frames
  uint8_t payload[ISOTP_BUF_SIZE + 2];
  payload[0] = (len >> 8) & 0xFF;
  payload[1] = len & 0xFF;
  memcpy(&payload[2], data, len);
  uint16_t total_len = len + 2;

  uint16_t idx = 0;
  twai_message_t msg;
  msg.identifier = tp20_tx_id;
  msg.extd = 0;
  msg.rtr = 0;

  while (idx < total_len) {
    msg.data_length_code = 8;
    memset(msg.data, 0, 8);

    uint16_t chunk = total_len - idx;
    bool is_last = (chunk <= 7);
    if (chunk > 7) chunk = 7;

    // Byte 0: TP 2.0 Header (opcode + seq)
    // Opcode 0x0 = more packets wait for ACK
    // Opcode 0x1 = last packet wait for ACK
    uint8_t opcode = is_last ? 0x10 : 0x00;
    msg.data[0] = opcode | tp20_tx_seq;
    memcpy(&msg.data[1], &payload[idx], chunk);
    msg.data_length_code = chunk + 1;

    twai_transmit_v2(s_diag_bus_handle, &msg, pdMS_TO_TICKS(10));
    ESP_LOGD(TAG, "TP20 Tx packet seq=%d, chunk=%d, last=%d", tp20_tx_seq, chunk, is_last);

    idx += chunk;
    tp20_tx_seq = (tp20_tx_seq + 1) & 0x0F;

    // For simplicity, wait a moment or wait for ACK. In a real automotive setup,
    // we block waiting for ACK, but to prevent deadlock in tester loss, we delay.
    // In our case we can wait.
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// --- UDS Service Handler ---
static void uds_process_payload(const uint8_t *req, uint16_t req_len, uint32_t tx_id) {
  if (req_len == 0) return;
  uint8_t sid = req[0];
  uint8_t resp[128];
  uint16_t resp_len = 0;

  system_settings_t *settings = system_settings_get();
  ecu_data_t ecu_data;
  ecu_data_get_copy(&ecu_data);

  ESP_LOGI(TAG, "UDS SID Request: 0x%02X, len: %d", sid, req_len);

  switch (sid) {
    case 0x10: // Diagnostic Session Control
      if (req_len >= 2) {
        resp[0] = 0x50; // Positive Response
        resp[1] = req[1];
        resp[2] = 0x00; // timing parameter
        resp[3] = 0x32; // timing parameter
        resp[4] = 0x01; // timing parameter
        resp[5] = 0xF4; // timing parameter
        resp_len = 6;
      } else {
        resp[0] = 0x7F; // Negative Response
        resp[1] = 0x10;
        resp[2] = 0x13; // Incorrect length
        resp_len = 3;
      }
      break;

    case 0x3E: // Tester Present
      if (req_len >= 2 && (req[1] & 0x80)) {
        // Suppress response requested
        return;
      }
      resp[0] = 0x7E; // Positive Response
      resp[1] = 0x00;
      resp_len = 2;
      break;

    case 0x22: // Read Data By Identifier (DID)
      if (req_len >= 3) {
        uint16_t did = (req[1] << 8) | req[2];
        resp[0] = 0x62; // Positive Response
        resp[1] = req[1];
        resp[2] = req[2];
        resp_len = 3;

        ESP_LOGI(TAG, "UDS Read DID: 0x%04X", did);

        switch (did) {
          case 0xF187: // Spare Part Number
            memcpy(&resp[3], settings->diag_part_number, 11);
            for (int i = strlen(settings->diag_part_number); i < 11; i++) {
              resp[3 + i] = ' ';
            }
            resp_len += 11;
            break;

          case 0xF191: // Hardware Number
            memcpy(&resp[3], settings->diag_hw_number, 11);
            for (int i = strlen(settings->diag_hw_number); i < 11; i++) {
              resp[3 + i] = ' ';
            }
            resp_len += 11;
            break;

          case 0xF197: // System Name / Component (System Description)
            memcpy(&resp[3], settings->diag_comp_name, 19);
            for (int i = strlen(settings->diag_comp_name); i < 19; i++) {
              resp[3 + i] = ' ';
            }
            resp_len += 19;
            break;

          case 0xF189: // Software Version
            memcpy(&resp[3], settings->diag_sw_version, 4);
            for (int i = strlen(settings->diag_sw_version); i < 4; i++) {
              resp[3 + i] = ' ';
            }
            resp_len += 4;
            break;

          case 0xF190: // VIN
            memcpy(&resp[3], settings->diag_vin, 17);
            resp_len += 17;
            break;

          case 0xF150: // Coding
          case 0xF15A: // Coding (Alt)
            resp[3] = (settings->diag_coding >> 16) & 0xFF;
            resp[4] = (settings->diag_coding >> 8) & 0xFF;
            resp[5] = settings->diag_coding & 0xFF;
            resp_len += 3;
            break;

          // Custom DIDs representing Wastegate and BOV
          case 0x1A01: // Wastegate Setpoint %
            resp[3] = (uint8_t)(ecu_data.wg_set_percent * 2.0f); // 0.5% scaling
            resp_len += 1;
            break;
          case 0x1A02: // Wastegate Position %
            resp[3] = (uint8_t)(ecu_data.wg_pos_percent * 2.0f); // 0.5% scaling
            resp_len += 1;
            break;
          case 0x1A03: // BOV Position %
            resp[3] = (uint8_t)(ecu_data.bov_percent * 2.0f);    // 0.5% scaling
            resp_len += 1;
            break;
          case 0x1A04: // Boost Pressure kPa
            resp[3] = (uint8_t)(ecu_data.map_kpa);               // 1 kPa scaling
            resp_len += 1;
            break;

          default: // DID not supported
            resp[0] = 0x7F;
            resp[1] = 0x22;
            resp[2] = 0x31; // Request Out Of Range
            resp_len = 3;
            break;
        }
      } else {
        resp[0] = 0x7F;
        resp[1] = 0x22;
        resp[2] = 0x13; // Incorrect length
        resp_len = 3;
      }
      break;

    default: // Service not supported
      resp[0] = 0x7F;
      resp[1] = sid;
      resp[2] = 0x11; // Service Not Supported
      resp_len = 3;
      break;
  }

  uds_send_response(resp, resp_len, tx_id);
}

// --- KWP2000 Service Handler ---
static void kwp2000_process_payload(const uint8_t *req, uint16_t req_len) {
  if (req_len == 0) return;
  uint8_t sid = req[0];
  uint8_t resp[128];
  uint16_t resp_len = 0;

  system_settings_t *settings = system_settings_get();
  ecu_data_t ecu_data;
  ecu_data_get_copy(&ecu_data);

  ESP_LOGI(TAG, "KWP2000 SID Request: 0x%02X, len: %d", sid, req_len);

  switch (sid) {
    case 0x10: // Start Diagnostic Session
      resp[0] = 0x50;
      resp[1] = (req_len >= 2) ? req[1] : 0x81; // default session
      resp_len = 2;
      break;

    case 0x09: // Acknowledge / Tester Present
      resp[0] = 0x09;
      resp_len = 1;
      break;

    case 0x1A: // Read ECU Identification
      if (req_len >= 2 && req[1] == 0x9B) { // Read Identification by ID
        resp[0] = 0x5A;
        resp[1] = 0x9B;
        resp_len = 2;

        // VAG KWP identification structure
        // 11 bytes Part Number (Software Number)
        memcpy(&resp[resp_len], settings->diag_part_number, 11);
        for (int i = strlen(settings->diag_part_number); i < 11; i++) {
          resp[resp_len + i] = ' ';
        }
        resp_len += 11;

        // 3 bytes coding (e.g. 1015)
        resp[resp_len++] = (settings->diag_coding >> 16) & 0xFF;
        resp[resp_len++] = (settings->diag_coding >> 8) & 0xFF;
        resp[resp_len++] = settings->diag_coding & 0xFF;

        // 13 bytes Component name (System Description)
        memcpy(&resp[resp_len], settings->diag_comp_name, 13);
        for (int i = strlen(settings->diag_comp_name); i < 13; i++) {
          resp[resp_len + i] = ' ';
        }
        resp_len += 13;
      } else {
        resp[0] = 0x7F;
        resp[1] = 0x1A;
        resp[2] = 0x31; // Request Out Of Range
        resp_len = 3;
      }
      break;

    case 0x29: // Read Data By Local Identifier (Measuring Block Group)
      if (req_len >= 2) {
        uint8_t group = req[1];
        resp[0] = 0xE7; // Measuring block group response
        resp[1] = group;
        resp_len = 2;

        ESP_LOGI(TAG, "KWP2000 Read Measuring Group: %d", group);

        if (group == 115) { // Group 115: RPM, Boost Specified, Boost Actual, WG position
          // Value 1: RPM (Type 1)
          fill_kwp_value(1, ecu_data.engine_rpm, &resp[resp_len], &resp[resp_len + 1], &resp[resp_len + 2]);
          resp_len += 3;
          // Value 2: Boost Specified / WG Setpoint (Type 10)
          fill_kwp_value(10, ecu_data.wg_set_percent, &resp[resp_len], &resp[resp_len + 1], &resp[resp_len + 2]);
          resp_len += 3;
          // Value 3: Boost Actual / MAP (Type 25)
          fill_kwp_value(25, ecu_data.map_kpa, &resp[resp_len], &resp[resp_len + 1], &resp[resp_len + 2]);
          resp_len += 3;
          // Value 4: WG Actual position (Type 10)
          fill_kwp_value(10, ecu_data.wg_pos_percent, &resp[resp_len], &resp[resp_len + 1], &resp[resp_len + 2]);
          resp_len += 3;
        } else if (group == 118) { // Group 118: WG Duty Cycle, IAT, BOV percent, Battery
          // Value 1: WG Duty cycle / Setpoint (Type 10)
          fill_kwp_value(10, ecu_data.wg_set_percent, &resp[resp_len], &resp[resp_len + 1], &resp[resp_len + 2]);
          resp_len += 3;
          // Value 2: IAT Temp (Type 5)
          fill_kwp_value(5, ecu_data.iat_temp, &resp[resp_len], &resp[resp_len + 1], &resp[resp_len + 2]);
          resp_len += 3;
          // Value 3: BOV Position (Type 10)
          fill_kwp_value(10, ecu_data.bov_percent, &resp[resp_len], &resp[resp_len + 1], &resp[resp_len + 2]);
          resp_len += 3;
          // Value 4: Battery Voltage (Type 12)
          fill_kwp_value(12, ecu_data.battery_voltage, &resp[resp_len], &resp[resp_len + 1], &resp[resp_len + 2]);
          resp_len += 3;
        } else {
          // Empty dummy group values
          for (int i = 0; i < 4; i++) {
            resp[resp_len++] = 0; // type
            resp[resp_len++] = 0; // val A
            resp[resp_len++] = 0; // val B
          }
        }
      } else {
        resp[0] = 0x7F;
        resp[1] = 0x29;
        resp[2] = 0x13; // Incorrect length
        resp_len = 3;
      }
      break;

    default: // Service not supported
      resp[0] = 0x7F;
      resp[1] = sid;
      resp[2] = 0x11; // Service Not Supported
      resp_len = 3;
      break;
  }

  tp20_send_response(resp, resp_len);
}

bool diagnostic_manager_handle_rx(twai_handle_t bus_handle, const twai_message_t *msg) {
  if (!msg) return false;

  system_settings_t *settings = system_settings_get();
  if (settings->diag_protocol == 0) {
    // Emulation is disabled
    return false;
  }

  uint32_t uds_req_id = 0, uds_resp_id = 0;
  get_uds_ids(settings->diag_address, &uds_req_id, &uds_resp_id);

  // Determine if this message is targeted to our diagnostics
  bool is_for_us = false;
  if ((settings->diag_protocol & 2) && msg->identifier == 0x200 &&
      msg->data_length_code >= 2 && msg->data[0] == settings->diag_address && msg->data[1] == 0xC0) {
    is_for_us = true;
  } else if (tp20_connected && msg->identifier == tp20_rx_id) {
    is_for_us = true;
  } else if ((settings->diag_protocol & 1) && msg->identifier == uds_req_id) {
    is_for_us = true;
  }

  if (is_for_us) {
    s_diag_bus_handle = bus_handle;
  }

  // 1. Check for TP 2.0 Channel Setup (Arbitration ID 0x200)
  if ((settings->diag_protocol & 2) && msg->identifier == 0x200) {
    if (msg->data_length_code >= 2 && msg->data[0] == settings->diag_address && msg->data[1] == 0xC0) {
      ESP_LOGI(TAG, "TP20 Connection Setup Request received for address 0x%02X on bus %p", settings->diag_address, bus_handle);
      
      // Tester Rx ID (Module Tx ID) requested
      tp20_tx_id = (msg->data[4] << 8) | msg->data[5];
      // Module Rx ID (Tester Tx ID) setup
      tp20_rx_id = 0x400 + settings->diag_address;

      twai_message_t reply;
      reply.identifier = 0x200 + settings->diag_address;
      reply.extd = 0;
      reply.rtr = 0;
      reply.data_length_code = 7;
      reply.data[0] = 0x00;
      reply.data[1] = 0xD0; // Positive Response
      reply.data[2] = 0x00; // Block Size = 0 (infinite)
      reply.data[3] = 0x00; // Timing parameters
      reply.data[4] = (tp20_rx_id >> 8) & 0xFF;
      reply.data[5] = tp20_rx_id & 0xFF;
      reply.data[6] = 0x01; // Protocol ID

      twai_transmit_v2(s_diag_bus_handle, &reply, pdMS_TO_TICKS(10));
      
      tp20_connected = true;
      tp20_rx_seq = 0;
      tp20_tx_seq = 0;
      tp20_last_activity = esp_timer_get_time() / 1000;
      
      ESP_LOGI(TAG, "TP20 Channel Established. Tx: 0x%03lX, Rx: 0x%03lX", (unsigned long)tp20_tx_id, (unsigned long)tp20_rx_id);
      return true;
    }
  }

  // 2. Check for dynamic TP2.0 channel messages
  if (tp20_connected && msg->identifier == tp20_rx_id) {
    tp20_last_activity = esp_timer_get_time() / 1000;
    if (msg->data_length_code == 0) return true;

    uint8_t tpci = msg->data[0];
    uint8_t opcode = tpci & 0xF0;
    uint8_t seq = tpci & 0x0F;

    if (opcode == 0xA0) { // Channel parameters config
      ESP_LOGI(TAG, "TP20 Parameter negotiation request");
      twai_message_t reply;
      reply.identifier = tp20_tx_id;
      reply.extd = 0;
      reply.rtr = 0;
      reply.data_length_code = msg->data_length_code;
      memcpy(reply.data, msg->data, msg->data_length_code);
      reply.data[0] = 0xA1; // Positive response opcode
      twai_transmit_v2(s_diag_bus_handle, &reply, pdMS_TO_TICKS(10));
      return true;
    } 
    else if (opcode == 0xA3) { // Keep alive
      twai_message_t reply;
      reply.identifier = tp20_tx_id;
      reply.extd = 0;
      reply.rtr = 0;
      reply.data_length_code = 1;
      reply.data[0] = 0xA3;
      twai_transmit_v2(s_diag_bus_handle, &reply, pdMS_TO_TICKS(10));
      return true;
    } 
    else if (opcode == 0xA8) { // Disconnect
      ESP_LOGI(TAG, "TP20 Disconnect request");
      tp20_connected = false;
      return true;
    } 
    else if (opcode == 0x10 || opcode == 0x00 || opcode == 0x30 || opcode == 0x20) { // Data Frame
      // Acknowledge the frame
      tp20_send_ack(seq);

      // Extract length if first frame in sequence
      uint16_t kwp_len = (msg->data[1] << 8) | msg->data[2];
      if (kwp_len <= 5 && msg->data_length_code >= 3) {
        // Fits in single TP2.0 frame
        kwp2000_process_payload(&msg->data[3], kwp_len);
      }
      return true;
    }
  }

  // 3. Check for UDS over ISO-TP (Arbitration ID matches uds_req_id)
  if ((settings->diag_protocol & 1) && msg->identifier == uds_req_id) {
    if (msg->data_length_code == 0) return false;

    uint8_t type = msg->data[0] & 0xF0;

    if (type == 0x00) { // Single Frame (SF)
      uint8_t len = msg->data[0] & 0x0F;
      if (len > 7) len = 7;
      uds_process_payload(&msg->data[1], len, uds_resp_id);
      return true;
    } 
    else if (type == 0x10) { // First Frame (FF)
      isotp_rx_len = ((msg->data[0] & 0x0F) << 8) | msg->data[1];
      if (isotp_rx_len > ISOTP_BUF_SIZE) isotp_rx_len = ISOTP_BUF_SIZE;
      
      memcpy(isotp_rx_buf, &msg->data[2], 6);
      isotp_rx_idx = 6;
      isotp_rx_seq = 1;
      isotp_rx_in_progress = true;

      // Send Flow Control (FC)
      isotp_send_flow_control(uds_resp_id);
      return true;
    } 
    else if (type == 0x20) { // Consecutive Frame (CF)
      if (isotp_rx_in_progress) {
        uint8_t seq = msg->data[0] & 0x0F;
        if (seq == isotp_rx_seq) {
          uint16_t rem = isotp_rx_len - isotp_rx_idx;
          uint16_t chunk = (rem > 7) ? 7 : rem;
          
          memcpy(&isotp_rx_buf[isotp_rx_idx], &msg->data[1], chunk);
          isotp_rx_idx += chunk;
          isotp_rx_seq = (isotp_rx_seq + 1) & 0x0F;

          if (isotp_rx_idx >= isotp_rx_len) {
            isotp_rx_in_progress = false;
            uds_process_payload(isotp_rx_buf, isotp_rx_len, uds_resp_id);
          }
        } else {
          // Sequence error, reset
          isotp_rx_in_progress = false;
          ESP_LOGE(TAG, "ISO-TP RX sequence mismatch. Expected %d, got %d", isotp_rx_seq, seq);
        }
      }
      return true;
    } 
    else if (type == 0x30) { // Flow Control (FC) - Tester received our FF and sent FC
      uint8_t fs = msg->data[0] & 0x0F; // flow status
      if (fs == 0 && uds_tx_in_progress) {
        // Tester is ready, continue UDS response transmission
        isotp_tx_continue(uds_resp_id);
      }
      return true;
    }
  }

  // 4. Timeout detection for TP2.0 connection
  if (tp20_connected) {
    uint64_t now = esp_timer_get_time() / 1000;
    if (now - tp20_last_activity > 5000) { // 5-second inactivity timeout
      ESP_LOGW(TAG, "TP20 connection timed out");
      tp20_connected = false;
    }
  }

  return false;
}
