#include "can_manager.h"
#include "can_parser.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sd_card_manager.h"
#include "ui/screens/ui_Screen3.h" // Include Screen3 header
#include "lua_manager.h"
#include "diagnostic_manager.h"
#include "diagnostic_tester.h"
#include "diagnostic_poll.h"
#include <stdio.h>
#include <time.h>

// Extern LVGL lock functions (defined in main.c usually)
extern bool example_lvgl_lock(int timeout_ms);
extern void example_lvgl_unlock(void);

static const char *TAG = "CAN_MGR";

#include <string.h>

ecu_data_t g_ecu_data = {0};

twai_handle_t g_can1_handle = NULL;
twai_handle_t g_can2_handle = NULL;

static void check_and_recover_twai(twai_handle_t handle, const char *name) {
  if (handle == NULL) return;
  
  twai_status_info_t status;
  if (twai_get_status_info_v2(handle, &status) == ESP_OK) {
    static uint32_t last_log_time[2] = {0};
    int idx = (strcmp(name, "CAN1") == 0) ? 0 : 1;
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    if (status.state == TWAI_STATE_BUS_OFF || 
        status.tx_error_counter > 100 || 
        status.rx_error_counter > 100 || 
        (now - last_log_time[idx]) > 3000) {
      
      ESP_LOGI(TAG, "%s Status: State=%d, TX_Err=%lu, RX_Err=%lu, Bus_Err=%lu, Arb_Lost=%lu, RX_FIFO=%lu",
               name, (int)status.state, 
               (unsigned long)status.tx_error_counter, 
               (unsigned long)status.rx_error_counter, 
               (unsigned long)status.bus_error_count, 
               (unsigned long)status.arb_lost_count,
               (unsigned long)status.msgs_to_rx);
      last_log_time[idx] = now;
    }
    
    if (status.state == TWAI_STATE_BUS_OFF) {
      ESP_LOGW(TAG, "%s is BUS-OFF! Initiating recovery...", name);
      esp_err_t err = twai_initiate_recovery_v2(handle);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initiate %s recovery: %s", name, esp_err_to_name(err));
      }
    } else if (status.state == TWAI_STATE_STOPPED) {
      ESP_LOGI(TAG, "%s is STOPPED. Restarting controller...", name);
      esp_err_t err = twai_start_v2(handle);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start %s after recovery: %s", name, esp_err_to_name(err));
      }
    }
  }
}

esp_err_t can_init(void) {
  // 1. Initialize CAN1 (TWAI0) - Powertrain Sniffer
  twai_general_config_t g_config1 =
      TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_IO, CAN_RX_IO, TWAI_MODE_NORMAL);
  g_config1.controller_id = 0;
  twai_timing_config_t t_config1 = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f_config1 = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  ESP_RETURN_ON_ERROR(twai_driver_install_v2(&g_config1, &t_config1, &f_config1, &g_can1_handle), TAG,
                      "TWAI0 driver install failed");
  ESP_LOGI(TAG, "TWAI0 driver installed");

  ESP_RETURN_ON_ERROR(twai_start_v2(g_can1_handle), TAG, "TWAI0 driver start failed");
  ESP_LOGI(TAG, "TWAI0 driver started");

  // 2. Initialize CAN2 (TWAI1) - Diagnostics / OBD2
  twai_general_config_t g_config2 =
      TWAI_GENERAL_CONFIG_DEFAULT(CAN2_TX_IO, CAN2_RX_IO, TWAI_MODE_NORMAL);
  g_config2.controller_id = 1;
  twai_timing_config_t t_config2 = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f_config2 = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  ESP_RETURN_ON_ERROR(twai_driver_install_v2(&g_config2, &t_config2, &f_config2, &g_can2_handle), TAG,
                      "TWAI1 driver install failed");
  ESP_LOGI(TAG, "TWAI1 driver installed");

  ESP_RETURN_ON_ERROR(twai_start_v2(g_can2_handle), TAG, "TWAI1 driver start failed");
  ESP_LOGI(TAG, "TWAI1 driver started");

  // 3. Create CAN RX Tasks
  xTaskCreatePinnedToCore(can_rx_task, "can1_rx_task", 4096, NULL, 5, NULL, 0);
  xTaskCreatePinnedToCore(can2_rx_task, "can2_rx_task", 4096, NULL, 5, NULL, 0);

  return ESP_OK;
}

void can_rx_task(void *pvParameters) {
  twai_message_t message;
  uint32_t rx_frame_count = 0;
  uint32_t last_stats_time = 0;
  
  while (1) {
    check_and_recover_twai(g_can1_handle, "CAN1");
    if (twai_receive_v2(g_can1_handle, &message, pdMS_TO_TICKS(1000)) == ESP_OK) {
      rx_frame_count++;
      
      // Periodic stats every 10 seconds
      uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
      if ((now - last_stats_time) > 10000) {
        twai_status_info_t status = {0};
        twai_get_status_info_v2(g_can1_handle, &status);
        ESP_LOGI(TAG, "CAN1 RX Stats: %lu frames total, FIFO=%lu, TX_Err=%lu, RX_Err=%lu",
                 (unsigned long)rx_frame_count,
                 (unsigned long)status.msgs_to_rx,
                 (unsigned long)status.tx_error_counter,
                 (unsigned long)status.rx_error_counter);
        last_stats_time = now;
      }
      
      // Intercept with VAG Diagnostic manager (Server Emulation)
      if (diagnostic_manager_handle_rx(g_can1_handle, &message)) {
        continue;
      }

      // Process message using the powerful new parser
      parse_can_message(&message);

      // Dispatch to Lua script engine
      lua_manager_handle_can_rx(message.identifier, message.data, message.data_length_code);

      // Send to UI Sniffer / Logger
      // Must take LVGL lock because we are calling UI functions
      if (example_lvgl_lock(10)) {
        ui_process_real_can_message_bus(1, message.identifier, message.data,
                                        message.data_length_code, false);
        example_lvgl_unlock();
      }
    } else {
      // Log timeout only periodically to avoid spam
      uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
      if ((now - last_stats_time) > 10000) {
        ESP_LOGW(TAG, "CAN1 RX: No frames for 10s (total received: %lu) - check physical wiring",
                 (unsigned long)rx_frame_count);
        last_stats_time = now;
      }
      vTaskDelay(pdMS_TO_TICKS(100)); // Prevent tight spinning when stopped
    }
  }
}

void can2_rx_task(void *pvParameters) {
  twai_message_t message;
  diagnostic_manager_init();
  diagnostic_poll_init(); // Start the background diagnostic live data poller
  while (1) {
    check_and_recover_twai(g_can2_handle, "CAN2");
    if (twai_receive_v2(g_can2_handle, &message, pdMS_TO_TICKS(1000)) == ESP_OK) {
      // Intercept with Live Data Diagnostic Poller
      if (diagnostic_poll_handle_rx(&message)) {
        continue;
      }
      // Intercept with VAG Diagnostic manager (Server Emulation)
      if (diagnostic_manager_handle_rx(g_can2_handle, &message)) {
        continue;
      }
      // Intercept with VAG Diagnostic tester (DTC Scanner / Active Poller)
      if (diagnostic_tester_handle_rx(&message)) {
        continue;
      }

      // Forward to UI Sniffer / Logger
      if (example_lvgl_lock(10)) {
        ui_process_real_can_message_bus(2, message.identifier, message.data,
                                        message.data_length_code, false);
        example_lvgl_unlock();
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(100)); // Prevent tight spinning when stopped
    }
  }
}

void can_send_ambient_temp(float temp) {
  twai_message_t msg;
  msg.identifier = 0x420;
  msg.data_length_code = 8;
  msg.extd = 0;
  msg.rtr = 0;
  memset(msg.data, 0, 8);

  float raw_val = (temp + 50.0f) * 2.0f;
  if (raw_val < 0.0f) raw_val = 0.0f;
  if (raw_val > 254.0f) raw_val = 254.0f;
  msg.data[2] = (uint8_t)raw_val;

  esp_err_t err = twai_transmit_v2(g_can1_handle, &msg, pdMS_TO_TICKS(10));
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to transmit ambient temperature message: %s", esp_err_to_name(err));
  } else {
    ESP_LOGD(TAG, "Transmitted ambient temp %.1f C (raw %d)", temp, msg.data[2]);
  }
}
