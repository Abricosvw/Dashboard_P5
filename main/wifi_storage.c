#include "wifi_storage.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "WIFI_STORAGE";
static const char *NAMESPACE = "wifi_known";

esp_err_t wifi_storage_init(void) {
  // NVS should already be initialized in wifi_controller_init
  return ESP_OK;
}

esp_err_t wifi_storage_save(const char *ssid, const char *password) {
  if (!ssid || strlen(ssid) == 0)
    return ESP_ERR_INVALID_ARG;

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK)
    return err;

  // 1. Check if already exists, or find empty slot
  int32_t count = 0;
  nvs_get_i32(handle, "count", &count);

  int existing_idx = -1;
  for (int i = 0; i < count; i++) {
    char key[16], s[33];
    size_t len = sizeof(s);
    snprintf(key, sizeof(key), "s%d", i);
    if (nvs_get_str(handle, key, s, &len) == ESP_OK) {
      if (strcmp(s, ssid) == 0) {
        existing_idx = i;
        break;
      }
    }
  }

  int target_idx = (existing_idx != -1) ? existing_idx : count;
  if (target_idx >= MAX_KNOWN_NETWORKS) {
    // Simple strategy: wrap around and overwrite oldest if full
    target_idx = 0;
  }

  char skey[16], pkey[16];
  snprintf(skey, sizeof(skey), "s%d", target_idx);
  snprintf(pkey, sizeof(pkey), "p%d", target_idx);

  nvs_set_str(handle, skey, ssid);
  nvs_set_str(handle, pkey, password ? password : "");

  if (existing_idx == -1 && count < MAX_KNOWN_NETWORKS) {
    count++;
    nvs_set_i32(handle, "count", count);
  }

  nvs_commit(handle);
  nvs_close(handle);
  ESP_LOGI(TAG, "Saved WiFi credentials for %s at index %d", ssid, target_idx);
  return ESP_OK;
}

esp_err_t wifi_storage_get_all(wifi_cred_t *networks, int *count) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    *count = 0;
    return (err == ESP_ERR_NVS_NOT_FOUND) ? ESP_OK : err;
  }

  int32_t c = 0;
  nvs_get_i32(handle, "count", &c);
  *count = 0;

  for (int i = 0; i < c && i < MAX_KNOWN_NETWORKS; i++) {
    char skey[16], pkey[16];
    snprintf(skey, sizeof(skey), "s%d", i);
    snprintf(pkey, sizeof(pkey), "p%d", i);

    size_t slen = sizeof(networks[*count].ssid);
    size_t plen = sizeof(networks[*count].password);

    if (nvs_get_str(handle, skey, networks[*count].ssid, &slen) == ESP_OK) {
      nvs_get_str(handle, pkey, networks[*count].password, &plen);
      (*count)++;
    }
  }

  nvs_close(handle);
  return ESP_OK;
}
