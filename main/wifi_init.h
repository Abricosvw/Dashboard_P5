#pragma once

#include "esp_err.h"
#include "esp_wifi_types.h"

/**
 * @brief Initialize WiFi in Access Point (AP) mode.
 *
 * @param ssid The SSID for the AP
 * @param pass The password for the AP
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_init_ap(const char *ssid, const char *pass);

/**
 * @brief Initialize WiFi in Station (STA) mode.
 *
 * @param ssid The SSID to connect to
 * @param pass The password for the network
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_init_sta(const char *ssid, const char *pass);

/**
 * @brief Initialize WiFi in AP+STA mode (both simultaneously).
 * AP creates hotspot, STA connects to router for internet.
 *
 * @param ap_ssid The SSID for the AP hotspot
 * @param ap_pass The password for the AP (empty for open)
 * @param sta_ssid The SSID to connect to for internet
 * @param sta_pass The password for the STA network
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_init_apsta(const char *ap_ssid, const char *ap_pass,
                          const char *sta_ssid, const char *sta_pass);

/**
 * @brief Scan for available WiFi networks.
 *
 * @param ap_info Pointer to store the scan results
 * @param number Pointer to the number of APs found (in/out)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_scan(wifi_ap_record_t *ap_info, uint16_t *number);
