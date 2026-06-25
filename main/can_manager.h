#pragma once

#include "esp_err.h"
#include "driver/twai.h"
#include "ecu_data.h"

// TWAI Pin Definitions (User provided)
#define CAN_TX_IO           (20)
#define CAN_RX_IO           (21)

#define CAN2_TX_IO          (22)
#define CAN2_RX_IO          (23)

// CAN Baudrate (Defaulting to 500kbps, common for automotive)
#define CAN_BAUDRATE_KBPS   (500)

// Global TWAI Handles for v2 APIs
extern twai_handle_t g_can1_handle; // Powertrain CAN
extern twai_handle_t g_can2_handle; // Diagnostics CAN / OBD2

/**
 * @brief Initialize the CAN (TWAI) driver and start the reception task.
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t can_init(void);

/**
 * @brief Task responsible for receiving and parsing CAN1 frames (Powertrain).
 */
void can_rx_task(void *pvParameters);

/**
 * @brief Task responsible for receiving and parsing CAN2 frames (Diagnostics).
 */
void can2_rx_task(void *pvParameters);
