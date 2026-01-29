#include "wifi_controller.h"
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "wifi_fallbacks.h"
#include <nvs_flash.h>
#include <sys/param.h>

#if !CONFIG_IDF_TARGET_ESP32P4 || CONFIG_ESP_WIFI_REMOTE_ENABLED
#include "esp_netif.h"
#endif
#include "cJSON.h"
#include "web_server.h"

static const char *TAG = "WIFI_CONTROLLER";

// Global state protected by mutex (or simple atomic for now since it's one
// writer)
static game_controller_state_t current_state = {0};
static SemaphoreHandle_t state_mutex = NULL;

// Embedded HTML (we could also mount SPIFFS, but embedding is easier for single
// file) We will use the file we created: main/web/joystick.html To embed it, we
// need to add it to CMakeLists.txt as EMBED_TXTFILES For now, let's assume we
// can read it or hardcode it. Actually, standard ESP-IDF way is embedding
// binary. Let's declare the external symbols.
extern const uint8_t joystick_html_start[] asm("_binary_joystick_html_start");
extern const uint8_t joystick_html_end[] asm("_binary_joystick_html_end");

#if !CONFIG_IDF_TARGET_ESP32P4 || CONFIG_ESP_WIFI_REMOTE_ENABLED
/* WiFi Event Handler */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_id == WIFI_EVENT_AP_STACONNECTED) {
    wifi_event_ap_staconnected_t *event =
        (wifi_event_ap_staconnected_t *)event_data;
    ESP_LOGI(TAG, "station " MACSTR " join, AID=%d", MAC2STR(event->mac),
             event->aid);
  } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
    wifi_event_ap_stadisconnected_t *event =
        (wifi_event_ap_stadisconnected_t *)event_data;
    ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d", MAC2STR(event->mac),
             event->aid);
  }
}
#endif

#if !CONFIG_IDF_TARGET_ESP32P4 || CONFIG_ESP_WIFI_REMOTE_ENABLED
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

#if !CONFIG_IDF_TARGET_ESP32P4 || CONFIG_ESP_WIFI_REMOTE_ENABLED
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_ap();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

  wifi_config_t wifi_config = {
      .ap =
          {
              .ssid = "ESP32P4_Dashboard",
              .ssid_len = strlen("ESP32P4_Dashboard"),
              .channel = 1,
              .password = "",
              .max_connection = 4,
              .authmode = WIFI_AUTH_OPEN,
              .pmf_cfg =
                  {
                      .required = false,
                  },
          },
  };

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
           "ESP32_GAME_CONTROLLER", "OPEN", 1);

  web_server_start();
#else
  ESP_LOGW(TAG, "WiFi Controller is NOT supported on ESP32-P4 target (No "
                "integrated WiFi)");
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
