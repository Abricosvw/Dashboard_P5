#include "wifi_init.h"
#include "ai_manager.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "esp_wifi_remote.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "web_server.h"
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define SLAVE_RESET_IO 54

static const char *TAG = "WIFI_INIT";
static bool s_wifi_initialized = false;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static int s_retry_num = 0;
#define MAX_STA_RETRY 5

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
    wifi_event_ap_staconnected_t *event =
        (wifi_event_ap_staconnected_t *)event_data;
    ESP_LOGI(TAG, "station " MACSTR " join, AID=%d", MAC2STR(event->mac),
             event->aid);
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_AP_STADISCONNECTED) {
    wifi_event_ap_stadisconnected_t *event =
        (wifi_event_ap_stadisconnected_t *)event_data;
    ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d", MAC2STR(event->mac),
             event->aid);
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    s_retry_num = 0;
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < MAX_STA_RETRY) {
      ESP_LOGI(TAG, "Disconnected from AP, retrying (%d/%d)...",
               s_retry_num + 1, MAX_STA_RETRY);
      esp_wifi_connect();
      s_retry_num++;
    } else {
      ESP_LOGW(TAG, "Failed to connect to AP after %d attempts", MAX_STA_RETRY);
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    s_retry_num = 0;
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

    // Start SNTP for time sync (required for SSL)
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    // Start a task to wait for time sync before starting AI
    extern void ai_start_after_time_sync(void *pvParameters);
    xTaskCreate(ai_start_after_time_sync, "ai_sync_task", 4096, NULL, 5, NULL);

    // Ensure Web Server is running
    web_server_start();
  }
}

static esp_err_t wifi_base_init(void) {
  if (s_wifi_initialized) {
    return ESP_OK;
  }

  static bool s_base_in_progress = false;
  if (s_base_in_progress) {
    return ESP_OK; // Someone else is already doing this
  }
  s_base_in_progress = true;

  ESP_LOGI(TAG, "Initializing WiFi base (NVS, Netif, Event Loop)...");

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ret = esp_netif_init();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(ret);
  }

  ret = esp_event_loop_create_default();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(ret);
  }

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ret = esp_wifi_init(&cfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_init failed: %s (Check Slave Connection)",
             esp_err_to_name(ret));
    s_base_in_progress = false;
    return ret;
  }

  ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            &wifi_event_handler, NULL, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register IP event handler: %s",
             esp_err_to_name(ret));
    s_base_in_progress = false;
    return ret;
  }

  // Create netifs early and correctly for remote wifi
  if (s_sta_netif == NULL) {
    s_sta_netif = esp_netif_create_default_wifi_sta();
    ESP_LOGI(TAG, "STA netif created: %p", s_sta_netif);
  }
  if (s_ap_netif == NULL) {
    s_ap_netif = esp_netif_create_default_wifi_ap();
    ESP_LOGI(TAG, "AP netif created: %p", s_ap_netif);
  }

  s_wifi_initialized = true;
  s_base_in_progress = false;
  return ESP_OK;
}

esp_err_t wifi_init_ap(const char *ssid, const char *pass) {
  wifi_base_init();

  wifi_config_t wifi_config = {
      .ap =
          {
              .max_connection = 4,
              .authmode = WIFI_AUTH_WPA2_PSK,
              .channel = 1,
          },
  };
  strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid));
  strncpy((char *)wifi_config.ap.password, pass,
          sizeof(wifi_config.ap.password));
  wifi_config.ap.ssid_len = strlen(ssid);

  if (strlen(pass) == 0) {
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
  }

  esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
  if (err != ESP_OK)
    return err;
  err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
  if (err != ESP_OK)
    return err;
  err = esp_wifi_start();
  if (err != ESP_OK)
    return err;

  return ESP_OK;
}

esp_err_t wifi_init_sta(const char *ssid, const char *pass) {
  wifi_base_init();

  wifi_config_t wifi_config = {
      .sta =
          {
              .threshold.authmode = WIFI_AUTH_WPA2_PSK,
              .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
          },
  };

  strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
  strncpy((char *)wifi_config.sta.password, pass,
          sizeof(wifi_config.sta.password));

  esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK)
    return err;
  err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  if (err != ESP_OK)
    return err;
  err = esp_wifi_start();
  if (err != ESP_OK)
    return err;
  err = esp_wifi_connect();
  if (err != ESP_OK)
    return err;

  return ESP_OK;
}

esp_err_t wifi_init_apsta(const char *ap_ssid, const char *ap_pass,
                          const char *sta_ssid, const char *sta_pass) {
  wifi_base_init();

  ESP_LOGI(TAG, "Initializing WiFi in AP+STA mode...");
  ESP_LOGI(TAG, "  AP SSID: %s", ap_ssid);
  ESP_LOGI(TAG, "  STA SSID: %s", sta_ssid);

  // Configure AP mode
  wifi_config_t ap_config = {
      .ap =
          {
              .max_connection = 4,
              .authmode = WIFI_AUTH_OPEN,
              .channel = 1,
          },
  };
  strncpy((char *)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid));
  strncpy((char *)ap_config.ap.password, ap_pass,
          sizeof(ap_config.ap.password));
  ap_config.ap.ssid_len = strlen(ap_ssid);
  if (strlen(ap_pass) > 0) {
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
  }

  // Configure STA mode
  wifi_config_t sta_config = {
      .sta =
          {
              .threshold.authmode = WIFI_AUTH_WPA2_PSK,
              .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
          },
  };
  strncpy((char *)sta_config.sta.ssid, sta_ssid, sizeof(sta_config.sta.ssid));
  strncpy((char *)sta_config.sta.password, sta_pass,
          sizeof(sta_config.sta.password));

  // Set AP+STA mode
  esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set APSTA mode: %s", esp_err_to_name(err));
    return err;
  }

  // Configure both interfaces
  err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(err));
    return err;
  }

  err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set STA config: %s", esp_err_to_name(err));
    return err;
  }

  // Start WiFi
  err = esp_wifi_start();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
    return err;
  }

  // Connect STA to router
  err = esp_wifi_connect();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "STA connect failed (will retry): %s", esp_err_to_name(err));
    // Don't return error - AP still works
  }

  ESP_LOGI(TAG, "WiFi AP+STA mode initialized successfully!");
  ESP_LOGI(TAG, "  AP IP: 192.168.4.1");

  return ESP_OK;
}

esp_err_t wifi_scan(wifi_ap_record_t *ap_info, uint16_t *number) {
  wifi_base_init();

  // Get current mode and preserve it (don't switch to STA which breaks APSTA)
  wifi_mode_t current_mode;
  esp_err_t err = esp_wifi_get_mode(&current_mode);
  if (err != ESP_OK) {
    // WiFi not started yet, set to STA for scanning
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK)
      return err;
    err = esp_wifi_start();
    if (err != ESP_OK)
      return err;
  }
  // If already in APSTA or STA mode, scanning will work without mode change

  wifi_scan_config_t scan_config = {
      .ssid = NULL,
      .bssid = NULL,
      .channel = 0,
      .show_hidden = false,
      .scan_type = WIFI_SCAN_TYPE_ACTIVE,
      .scan_time.active.min = 100,
      .scan_time.active.max = 200,
  };

  ESP_LOGI(TAG, "Starting WiFi scan...");
  esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(ret));
    return ret;
  }

  uint16_t ap_count = 0;
  ret = esp_wifi_scan_get_ap_num(&ap_count);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get AP count: %s", esp_err_to_name(ret));
    return ret;
  }

  if (ap_count > *number) {
    ap_count = *number;
  }

  ret = esp_wifi_scan_get_ap_records(&ap_count, ap_info);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get AP records: %s", esp_err_to_name(ret));
    return ret;
  }
  *number = ap_count;

  ESP_LOGI(TAG, "Scan finished. Found %d APs", ap_count);
  return ESP_OK;
}

void ai_start_after_time_sync(void *pvParameters) {
  ESP_LOGI(TAG, "Waiting for system time to be set...");
  int retry = 0;
  const int retry_count = 10;
  while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET &&
         ++retry < retry_count) {
    ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry,
             retry_count);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }

  if (retry < retry_count) {
    ESP_LOGI(TAG, "Time synced successfully");
  } else {
    ESP_LOGW(TAG, "Time sync timeout, proceeding anyway (SSL might fail)");
  }

  ai_manager_init();
  ai_manager_start();
  vTaskDelete(NULL);
}
