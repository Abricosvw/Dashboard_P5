#include "wifi_controller.h"
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "wifi_fallbacks.h"
#include "wifi_init.h"
#include <nvs_flash.h>
#include <sys/param.h>

#if !CONFIG_IDF_TARGET_ESP32P4 || CONFIG_ESP_WIFI_REMOTE_ENABLED ||            \
    CONFIG_ESP_HOSTED_ENABLED
#include "esp_netif.h"
#endif
#include "cJSON.h"
#include "web_server.h"
#include "wifi_storage.h"

static const char *TAG = "WIFI_CONTROLLER";

// Global state protected by mutex (or simple atomic for now since it's one
// writer)
static game_controller_state_t current_state = {0};
static SemaphoreHandle_t state_mutex = NULL;
static volatile bool is_wifi_scanning =
    false; // Track scanning state to prevent crash

// Forward declaration
esp_err_t wifi_controller_connect_saved(void);

// Embedded HTML (we could also mount SPIFFS, but embedding is easier for single
// file) We will use the file we created: main/web/joystick.html To embed it, we
// need to add it to CMakeLists.txt as EMBED_TXTFILES For now, let's assume we
// can read it or hardcode it. Actually, standard ESP-IDF way is embedding
// binary. Let's declare the external symbols.
extern const uint8_t joystick_html_start[] asm("_binary_joystick_html_start");
extern const uint8_t joystick_html_end[] asm("_binary_joystick_html_end");

#if !CONFIG_IDF_TARGET_ESP32P4 || CONFIG_ESP_WIFI_REMOTE_ENABLED ||            \
    CONFIG_ESP_HOSTED_ENABLED
#endif

#if !CONFIG_IDF_TARGET_ESP32P4 || CONFIG_ESP_WIFI_REMOTE_ENABLED ||            \
    CONFIG_ESP_HOSTED_ENABLED
#endif

void wifi_controller_init(void) {
  state_mutex = xSemaphoreCreateMutex();

  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

#if !CONFIG_IDF_TARGET_ESP32P4 || CONFIG_ESP_WIFI_REMOTE_ENABLED ||            \
    CONFIG_ESP_HOSTED_ENABLED

  wifi_storage_init();

  // Try to find the best known network
  wifi_controller_connect_saved();

  web_server_start();
#else
  ESP_LOGW(TAG, "WiFi Controller is NOT supported on ESP32-P4 target (No "
                "integrated WiFi)");
#endif
}

/**
 * @brief Connect to saved WiFi AP from NVS
 */
esp_err_t wifi_controller_connect_saved(void) {
  ESP_LOGI(TAG, "Scanning for known networks...");

  wifi_cred_t *known = malloc(sizeof(wifi_cred_t) * MAX_KNOWN_NETWORKS);
  if (!known) {
    ESP_LOGE(TAG, "Failed to allocate memory for known networks");
    return ESP_ERR_NO_MEM;
  }

  int known_count = 0;
  wifi_storage_get_all(known, &known_count);

  if (known_count == 0) {
    ESP_LOGI(TAG, "No known networks in memory. Starting AP mode.");
    free(known);
    return wifi_init_ap("ESP32P4_Dashboard", "");
  }

  // Scan current air
  uint16_t ap_max = 20;
  wifi_ap_record_t *ap_info = malloc(sizeof(wifi_ap_record_t) * ap_max);
  if (!ap_info) {
    ESP_LOGE(TAG, "Failed to allocate memory for scan results");
    free(known);
    return ESP_ERR_NO_MEM;
  }

  uint16_t ap_count = ap_max;
  if (wifi_scan(ap_info, &ap_count) != ESP_OK) {
    ESP_LOGE(TAG, "WiFi Scan failed during auto-connect");
    free(known);
    free(ap_info);
    return ESP_FAIL;
  }

  // Find best match
  int best_rssi = -1000;
  int best_known_idx = -1;

  for (int i = 0; i < ap_count; i++) {
    for (int j = 0; j < known_count; j++) {
      if (strcmp((char *)ap_info[i].ssid, known[j].ssid) == 0) {
        if (ap_info[i].rssi > best_rssi) {
          best_rssi = ap_info[i].rssi;
          best_known_idx = j;
        }
      }
    }
  }

  esp_err_t ret = ESP_OK;
  if (best_known_idx != -1) {
    ESP_LOGI(TAG, "Best known network found: %s (RSSI: %d). Connecting...",
             known[best_known_idx].ssid, best_rssi);
    ret = wifi_init_apsta("ESP32P4_Dashboard", "", known[best_known_idx].ssid,
                          known[best_known_idx].password);
  } else {
    ESP_LOGI(TAG, "None of the %d known networks are in range. AP mode only.",
             known_count);
    ret = wifi_init_ap("ESP32P4_Dashboard", "");
  }

  free(known);
  free(ap_info);
  return ret;
}

static int compare_rssi(const void *a, const void *b) {
  return ((wifi_ap_record_t *)b)->rssi - ((wifi_ap_record_t *)a)->rssi;
}

int wifi_controller_scan(wifi_scan_result_t *results, int max_results) {
#if !CONFIG_IDF_TARGET_ESP32P4 || CONFIG_ESP_WIFI_REMOTE_ENABLED ||            \
    CONFIG_ESP_HOSTED_ENABLED
  is_wifi_scanning = true; // Set flag before scanning

  wifi_ap_record_t *ap_info = malloc(sizeof(wifi_ap_record_t) * max_results);
  if (!ap_info) {
    is_wifi_scanning = false;
    return 0;
  }

  uint16_t number = max_results;
  if (wifi_scan(ap_info, &number) != ESP_OK) {
    free(ap_info);
    is_wifi_scanning = false;
    return 0;
  }

  // Sort by RSSI descending
  if (number > 0) {
    qsort(ap_info, number, sizeof(wifi_ap_record_t), compare_rssi);
  }

  // Limit to top 10 as requested
  int count = (number > 10) ? 10 : number;

  for (int i = 0; i < count; i++) {
    memset(results[i].ssid, 0, 33);
    strncpy(results[i].ssid, (char *)ap_info[i].ssid, 32);
    results[i].rssi = ap_info[i].rssi;
    results[i].authmode = ap_info[i].authmode;

    // DEBUG: Log SSID and hex values
    char hex[65] = {0};
    for (int j = 0; j < 32; j++)
      sprintf(hex + j * 2, "%02X", ap_info[i].ssid[j]);
    ESP_LOGI(TAG, "Sorted Result [%d]: SSID='%s', RSSI=%d, Auth=%d", i,
             results[i].ssid, results[i].rssi, results[i].authmode);
  }

  ESP_LOGI(TAG, "WiFi scan processing finished, freeing ap_info");
  free(ap_info);
  is_wifi_scanning = false; // Clear flag after scanning
  return count;
#else
  return 0;
#endif
}

esp_err_t wifi_controller_connect_to_ap(const char *ssid,
                                        const char *password) {
#if !CONFIG_IDF_TARGET_ESP32P4 || CONFIG_ESP_WIFI_REMOTE_ENABLED ||            \
    CONFIG_ESP_HOSTED_ENABLED
  wifi_storage_save(ssid, password);
  return wifi_init_sta(ssid, password);
#else
  return ESP_ERR_NOT_SUPPORTED;
#endif
}

void wifi_controller_get_state(game_controller_state_t *state) {
  if (state_mutex && xSemaphoreTake(state_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    *state = current_state;
    xSemaphoreGive(state_mutex);
  }
}

void wifi_controller_update_state(const game_controller_state_t *state) {
  if (state && state_mutex &&
      xSemaphoreTake(state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    current_state = *state;
    xSemaphoreGive(state_mutex);
  }
}
void wifi_controller_get_info(wifi_controller_info_t *info) {
  if (!info)
    return;
  memset(info, 0, sizeof(wifi_controller_info_t));

#if !CONFIG_IDF_TARGET_ESP32P4 || CONFIG_ESP_WIFI_REMOTE_ENABLED ||            \
    CONFIG_ESP_HOSTED_ENABLED
  // Skip AP info query during scanning to prevent crash in esp_hosted
  if (is_wifi_scanning) {
    ESP_LOGD(TAG, "Skipping AP info query during WiFi scan");
  } else {
    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret == ESP_OK) {
      strncpy(info->ssid, (char *)ap_info.ssid, 32);
      info->rssi = ap_info.rssi;
      info->speed = 54; // Assume base 802.11g for now
      ESP_LOGI(TAG, "WiFi Info: SSID=%s, RSSI=%d, Speed=%d", info->ssid,
               info->rssi, info->speed);
    } else if (ret == ESP_ERR_WIFI_NOT_CONNECT) {
      ESP_LOGD(TAG, "esp_wifi_sta_get_ap_info: Not connected");
    } else {
      ESP_LOGW(TAG, "esp_wifi_sta_get_ap_info failed: %s (0x%x)",
               esp_err_to_name(ret), ret);
    }
  }

  esp_netif_ip_info_t ip_info;
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
    esp_ip4addr_ntoa(&ip_info.ip, info->ip, 16);
    ESP_LOGI(TAG, "WiFi IP: %s", info->ip);
  } else {
    ESP_LOGW(TAG, "Failed to get IP info (netif=%p)", netif);
  }
#endif
}
