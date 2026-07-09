#include "diagnostic_poll.h"
#include "can_manager.h"
#include "ecu_data.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <inttypes.h>
#include "diagnostic_tester.h"

static const char *TAG = "DIAG_POLL";

// KWP2000 over TP2.0 Channel Configuration
typedef struct {
    uint8_t address;
    const char *name;
    bool open;
    uint32_t tx_id;          // Tester transmits to this ID
    uint32_t rx_id;          // Tester receives from this ID
    uint8_t tx_seq;          // Tester transmit sequence (0-15)
    uint8_t rx_seq;          // Tester expected rx sequence (0-15)
    uint32_t last_poll_time; // Last successful poll timestamp
    uint32_t next_retry_time; // Timestamp when we can attempt connection again (milliseconds)
} tp20_channel_t;

static tp20_channel_t s_channels[] = {
    {0x01, "Engine Control Module", false, 0, 0, 0, 0, 0, 0},
    {0x02, "Transmission Control", false, 0, 0, 0, 0, 0, 0}
};
#define NUM_CHANNELS (sizeof(s_channels) / sizeof(s_channels[0]))

static TaskHandle_t s_poll_task_handle = NULL;
static QueueHandle_t s_rx_queue = NULL;

// Helper to wait for a specific CAN ID from the queue
static bool wait_for_frame(uint32_t expected_id, uint32_t timeout_ms, twai_message_t *out_msg) {
    if (s_rx_queue == NULL) return false;
    
    twai_message_t msg;
    uint32_t start_time = esp_log_timestamp();
    while (esp_log_timestamp() - start_time < timeout_ms) {
        uint32_t remaining = timeout_ms - (esp_log_timestamp() - start_time);
        if (xQueueReceive(s_rx_queue, &msg, pdMS_TO_TICKS(remaining)) == pdTRUE) {
            if (msg.identifier == expected_id) {
                *out_msg = msg;
                return true;
            }
        }
    }
    return false;
}

// Open TP2.0 diagnostic channel
static esp_err_t tp20_open_channel(tp20_channel_t *ch) {
    ch->open = false;
    ch->tx_seq = 0;
    ch->rx_seq = 0;
    
    if (g_can2_handle == NULL) return ESP_ERR_INVALID_STATE;
    
    // Reset RX Queue to clear any old/stale frames
    if (s_rx_queue != NULL) {
        xQueueReset(s_rx_queue);
    }
    
    // Step 1: Send channel setup request to 0x200
    twai_message_t tx_msg;
    tx_msg.identifier = 0x200;
    tx_msg.extd = 0;
    tx_msg.rtr = 0;
    tx_msg.data_length_code = 7;
    memset(tx_msg.data, 0, 8);
    tx_msg.data[0] = ch->address;
    tx_msg.data[1] = 0xC0; // Connection setup request
    tx_msg.data[2] = 0x00;
    tx_msg.data[3] = 0x10;
    tx_msg.data[4] = 0x00;
    tx_msg.data[5] = 0x03;
    tx_msg.data[6] = 0x01; // Protocol ID
    
    ESP_LOGI(TAG, "Opening TP2.0 channel to %s (0x%02X)...", ch->name, ch->address);
    esp_err_t err = twai_transmit_v2(g_can2_handle, &tx_msg, pdMS_TO_TICKS(15));
    if (err != ESP_OK) return err;
    
    // Step 2: Wait for handshake response on 0x200 + address (optimized to 40ms)
    twai_message_t rx_msg;
    if (!wait_for_frame(0x200 + ch->address, 40, &rx_msg)) {
        ESP_LOGE(TAG, "Timeout waiting for TP2.0 handshake from module 0x%02X", ch->address);
        return ESP_ERR_TIMEOUT;
    }
    
    if (rx_msg.data_length_code < 7 || rx_msg.data[0] != 0x00 || rx_msg.data[1] != 0xD0) {
        ESP_LOGE(TAG, "Invalid TP2.0 handshake response from module 0x%02X", ch->address);
        return ESP_FAIL;
    }
    
    // Decode Negotiated CAN IDs
    ch->tx_id = (rx_msg.data[5] << 8) | rx_msg.data[4];
    ch->rx_id = (rx_msg.data[3] << 8) | (rx_msg.data[6] & 0xFE);
    ESP_LOGI(TAG, "Handshake complete. Tx ID: 0x%"PRIx32" Rx ID: 0x%"PRIx32, ch->tx_id, ch->rx_id);
    
    // Step 3: Parameters negotiation (A0/A1)
    tx_msg.identifier = ch->tx_id;
    tx_msg.data_length_code = 6;
    tx_msg.data[0] = 0xA0; // Parameters Request Opcode
    tx_msg.data[1] = 0x0F; // Block Size (BS)
    tx_msg.data[2] = 0x8A; // T1 timeout
    tx_msg.data[3] = 0xFF; // T2 timeout
    tx_msg.data[4] = 0x32; // T3 timeout
    tx_msg.data[5] = 0xFF; // T4 timeout
    
    err = twai_transmit_v2(g_can2_handle, &tx_msg, pdMS_TO_TICKS(15));
    if (err != ESP_OK) return err;
    
    // Wait for parameter response (optimized to 40ms)
    if (!wait_for_frame(ch->rx_id, 40, &rx_msg)) {
        ESP_LOGE(TAG, "Timeout waiting for TP2.0 parameters handshake response");
        return ESP_ERR_TIMEOUT;
    }
    
    if (rx_msg.data_length_code < 1 || (rx_msg.data[0] & 0xF0) != 0xA0) {
        ESP_LOGE(TAG, "Invalid TP2.0 parameters response: 0x%02X", rx_msg.data[0]);
        return ESP_FAIL;
    }
    
    ch->open = true;
    ch->last_poll_time = esp_log_timestamp();
    ESP_LOGI(TAG, "TP2.0 channel to %s (0x%02X) is now OPEN", ch->name, ch->address);
    return ESP_OK;
}

// Perform generic KWP2000 transaction over TP2.0 with variable payload length
static esp_err_t tp20_kwp_transaction_generic(tp20_channel_t *ch, const uint8_t *payload, uint16_t payload_len, uint8_t *resp_payload, uint16_t *resp_len) {
    if (!ch->open) return ESP_ERR_INVALID_STATE;
    if (payload_len > 5) return ESP_ERR_INVALID_ARG; // Must fit in single frame
    
    // Reset RX Queue to clear any old/stale frames before sending request
    if (s_rx_queue != NULL) {
        xQueueReset(s_rx_queue);
    }
    
    // Step 1: Send request as single-frame (Type 0x10 | TxSeq)
    twai_message_t tx_msg;
    tx_msg.identifier = ch->tx_id;
    tx_msg.extd = 0;
    tx_msg.rtr = 0;
    tx_msg.data_length_code = payload_len + 3;
    memset(tx_msg.data, 0, 8);
    tx_msg.data[0] = 0x10 | (ch->tx_seq & 0x0F);
    tx_msg.data[1] = (payload_len >> 8) & 0xFF;
    tx_msg.data[2] = payload_len & 0xFF;
    memcpy(&tx_msg.data[3], payload, payload_len);
    
    // Transmit request
    esp_err_t err = twai_transmit_v2(g_can2_handle, &tx_msg, pdMS_TO_TICKS(15));
    if (err != ESP_OK) {
        ch->open = false;
        return err;
    }
    
    ch->tx_seq = (ch->tx_seq + 1) & 0x0F;
    
    // Step 2: Wait for ACK frame from module (optimized to 30ms)
    twai_message_t rx_msg;
    if (!wait_for_frame(ch->rx_id, 30, &rx_msg)) {
        ESP_LOGE(TAG, "Timeout waiting for KWP request Tx ACK from module 0x%02X", ch->address);
        ch->open = false;
        return ESP_ERR_TIMEOUT;
    }
    
    uint8_t expected_ack = 0xB0 | ch->tx_seq;
    if (rx_msg.data_length_code != 1 || rx_msg.data[0] != expected_ack) {
        ESP_LOGD(TAG, "Unexpected KWP ACK: 0x%02X (Expected: 0x%02X)", rx_msg.data[0], expected_ack);
        // Note: We don't crash here since some ECUs might send data immediately.
    }
    
    // Step 3: Receive response frames and reassemble KWP payload
    uint16_t total_len = 0;
    uint16_t collected = 0;
    bool in_msg = false;
    uint8_t expected_seq = 0;
    uint8_t last_rx_seq = 0;
    
    uint32_t start_time = esp_log_timestamp();
    while (esp_log_timestamp() - start_time < 80) { // Total transaction limit 80ms
        if (!wait_for_frame(ch->rx_id, 35, &rx_msg)) { // Payload frame timeout 35ms
            ESP_LOGE(TAG, "Timeout waiting for response payload frame from module 0x%02X", ch->address);
            ch->open = false;
            return ESP_ERR_TIMEOUT;
        }
        
        if (rx_msg.data_length_code < 1) continue;
        
        uint8_t first_byte = rx_msg.data[0];
        uint8_t flags = first_byte & 0xF0;
        uint8_t seq = first_byte & 0x0F;
        
        if (flags == 0xA0 || flags == 0xA1 || flags == 0xB0) {
            // Protocol parameter changes/ACKs, skip
            continue;
        }
        
        if (flags == 0x20) { // Multi-frame packet, more to follow
            if (rx_msg.data_length_code >= 3 && !in_msg) {
                total_len = rx_msg.data[2];
                uint8_t payload_len_chunk = rx_msg.data_length_code - 3;
                if (payload_len_chunk > total_len) payload_len_chunk = total_len;
                
                memcpy(resp_payload, &rx_msg.data[3], payload_len_chunk);
                collected = payload_len_chunk;
                in_msg = true;
                expected_seq = (seq + 1) & 0x0F;
                last_rx_seq = seq;
            } else if (in_msg) {
                if (seq != expected_seq) {
                    ESP_LOGE(TAG, "TP2.0 Sequence error. Expected %d, got %d", expected_seq, seq);
                    ch->open = false;
                    return ESP_FAIL;
                }
                uint8_t payload_len_chunk = rx_msg.data_length_code - 1;
                uint16_t rem = total_len - collected;
                if (payload_len_chunk > rem) payload_len_chunk = rem;
                
                memcpy(resp_payload + collected, &rx_msg.data[1], payload_len_chunk);
                collected += payload_len_chunk;
                expected_seq = (seq + 1) & 0x0F;
                last_rx_seq = seq;
            }
        } 
        else if (flags == 0x10) { // Final frame of message
            if (in_msg) {
                if (seq != expected_seq) {
                    ESP_LOGE(TAG, "TP2.0 Sequence error on final frame. Expected %d, got %d", expected_seq, seq);
                    ch->open = false;
                    return ESP_FAIL;
                }
                uint8_t payload_len_chunk = rx_msg.data_length_code - 1;
                uint16_t rem = total_len - collected;
                if (payload_len_chunk > rem) payload_len_chunk = rem;
                
                memcpy(resp_payload + collected, &rx_msg.data[1], payload_len_chunk);
                collected += payload_len_chunk;
                last_rx_seq = seq;
                
                // Message complete: Tester must send ACK back (B[seq+1])
                twai_message_t ack;
                ack.identifier = ch->tx_id;
                ack.extd = 0;
                ack.rtr = 0;
                ack.data_length_code = 1;
                ack.data[0] = 0xB0 | ((last_rx_seq + 1) & 0x0F);
                twai_transmit_v2(g_can2_handle, &ack, pdMS_TO_TICKS(10));
                
                *resp_len = collected;
                ch->last_poll_time = esp_log_timestamp();
                return ESP_OK;
            } else {
                // Single frame response
                if (rx_msg.data_length_code >= 3) {
                    total_len = rx_msg.data[2];
                    uint8_t payload_len_chunk = rx_msg.data_length_code - 3;
                    if (payload_len_chunk > total_len) payload_len_chunk = total_len;
                    
                    memcpy(resp_payload, &rx_msg.data[3], payload_len_chunk);
                    *resp_len = payload_len_chunk;
                    ch->last_poll_time = esp_log_timestamp();
                    return ESP_OK;
                }
            }
        }
    }
    
    return ESP_ERR_TIMEOUT;
}

// Perform standard KWP2000 transaction over TP2.0 (wrapper for backward compatibility)
static esp_err_t tp20_kwp_transaction(tp20_channel_t *ch, uint8_t service, uint8_t group, uint8_t *resp_payload, uint16_t *resp_len) {
    uint8_t req[2] = { service, group };
    return tp20_kwp_transaction_generic(ch, req, 2, resp_payload, resp_len);
}

// Convert VAG formula types to float
static float parse_vag_value(uint8_t type, uint8_t a, uint8_t b) {
    switch (type) {
        case 1:
            // RPM
            return 0.2f * a * b;
        case 5:
            // Temperature (Celsius)
            // Formula: 0.1 * a * (b - 100)
            return 0.1f * a * (b - 100);
        case 21:
            // Voltage
            // Formula: 0.001 * a * b
            return 0.001f * a * b;
        case 27:
            // Timing Angle (BTDC / ATDC)
            // Formula: |b - 128| * 0.01 * a
            return abs(b - 128) * 0.01f * a;
        case 31:
            // Lambda
            // Formula: a * b / 2560
            return (a * b) / 2560.0f;
        case 33:
            // TPS / Percentage
            // Formula: 100 * b / a (if a==0 -> 100 * b)
            if (a == 0) return 100.0f * b;
            return 100.0f * b / a;
        case 96:
            // Pressure G71 (mbar absolute)
            // Formula: 0.1 * a * b
            // Convert to kPa: value_mbar / 10 = 0.01 * a * b
            return 0.01f * a * b;
        default:
            return 0.0f;
    }
}

typedef struct {
    uint8_t group;
    const uint8_t *payload;
    uint16_t len;
} diag_update_ctx_t;

static void parse_ecu_group_transaction(ecu_data_t *state, void *ctx) {
    diag_update_ctx_t *up = (diag_update_ctx_t *)ctx;
    uint8_t group = up->group;
    const uint8_t *payload = up->payload;
    uint16_t len = up->len;
    
    const uint8_t *params = &payload[2];
    uint16_t params_len = len - 2;
    uint32_t now = esp_timer_get_time() / 1000;
    
    if (group == 3) {
        // Group 3: RPM, MAP, TPS, Timing Angle, IAT, Ambient Temp
        if (params_len >= 3) {
            state->engine_rpm_diag = parse_vag_value(params[0], params[1], params[2]);
            state->last_diag_update_ms[GAUGE_RPM] = now;
        }
        if (params_len >= 6) {
            state->map_kpa_diag = parse_vag_value(params[3], params[4], params[5]);
            state->last_diag_update_ms[GAUGE_MAP] = now;
            state->last_diag_update_ms[GAUGE_BOOST_ACT] = now;
        }
        if (params_len >= 9) {
            state->tps_position_diag = parse_vag_value(params[6], params[7], params[8]);
            state->last_diag_update_ms[GAUGE_TPS] = now;
        }
        // Timing Angle (params[9..11]) is ignored for now
        if (params_len >= 15) {
            state->iat_temp_diag = parse_vag_value(params[12], params[13], params[14]);
            state->last_diag_update_ms[GAUGE_IAT] = now;
        }
    } 
    else if (group == 4) {
        // Group 4: RPM, Voltage, Temps (Coolant G62 is parameter 6, Ambient G17 is parameter 7)
        if (params_len >= 6) {
            state->battery_voltage_diag = parse_vag_value(params[3], params[4], params[5]);
            state->last_diag_update_ms[GAUGE_BATTERY] = now;
        }
        if (params_len >= 18) {
            state->clt_temp_diag = parse_vag_value(params[15], params[16], params[17]);
            state->last_diag_update_ms[GAUGE_WATER_TEMP] = now;
        }
        if (params_len >= 21) {
            state->ambient_temp_diag = parse_vag_value(params[18], params[19], params[20]);
            state->last_diag_update_ms[GAUGE_AMBIENT_TEMP] = now;
        }
    } 
    else if (group == 31) {
        // Group 31: Lambda actual, Lambda specified
        if (params_len >= 3) {
            state->afr_val_diag = parse_vag_value(params[0], params[1], params[2]);
            state->last_diag_update_ms[GAUGE_AFR] = now;
        }
        if (params_len >= 6) {
            state->afr_target_diag = parse_vag_value(params[3], params[4], params[5]);
            state->last_diag_update_ms[GAUGE_AFR] = now;
        }
    }
}

// Parse ECU Group responses and update global structure
static void parse_ecu_data_group(uint8_t group, const uint8_t *payload, uint16_t len) {
    if (len < 2 || payload[0] != 0x61 || payload[1] != group) return;
    
    diag_update_ctx_t ctx = { .group = group, .payload = payload, .len = len };
    ecu_data_update_transaction(parse_ecu_group_transaction, &ctx);
}

static void parse_tcu_group_transaction(ecu_data_t *state, void *ctx) {
    diag_update_ctx_t *up = (diag_update_ctx_t *)ctx;
    uint8_t group = up->group;
    const uint8_t *payload = up->payload;
    uint16_t len = up->len;
    
    const uint8_t *params = &payload[2];
    uint16_t params_len = len - 2;
    uint32_t now = esp_timer_get_time() / 1000;
    
    if (group == 2) {
        // Group 2: Mode selection and Gear position (Formula type 17: ASCII bytes)
        if (params_len >= 3 && params[0] == 17) {
            uint8_t mode_a = params[1];
            uint8_t mode_b = params[2];
            int8_t selector_pos = 0; // 0=Unknown, 1=P, 2=R, 3=N, 4=D, 5=S, 6=Tiptronic
            
            if (mode_a == ' ' && mode_b == 'P') selector_pos = 2;
            else if (mode_a == ' ' && mode_b == 'R') selector_pos = 3;
            else if (mode_a == ' ' && mode_b == 'N') selector_pos = 4;
            else if (mode_a == ' ' && mode_b == 'D') selector_pos = 5;
            else if (mode_a == ' ' && mode_b == 'S') selector_pos = 6;
            else if (mode_a == 'T' && mode_b == 'T') selector_pos = 7;
            else if (mode_a == 'P' && mode_b == 'L') selector_pos = 7;
            else if (mode_a == 'M' && mode_b == 'I') selector_pos = 7;
            
            state->selector_position_diag = selector_pos;
            state->last_diag_update_ms[GAUGE_TCU] = now;
        }
        
        if (params_len >= 12 && params[9] == 17) {
            uint8_t gear_b = params[11];
            int8_t gear = 0;
            
            if (gear_b >= '0' && gear_b <= '6') {
                gear = gear_b - '0';
            } else if (gear_b == 'R') {
                gear = -1;
            }
            
            state->gear_diag = gear;
            state->last_diag_update_ms[GAUGE_TCU] = now;
        }
    }
}

// Parse DSG TCU Group responses and update global structure
static void parse_tcu_data_group(uint8_t group, const uint8_t *payload, uint16_t len) {
    if (len < 2 || payload[0] != 0x61 || payload[1] != group) return;
    
    diag_update_ctx_t ctx = { .group = group, .payload = payload, .len = len };
    ecu_data_update_transaction(parse_tcu_group_transaction, &ctx);
}

// Background poller task
static void diagnostic_poll_task(void *pvParameters) {
    ESP_LOGI(TAG, "TP2.0/KWP2000 Diagnostic Poller Task Started");
    
    uint8_t poll_buf[256];
    uint16_t poll_len = 0;
    
    uint32_t last_group4_poll = 0;
    uint32_t last_tcu_poll = 0;
    
    while (1) {
        if (g_can2_handle == NULL) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        uint32_t now = esp_log_timestamp();
        
        // Yield if diagnostic tester is busy (scanning or clearing)
        tester_state_t state = diagnostic_tester_get_state();
        if (state == TESTER_SCANNING || state == TESTER_CLEARING) {
            // Drop channels so scanner can work without collision
            s_channels[0].open = false;
            s_channels[1].open = false;
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        
        // 1. Process Engine ECU
        tp20_channel_t *eng_ch = &s_channels[0];
        if (!eng_ch->open) {
            if (now >= eng_ch->next_retry_time) {
                if (tp20_open_channel(eng_ch) != ESP_OK) {
                    eng_ch->next_retry_time = now + 4000; // Retry in 4 seconds
                }
            }
        }
        
        if (eng_ch->open) {
            // Poll Group 3 (MAP, RPM, TPS, IAT) - polled on every iteration
            if (tp20_kwp_transaction(eng_ch, 0x21, 0x03, poll_buf, &poll_len) == ESP_OK) {
                parse_ecu_data_group(3, poll_buf, poll_len);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            
            // Poll Group 4 (Coolant temp, Battery voltage) - polled once every 2000ms
            if (now - last_group4_poll >= 2000) {
                if (tp20_kwp_transaction(eng_ch, 0x21, 0x04, poll_buf, &poll_len) == ESP_OK) {
                    parse_ecu_data_group(4, poll_buf, poll_len);
                    last_group4_poll = now;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            
            // Poll Group 31 (Lambda) - polled on every iteration
            if (tp20_kwp_transaction(eng_ch, 0x21, 0x1F, poll_buf, &poll_len) == ESP_OK) {
                parse_ecu_data_group(31, poll_buf, poll_len);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        
        // 2. Process DSG TCU
        tp20_channel_t *tcu_ch = &s_channels[1];
        if (!tcu_ch->open) {
            if (now >= tcu_ch->next_retry_time) {
                if (tp20_open_channel(tcu_ch) != ESP_OK) {
                    tcu_ch->next_retry_time = now + 4000; // Retry in 4 seconds
                }
            }
        }
        
        if (tcu_ch->open) {
            // Poll Group 2 (Gears, Shifter position) - polled once every 150ms
            if (now - last_tcu_poll >= 150) {
                if (tp20_kwp_transaction(tcu_ch, 0x21, 0x02, poll_buf, &poll_len) == ESP_OK) {
                    parse_tcu_data_group(2, poll_buf, poll_len);
                    last_tcu_poll = now;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        
        // Sleep slightly to yield CPU (15ms sleep results in responsive ~25Hz poll loop)
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

// Initialize Diagnostic Poller
void diagnostic_poll_init(void) {
    if (s_poll_task_handle != NULL) {
        ESP_LOGE(TAG, "Diagnostic poller task already running");
        return;
    }
    
    // Create RX Queue
    s_rx_queue = xQueueCreate(16, sizeof(twai_message_t));
    if (s_rx_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create Diagnostic Poller RX Queue");
        return;
    }
    
    // Reset channels
    s_channels[0].open = false;
    s_channels[1].open = false;
    
    xTaskCreatePinnedToCore(diagnostic_poll_task, "diag_poller", 4096, NULL, 4, &s_poll_task_handle, 1);
    ESP_LOGI(TAG, "Diagnostic poller initialized");
}

// Receive Callback hook in CAN Manager
bool diagnostic_poll_handle_rx(const twai_message_t *msg) {
    if (!msg) return false;
    
    // Skip if diagnostic tester is busy
    tester_state_t state = diagnostic_tester_get_state();
    if (state == TESTER_SCANNING || state == TESTER_CLEARING) {
        return false;
    }
    
    bool is_poller_msg = false;
    // Route handshake responses (0x201, 0x202) and communication responses (0x300) to poller queue
    if (msg->identifier == 0x201 || msg->identifier == 0x202 || msg->identifier == 0x300) {
        is_poller_msg = true;
    } else {
        // Also route dynamically negotiated Rx IDs for open channels
        for (int i = 0; i < NUM_CHANNELS; i++) {
            if (s_channels[i].open && msg->identifier == s_channels[i].rx_id) {
                is_poller_msg = true;
                break;
            }
        }
    }
    
    if (is_poller_msg) {
        if (s_rx_queue != NULL) {
            xQueueSendFromISR(s_rx_queue, msg, NULL);
            return true;
        }
    }
    return false;
}

esp_err_t diagnostic_poll_read_kwp_dtcs(uint8_t address, uint8_t *dtc_buf, uint16_t *dtc_len) {
    if (g_can2_handle == NULL) return ESP_ERR_INVALID_STATE;
    
    // Find if we already have a channel structure for this address
    tp20_channel_t *ch = NULL;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (s_channels[i].address == address) {
            ch = &s_channels[i];
            break;
        }
    }
    
    // If not found, use a temporary channel structure
    tp20_channel_t temp_ch;
    if (ch == NULL) {
        temp_ch.address = address;
        temp_ch.name = "Temp Module";
        temp_ch.open = false;
        temp_ch.tx_id = 0;
        temp_ch.rx_id = 0;
        temp_ch.tx_seq = 0;
        temp_ch.rx_seq = 0;
        temp_ch.last_poll_time = 0;
        temp_ch.next_retry_time = 0;
        ch = &temp_ch;
    }
    
    // Temporarily clear next retry time so manual scan pings instantly
    ch->next_retry_time = 0;
    
    if (!ch->open) {
        esp_err_t err = tp20_open_channel(ch);
        if (err != ESP_OK) return err;
    }
    
    // Request: Read DTCs by Status Mask (18 02 FF 00)
    uint8_t req[] = { 0x18, 0x02, 0xFF, 0x00 };
    return tp20_kwp_transaction_generic(ch, req, sizeof(req), dtc_buf, dtc_len);
}

esp_err_t diagnostic_poll_clear_kwp_dtcs(uint8_t address) {
    if (g_can2_handle == NULL) return ESP_ERR_INVALID_STATE;
    
    tp20_channel_t *ch = NULL;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (s_channels[i].address == address) {
            ch = &s_channels[i];
            break;
        }
    }
    
    tp20_channel_t temp_ch;
    if (ch == NULL) {
        temp_ch.address = address;
        temp_ch.name = "Temp Module";
        temp_ch.open = false;
        temp_ch.tx_id = 0;
        temp_ch.rx_id = 0;
        temp_ch.tx_seq = 0;
        temp_ch.rx_seq = 0;
        temp_ch.last_poll_time = 0;
        temp_ch.next_retry_time = 0;
        ch = &temp_ch;
    }
    
    ch->next_retry_time = 0;
    
    if (!ch->open) {
        esp_err_t err = tp20_open_channel(ch);
        if (err != ESP_OK) return err;
    }
    
    // Request: Clear DTC Memory (14 FF 00)
    uint8_t req[] = { 0x14, 0xFF, 0x00 };
    uint8_t resp[32];
    uint16_t resp_len = 0;
    return tp20_kwp_transaction_generic(ch, req, sizeof(req), resp, &resp_len);
}
