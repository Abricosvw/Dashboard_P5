#include "diagnostic_tester.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "can_manager.h"
#include "telegram_manager.h"
#include "diagnostic_poll.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static const char *TAG = "DIAG_TESTER";

static twai_handle_t s_tester_bus_handle = NULL;

// --- diagnostic targets ---
typedef struct {
    uint8_t addr;
    const char *name;
    uint32_t req_id;
    uint32_t resp_id;
} diag_target_t;

static const diag_target_t scan_targets[] = {
    {0x01, "Engine Control Module", 0x7E0, 0x7E8},
    {0x02, "Transmission Control", 0x7E1, 0x7E9},
    {0x03, "ABS Brakes Control", 0x713, 0x77D},
    {0x15, "Airbag Control Module", 0x715, 0x77F},
    {0x17, "Instrument Cluster", 0x714, 0x77E},
    {0x20, "Central Electrics (BCM)", 0x70C, 0x776},       // Corrected (UDS 70C/776, TP2.0 20)
    {0x1F, "Gateway J533", 0x710, 0x77A},                  // Corrected (TP2.0 1F)
    {0x28, "Steering Assist", 0x712, 0x77C},               // Corrected (TP2.0 28)
    {0x33, "ACC / Auto Distance", 0x757, 0x7C1},           // Corrected (TP2.0 33)
    {0x22, "AWD / Haldex", 0x70F, 0x779},
    {0x08, "Air Conditioning", 0x746, 0x7B0},
    {0x42, "Driver Door Electronics", 0x711, 0x77B}        // Added (UDS 711/77B)
};
#define NUM_SCAN_TARGETS (sizeof(scan_targets) / sizeof(scan_targets[0]))


// --- common DTCs dictionary in RAM ---
typedef struct {
    uint16_t code;
    const char *desc;
} dtc_lookup_t;

static const dtc_lookup_t common_dtcs[] = {
    {0x0234, "Boost Limit Exceeded (Overboost)"},
    {0x0299, "Boost Pressure Regulation: Control Range Not Reached (Underboost)"},
    {0x0101, "Mass Air Flow (MAF) Sensor: Implausible Signal"},
    {0x0113, "Intake Air Temperature (IAT) Sensor: Signal Too High"},
    {0x0171, "System Too Lean (Bank 1)"},
    {0x0300, "Random/Multiple Cylinder Misfire Detected"},
    {0x0301, "Cylinder 1: Misfire Detected"},
    {0x0302, "Cylinder 2: Misfire Detected"},
    {0x0303, "Cylinder 3: Misfire Detected"},
    {0x0304, "Cylinder 4: Misfire Detected"},
    {0x0507, "Idle Control System: RPM Higher than Expected"},
    {0x3FE0, "Control Module: Electrical Error in Circuit (VAG 16352)"},
    {0x0214, "Supply Voltage B+: Signal Too Low (VAG 00532)"},
    {0x0420, "Catalyst System; Bank 1: Efficiency Below Threshold"},
    {0x1001, "Wastegate Actuator: Mechanical Malfunction"},
    {0x1002, "Wastegate Actuator: Electrical Fault"},
    {0x1003, "Blow-off Valve: Mechanical Malfunction"},
    {0x1004, "Blow-off Valve: Electrical Fault"}
};

// --- tester state ---
static tester_state_t tester_state = TESTER_IDLE;
static int tester_progress = 0;
static char tester_logs[4096] = {0};
static TaskHandle_t tester_task_handle = NULL;

// --- cached scan results ---
static bool s_scan_performed = false;
static bool s_module_active[NUM_SCAN_TARGETS] = {false};
static bool s_module_is_uds[NUM_SCAN_TARGETS] = {false};

// --- ISO-TP client reception variables ---
static uint32_t expected_req_id = 0;
static uint32_t expected_resp_id = 0;
static uint8_t rx_buf[256];
static uint16_t rx_len = 0;
static uint16_t rx_idx = 0;
static uint8_t rx_seq = 0;
static bool rx_in_progress = false;
static bool rx_success = false;

// --- log formatting helper ---
static void tester_log_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char line[256];
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    // Append to logs buffer (with overflow protection)
    size_t cur_len = strlen(tester_logs);
    size_t line_len = strlen(line);
    if (cur_len + line_len + 1 < sizeof(tester_logs)) {
        strcat(tester_logs, line);
    } else {
        // Shift buffer to make space or clear
        strcpy(tester_logs, "[Logs truncated due to size limit]\n");
        strcat(tester_logs, line);
    }
    ESP_LOGI(TAG, "%s", line);
}

// --- DTC string formatting helper ---
static void get_obd_code_string(uint16_t code, char *out_str, size_t out_size) {
    char type = 'P';
    uint16_t num = code;
    if (code >= 0xC000) {
        type = 'U';
        num -= 0xC000;
    } else if (code >= 0x8000) {
        type = 'B';
        num -= 0x8000;
    } else if (code >= 0x4000) {
        type = 'C';
        num -= 0x4000;
    }
    snprintf(out_str, out_size, "%c%04X (VAG %05u)", type, num, code);
}

// --- DTC dictionary lookup ---
static const char* lookup_dtc_description(uint16_t code, char *buf, size_t buf_size) {
    int num_common = sizeof(common_dtcs) / sizeof(common_dtcs[0]);
    for (int i = 0; i < num_common; i++) {
        if (common_dtcs[i].code == code) {
            strncpy(buf, common_dtcs[i].desc, buf_size);
            return buf;
        }
    }

    // Try SD Card lookup
    FILE *f = fopen("/sdcard/SYSTEM/DB/dtc_codes.txt", "r");
    if (f) {
        char line[128];
        char search_str[16];
        snprintf(search_str, sizeof(search_str), "%05u:", code); // VAG formatting
        char search_str_obd[16];
        char obd_type = 'P';
        uint16_t obd_num = code;
        if (code >= 0xC000) {
            obd_type = 'U';
            obd_num -= 0xC000;
        } else if (code >= 0x8000) {
            obd_type = 'B';
            obd_num -= 0x8000;
        } else if (code >= 0x4000) {
            obd_type = 'C';
            obd_num -= 0x4000;
        }
        snprintf(search_str_obd, sizeof(search_str_obd), "%c%04X:", obd_type, obd_num); // OBD formatting

        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, search_str, strlen(search_str)) == 0) {
                char *val = line + strlen(search_str);
                val[strcspn(val, "\r\n")] = '\0';
                strncpy(buf, val, buf_size);
                fclose(f);
                return buf;
            }
            if (strncmp(line, search_str_obd, strlen(search_str_obd)) == 0) {
                char *val = line + strlen(search_str_obd);
                val[strcspn(val, "\r\n")] = '\0';
                strncpy(buf, val, buf_size);
                fclose(f);
                return buf;
            }
        }
        fclose(f);
    }

    snprintf(buf, buf_size, "Unknown trouble code");
    return buf;
}

// --- ISO-TP UDS Transaction Helper ---
static esp_err_t uds_transaction(twai_handle_t bus_handle, uint32_t req_id, uint32_t resp_id, const uint8_t *req, uint16_t req_len, uint32_t timeout_ms) {
    if (!bus_handle) return ESP_ERR_INVALID_ARG;
    expected_req_id = req_id;
    expected_resp_id = resp_id;
    rx_len = 0;
    rx_idx = 0;
    rx_in_progress = false;
    rx_success = false;
    s_tester_bus_handle = bus_handle;

    // Reset notification value
    ulTaskNotifyTake(pdTRUE, 0);

    // Send UDS request
    twai_message_t tx_msg;
    tx_msg.identifier = req_id;
    tx_msg.extd = 0;
    tx_msg.rtr = 0;

    if (req_len <= 7) {
        tx_msg.data_length_code = 8;
        memset(tx_msg.data, 0, 8);
        tx_msg.data[0] = req_len;
        memcpy(&tx_msg.data[1], req, req_len);

        esp_err_t err = twai_transmit_v2(bus_handle, &tx_msg, pdMS_TO_TICKS(10));
        if (err != ESP_OK) return err;
    } else {
        // Multi-frame send (First Frame + Flow Control reception + Consecutive frames)
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Wait for response via task notification
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms)) > 0) {
        if (rx_success) {
            return ESP_OK;
        }
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t uds_transaction_dual(uint32_t req_id, uint32_t resp_id, const uint8_t *req, uint16_t req_len, uint32_t timeout_ms) {
    // Only query on CAN2 (OBD2/Diagnostics)
    return uds_transaction(g_can2_handle, req_id, resp_id, req, req_len, timeout_ms);
}

static const char* decode_model_year(char c) {
    switch (c) {
        case '1': return "2001";
        case '2': return "2002";
        case '3': return "2003";
        case '4': return "2004";
        case '5': return "2005";
        case '6': return "2006";
        case '7': return "2007";
        case '8': return "2008";
        case '9': return "2009";
        case 'A': return "2010";
        case 'B': return "2011";
        case 'C': return "2012";
        case 'D': return "2013";
        case 'E': return "2014";
        case 'F': return "2015";
        case 'G': return "2016";
        case 'H': return "2017";
        case 'J': return "2018";
        case 'K': return "2019";
        case 'L': return "2020";
        case 'M': return "2021";
        case 'N': return "2022";
        case 'P': return "2023";
        case 'R': return "2024";
        case 'S': return "2025";
        case 'T': return "2026";
        case 'V': return "2027";
        default: return "Unknown";
    }
}

static const char* decode_manufacturer(const char *vin) {
    if (strncmp(vin, "WVW", 3) == 0 || strncmp(vin, "WVG", 3) == 0 || strncmp(vin, "WV1", 3) == 0 || strncmp(vin, "WV2", 3) == 0 ||
        strncmp(vin, "1VW", 3) == 0 || strncmp(vin, "3VW", 3) == 0 || strncmp(vin, "9BW", 3) == 0) {
        return "Volkswagen";
    } else if (strncmp(vin, "WA1", 3) == 0 || strncmp(vin, "WUA", 3) == 0 || strncmp(vin, "TRU", 3) == 0 || strncmp(vin, "LFV", 3) == 0) {
        return "Audi";
    } else if (strncmp(vin, "TMB", 3) == 0) {
        return "Skoda";
    } else if (strncmp(vin, "VSS", 3) == 0) {
        return "Seat";
    } else if (strncmp(vin, "WP0", 3) == 0 || strncmp(vin, "WP1", 3) == 0) {
        return "Porsche";
    }
    return "VAG Vehicle";
}

static bool ping_tp20_module(uint8_t address) {
    if (g_can2_handle == NULL) return false;

    twai_message_t tx_msg;
    tx_msg.identifier = 0x200;
    tx_msg.extd = 0;
    tx_msg.rtr = 0;
    tx_msg.data_length_code = 7;
    memset(tx_msg.data, 0, 7);
    tx_msg.data[0] = address;
    tx_msg.data[1] = 0xC0; // Connection setup request
    tx_msg.data[2] = 0x00;
    tx_msg.data[3] = 0x10;
    tx_msg.data[4] = 0x00;
    tx_msg.data[5] = 0x03;
    tx_msg.data[6] = 0x01; // Protocol ID

    expected_resp_id = 0x200 + address;
    rx_success = false;
    ulTaskNotifyTake(pdTRUE, 0);

    s_tester_bus_handle = g_can2_handle;
    twai_transmit_v2(g_can2_handle, &tx_msg, pdMS_TO_TICKS(10));
    
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(40)) > 0 && rx_success) {
        return true;
    }
    return false;
}

// --- Active Scan Background Worker ---
static void scanner_worker_task(void *pvParameters) {
    tester_state = TESTER_SCANNING;
    tester_progress = 0;
    tester_logs[0] = '\0';

    tester_log_printf("=================================\n");
    tester_log_printf("  VAG ACTIVE DIAGNOSTIC AUTO-SCAN\n");
    tester_log_printf("=================================\n\n");

    tester_log_printf("Reading VIN and vehicle details...\n");
    char vin[18] = {0};
    bool vin_success = false;

    // Method 1: OBD-II Mode 9 PID 02 on 0x7DF -> 0x7E8
    uint8_t req_obd[] = {0x09, 0x02};
    esp_err_t err = uds_transaction_dual(0x7DF, 0x7E8, req_obd, sizeof(req_obd), 400);
    if (err == ESP_OK && rx_len >= 20 && rx_buf[0] == 0x49 && rx_buf[1] == 0x02) {
        memcpy(vin, &rx_buf[3], 17);
        vin[17] = '\0';
        vin_success = true;
    }

    // Method 2: UDS Service 22 DID F190 on Engine 0x7E0 -> 0x7E8
    if (!vin_success) {
        uint8_t req_uds[] = {0x22, 0xF1, 0x90};
        err = uds_transaction_dual(0x7E0, 0x7E8, req_uds, sizeof(req_uds), 400);
        if (err == ESP_OK && rx_len >= 20 && rx_buf[0] == 0x62 && rx_buf[1] == 0xF1 && rx_buf[2] == 0x90) {
            memcpy(vin, &rx_buf[3], 17);
            vin[17] = '\0';
            vin_success = true;
        }
    }

    // Method 3: UDS Service 22 DID F190 on Gateway 0x710 -> 0x77A
    if (!vin_success) {
        uint8_t req_uds[] = {0x22, 0xF1, 0x90};
        err = uds_transaction_dual(0x710, 0x77A, req_uds, sizeof(req_uds), 400);
        if (err == ESP_OK && rx_len >= 20 && rx_buf[0] == 0x62 && rx_buf[1] == 0xF1 && rx_buf[2] == 0x90) {
            memcpy(vin, &rx_buf[3], 17);
            vin[17] = '\0';
            vin_success = true;
        }
    }

    // Method 4: UDS Service 22 DID F190 on Instrument Cluster 0x714 -> 0x77E
    if (!vin_success) {
        uint8_t req_uds[] = {0x22, 0xF1, 0x90};
        err = uds_transaction_dual(0x714, 0x77E, req_uds, sizeof(req_uds), 400);
        if (err == ESP_OK && rx_len >= 20 && rx_buf[0] == 0x62 && rx_buf[1] == 0xF1 && rx_buf[2] == 0x90) {
            memcpy(vin, &rx_buf[3], 17);
            vin[17] = '\0';
            vin_success = true;
        }
    }

    if (vin_success) {
        tester_log_printf("  VIN:          %s\n", vin);
        tester_log_printf("  Manufacturer: %s\n", decode_manufacturer(vin));
        tester_log_printf("  Model Year:   %s (%c)\n\n", decode_model_year(vin[9]), vin[9]);
    } else {
        tester_log_printf("  VIN:          Unable to read (No response)\n\n");
    }

    tester_log_printf("Scanning for active modules...\n");

    s_scan_performed = true;
    memset(s_module_active, 0, sizeof(s_module_active));
    memset(s_module_is_uds, 0, sizeof(s_module_is_uds));
    int active_count = 0;

    for (int i = 0; i < NUM_SCAN_TARGETS; i++) {
        tester_progress = (i * 40) / NUM_SCAN_TARGETS; // Update progress bar up to 40%

        // Ping Method 1: UDS Tester Present (02 3E 00)
        uint8_t ping[] = {0x3E, 0x00};
        err = uds_transaction_dual(scan_targets[i].req_id, scan_targets[i].resp_id, ping, sizeof(ping), 60);
        if (err == ESP_OK) {
            s_module_active[i] = true;
            s_module_is_uds[i] = true;
            active_count++;
            tester_log_printf("  * [%02X] %s (UDS)\n", scan_targets[i].addr, scan_targets[i].name);
        } else {
            // Ping Method 2: TP2.0 setup ping
            if (ping_tp20_module(scan_targets[i].addr)) {
                s_module_active[i] = true;
                s_module_is_uds[i] = false;
                active_count++;
                tester_log_printf("  * [%02X] %s (TP2.0)\n", scan_targets[i].addr, scan_targets[i].name);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }

    if (active_count == 0) {
        tester_log_printf("  No active modules detected.\n\n");
    } else {
        tester_log_printf("  Total detected modules: %d\n\n", active_count);
    }

    tester_log_printf("Scanning for fault codes in detected modules...\n");
    int faults_found_total = 0;

    for (int i = 0; i < NUM_SCAN_TARGETS; i++) {
        tester_progress = 40 + ((i * 60) / NUM_SCAN_TARGETS); // Remaining 60% for fault scan

        if (!s_module_active[i]) {
            continue; // Skip inactive modules
        }

        tester_log_printf("Checking [%02X] %s...\n", scan_targets[i].addr, scan_targets[i].name);

        if (s_module_is_uds[i]) {
            // UDS: Read DTCs by Status Mask (Service 0x19, Subfunction 0x02, Mask 0x0C)
            uint8_t req[] = {0x19, 0x02, 0x0C};
            err = uds_transaction_dual(scan_targets[i].req_id, scan_targets[i].resp_id, req, sizeof(req), 150);
            if (err == ESP_OK) {
                if (rx_len >= 3 && rx_buf[0] == 0x59 && rx_buf[1] == 0x02) {
                    uint16_t num_bytes = rx_len - 3;
                    uint16_t num_dtcs = num_bytes / 4;
                    
                    if (num_dtcs == 0) {
                        tester_log_printf(" -> Status: OK (0 Faults)\n\n");
                    } else {
                        tester_log_printf(" -> ⚠️ FOUND %d FAULT(S):\n", num_dtcs);
                        faults_found_total += num_dtcs;

                        for (int d = 0; d < num_dtcs; d++) {
                            uint16_t offset = 3 + (d * 4);
                            uint16_t dtc_code = (rx_buf[offset] << 8) | rx_buf[offset + 1];
                            uint8_t ftb = rx_buf[offset + 2];
                            uint8_t status = rx_buf[offset + 3];

                            char code_str[32];
                            get_obd_code_string(dtc_code, code_str, sizeof(code_str));

                            char desc_str[128];
                            lookup_dtc_description(dtc_code, desc_str, sizeof(desc_str));

                            tester_log_printf("    * %s: %s (Symptom: %02X, Status: %02X)\n", 
                                              code_str, desc_str, ftb, status);
                        }
                        tester_log_printf("\n");
                    }
                } else {
                    tester_log_printf(" -> Error: Invalid response format received.\n\n");
                }
            } else {
                tester_log_printf(" -> Error: Read DTC failed or timed out.\n\n");
            }
        } else {
            // TP2.0 / KWP2000 DTC Scan
            uint8_t kwp_resp[128];
            uint16_t kwp_len = 0;
            err = diagnostic_poll_read_kwp_dtcs(scan_targets[i].addr, kwp_resp, &kwp_len);
            if (err == ESP_OK) {
                if (kwp_len >= 2 && kwp_resp[0] == 0x58) {
                    uint8_t num_dtcs = kwp_resp[1];
                    if (num_dtcs == 0) {
                        tester_log_printf(" -> Status: OK (0 Faults)\n\n");
                    } else {
                        tester_log_printf(" -> ⚠️ FOUND %d FAULT(S):\n", num_dtcs);
                        faults_found_total += num_dtcs;

                        for (int d = 0; d < num_dtcs; d++) {
                            uint16_t offset = 2 + (d * 3);
                            if (offset + 2 >= kwp_len) break;
                            
                            uint16_t dtc_code = (kwp_resp[offset] << 8) | kwp_resp[offset + 1];
                            uint8_t status = kwp_resp[offset + 2];

                            char code_str[32];
                            get_obd_code_string(dtc_code, code_str, sizeof(code_str));

                            char desc_str[128];
                            lookup_dtc_description(dtc_code, desc_str, sizeof(desc_str));

                            tester_log_printf("    * %s: %s (Status: %02X)\n", 
                                              code_str, desc_str, status);
                        }
                        tester_log_printf("\n");
                    }
                } else {
                    tester_log_printf(" -> Error: Invalid KWP2000 response format.\n\n");
                }
            } else {
                tester_log_printf(" -> Error: TP2.0 DTC read failed (Err %s).\n\n", esp_err_to_name(err));
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(80));
    }

    tester_progress = 100;
    tester_state = TESTER_FINISHED;
    tester_log_printf("---------------------------------\n");
    tester_log_printf("Scan completed. Total faults found: %d\n", faults_found_total);
    tester_log_printf("---------------------------------\n");

    // Send full diagnostic report to Telegram
    telegram_send_message(tester_logs);
    ESP_LOGI(TAG, "Diagnostic scan report sent to Telegram");

    tester_task_handle = NULL;
    vTaskDelete(NULL);
}

// --- Active Clear Background Worker ---
static void clear_worker_task(void *pvParameters) {
    tester_state = TESTER_CLEARING;
    tester_progress = 0;
    tester_logs[0] = '\0';

    tester_log_printf("=================================\n");
    tester_log_printf("  VAG DIAGNOSTIC FAULT CLEARING\n");
    tester_log_printf("=================================\n\n");

    for (int i = 0; i < NUM_SCAN_TARGETS; i++) {
        const diag_target_t *target = &scan_targets[i];
        tester_log_printf("Clearing DTCs in [%02X] %s...\n", target->addr, target->name);

        tester_progress = (i * 100) / NUM_SCAN_TARGETS;

        bool active = false;
        bool is_uds = false;

        if (s_scan_performed) {
            active = s_module_active[i];
            is_uds = s_module_is_uds[i];
        } else {
            // Dynamic check fallback if scan was not run first
            uint8_t ping[] = {0x3E, 0x00};
            esp_err_t err = uds_transaction_dual(target->req_id, target->resp_id, ping, sizeof(ping), 60);
            if (err == ESP_OK) {
                active = true;
                is_uds = true;
            } else if (ping_tp20_module(target->addr)) {
                active = true;
                is_uds = false;
            }
        }

        if (active) {
            if (is_uds) {
                // UDS active: Clear DTCs (Service 0x14, Group All 0xFF 0xFF 0xFF)
                uint8_t req[] = {0x14, 0xFF, 0xFF, 0xFF};
                esp_err_t err = uds_transaction_dual(target->req_id, target->resp_id, req, sizeof(req), 150);
                if (err == ESP_OK) {
                    if (rx_len >= 1 && rx_buf[0] == 0x54) {
                        tester_log_printf(" -> Status: Faults cleared successfully!\n\n");
                    } else {
                        tester_log_printf(" -> Status: Negative response or clearing error.\n\n");
                    }
                } else {
                    tester_log_printf(" -> Status: Clear DTCs failed or timed out.\n\n");
                }
            } else {
                // TP2.0 / KWP2000 active: Clear DTCs (Service 0x14, option 0xFF 0x00)
                esp_err_t err = diagnostic_poll_clear_kwp_dtcs(target->addr);
                if (err == ESP_OK) {
                    tester_log_printf(" -> Status: Faults cleared successfully!\n\n");
                } else {
                    tester_log_printf(" -> Status: TP2.0 Clear DTCs failed (Err %s).\n\n", esp_err_to_name(err));
                }
            }
        } else {
            tester_log_printf(" -> Status: No response (Module not present)\n\n");
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    tester_progress = 100;
    tester_state = TESTER_FINISHED;
    tester_log_printf("---------------------------------\n");
    tester_log_printf("Fault clearing process completed.\n");
    tester_log_printf("---------------------------------\n");

    // Send clearing report to Telegram
    telegram_send_message(tester_logs);
    ESP_LOGI(TAG, "Fault clearing report sent to Telegram");

    tester_task_handle = NULL;
    vTaskDelete(NULL);
}

// --- Public APIs ---

void diagnostic_tester_init(void) {
    tester_state = TESTER_IDLE;
    tester_progress = 0;
    tester_logs[0] = '\0';
    tester_task_handle = NULL;
}

void diagnostic_tester_start_scan(void) {
    if (tester_state == TESTER_SCANNING || tester_state == TESTER_CLEARING) {
        ESP_LOGE(TAG, "Cannot start scan: Tester task is busy.");
        return;
    }
    xTaskCreate(scanner_worker_task, "scanner_worker", 4096, NULL, 4, &tester_task_handle);
}

void diagnostic_tester_start_clear(void) {
    if (tester_state == TESTER_SCANNING || tester_state == TESTER_CLEARING) {
        ESP_LOGE(TAG, "Cannot start DTC clear: Tester task is busy.");
        return;
    }
    xTaskCreate(clear_worker_task, "clear_worker", 4096, NULL, 4, &tester_task_handle);
}

tester_state_t diagnostic_tester_get_state(void) {
    return tester_state;
}

int diagnostic_tester_get_progress(void) {
    return tester_progress;
}

const char* diagnostic_tester_get_logs(void) {
    return tester_logs;
}

void diagnostic_tester_clear_logs(void) {
    tester_logs[0] = '\0';
}

bool diagnostic_tester_handle_rx(const twai_message_t *msg) {
    if (!msg) return false;
    if (tester_state != TESTER_SCANNING && tester_state != TESTER_CLEARING) return false;
    if (msg->identifier != expected_resp_id) return false;

    if (msg->data_length_code == 0) return false;

    uint8_t type = msg->data[0] & 0xF0;
    if (type == 0x00) { // Single Frame
        uint8_t len = msg->data[0] & 0x0F;
        if (len > 7) len = 7;
        rx_len = len;
        memcpy(rx_buf, &msg->data[1], len);
        rx_success = true;
        if (tester_task_handle) {
            xTaskNotifyGive(tester_task_handle);
        }
    } 
    else if (type == 0x10) { // First Frame
        rx_len = ((msg->data[0] & 0x0F) << 8) | msg->data[1];
        if (rx_len > sizeof(rx_buf)) rx_len = sizeof(rx_buf);
        memcpy(rx_buf, &msg->data[2], 6);
        rx_idx = 6;
        rx_seq = 1;
        rx_in_progress = true;
        rx_success = false;

        // Send Flow Control frame
        twai_message_t fc;
        uint32_t fc_id = expected_req_id;
        if (expected_req_id == 0x7DF) {
            if (msg->identifier >= 0x7E8 && msg->identifier <= 0x7EF) {
                fc_id = msg->identifier - 8;
            } else if (msg->identifier >= 0x700 && msg->identifier <= 0x7FF) {
                fc_id = msg->identifier - 0x6A;
            }
        }
        fc.identifier = fc_id;
        fc.extd = 0;
        fc.rtr = 0;
        fc.data_length_code = 8;
        memset(fc.data, 0, 8);
        fc.data[0] = 0x30; // Continue-to-send
        fc.data[1] = 0x00; // Block size = 0
        fc.data[2] = 0x00; // STmin = 0
        twai_transmit_v2(s_tester_bus_handle, &fc, pdMS_TO_TICKS(10));
    } 
    else if (type == 0x20) { // Consecutive Frame
        if (rx_in_progress) {
            uint8_t seq = msg->data[0] & 0x0F;
            if (seq == rx_seq) {
                uint16_t rem = rx_len - rx_idx;
                uint16_t chunk = (rem > 7) ? 7 : rem;
                memcpy(&rx_buf[rx_idx], &msg->data[1], chunk);
                rx_idx += chunk;
                rx_seq = (rx_seq + 1) & 0x0F;

                if (rx_idx >= rx_len) {
                    rx_in_progress = false;
                    rx_success = true;
                    if (tester_task_handle) {
                        xTaskNotifyGive(tester_task_handle);
                    }
                }
            } else {
                rx_in_progress = false;
                rx_success = false;
                ESP_LOGE(TAG, "Consecutive frame sequence error. Expected %d, got %d", rx_seq, seq);
            }
        }
    }
    return true;
}
