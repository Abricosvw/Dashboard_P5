#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Telegram Manager
 * Creates the long-polling background task if configured.
 * @return ESP_OK on success
 */
esp_err_t telegram_init(void);

/**
 * @brief Send a message to the configured Telegram chat
 * @param msg The text message to send
 */
void telegram_send_message(const char *msg);

#ifdef __cplusplus
}
#endif
