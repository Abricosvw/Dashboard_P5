#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Initialize the AI Manager (Allocates resources)
 * @return ESP_OK on success
 */
esp_err_t ai_manager_init(void);

/**
 * @brief Start the AI Manager (Connects to Server)
 * @return ESP_OK on success
 */
esp_err_t ai_manager_start(void);

/**
 * @brief Manually trigger listening mode (e.g. from Button)
 */
void ai_manager_trigger_listening(void);

/**
 * @brief Enable or disable voice wake-word activation
 */
void ai_manager_set_voice_activation(bool enabled);
