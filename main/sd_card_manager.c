#include "sd_card_manager.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "SD_CARD";
static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;

// Custom host init that handles already-initialized host (shared with WiFi
// SDIO)
static esp_err_t sd_host_init_shared(void) {
  esp_err_t ret = sdmmc_host_init();
  if (ret == ESP_ERR_INVALID_STATE) {
    // Host already initialized by esp_hosted (WiFi SDIO on Slot 1)
    ESP_LOGI(TAG, "SDMMC host already initialized (shared with WiFi SDIO)");
    return ESP_OK;
  }
  return ret;
}

esp_err_t sd_card_init(void) {
  if (s_mounted) {
    ESP_LOGW(TAG, "SD Card already mounted");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Initializing SD card");

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 10,
      .allocation_unit_size = 16 * 1024};

  ESP_LOGI(TAG, "Using SDMMC peripheral Slot 0 (SD card)");

  // Initialize host for Slot 0 (dedicated SD card slot)
  // WiFi SDIO uses Slot 1 via esp_hosted
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_0;
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;
  host.init = sd_host_init_shared; // Use custom init to handle shared host

#ifdef CONFIG_IDF_TARGET_ESP32P4
  host.flags |= SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF;
#endif

  // Slot 0 configuration with explicit GPIO pins per Waveshare docs
  sdmmc_slot_config_t slot_config = {
      .clk = SD_PIN_CLK, // GPIO 43
      .cmd = SD_PIN_CMD, // GPIO 44
      .d0 = SD_PIN_D0,   // GPIO 39
      .d1 = SD_PIN_D1,   // GPIO 40
      .d2 = SD_PIN_D2,   // GPIO 41
      .d3 = SD_PIN_D3,   // GPIO 42
      .cd = SDMMC_SLOT_NO_CD,
      .wp = SDMMC_SLOT_NO_WP,
      .width = 4,
      .flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP,
  };

  ESP_LOGI(TAG, "Mounting filesystem (CLK=%d, CMD=%d, D0=%d)", SD_PIN_CLK,
           SD_PIN_CMD, SD_PIN_D0);
  esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot_config,
                                          &mount_config, &s_card);

  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE(TAG, "Failed to mount filesystem.");
    } else {
      ESP_LOGE(TAG,
               "Failed to initialize the card (%s). "
               "Make sure SD card lines have pull-up resistors in place.",
               esp_err_to_name(ret));
    }
    return ret;
  }

  ESP_LOGI(TAG, "Filesystem mounted");
  sdmmc_card_print_info(stdout, s_card);
  s_mounted = true;

  return ESP_OK;
}

esp_err_t sd_card_deinit(void) {
  if (!s_mounted) {
    return ESP_OK;
  }

  esp_err_t ret = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Card unmounted");
    s_mounted = false;
    s_card = NULL;
  } else {
    ESP_LOGE(TAG, "Failed to unmount card (%s)", esp_err_to_name(ret));
  }
  return ret;
}

bool sd_card_is_mounted(void) { return s_mounted; }
