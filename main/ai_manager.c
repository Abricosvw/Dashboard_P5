#include "ai_manager.h"
#include "ai_commands.h"
#include "ai_config.h"
#include "audio_manager.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "mbedtls/base64.h"
#include "ui/screens/ui_Screen7.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

// ESP-SR Headers for Wake Word Detection
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"

// #include "model_path_info.h" - Removed as it's not standard

// Manual declarations to resolve header conflicts
bool example_lvgl_lock(int timeout_ms);
void example_lvgl_unlock(void);
extern void ui_Screen7_set_status(const char *text);
extern void ui_Screen6_reset_ai_button(void); // New helper

static const char *TAG = "AI_MGR_GEMINI";

static bool s_voice_activation_enabled = true;
static volatile bool s_wake_word_paused = false;
static TaskHandle_t s_wake_task_handle = NULL;
static volatile bool s_manual_trigger_requested = false;

static void ai_wake_word_task(void *pvParameters);
static void update_ui_status(const char *text);
static void process_ai_interaction(void); // Internal helper

// System prompt for Gemini - provides context about the device
static const char *SYSTEM_PROMPT =
    "Ты встроенный ИИ автомобильной панели 'ESP Claw' (также называется "
    "'ECU Dashboard P5'). Аппаратная платформа: ESP32-P4. "
    "Ты и есть ESP Claw — это твоё имя и название устройства. "
    "Ты управляешь САМИМ дашбордом: экранами, кнопками, настройками, Lua-скриптами. "
    "У тебя есть прямой доступ к CAN-шине автомобиля для чтения данных ECU (обороты, "
    "температура, наддув и т.д.). Внешняя среда (двигатель, ECU) подключена к тебе через CAN bus. "
    "Отвечай кратко на русском языке. "
    "ВАЖНО: У тебя ДВА терминала: 1) CAN-терминал (Экран 3) — для просмотра CAN-данных, "
    "кнопки: sniffer, clear, record. 2) Lua-терминал (Экран 6) — для скриптов, "
    "кнопки: run_lua, save_lua, clear_lua (очистить текст скрипта). "
    "Доступные экраны: 1-Главный, 2-Датчики, 3-CAN терминал, 4-Доп датчики, "
    "5-Момент, 6-Настройки и Lua терминал. "
    "Если пользователь спрашивает текущие параметры (обороты, температуру, "
    "наддув и т.д.), ОБЯЗАТЕЛЬНО используй функцию get_ecu_data(). "
    "Не генерируй Lua для простых ответов на вопросы. "
    "Если пользователь просит ОТПРАВИТЬ СООБЩЕНИЕ в Telegram, используй функцию "
    "send_telegram(message). НЕ генерируй Lua скрипт для отправки в Telegram! "
    "Если пользователь просит СОЗДАТЬ ПРАВИЛО, АВТОМАТИЗАЦИЮ или скрипт, "
    "используй generate_lua_rule(lua_code, auto_execute=true). "
    "Если пользователь просит нажать кнопку в интерфейсе (Help, Telegram, GPIO Map, "
    "WiFi, Voice AI и т.д.), используй функцию click_ui_button(button_name). "
    "Если просят очистить/удалить скрипт в терминале — используй click_ui_button('clear_lua'). "
    "Ты можешь управлять: экранами, яркостью, датчиками, демо-режимом, "
    "CAN-сниффером, записью логов, Lua-скриптами, настройками и всеми кнопками UI.";

// Function declarations for Gemini Function Calling
static const char *FUNCTION_DECLARATIONS =
    "[{"
    "  \"name\": \"switch_screen\","
    "  \"description\": \"Переключить на указанный экран\","
    "  \"parameters\": {"
    "    \"type\": \"object\","
    "    \"properties\": {"
    "      \"screen_number\": {\"type\": \"integer\", \"description\": \"Номер "
    "экрана от 1 до 6\"}"
    "    },"
    "    \"required\": [\"screen_number\"]"
    "  }"
    "},{"
    "  \"name\": \"toggle_gauge\","
    "  \"description\": \"Включить или выключить отображение датчика\","
    "  \"parameters\": {"
    "    \"type\": \"object\","
    "    \"properties\": {"
    "      \"gauge_name\": {\"type\": \"string\", \"description\": \"Название "
    "датчика: MAP, Wastegate, TPS, RPM, Boost, TCU, Oil Press, Oil Temp, Water "
    "Temp, Fuel Press, Battery\"},"
    "      \"enable\": {\"type\": \"boolean\", \"description\": "
    "\"true=включить, false=выключить\"}"
    "    },"
    "    \"required\": [\"gauge_name\", \"enable\"]"
    "  }"
    "},{"
    "  \"name\": \"search_can_id\","
    "  \"description\": \"Найти и отфильтровать CAN ID в терминале\","
    "  \"parameters\": {"
    "    \"type\": \"object\","
    "    \"properties\": {"
    "      \"can_id\": {\"type\": \"string\", \"description\": \"CAN ID в "
    "формате 0x123 или десятичном\"}"
    "    },"
    "    \"required\": [\"can_id\"]"
    "  }"
    "},{"
    "  \"name\": \"set_brightness\","
    "  \"description\": \"Установить яркость экрана\","
    "  \"parameters\": {"
    "    \"type\": \"object\","
    "    \"properties\": {"
    "      \"percent\": {\"type\": \"integer\", \"description\": \"Яркость от "
    "0 до 100 процентов\"}"
    "    },"
    "    \"required\": [\"percent\"]"
    "  }"
    "},{"
    "  \"name\": \"get_status\","
    "  \"description\": \"Получить текущий статус системы (WiFi, Demo mode, "
    "настройки)\","
    "  \"parameters\": {\"type\": \"object\", \"properties\": {}}"
    "},{"
    "  \"name\": \"toggle_demo_mode\","
    "  \"description\": \"Включить или выключить демо-режим с тестовыми "
    "данными\","
    "  \"parameters\": {"
    "    \"type\": \"object\","
    "    \"properties\": {"
    "      \"enable\": {\"type\": \"boolean\", \"description\": "
    "\"true=включить, false=выключить\"}"
    "    },"
    "    \"required\": [\"enable\"]"
    "  }"
    "},{"
    "  \"name\": \"save_settings\","
    "  \"description\": \"Сохранить текущие настройки на SD карту\","
    "  \"parameters\": {\"type\": \"object\", \"properties\": {}}"
    "},{"
    "  \"name\": \"get_ecu_data\","
    "  \"description\": \"Получить текущие данные двигателя из CAN-шины "
    "(обороты, температура, наддув, батарея)\","
    "  \"parameters\": {\"type\": \"object\", \"properties\": {}}"
    "},{"
    "  \"name\": \"generate_lua_rule\","
    "  \"description\": \"Сгенерировать Lua скрипт. Функции: get_rpm(), "
    "get_engine_temp(), set_fan_speed(speed), show_warning(msg), "
    "telegram_send(msg), switch_screen(id), click_btn_help(), gpio_set(pin, "
    "level)\","
    "  \"parameters\": {"
    "    \"type\": \"object\","
    "    \"properties\": {"
    "      \"lua_code\": {\"type\": \"string\", \"description\": \"Готовый код "
    "на языке Lua\"},"
    "      \"auto_execute\": {\"type\": \"boolean\", \"description\": "
    "\"Запустить скрипт сразу после генерации (обычно true)\"}"
    "    },"
    "    \"required\": [\"lua_code\", \"auto_execute\"]"
    "  }"
    "},{"
    "  \"name\": \"send_telegram\","
    "  \"description\": \"Отправить текстовое сообщение в Telegram бот пользователю\","
    "  \"parameters\": {"
    "    \"type\": \"object\","
    "    \"properties\": {"
    "      \"message\": {\"type\": \"string\", \"description\": \"Текст сообщения для отправки в Telegram\"}"
    "    },"
    "    \"required\": [\"message\"]"
    "  }"
    "},{"
    "  \"name\": \"click_ui_button\","
    "  \"description\": \"Нажать кнопку в интерфейсе (управление экраном)\","
    "  \"parameters\": {"
    "    \"type\": \"object\","
    "    \"properties\": {"
    "      \"button_name\": {\"type\": \"string\", \"description\": \"Имя "
    "кнопки: 'demo_mode', 'enable_screen3', 'nav_buttons', 'save_settings', "
    "'reset_settings', 'intro_sound', 'wifi', 'voice_ai', 'ai', 'run_lua', "
    "'save_lua', 'clear_lua', 'help', 'gpio', 'telegram', 'sniffer', 'clear', 'record'\"}"
    "    },"
    "    \"required\": [\"button_name\"]"
    "  }"
    "}]";

static void update_ui_status(const char *text) {
  if (example_lvgl_lock(500)) {
    ui_Screen7_set_status(text);
    


    // If we transition back to Idle or Error, reset the trigger button color
    if (strstr(text, "Idle") || strstr(text, "Error") || strstr(text, "say:")) {
      ui_Screen6_reset_ai_button();
    }
    example_lvgl_unlock();
  }
}

// Function to list available models for this API key
static void list_available_models(void *pvParameters) {
  ESP_LOGI(TAG, "Waiting for network & time sync before checking models...");

  // Wait for time sync (indicates network is up and TLS can be verified)
  extern void ai_start_after_time_sync(
      void *pvParameters); // Just using the SNTP check logic
  int retry = 0;
  while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < 30) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  ESP_LOGI(TAG, "Checking available models (Background Task)...");
  char url[256];
  snprintf(url, sizeof(url),
           "https://generativelanguage.googleapis.com/v1beta/models?key=%s",
           GEMINI_API_KEY);

  esp_http_client_config_t config = {
      .url = url,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .method = HTTP_METHOD_GET,
      .timeout_ms = 10000,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open ListModels connection");
    esp_http_client_cleanup(client);
    vTaskDelete(NULL); // FreeRTOS tasks must not return!
    return;            // Unreachable, but keeps compiler happy
  }

  esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);
  ESP_LOGI(TAG, "ListModels HTTP Status: %d", status);

  char *buf = malloc(4096);
  if (buf) {
    int len = 0, total = 0;
    while ((len = esp_http_client_read(client, buf + total, 4095 - total)) >
           0) {
      total += len;
      if (total >= 4095)
        break;
    }
    buf[total] = 0;

    if (status == 200) {
      ESP_LOGI(TAG, "Available models response (first 1000 chars):");
      char truncated[1001];
      strncpy(truncated, buf, 1000);
      truncated[1000] = 0;
      ESP_LOGI(TAG, "%s", truncated);
    } else {
      ESP_LOGE(TAG, "ListModels Error: %s", buf);
    }
    free(buf);
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  vTaskDelete(NULL);
}

static bool s_ai_initialized = false;
esp_err_t ai_manager_init(void) {
  if (s_ai_initialized)
    return ESP_OK;

  ESP_LOGI(TAG, "Initializing Gemini AI Manager...");
  ESP_LOGI(TAG, "API Key (first 10 chars): %.10s...", GEMINI_API_KEY);
  update_ui_status("AI: System Ready\n(Gemini Integration)");

  s_ai_initialized = true;
  return ESP_OK;
}

static bool s_ai_started = false;
esp_err_t ai_manager_start(void) {
  if (s_ai_started) {
    ESP_LOGI(TAG, "AI Manager already started, skipping task creation");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Gemini AI Manager Started");
  // Create background task to list models (so it doesn't block startup)
  xTaskCreate(list_available_models, "list_models_task", 8192, NULL, 2, NULL);

  // Create Wake Word detection task
  xTaskCreatePinnedToCore(ai_wake_word_task, "ai_wake_word", 16 * 1024, NULL, 6,
                          &s_wake_task_handle, 1);

  s_ai_started = true;
  return ESP_OK;
}

void ai_manager_set_voice_activation(bool enabled) {
  s_voice_activation_enabled = enabled;
  ESP_LOGI(TAG, "Voice activation %s", enabled ? "enabled" : "disabled");
}

static const esp_afe_sr_iface_t *s_afe_handle = NULL;
static esp_afe_sr_data_t *s_afe_data = NULL;

static void ai_fetch_task(void *pvParameters) {
  ESP_LOGI(TAG, "AFE Fetch Task Started");
  while (1) {
    if (s_wake_word_paused || !s_voice_activation_enabled || !s_afe_data ||
        !s_afe_handle) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // fetch blocks until a frame is ready
    afe_fetch_result_t *res = s_afe_handle->fetch(s_afe_data);

    if (res && res->wakeup_state == WAKENET_DETECTED) {
      ESP_LOGI(TAG, "WAKE WORD DETECTED!");
      // Signal manual trigger to feed task to ensure synchronized pause
      s_manual_trigger_requested = true;
    } else {
      // Yield to prevent watchdog if fetch doesn't block
      vTaskDelay(1);
    }
  }
}

static void ai_wake_word_task(void *pvParameters) {
  ESP_LOGI(TAG, "Wake Word Task Started");

  // 0. Initialize Models from Flash Partition
  srmodel_list_t *models = esp_srmodel_init("model");
  if (!models) {
    ESP_LOGE(TAG, "Failed to initialize SR models from partition 'model'!");
    vTaskDelete(NULL);
    return;
  }
  ESP_LOGI(TAG, "SR Models initialized successfully. Found %d models.",
           models->num);

  // 1. Initialize AFE (Acoustic Front-End)
  s_afe_handle = &esp_afe_sr_v1;
  afe_config_t afe_config = AFE_CONFIG_DEFAULT();
  afe_config.wakenet_init = true;
  afe_config.aec_init = false;
  afe_config.se_init = true; // Enable Speech Enhancement
  afe_config.vad_init = true;
  afe_config.voice_communication_init = false;
  afe_config.afe_linear_gain = 3.0;       // Boost sensitivity
  afe_config.pcm_config.total_ch_num = 2; // Total MUST be mic_num + ref_num
  afe_config.pcm_config.mic_num = 1;      // Mic on Ch0
  afe_config.pcm_config.ref_num = 1;      // Ref on Ch1 (we will zero it out)
  afe_config.wakenet_mode = DET_MODE_90;  // Force 1CH wake word mode

#if CONFIG_SR_WN_WN9_HILEXIN
  afe_config.wakenet_model_name = "wn9_hilexin";
#elif CONFIG_SR_WN_WN9_JARVIS_TTS
  afe_config.wakenet_model_name = "wn9_jarvis_tts";
#elif CONFIG_SR_WN_WN9_HIESP
  afe_config.wakenet_model_name = "wn9_hiesp";
#else
  afe_config.wakenet_model_name = WAKENET_MODEL_NAME;
#endif

  s_afe_data = s_afe_handle->create_from_config(&afe_config);
  if (!s_afe_data) {
    ESP_LOGE(TAG,
             "Failed to create AFE data (check wake word models in sdkconfig)");
    vTaskDelete(NULL);
    return;
  }
  ESP_LOGI(TAG, "AFE created successfully with model: %s",
           afe_config.wakenet_model_name ? afe_config.wakenet_model_name
                                         : "NULL");

  i2s_chan_handle_t rx_handle = audio_get_rx_handle();
  if (!rx_handle) {
    ESP_LOGE(TAG, "I2S RX handle not available for WWD!");
    vTaskDelete(NULL);
    return;
  }

  int audio_chunksize = s_afe_handle->get_feed_chunksize(s_afe_data);
  int n_ch_afe = 2;
  int n_ch_i2s = 2;

  int16_t *i2s_stereo_buffer =
      malloc(audio_chunksize * sizeof(int16_t) * n_ch_i2s);
  int16_t *afe_feed_buffer =
      malloc(audio_chunksize * sizeof(int16_t) * n_ch_afe);

  if (!i2s_stereo_buffer || !afe_feed_buffer) {
    ESP_LOGE(TAG, "Failed to allocate WWD buffers");
    free(i2s_stereo_buffer);
    free(afe_feed_buffer);
    vTaskDelete(NULL);
    return;
  }

  // Start fetch task
  xTaskCreatePinnedToCore(ai_fetch_task, "ai_fetch_task", 8192, NULL, 5, NULL,
                          1);

  ESP_LOGI(TAG, "Wake word feed task entering loop...");
  while (1) {
    // 2.A Check if manual trigger was requested via UI button
    if (s_manual_trigger_requested) {
      s_manual_trigger_requested = false;
      ESP_LOGI(TAG, "[%" PRIu32 "] Manual trigger detected in AI task",
               (uint32_t)esp_log_timestamp());
      process_ai_interaction();
      continue;
    }

    if (!s_voice_activation_enabled || s_wake_word_paused) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    size_t bytes_read = 0;
    int timeout_ms =
        (s_wake_word_paused || !s_voice_activation_enabled) ? 20 : 200;

    esp_err_t ret =
        i2s_channel_read(rx_handle, i2s_stereo_buffer,
                         audio_chunksize * sizeof(int16_t) * n_ch_i2s,
                         &bytes_read, pdMS_TO_TICKS(timeout_ms));

    if (s_manual_trigger_requested)
      continue;

    size_t expected_bytes = audio_chunksize * sizeof(int16_t) * n_ch_i2s;
    if (ret == ESP_OK && bytes_read == expected_bytes) {
      // Prepare AFE data
      for (int i = 0; i < audio_chunksize; i++) {
        afe_feed_buffer[i * 2] = i2s_stereo_buffer[i * 2]; // Mic from L
        afe_feed_buffer[i * 2 + 1] = 0;                    // Zero the Ref
      }

      // Feed AFE (Fetch is now in separate task)
      s_afe_handle->feed(s_afe_data, afe_feed_buffer);
    } else if (ret != ESP_ERR_TIMEOUT) {
      ESP_LOGW(TAG, "I2S Read Error: %s", esp_err_to_name(ret));
    }
  }

  free(i2s_stereo_buffer);
  free(afe_feed_buffer);
  s_afe_handle->destroy(s_afe_data);
  vTaskDelete(NULL);
}

// Forward declaration
static void speak_response(const char *text);
static void ai_wake_word_task(void *pvParameters);

// Function to handle the response from Gemini
static void handle_gemini_response(const char *json_str) {
  cJSON *root = cJSON_Parse(json_str);
  if (!root) {
    ESP_LOGE(TAG, "Failed to parse Gemini response");
    update_ui_status("AI: Error\nFailed to parse response");
    return;
  }

  // Gemini JSON structure: candidates[0].content.parts[0]
  cJSON *candidates = cJSON_GetObjectItem(root, "candidates");
  if (cJSON_IsArray(candidates) && cJSON_GetArraySize(candidates) > 0) {
    cJSON *candidate = cJSON_GetArrayItem(candidates, 0);
    cJSON *content = cJSON_GetObjectItem(candidate, "content");
    cJSON *parts = cJSON_GetObjectItem(content, "parts");
    if (cJSON_IsArray(parts) && cJSON_GetArraySize(parts) > 0) {
      cJSON *part = cJSON_GetArrayItem(parts, 0);

      // Check for function call first
      cJSON *function_call = cJSON_GetObjectItem(part, "functionCall");
      if (function_call) {
        cJSON *fn_name = cJSON_GetObjectItem(function_call, "name");
        cJSON *fn_args = cJSON_GetObjectItem(function_call, "args");

        if (fn_name && cJSON_IsString(fn_name)) {
          ESP_LOGI(TAG, "Function call detected: %s", fn_name->valuestring);

          char *args_str = fn_args ? cJSON_PrintUnformatted(fn_args) : NULL;

          // Special case for generating Lua code
          if (strcmp(fn_name->valuestring, "generate_lua_rule") == 0 &&
              fn_args) {
            cJSON *lua_code = cJSON_GetObjectItem(fn_args, "lua_code");
            cJSON *auto_exec = cJSON_GetObjectItem(fn_args, "auto_execute");
            bool should_execute = (auto_exec && cJSON_IsTrue(auto_exec));

            if (lua_code && cJSON_IsString(lua_code)) {
              extern void ui_Screen6_set_lua_text(const char *text);
              extern esp_err_t lua_manager_execute(const char *script);

              if (example_lvgl_lock(500)) {
                ui_Screen6_set_lua_text(lua_code->valuestring);
                example_lvgl_unlock();
              }

              if (should_execute) {
                lua_manager_execute(lua_code->valuestring);
                update_ui_status("✅ Код сгенерирован и ВЫПОЛНЕН");
                speak_response("Код сгенерирован и запущен.");
              } else {
                update_ui_status("✅ Код добавлен в Lua Editor");
                speak_response("Код сгенерирован в редакторе скриптов.");
              }
            }
          } else if (strcmp(fn_name->valuestring, "send_telegram") == 0 &&
                     fn_args) {
            // Direct Telegram message sending (no Lua!)
            cJSON *msg_json = cJSON_GetObjectItem(fn_args, "message");
            if (msg_json && cJSON_IsString(msg_json) && 
                msg_json->valuestring[0]) {
              extern void telegram_send_message(const char *msg);
              telegram_send_message(msg_json->valuestring);
              
              // Show in AI terminal
              if (example_lvgl_lock(500)) {
                extern void ui_Screen7_append_text(const char *text);
                char tg_line[512];
                snprintf(tg_line, sizeof(tg_line), "[TG ←] %s", 
                         msg_json->valuestring);
                ui_Screen7_append_text(tg_line);
                example_lvgl_unlock();
              }
              
              update_ui_status("✅ Сообщение отправлено в Telegram");
              speak_response("Сообщение отправлено в Телеграм.");
            }
          } else {
            // Execute the normal UI function
            ai_cmd_result_t result =
                ai_execute_function_call(fn_name->valuestring, args_str);

            // Show result to user
            char status_buf[512];
            snprintf(status_buf, sizeof(status_buf), "%s\n\n%s",
                     result.success ? "✅ Команда выполнена:" : "❌ Ошибка:",
                     result.message);
            update_ui_status(status_buf);

            // Also append to Screen 7 terminal if locked
            if (example_lvgl_lock(500)) {
              ui_Screen7_append_text(result.message);
              example_lvgl_unlock();
            }

            // Speak the result
            speak_response(result.message);
          }

          if (args_str)
            free(args_str);

          cJSON_Delete(root);
          return;
        }
      }

      // Fall back to text response
      cJSON *text = cJSON_GetObjectItem(part, "text");
      if (cJSON_IsString(text)) {
        ESP_LOGI(TAG, "Gemini Response: %s", text->valuestring);

        char status_buf[512];
        snprintf(status_buf, sizeof(status_buf), "AI Response:\n%s",
                 text->valuestring);
        update_ui_status(status_buf);

        // Also append to Screen 7 terminal if locked
        if (example_lvgl_lock(500)) {
          ui_Screen7_append_text(text->valuestring);
          example_lvgl_unlock();
        }

        // Speak the response (TTS placeholder)
        speak_response(text->valuestring);

        cJSON_Delete(root);
        return;
      }
    }
  }

  // Check for errors
  cJSON *error = cJSON_GetObjectItem(root, "error");
  if (error) {
    cJSON *message = cJSON_GetObjectItem(error, "message");
    if (message) {
      ESP_LOGE(TAG, "Gemini API Error: %s", message->valuestring);
      char err_buf[256];
      snprintf(err_buf, sizeof(err_buf), "AI: API Error\n%s",
               message->valuestring);
      update_ui_status(err_buf);
      cJSON_Delete(root);
      return;
    }
  }

  ESP_LOGW(TAG, "No valid response from Gemini");
  update_ui_status("AI: Empty response\nCheck API permissions");
  cJSON_Delete(root);
}

static void speak_response(const char *text) {
  // For now, just play a confirmation sound
  // TODO: Integrate Google TTS API to convert text to speech
  ESP_LOGI(TAG, "TTS would say: %s", text);

  // Send response back to Telegram so the user can read it!
  extern void telegram_send_message(const char *msg);
  telegram_send_message(text);

  // Play a simple notification sound if available
  // audio_play_tone(880, 200);  // Simple beep for now
}

static void ai_text_query_task(void *pvParameters) {
  char *query_text = (char *)pvParameters;
  ESP_LOGI(TAG, "Gemini Text Query Starting: %s", query_text);

  update_ui_status(
      "🌐 Подключаюсь к серверу...\n\nОтправка текстового запроса");

  cJSON *root = cJSON_CreateObject();

  // Add system instruction
  cJSON *system_inst = cJSON_AddObjectToObject(root, "system_instruction");
  cJSON *sys_parts = cJSON_AddArrayToObject(system_inst, "parts");
  cJSON *sys_text = cJSON_CreateObject();
  cJSON_AddStringToObject(sys_text, "text", SYSTEM_PROMPT);
  cJSON_AddItemToArray(sys_parts, sys_text);

  // Add contents with user text
  cJSON *contents = cJSON_AddArrayToObject(root, "contents");
  cJSON *content = cJSON_CreateObject();
  cJSON_AddItemToArray(contents, content);
  cJSON *parts = cJSON_AddArrayToObject(content, "parts");

  cJSON *part_text = cJSON_CreateObject();
  cJSON_AddStringToObject(part_text, "text", query_text);
  cJSON_AddItemToArray(parts, part_text);

  // Add tools array with function declarations
  cJSON *tools = cJSON_AddArrayToObject(root, "tools");
  cJSON *tool = cJSON_CreateObject();
  cJSON_AddItemToArray(tools, tool);
  cJSON *func_decls = cJSON_Parse(FUNCTION_DECLARATIONS);
  if (func_decls) {
    cJSON_AddItemToObject(tool, "functionDeclarations", func_decls);
  }

  char *post_data = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);

  // Prepare HTTP client
  char url[256];
  snprintf(url, sizeof(url), "%s?key=%s", GEMINI_API_ENDPOINT, GEMINI_API_KEY);

  esp_http_client_config_t http_cfg = {
      .url = url,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .method = HTTP_METHOD_POST,
      .timeout_ms = 45000,
      .buffer_size = 8192,
      .buffer_size_tx = 4096,
  };
  esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
  esp_http_client_set_header(client, "Content-Type", "application/json");

  esp_err_t err = ESP_FAIL;
  int retries = 3;
  while (retries-- > 0) {
    err = esp_http_client_open(client, strlen(post_data));
    if (err == ESP_OK)
      break;
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_http_client_cleanup(client);
    client = esp_http_client_init(&http_cfg);
    esp_http_client_set_header(client, "Content-Type", "application/json");
  }

  if (err != ESP_OK) {
    update_ui_status("❌ Ошибка сети\n\nНе удалось подключиться");
    free(post_data);
    esp_http_client_cleanup(client);
    free(query_text);
    vTaskDelete(NULL);
    return;
  }

  int wlen = esp_http_client_write(client, post_data, strlen(post_data));
  if (wlen < 0) {
    update_ui_status("❌ Ошибка отправки\n\nПроверьте интернет");
    free(post_data);
    esp_http_client_cleanup(client);
    free(query_text);
    vTaskDelete(NULL);
    return;
  }

  update_ui_status("⏳ Жду ответ...\n\nСервер обрабатывает запрос");
  esp_http_client_fetch_headers(client);
  int status_code = esp_http_client_get_status_code(client);

  char *res_buf = malloc(8192);
  if (res_buf) {
    int total_read = 0;
    int read_len;
    while ((read_len = esp_http_client_read(client, res_buf + total_read,
                                            8191 - total_read)) > 0) {
      total_read += read_len;
      if (total_read >= 8191)
        break;
    }
    res_buf[total_read] = 0;

    if (status_code == 200 && total_read > 0) {
      handle_gemini_response(res_buf);
    } else if (total_read > 0) {
      char status_err[128];
      snprintf(status_err, sizeof(status_err), "❌ Ошибка HTTP %d\n\nСм. логи",
               status_code);
      update_ui_status(status_err);
    } else {
      char status_err[64];
      snprintf(status_err, sizeof(status_err),
               "❌ Ошибка HTTP %d\n\nПустой ответ", status_code);
      update_ui_status(status_err);
    }
    free(res_buf);
  }

  esp_http_client_close(client);
  free(post_data);
  esp_http_client_cleanup(client);
  free(query_text);
  vTaskDelete(NULL);
}

void ai_manager_send_text_query(const char *text) {
  if (!text || strlen(text) == 0)
    return;
  char *query_copy = strdup(text);
  if (query_copy) {
    xTaskCreate(ai_text_query_task, "ai_text_query", 8192, query_copy, 5, NULL);
  }
}

void ai_manager_trigger_listening(void) {
  // Now asynchronous to avoid UI blocking and I2S contention
  s_manual_trigger_requested = true;
  ESP_LOGI(TAG, "[%" PRIu32 "] Manual trigger requested (ASYNC)",
           (uint32_t)esp_log_timestamp());
}

static void process_ai_interaction(void) {
  ESP_LOGI(TAG, "[%" PRIu32 "] Gemini Interaction Starting...",
           (uint32_t)esp_log_timestamp());

  // Pause wake word detection during recording/processing
  s_wake_word_paused = true;

  // Recording duration in seconds
  const int record_seconds = 5;

  // Show recording message with duration
  update_ui_status("🎤 Запись...\n\nГоворите! [5 сек]");

  // Force immediate screen refresh (uses lock inside)
  // lv_refr_now(NULL); // Call only from tasks that don't block LVGL

  // Create AI_Rec directory if it doesn't exist
  const char *ai_rec_dir = "/sdcard/SYSTEM/SOUND/AI_Rec";
  struct stat st = {0};
  if (stat(ai_rec_dir, &st) == -1) {
    mkdir(ai_rec_dir, 0755);
    ESP_LOGI(TAG, "Created AI recording directory: %s", ai_rec_dir);
  }

  const char *wav_path = "/sdcard/SYSTEM/SOUND/AI_Rec/ai_rec.wav";

  // Start recording - This call is now safe because we are the only task
  // reading I2S
  esp_err_t rec_err = audio_record_wav(wav_path, record_seconds * 1000);
  if (rec_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to record audio: %s", esp_err_to_name(rec_err));
    update_ui_status("❌ Ошибка записи\n\nПроблема с микрофоном");
    s_wake_word_paused = false;
    return;
  }

  // Recording complete
  update_ui_status("🔄 Обработка...\n\nКодирование аудио");
  // ... rest of the existing Gemini logic ...

  FILE *f = fopen(wav_path, "rb");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open recorded file");
    update_ui_status("❌ Ошибка\n\nНе удалось открыть файл");
    s_wake_word_paused = false;
    return;
  }
  fseek(f, 0, SEEK_END);
  size_t file_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  unsigned char *raw_audio = malloc(file_size);
  if (!raw_audio) {
    fclose(f);
    update_ui_status("❌ Ошибка\n\nНедостаточно памяти");
    s_wake_word_paused = false;
    return;
  }
  fread(raw_audio, 1, file_size, f);
  fclose(f);

  size_t b64_len = 0;
  mbedtls_base64_encode(NULL, 0, &b64_len, raw_audio, file_size);
  unsigned char *b64_audio = malloc(b64_len + 1);
  if (!b64_audio) {
    free(raw_audio);
    update_ui_status("❌ Ошибка\n\nНедостаточно памяти (B64)");
    s_wake_word_paused = false;
    return;
  }

  size_t out_len = 0;
  mbedtls_base64_encode(b64_audio, b64_len, &out_len, raw_audio, file_size);
  b64_audio[out_len] = '\0';
  free(raw_audio);

  update_ui_status("🌐 Подключаюсь к серверу...\n\nОтправка запроса на Gemini");

  cJSON *root = cJSON_CreateObject();

  // Add system instruction
  cJSON *system_inst = cJSON_AddObjectToObject(root, "system_instruction");
  cJSON *sys_parts = cJSON_AddArrayToObject(system_inst, "parts");
  cJSON *sys_text = cJSON_CreateObject();
  cJSON_AddStringToObject(sys_text, "text", SYSTEM_PROMPT);
  cJSON_AddItemToArray(sys_parts, sys_text);

  // Add contents with user audio
  cJSON *contents = cJSON_AddArrayToObject(root, "contents");
  cJSON *content = cJSON_CreateObject();
  cJSON_AddItemToArray(contents, content);
  cJSON *parts = cJSON_AddArrayToObject(content, "parts");

  // Part 1: Inline Audio Data
  cJSON *part_audio = cJSON_CreateObject();
  cJSON *inline_data = cJSON_AddObjectToObject(part_audio, "inlineData");
  cJSON_AddStringToObject(inline_data, "mimeType", "audio/wav");
  cJSON_AddStringToObject(inline_data, "data", (char *)b64_audio);
  cJSON_AddItemToArray(parts, part_audio);

  // Add tools array with function declarations
  cJSON *tools = cJSON_AddArrayToObject(root, "tools");
  cJSON *tool = cJSON_CreateObject();
  cJSON_AddItemToArray(tools, tool);
  cJSON *func_decls = cJSON_Parse(FUNCTION_DECLARATIONS);
  if (func_decls) {
    cJSON_AddItemToObject(tool, "functionDeclarations", func_decls);
  }

  char *post_data = cJSON_PrintUnformatted(root);
  free(b64_audio);
  cJSON_Delete(root);

  // Prepare HTTP client
  char url[256];
  snprintf(url, sizeof(url), "%s?key=%s", GEMINI_API_ENDPOINT, GEMINI_API_KEY);
  ESP_LOGI(TAG, "Gemini URL: %s", GEMINI_API_ENDPOINT);

  esp_http_client_config_t http_cfg = {
      .url = url,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .method = HTTP_METHOD_POST,
      .timeout_ms = 45000,
      .buffer_size = 8192,
      .buffer_size_tx = 4096,
  };
  esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
  esp_http_client_set_header(client, "Content-Type", "application/json");

  // Use streaming API to capture response body
  esp_err_t err = ESP_FAIL;
  int retries = 3;
  while (retries-- > 0) {
    ESP_LOGI(TAG, "Attempting to connect to Gemini API... (retries left: %d)",
             retries);
    err = esp_http_client_open(client, strlen(post_data));
    if (err == ESP_OK) {
      break;
    }
    ESP_LOGW(TAG, "Connection failed: %s. Retrying in 2s...",
             esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(2000));
    // Must cleanup and re-init client for a clean retry state
    esp_http_client_cleanup(client);
    client = esp_http_client_init(&http_cfg);
    esp_http_client_set_header(client, "Content-Type", "application/json");
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open HTTP connection after retries: %s",
             esp_err_to_name(err));
    update_ui_status("❌ Ошибка сети\n\nНе удалось подключиться");
    free(post_data);
    esp_http_client_cleanup(client);
    s_wake_word_paused = false;
    return;
  }

  // Write POST data
  int wlen = esp_http_client_write(client, post_data, strlen(post_data));
  if (wlen < 0) {
    ESP_LOGE(TAG, "Write failed");
    update_ui_status("❌ Ошибка отправки\n\nПроверьте интернет");
    free(post_data);
    esp_http_client_cleanup(client);
    s_wake_word_paused = false;
    return;
  }

  // Fetch headers to get status code
  update_ui_status("⏳ Жду ответ...\n\nСервер обрабатывает запрос");
  int content_length = esp_http_client_fetch_headers(client);
  int status_code = esp_http_client_get_status_code(client);
  ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d", status_code,
           content_length);

  // Read response body
  char *res_buf = malloc(8192);
  if (res_buf) {
    int total_read = 0;
    int read_len;
    while ((read_len = esp_http_client_read(client, res_buf + total_read,
                                            8191 - total_read)) > 0) {
      total_read += read_len;
      if (total_read >= 8191)
        break;
    }
    res_buf[total_read] = 0;
    ESP_LOGI(TAG, "Response body length: %d", total_read);

    if (status_code == 200 && total_read > 0) {
      handle_gemini_response(res_buf);
    } else if (total_read > 0) {
      ESP_LOGE(TAG, "Gemini Error Body: %s", res_buf);
      char status_err[128];
      snprintf(status_err, sizeof(status_err), "❌ Ошибка HTTP %d\n\nСм. логи",
               status_code);
      update_ui_status(status_err);
    } else {
      ESP_LOGE(TAG, "No response body (HTTP %d)", status_code);
      char status_err[64];
      snprintf(status_err, sizeof(status_err),
               "❌ Ошибка HTTP %d\n\nПустой ответ", status_code);
      update_ui_status(status_err);
    }
    free(res_buf);
  }

  esp_http_client_close(client);
  free(post_data);
  esp_http_client_cleanup(client);

  // Resume wake word task if it was active
  if (s_voice_activation_enabled) {
    s_wake_word_paused = false;
    ESP_LOGI(TAG, "Wake Word Detection Resumed");
  }
}
