#include "audio_manager.h"
#include "background_task.h"
#include "can_manager.h"
#include "display_init.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "main_gui.h"
#include "nvs_flash.h"
#include "sd_card_manager.h"
#include "settings_manager.h"
#include "ui/settings_config.h"
#include "wifi_controller.h"
#include "wifi_init.h"
#include <dirent.h>

static const char *TAG = "MAIN";

// WIFI Credentials
#define WIFI_SSID "ESP32P4_Dashboard"
#define WIFI_PASS "12345678"

// Shared I2C Bus Handle (Used by display, touch, audio)
i2c_master_bus_handle_t i2c1_bus = NULL;

// Helper to list files on SD card
static void list_sd_files(const char *path) {
  DIR *dir = opendir(path);
  if (!dir) {
    ESP_LOGW(TAG, "Cannot list %s", path);
    return;
  }
  ESP_LOGI(TAG, "Files in %s:", path);
  struct dirent *de;
  while ((de = readdir(dir)) != NULL) {
    ESP_LOGI(TAG, "  %s", de->d_name);
  }
  closedir(dir);
}

void app_main(void) {
  ESP_LOGI(TAG, "=== Dashboard_P4 Starting ===");

  // Initialize NVS first (needed for WiFi and other settings)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_LOGI(TAG, "Initial Heap: Internal [%d] bytes, SPIRAM [%d] bytes",
           heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  ESP_LOGI(TAG, "[1] Power initialization...");
  board_init_power();
  vTaskDelay(pdMS_TO_TICKS(100));

  // =========================================================================
  // PHASE 2: I2C BUS & RESET - ~150ms
  // =========================================================================
  ESP_LOGI(TAG, "[2] I2C Bus init (SDA=%d, SCL=%d)...", LCD_I2C_SDA_IO,
           LCD_I2C_SCL_IO);
  i2c_master_bus_config_t i2c_bus_config = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .i2c_port = I2C_NUM_0,
      .scl_io_num = LCD_I2C_SCL_IO,
      .sda_io_num = LCD_I2C_SDA_IO,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };
  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &i2c1_bus));

  // Backlight driver (Touch moved later)
  ESP_LOGI(TAG, "[2.2] Backlight init...");
  board_init_backlight(i2c1_bus);
  board_set_backlight(80);

  // =========================================================================
  // PHASE 3: SD CARD (Settings) - ~100ms
  // =========================================================================
  ESP_LOGI(TAG, "[3] SD Card init...");
  if (sd_card_init() == ESP_OK) {
    list_sd_files("/sdcard");
    app_settings_init();

    ESP_LOGI(TAG, "Loading UI settings from SD card...");
    settings_load();
    settings_apply_changes();

    ESP_LOGI(TAG, "Settings applied immediately from SD card");
  } else {
    ESP_LOGW(TAG, "SD Card not available, loading default UI settings");
    settings_init_defaults(NULL); // Load defaults if SD is missing
    app_settings_init();          // Still init app settings defaults
  }

  // Initialize background task worker for async settings saves
  ESP_LOGI(TAG, "[3.1] Background task init...");
  background_task_init();

  // =========================================================================
  // PHASE 4: DISPLAY/GUI (Visuals) - ~1500ms
  // =========================================================================
  ESP_LOGI(TAG, "[4] Display init (MIPI DSI)...");
  esp_lcd_panel_handle_t panel = NULL;
  esp_lcd_panel_io_handle_t io = NULL;
  if (board_init_display(&panel, &io) == ESP_OK) {
    // Initialize Touch AFTER display to avoid interference
    esp_lcd_touch_handle_t touch_handle = NULL;
    board_init_touch(i2c1_bus, &touch_handle);

    ESP_LOGI(TAG, "[4.1] GUI init...");
    main_gui_init(panel, io, touch_handle);
  } else {
    ESP_LOGE(TAG, "Display init failed!");
  }

  // =========================================================================
  // PHASE 5: AUDIO (Can play sounds from SD) - ~200ms
  // =========================================================================
  ESP_LOGI(TAG, "[5] Audio init (ES8311)...");
  if (audio_init() == ESP_OK) {
    ESP_LOGI(TAG, "Setting volume to 50%%...");
    audio_set_volume(50);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "Playing startup sound from SD card (%s)...",
             settings_get_boot_sound_path());
    audio_play_wav(settings_get_boot_sound_path());
  } else {
    ESP_LOGE(TAG, "Audio initialization failed!");
  }

  // =========================================================================
  // PHASE 6: WIFI (AP Mode + Web Server)
  // =========================================================================
  ESP_LOGI(TAG,
           "[6] WiFi Access Point init (delayed 6s to allow audio buffer)...");
  vTaskDelay(pdMS_TO_TICKS(6000));
  wifi_controller_init();

  // =========================================================================
  // PHASE 7: APPLICATION SERVICES
  // =========================================================================
  ESP_LOGI(TAG, "[7] CAN Bus init - DISABLED FOR DEBUGGING...");
  // can_init();

  ESP_LOGI(TAG, "=== System Ready! ===");

  // Main loop
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
