#include "ai_manager.h"
#include "ai_commands.h"
#include "ai_config.h"
#include "audio_manager.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "mbedtls/base64.h"
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
extern void ui_Screen6_set_ai_info(const char *text);
extern void ui_Screen6_reset_ai_button(void); // New helper

static const char *TAG = "AI_MGR_GEMINI";

static bool s_voice_activation_enabled = false;
static volatile bool s_wake_word_paused = false;
static TaskHandle_t s_wake_task_handle = NULL;
static volatile bool s_manual_trigger_requested = false;

static void ai_wake_word_task(void *pvParameters);
static void update_ui_status(const char *text);
static void process_ai_interaction(void); // Internal helper

// System prompt for Gemini - provides context about the device
static const char *SYSTEM_PROMPT =
    "Ты голосовой ассистент для автомобильной приборной панели ECU Dashboard "
    "на ESP32-P4. "
    "Ты можешь управлять устройством голосовыми командами. Отвечай кратко на "
    "русском языке. "
    "Доступные экраны: 1-Главный (MAP,RPM,Boost), 2-Датчики (масло,вода), "
    "3-CAN терминал, "
    "4-Дополнительные датчики, 5-Крутящий момент, 6-Настройки. "
    "Ты также можешь писать скрипты на Lua (ESP-Claw Terminal) "
    "для управления поведением машины, если пользователь просит создать правило. "
    "Когда пользователь просит выполнить действие, используй соответствующую "
    "функцию.";

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
    "  \"name\": \"generate_lua_rule\","
    "  \"description\": \"Сгенерировать Lua скрипт для ESP-Claw терминала\","
    "  \"parameters\": {"
    "    \"type\": \"object\","
    "    \"properties\": {"
    "      \"lua_code\": {\"type\": \"string\", \"description\": \"Готовый код "
    "на языке Lua\"}"
    "    },"
    "    \"required\": [\"lua_code\"]"
    "  }"
    "}]";

static void update_ui_status(const char *text) {
  if (example_lvgl_lock(500)) {
    ui_Screen6_set_ai_info(text);
    // If we transition back to Idle or Error, reset the trigger button color
    if (strstr(text, "Idle") || strstr(text, "Error") || strstr(text, "say:")) {
      ui_Screen6_reset_ai_button();
    }
    example_lvgl_unlock();
  }
}

// Function to list available models for this API key
static void list_available_models(void *pvParameters) {
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

static void ai_wake_word_task(void *pvParameters) {
  ESP_LOGI(TAG, "Wake Word Task Started");

  // 0. Initialize Models from Flash Partition
  // On ESP32-P4, models are stored in a dedicated partition named "model"
  srmodel_list_t *models = esp_srmodel_init("model");
  if (!models) {
    ESP_LOGE(TAG, "Failed to initialize SR models from partition 'model'!");
    vTaskDelete(NULL);
    return;
  }
  ESP_LOGI(TAG, "SR Models initialized successfully. Found %d models.",
           models->num);
  for (int i = 0; i < models->num; i++) {
    ESP_LOGI(TAG, "  - %s", models->model_name[i]);
  }

  // 1. Initialize AFE (Acoustic Front-End)
  const esp_afe_sr_iface_t *afe_handle = &esp_afe_sr_v1;
  afe_config_t afe_config = AFE_CONFIG_DEFAULT();
  afe_config.wakenet_init = true;
  afe_config.aec_init = false;
  afe_config.se_init = true; // Enable Speech Enhancement
  afe_config.vad_init = true;
  afe_config.voice_communication_init = false;
  afe_config.afe_linear_gain = 3.0; // Boost sensitivity

  afe_config.pcm_config.total_ch_num = 2; // Total MUST be mic_num + ref_num
  afe_config.pcm_config.mic_num = 1;      // Mic on Ch0
  afe_config.pcm_config.ref_num = 1;      // Ref on Ch1 (we will zero it out)

  // Set model name explicitly for ESP32-P4 if default macro is NULL
#if CONFIG_SR_WN_WN9_HILEXIN
  afe_config.wakenet_model_name = "wn9_hilexin";
#elif CONFIG_SR_WN_WN9_JARVIS_TTS
  afe_config.wakenet_model_name = "wn9_jarvis_tts";
#elif CONFIG_SR_WN_WN9_HIESP
  afe_config.wakenet_model_name = "wn9_hiesp";
#else
  // Fallback to macro if defined, otherwise AFE will fail to create
  afe_config.wakenet_model_name = WAKENET_MODEL_NAME;
#endif

  esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(&afe_config);
  if (!afe_data) {
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

  // Buffers for I2S (Stereo) and AFE (Feed as Stereo but one is Zero)
  int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
  int n_ch_afe = 2; // Matches mic_num + ref_num
  int n_ch_i2s = 2; // I2S hardware is stereo

  ESP_LOGI(
      TAG,
      "AFE chunk size: %d samples, I2S buffer: %d bytes, AFE buffer: %d bytes",
      audio_chunksize, audio_chunksize * sizeof(int16_t) * n_ch_i2s,
      audio_chunksize * sizeof(int16_t) * n_ch_afe);

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

  ESP_LOGI(TAG, "Wake word task entering loop...");
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
      static int pause_log_cnt = 0;
      if (++pause_log_cnt >= 50) {
        ESP_LOGI(TAG, "AI Loop Paused: enabled=%d, paused=%d",
                 s_voice_activation_enabled, s_wake_word_paused);
        pause_log_cnt = 0;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // Ensure sample rate is correct for AFE (16kHz)
    // If another task changed it, we should ideally wait or reset it.
    // audio_manager now handles the reset, but we check here for safety.

    size_t bytes_read = 0;
    // 1. Read Stereo from I2S hardware
    // Use short timeout if we just want to check for button triggers
    int timeout_ms =
        (s_wake_word_paused || !s_voice_activation_enabled) ? 20 : 200;

    // DEBUG: Start timing
    uint32_t t_start = (uint32_t)esp_log_timestamp();

    esp_err_t ret =
        i2s_channel_read(rx_handle, i2s_stereo_buffer,
                         audio_chunksize * sizeof(int16_t) * n_ch_i2s,
                         &bytes_read, pdMS_TO_TICKS(timeout_ms));

    uint32_t t_read = (uint32_t)esp_log_timestamp();

    static int heartbeat_cnt = 0;
    if (++heartbeat_cnt >= 50) {
      ESP_LOGI(TAG,
               "AI Task Heartbeat: paused=%d, I2S_ret=%d, bytes=%d, chunk=%d, "
               "t_read=%" PRIu32 " ms",
               s_wake_word_paused, ret, (int)bytes_read, audio_chunksize,
               (uint32_t)(t_read - t_start));
      heartbeat_cnt = 0;
    }

    // Immediate check after read (in case read blocked for some reason)
    if (s_manual_trigger_requested)
      continue;

    size_t expected_bytes = audio_chunksize * sizeof(int16_t) * n_ch_i2s;
    if (ret == ESP_OK && bytes_read == expected_bytes) {
      // 2. Prepare AFE data
      for (int i = 0; i < audio_chunksize; i++) {
        afe_feed_buffer[i * 2] = i2s_stereo_buffer[i * 2]; // Mic from L
        afe_feed_buffer[i * 2 + 1] = 0;                    // Zero the Ref
      }

      // 3. Feed and fetch
      uint32_t t_feed_start = (uint32_t)esp_log_timestamp();
      afe_handle->feed(afe_data, afe_feed_buffer);
      uint32_t t_after_feed = (uint32_t)esp_log_timestamp();
      afe_fetch_result_t *res = afe_handle->fetch(afe_data);
      uint32_t t_fetch_end = (uint32_t)esp_log_timestamp();

      uint32_t feed_time = t_after_feed - t_feed_start;
      uint32_t fetch_time = t_fetch_end - t_after_feed;

      if (feed_time > 100 || fetch_time > 500) {
        ESP_LOGW(TAG, "AFE Timing: feed=%" PRIu32 " ms, fetch=%" PRIu32 " ms",
                 feed_time, fetch_time);
      }

      static int log_cnt = 0;
      if (++log_cnt >= 30) { // Log every ~1s heartbeat
        if (res) {
          if (res->data_volume > -50.0 || res->wakeup_state > 0) {
            ESP_LOGI(TAG, "WWD Loop: Vol=%.1f dB, VAD=%d, State=%d",
                     res->data_volume, res->vad_state, res->wakeup_state);
          }
        }
        log_cnt = 0;
      }

      if (res && res->wakeup_state == WAKENET_DETECTED) {
        ESP_LOGI(TAG, "WAKE WORD DETECTED!");
        process_ai_interaction();
      }
    } else if (ret != ESP_ERR_TIMEOUT) {
      ESP_LOGW(TAG, "I2S Read Error: %s", esp_err_to_name(ret));
    }
  }

  free(i2s_stereo_buffer);
  free(afe_feed_buffer);
  afe_handle->destroy(afe_data);
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
          if (strcmp(fn_name->valuestring, "generate_lua_rule") == 0 && fn_args) {
            cJSON *lua_code = cJSON_GetObjectItem(fn_args, "lua_code");
            if (lua_code && cJSON_IsString(lua_code)) {
                extern void ui_Screen6_set_lua_terminal_text(const char *text);
                if (example_lvgl_lock(500)) {
                    ui_Screen6_set_lua_terminal_text(lua_code->valuestring);
                    example_lvgl_unlock();
                }
                update_ui_status("✅ Код сгенерирован и добавлен в Терминал");
                speak_response("Код сгенерирован в терминале");
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

// Speak the response using TTS (save as WAV and play)
static void speak_response(const char *text) {
  // For now, just play a confirmation sound
  // TODO: Integrate Google TTS API to convert text to speech
  ESP_LOGI(TAG, "TTS would say: %s", text);

  // Play a simple notification sound if available
  // audio_play_tone(880, 200);  // Simple beep for now
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
  esp_err_t err = esp_http_client_open(client, strlen(post_data));
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
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
