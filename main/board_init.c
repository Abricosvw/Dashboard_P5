#include "display_init.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_ili9881c.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_gt9271.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BOARD_INIT";
static bool s_backlight_init_done = false;
static i2c_master_dev_handle_t s_bk_i2c_handle = NULL;
static esp_lcd_touch_handle_t s_touch_handle = NULL;

esp_err_t board_init_power(void) {
  ESP_LOGI(TAG, "Initializing Board Power (LDOs)...");

  // 1. Enable MIPI DSI PHY Power (LDO Channel 3, 2.5V)
  ESP_LOGI(TAG, "  Enabling MIPI DSI PHY power (LDO3: %dmV)...",
           LCD_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV);
  esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
  esp_ldo_channel_config_t ldo_mipi_phy_config = {
      .chan_id = LCD_MIPI_DSI_PHY_PWR_LDO_CHAN,          // Channel 3
      .voltage_mv = LCD_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV, // 2500mV
  };
  ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy));

  // 2. Enable Peripheral Power (LDO Channel 4, 3.3V) - Needed for SD/Touch
  ESP_LOGI(TAG, "  Enabling Peripheral power (LDO4: %dmV)...",
           LCD_PERIPH_PWR_LDO_VOLTAGE_MV);
  esp_ldo_channel_handle_t ldo_periph = NULL;
  esp_ldo_channel_config_t ldo_periph_config = {
      .chan_id = LCD_PERIPH_PWR_LDO_CHAN,          // Channel 4
      .voltage_mv = LCD_PERIPH_PWR_LDO_VOLTAGE_MV, // 3300mV
  };
  ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_periph_config, &ldo_periph));

  vTaskDelay(pdMS_TO_TICKS(300)); // Increased wait for power to stabilize
  return ESP_OK;
}

esp_err_t board_init_backlight(i2c_master_bus_handle_t bus_handle) {
  if (bus_handle == NULL) {
    ESP_LOGE(TAG, "I2C bus handle is NULL");
    return ESP_ERR_INVALID_ARG;
  }

  i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = LCD_BK_I2C_ADDR,
      .scl_speed_hz = 400000,
  };

  ESP_RETURN_ON_ERROR(
      i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_bk_i2c_handle), TAG,
      "Failed to add backlight I2C device");
  s_backlight_init_done = true;
  ESP_LOGI(TAG, "Backlight I2C device added at address 0x%02X",
           LCD_BK_I2C_ADDR);
  return ESP_OK;
}

esp_err_t board_set_backlight(uint32_t level_percent) {
  if (level_percent > 100)
    level_percent = 100;

  // Map 0-100% to 0-255
  uint8_t duty = (uint8_t)((255 * level_percent) / 100);

  ESP_LOGI(TAG, "Setting backlight to %u%% (Val=%u)",
           (unsigned int)level_percent, (unsigned int)duty);

  if (!s_backlight_init_done || !s_bk_i2c_handle) {
    ESP_LOGE(TAG, "Backlight not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t write_buf[2] = {LCD_BK_I2C_REG, duty};
  ESP_RETURN_ON_ERROR(
      i2c_master_transmit(s_bk_i2c_handle, write_buf, sizeof(write_buf), -1),
      TAG, "Failed to write backlight brightness");

  return ESP_OK;
}

#include "esp_attr.h"
#include "esp_rom_sys.h"

static void IRAM_ATTR touch_callback(esp_lcd_touch_handle_t tp) {
  // Placeholder to ensure GPIO ISR is registered in the driver
  // lvgl_port will later override this via
  // esp_lcd_touch_register_interrupt_callback
}

esp_err_t board_init_touch(i2c_master_bus_handle_t bus_handle,
                           esp_lcd_touch_handle_t *ret_touch) {
  if (bus_handle == NULL) {
    ESP_LOGE(TAG, "I2C bus handle is NULL");
    return ESP_ERR_INVALID_ARG;
  }

  // Install GPIO ISR service (once)
  static bool s_isr_service_installed = false;
  if (!s_isr_service_installed) {
    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
      s_isr_service_installed = true;
    } else {
      ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s",
               esp_err_to_name(err));
    }
  }

  esp_lcd_panel_io_handle_t tp_io_handle = NULL;
  esp_lcd_panel_io_i2c_config_t tp_io_config =
      ESP_LCD_TOUCH_IO_I2C_GT9271_CONFIG();
  tp_io_config.scl_speed_hz = 400000;

  // Check if we need to set the address from Kconfig
  if (TOUCH_I2C_ADDR != 0) {
    tp_io_config.dev_addr = TOUCH_I2C_ADDR;
  }

  // Cast dev_addr to unsigned int to fix format string warning/error
  ESP_LOGI(TAG, "Initializing Touch IO at address 0x%02X...",
           (unsigned int)tp_io_config.dev_addr);
  ESP_RETURN_ON_ERROR(
      esp_lcd_new_panel_io_i2c(bus_handle, &tp_io_config, &tp_io_handle), TAG,
      "New Panel IO I2C failed");

  esp_lcd_touch_config_t tp_cfg = {
      .x_max = LCD_PHYS_H_RES,
      .y_max = LCD_PHYS_V_RES,
      .rst_gpio_num = TOUCH_RST_IO,
      .int_gpio_num = GPIO_NUM_NC, // Disable interrupt GPIO - use polling mode
      .levels =
          {
              .reset = 0,
              .interrupt = 0,
          },
      .flags =
          {
              // Let esp_lvgl_port handle coordinate transformation based on
              // display rotation. Do NOT apply transformations here to avoid
              // double-transformation causing touch misalignment.
              .swap_xy = 0,
              .mirror_x = 0,
              .mirror_y = 0,
          },
      .interrupt_callback = NULL, // NULL = polling mode for LVGL port
  };

  ESP_LOGI(TAG, "Initializing Touch Driver (GT9271)...");

  // Robust GT911/9271 Address Selection Strapping
  // Address 0x5D: INT LOW during RST rising edge
  gpio_config_t touch_reset_conf = {
      .pin_bit_mask = (1ULL << TOUCH_RST_IO) | (1ULL << TOUCH_INT_IO),
      .mode = GPIO_MODE_OUTPUT,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&touch_reset_conf);

  gpio_set_level(TOUCH_RST_IO, 0);
  gpio_set_level(TOUCH_INT_IO, 0); // Pull INT low for 0x5D
  vTaskDelay(pdMS_TO_TICKS(20));
  gpio_set_level(TOUCH_RST_IO, 1);
  vTaskDelay(pdMS_TO_TICKS(10));

  // Crucial: Set INT to input mode immediately after strapping
  gpio_config_t int_input_conf = {
      .pin_bit_mask = (1ULL << TOUCH_INT_IO),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&int_input_conf);
  vTaskDelay(pdMS_TO_TICKS(50)); // Wait for GT9271 to stabilize

  ESP_RETURN_ON_ERROR(
      esp_lcd_touch_new_i2c_gt9271(tp_io_handle, &tp_cfg, &s_touch_handle), TAG,
      "New Touch GT9271 failed");

  // Register debug callback
  // The callback is now registered via tp_cfg.interrupt_callback
  // esp_lcd_touch_register_interrupt_callback(s_touch_handle, touch_callback);

  ESP_LOGI(TAG, "Touch initialized successfully (GT9271)");

  // Manual read to clear any initial interrupt state
  esp_lcd_touch_read_data(s_touch_handle);

  if (ret_touch)
    *ret_touch = s_touch_handle;
  return ESP_OK;
}

esp_err_t board_init_display(esp_lcd_panel_handle_t *ret_panel,
                             esp_lcd_panel_io_handle_t *ret_io) {
  ESP_LOGI(TAG, "Initializing MIPI DSI bus (ILI9881C Driver)");

  // Feed watchdog before long operations
  vTaskDelay(pdMS_TO_TICKS(10));

  // 1. Create DSI Bus
  ESP_LOGI(TAG, "  [1/5] Creating DSI bus...");
  esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
  esp_lcd_dsi_bus_config_t bus_config = {
      .bus_id = 0,
      .num_data_lanes = 2,
      .phy_clk_src = 0,
      .lane_bit_rate_mbps = 800, // Balanced for 60Hz @ 16-bit
  };
  ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus), TAG,
                      "New DSI bus failed");
  vTaskDelay(pdMS_TO_TICKS(10));

  // 2. Create Panel IO
  ESP_LOGI(TAG, "  [2/5] Creating DBI panel IO...");
  esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;
  esp_lcd_dbi_io_config_t dbi_config = ILI9881C_PANEL_IO_DBI_CONFIG();
  ESP_RETURN_ON_ERROR(
      esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io), TAG,
      "New DSI IO failed");
  vTaskDelay(pdMS_TO_TICKS(10));

  // 3. Hardware Reset
  if (LCD_RST_IO >= 0) {
    ESP_LOGI(TAG, "  [3/5] Hardware reset (GPIO %d)...", LCD_RST_IO);
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LCD_RST_IO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);
    gpio_set_level(LCD_RST_IO, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(LCD_RST_IO, 1);
    vTaskDelay(pdMS_TO_TICKS(500)); // Increased wait for hardware sync
  }

  // 4. Configure DPI panel - 736px WIDTH FOR STRIDE ALIGNMENT
  ESP_LOGI(TAG, "  [4/5] Creating ILI9881C panel (736x1280 @ 50MHz)...");
  esp_lcd_dpi_panel_config_t dpi_config = {
      .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
      .dpi_clock_freq_mhz = 50, // Step 4: Lowered for transmission stability
      .virtual_channel = 0,
      .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
      .num_fbs = 2,
      .video_timing =
          {
              .h_size = 736, // Multiple of 64 bytes (736 * 2 = 1472)
              .v_size = 1280,
              .hsync_back_porch = 120, // Step 3: Keep 120
              .hsync_pulse_width = 80, // Step 5: Increased for line sync lock
              .hsync_front_porch = 80,
              .vsync_back_porch = 80,  // Step 2: Keep 80
              .vsync_pulse_width = 20, // Step 1: Keep 20
              .vsync_front_porch = 40,
          },
      .flags.use_dma2d = true, // REQUIRED for system stability on P4
  };

  ili9881c_vendor_config_t vendor_config = {
      .mipi_config =
          {
              .dsi_bus = mipi_dsi_bus,
              .dpi_config = &dpi_config,
              .lane_num = 2, // 2-lane MIPI DSI (ILI9881C)
          },
  };

  const esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = LCD_RST_IO,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .bits_per_pixel = 16, // STRICT 16-bit
      .vendor_config = &vendor_config,
  };

  esp_err_t ret =
      esp_lcd_new_panel_ili9881c(mipi_dbi_io, &panel_config, ret_panel);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create ILI9881C panel: %s", esp_err_to_name(ret));
    return ret;
  }
  *ret_io = mipi_dbi_io;
  vTaskDelay(pdMS_TO_TICKS(50));

  // 5. Initialize panel
  ESP_LOGI(TAG, "  [5/5] Initializing panel (may take a few seconds)...");

  ESP_LOGI(TAG, "    - Panel reset...");
  ret = esp_lcd_panel_reset(*ret_panel);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Panel reset failed: %s", esp_err_to_name(ret));
    return ret;
  }
  vTaskDelay(pdMS_TO_TICKS(200));

  ESP_LOGI(TAG, "    - Panel init (sending init commands)...");
  ret = esp_lcd_panel_init(*ret_panel);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Panel init failed: %s", esp_err_to_name(ret));
    return ret;
  }
  vTaskDelay(pdMS_TO_TICKS(100));

  ESP_LOGI(TAG, "Panel initialized successfully!");
  return ESP_OK;
}
