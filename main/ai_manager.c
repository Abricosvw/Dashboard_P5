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
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

// Manual declarations to resolve header conflicts
bool example_lvgl_lock(int timeout_ms);
void example_lvgl_unlock(void);
extern void ui_Screen6_set_ai_info(const char *text);

static const char *TAG = "AI_MGR_GEMINI";

// System prompt for Gemini - provides context about the device
static const char *SYSTEM_PROMPT =
    "Ты голосовой ассистент для автомобильной приборной панели ECU Dashboard "
    "на ESP32-P4. "
    "Ты можешь управлять устройством голосовыми командами. Отвечай кратко на "
    "русском языке. "
    "Доступные экраны: 1-Главный (MAP,RPM,Boost), 2-Датчики (масло,вода), "
    "3-CAN терминал, "
    "4-Дополнительные датчики, 5-Крутящий момент, 6-Настройки. "
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
    "}]";

static void update_ui_status(const char *text) {
  if (example_lvgl_lock(500)) {
    ui_Screen6_set_ai_info(text);
    example_lvgl_unlock();
  }
}

// Function to list available models for this API key
static void list_available_models(void) {
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
    return;
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
}

esp_err_t ai_manager_init(void) {
  ESP_LOGI(TAG, "Initializing Gemini AI Manager...");
  ESP_LOGI(TAG, "API Key (first 10 chars): %.10s...", GEMINI_API_KEY);
  update_ui_status("AI: System Ready\n(Gemini Integration)");
  return ESP_OK;
}

esp_err_t ai_manager_start(void) {
  ESP_LOGI(TAG, "Gemini AI Manager Started");
  ESP_LOGI(TAG, "Checking available models...");
  list_available_models();
  return ESP_OK;
}

// Forward declaration
static void speak_response(const char *text);

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

          // Execute the function
          ai_cmd_result_t result =
              ai_execute_function_call(fn_name->valuestring, args_str);

          if (args_str)
            free(args_str);

          // Show result to user
          char status_buf[512];
          snprintf(status_buf, sizeof(status_buf), "%s\n\n%s",
                   result.success ? "✅ Команда выполнена:" : "❌ Ошибка:",
                   result.message);
          update_ui_status(status_buf);

          // Speak the result
          speak_response(result.message);

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
  ESP_LOGI(TAG, "Gemini Triggered - Recording 5s...");

  // Recording duration in seconds
  const int record_seconds = 5;

  // Show recording message with duration
  update_ui_status("🎤 Recording...\n\nSpeak now! [5 sec]");

  // Force immediate screen refresh before blocking recording
  lv_refr_now(NULL);

  // Create AI_Rec directory if it doesn't exist
  const char *ai_rec_dir = "/sdcard/SYSTEM/SOUND/AI_Rec";
  struct stat st = {0};
  if (stat(ai_rec_dir, &st) == -1) {
    mkdir(ai_rec_dir, 0755);
    ESP_LOGI(TAG, "Created AI recording directory: %s", ai_rec_dir);
  }

  const char *wav_path = "/sdcard/SYSTEM/SOUND/AI_Rec/ai_rec.wav";

  // Start blocking recording
  esp_err_t rec_err = audio_record_wav(wav_path, record_seconds * 1000);
  if (rec_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to record audio: %s", esp_err_to_name(rec_err));
    update_ui_status("AI: Error\nMic/SD Card failure");
    return;
  }

  // Recording complete
  update_ui_status("🔄 Processing...\n\nEncoding audio...");

  FILE *f = fopen(wav_path, "rb");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open recorded file");
    update_ui_status("AI: Error\nFile access failure");
    return;
  }
  fseek(f, 0, SEEK_END);
  size_t file_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  unsigned char *raw_audio = malloc(file_size);
  if (!raw_audio) {
    fclose(f);
    update_ui_status("AI: Error\nMemory allocation failure");
    return;
  }
  fread(raw_audio, 1, file_size, f);
  fclose(f);

  size_t b64_len = 0;
  mbedtls_base64_encode(NULL, 0, &b64_len, raw_audio, file_size);
  unsigned char *b64_audio = malloc(b64_len + 1);
  if (!b64_audio) {
    free(raw_audio);
    update_ui_status("AI: Error\nB64 allocation failure");
    return;
  }

  size_t out_len = 0;
  mbedtls_base64_encode(b64_audio, b64_len, &out_len, raw_audio, file_size);
  b64_audio[out_len] = '\0';
  free(raw_audio);

  update_ui_status("AI: Thinking...\n(Uploading to Gemini)");

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
    update_ui_status("AI: Error\nConnection failed");
    free(post_data);
    esp_http_client_cleanup(client);
    return;
  }

  // Write POST data
  int wlen = esp_http_client_write(client, post_data, strlen(post_data));
  if (wlen < 0) {
    ESP_LOGE(TAG, "Write failed");
    update_ui_status("AI: Error\nWrite failed");
    free(post_data);
    esp_http_client_cleanup(client);
    return;
  }

  // Fetch headers to get status code
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
      snprintf(status_err, sizeof(status_err), "AI: Error HTTP %d\nSee logs",
               status_code);
      update_ui_status(status_err);
    } else {
      ESP_LOGE(TAG, "No response body (HTTP %d)", status_code);
      char status_err[64];
      snprintf(status_err, sizeof(status_err), "AI: Error HTTP %d\nEmpty body",
               status_code);
      update_ui_status(status_err);
    }
    free(res_buf);
  }

  esp_http_client_close(client);
  free(post_data);
  esp_http_client_cleanup(client);
}
