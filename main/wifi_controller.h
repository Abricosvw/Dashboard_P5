#ifndef WIFI_CONTROLLER_H
#define WIFI_CONTROLLER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  int16_t x; // -100 to 100
  int16_t y; // -100 to 100
  bool button_a;
  bool button_b;
  bool button_start;
  bool button_select;
} game_controller_state_t;

typedef struct {
  char ssid[33];
  int rssi;
  int authmode;
} wifi_scan_result_t;

typedef struct {
  char ssid[33];
  char ip[16];
  int rssi;
  int speed; // Mbps (approx)
} wifi_controller_info_t;

/**
 * @brief Initialize WiFi SoftAP and Web Server
 */
void wifi_controller_init(void);

/**
 * @brief Get the latest controller state
 * @param state Pointer to structure to fill
 */
void wifi_controller_get_state(game_controller_state_t *state);

/**
 * @brief Update the controller state (thread-safe)
 * @param state New state to apply
 */
void wifi_controller_update_state(const game_controller_state_t *state);

/**
 * @brief Scan for WiFi networks
 * @param results Array to store results
 * @param max_results Maximum number of results to return
 * @return Number of networks found
 */
int wifi_controller_scan(wifi_scan_result_t *results, int max_results);

/**
 * @brief Connect to a specific WiFi AP
 * @param ssid SSID of the AP
 * @param password Password of the AP
 * @return ESP_OK on success
 */
esp_err_t wifi_controller_connect_to_ap(const char *ssid, const char *password);

/**
 * @brief Connect to saved WiFi AP from NVS
 * @return ESP_OK on success
 */
esp_err_t wifi_controller_connect_saved(void);

/**
 * @brief Get current WiFi connection info
 * @param info Pointer to structure to fill
 */
void wifi_controller_get_info(wifi_controller_info_t *info);

#endif // WIFI_CONTROLLER_H
