#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

#include "esp_lcd_touch.h"

/**
 * @brief Initialize LVGL and the UI
 *
 * @param panel_handle The hande to the initialized LCD panel
 * @param io_handle The handle to the LCD panel IO
 * @param touch_handle The handle to the touch controller
 * @return esp_err_t ESP_OK on success
 */
esp_err_t main_gui_init(esp_lcd_panel_handle_t panel_handle,
                        esp_lcd_panel_io_handle_t io_handle,
                        esp_lcd_touch_handle_t touch_handle);

// LVGL Locking mechanism for FreeRTOS tasks
bool example_lvgl_lock(int timeout_ms);
void example_lvgl_unlock(void);
