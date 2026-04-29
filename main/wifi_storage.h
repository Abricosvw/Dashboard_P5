#ifndef WIFI_STORAGE_H
#define WIFI_STORAGE_H

#include "esp_err.h"
#include <stdbool.h>

#define MAX_KNOWN_NETWORKS 10

typedef struct {
  char ssid[33];
  char password[64];
} wifi_cred_t;

/**
 * @brief Initialize WiFi credential storage
 */
esp_err_t wifi_storage_init(void);

/**
 * @brief Save a WiFi network to the known list
 * @param ssid SSID of the network
 * @param password Password of the network
 * @return ESP_OK on success
 */
esp_err_t wifi_storage_save(const char *ssid, const char *password);

/**
 * @brief Get all known networks
 * @param networks Array to fill
 * @param count Pointer to store the number of networks found
 * @return ESP_OK on success
 */
esp_err_t wifi_storage_get_all(wifi_cred_t *networks, int *count);

#endif // WIFI_STORAGE_H
