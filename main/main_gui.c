#include "main_gui.h"
#include "display_init.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "ui/ui.h"

static const char *TAG = "MAIN_GUI";

// LVGL lock function for thread-safe access
bool example_lvgl_lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }

// LVGL unlock function
void example_lvgl_unlock(void) { lvgl_port_unlock(); }

esp_err_t main_gui_init(esp_lcd_panel_handle_t panel_handle,
                        esp_lcd_panel_io_handle_t io_handle,
                        esp_lcd_touch_handle_t touch_handle) {
  ESP_LOGI(TAG, "Initializing GUI...");

  // 1. Initialize LVGL port (creates LVGL task and mutex)
  ESP_LOGI(TAG, "  Initializing LVGL port...");
  const lvgl_port_cfg_t lvgl_cfg = {
      .task_priority = 5,
      .task_stack = 65536,
      .task_affinity = 1,
      .task_max_sleep_ms = 500,
      .timer_period_ms = 5,
  };
  esp_err_t ret = lvgl_port_init(&lvgl_cfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "LVGL port init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  // Display configuration for 720x1280 Portrait
  ESP_LOGI(TAG, "  Connecting DSI display to LVGL (720x1280)...");

  const lvgl_port_display_cfg_t disp_cfg = {
      .io_handle = io_handle,
      .panel_handle = panel_handle,
      .buffer_size = 736 * 320, // MUST MATCH HARDWARE STRIDE (736)
      // Single buffer to prevent artifacts
      .double_buffer = false,
      .trans_size = 0,
      .hres = 736, // MUST BE 736 FOR CORRECT MEMORY ALIGNMENT
      .vres = 1280,
      .monochrome = false,
      .flags =
          {
              .buff_dma = false,
              .buff_spiram = true,
              .sw_rotate = false,
          },
  };

  const lvgl_port_display_dsi_cfg_t dsi_cfg = {
      .flags =
          {
              // avoid_tearing=true causes WDT timeout in wait_for_flushing
              .avoid_tearing = false,
          },
  };

  lv_disp_t *disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
  if (disp == NULL) {
    ESP_LOGE(TAG, "Failed to add DSI display to LVGL");
    return ESP_FAIL;
  }

  // 3. Native Portrait mode - 736x1280

  // 4. Add touch input to LVGL
  if (touch_handle) {
    ESP_LOGI(TAG, "  Adding touch input to LVGL...");
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = touch_handle,
        .scale =
            {
                .x = 1.0f,
                .y = 1.0f,
            },
    };
    lv_indev_t *indev = lvgl_port_add_touch(&touch_cfg);
    if (indev) {
      ESP_LOGI(TAG, "  Touch input device added successfully");
      if (lvgl_port_lock(1000)) {
        // Indev events are not supported in LVGL 8
        lvgl_port_unlock();
      }
    } else {
      ESP_LOGW(TAG, "  Failed to add touch input device");
    }
  }

  // 5. Initialize UI (with LVGL lock)
  ESP_LOGI(TAG, "  Initializing SquareLine UI...");
  if (lvgl_port_lock(1000)) {
    ui_init();
    lvgl_port_unlock();
    ESP_LOGI(TAG, "GUI initialized successfully in Native Portrait!");
    return ESP_OK;
  }

  ESP_LOGE(TAG, "Failed to acquire LVGL lock for UI init");
  return ESP_FAIL;
}
