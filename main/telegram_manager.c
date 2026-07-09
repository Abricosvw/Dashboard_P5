/**
 * @file telegram_manager.c
 * @brief Telegram integration using ESP-Claw's cap_im_tg as the single poller.
 *
 * This module no longer runs its own polling loop. Instead it:
 *   1. Initialises the ESP-Claw capability subsystem (claw_cap, event_router).
 *   2. Registers and starts cap_im_tg with the bot token.
 *   3. Adds an event-router rule that forwards every inbound Telegram text
 *      message to ai_manager_send_text_query() for dashboard AI processing.
 *   4. Provides telegram_send_message() as a thin wrapper around
 *      cap_im_tg_send_text().
 */

#include "telegram_manager.h"
#include "ai_commands.h"
#include "ai_config.h"
#include "ai_manager.h"
#include "ecu_data.h"
#include "cJSON.h"

#include "cap_im_tg.h"
#include "claw_cap.h"
#include "claw_event.h"
#include "claw_event_publisher.h"
#include "claw_event_router.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "TELEGRAM";
static bool telegram_running = false;

/* ------------------------------------------------------------------ */
/*  Event-router hook: called for every inbound Telegram text message  */
/* ------------------------------------------------------------------ */

/**
 * We register a simple event-router rule whose action is RUN_AGENT.
 * However, the claw_core agent isn't configured in this project, so
 * instead we use a lightweight polling approach: a small task watches
 * the event router for unprocessed Telegram messages and dispatches
 * them to ai_manager_send_text_query().
 *
 * The dispatcher listens on the event_router default queue.  When
 * cap_im_tg publishes a "message" event, the default route sends it
 * to the agent.  Since we have no agent, we intercept it here.
 */

/**
 * Lightweight dispatcher task.
 * cap_im_tg publishes messages through claw_event_router_publish_message().
 * With default_route_messages_to_agent=false, we instead add a rule that
 * calls a custom cap ("dashboard_tg_bridge") which forwards text to
 * ai_manager.
 */

/* ---- dashboard_tg_bridge capability ---- */

static SemaphoreHandle_t s_tg_bridge_sem = NULL;

static esp_err_t dashboard_tg_bridge_execute(const char *input_json,
                                              const claw_cap_call_context_t *ctx,
                                              char *output,
                                              size_t output_size) {
  (void)ctx;

  /* Serialize: only one Telegram message dispatched to AI at a time.
   * This prevents SDIO DMA pool exhaustion when a burst of messages arrives. */
  if (!s_tg_bridge_sem) {
    s_tg_bridge_sem = xSemaphoreCreateBinary();
    if (s_tg_bridge_sem) xSemaphoreGive(s_tg_bridge_sem);
  }

  if (!s_tg_bridge_sem || xSemaphoreTake(s_tg_bridge_sem, pdMS_TO_TICKS(3000)) != pdTRUE) {
    ESP_LOGW(TAG, "Bridge busy, dropping message");
    snprintf(output, output_size, "BUSY");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "tg_bridge_execute CALLED! input_json: %s", input_json ? input_json : "NULL");

  if (input_json && strlen(input_json) > 2) {
    cJSON *root = cJSON_Parse(input_json);
    if (root) {
      cJSON *text_json = cJSON_GetObjectItem(root, "text");
      if (cJSON_IsString(text_json) && text_json->valuestring &&
          text_json->valuestring[0]) {
        
        const char *msg = text_json->valuestring;
        ESP_LOGI(TAG, "Received Telegram Command: %s", msg);
        
        extern void ui_Screen7_append_text(const char *text);
        extern void ui_Screen6_set_lua_text(const char *text);
        extern bool example_lvgl_lock(int timeout_ms);
        extern void example_lvgl_unlock(void);
        
        /* Check if this is a native Telegram slash command */
        bool processed = false;
        
        if (strcasecmp(msg, "/help") == 0 || strcasecmp(msg, "help") == 0) {
            extern const char* lua_manager_get_help_text(void);
            telegram_send_message(lua_manager_get_help_text());
            
            // Highlight button click in UI for visual synchronization
            ai_cmd_click_ui_button("help");
            processed = true;
        } 
        else if (strcasecmp(msg, "/gpio") == 0 || strcasecmp(msg, "gpio") == 0) {
            const char *gpio_text = 
                "⚡ *ESP32-P4 GPIO Map:*\n\n"
                "• *GPIO 0-3:* JTAG / System (DO NOT USE)\n"
                "• *FREE PINS:* 6, 22-32, 34-38, 45-52\n";
            telegram_send_message(gpio_text);
            
            ai_cmd_click_ui_button("gpio");
            processed = true;
        } 
        else if (strcasecmp(msg, "/tg") == 0 || strcasecmp(msg, "tg") == 0 || 
                 strcasecmp(msg, "/telegram") == 0 || strcasecmp(msg, "telegram") == 0) {
            const char *tg_text = 
                "📲 *Telegram Messenger Setup & Commands:*\n\n"
                "1. Configure `BOT_TOKEN` in `ai_config.h`\n"
                "2. Configure `CHAT_ID` in `ai_config.h`\n"
                "3. Send `/lua <code>` to load Lua scripts\n"
                "4. Send text to ask AI questions\n\n"
                "📌 *Available Commands:*\n"
                "• `/help` - Show Lua scripting help\n"
                "• `/gpio` - Show physical GPIO mapping\n"
                "• `/tg` or `/telegram` - Show this Telegram guide\n"
                "• `/status` - Get system running status\n"
                "• `/ecu` - Fetch live vehicle ECU telemetry\n"
                "• `/launch` - Launch Control diagnostics (5 conditions)\n"
                "• `/demo_on` / `/demo_off` - Toggle live demo simulator\n"
                "• `/save` - Save current settings to SD card\n"
                "• `/lua <code>` - Upload Lua script to editor";
            telegram_send_message(tg_text);
            
            ai_cmd_click_ui_button("telegram");
            processed = true;
        }
        else if (strcasecmp(msg, "/status") == 0 || strcasecmp(msg, "status") == 0) {
            ai_cmd_result_t r = ai_cmd_get_status();
            telegram_send_message(r.message);
            processed = true;
        }
        else if (strcasecmp(msg, "/ecu") == 0 || strcasecmp(msg, "ecu") == 0) {
            ai_cmd_result_t r = ai_cmd_get_ecu_data();
            telegram_send_message(r.message);
            processed = true;
        }
        else if (strcasecmp(msg, "/demo_on") == 0) {
            ai_cmd_result_t r = ai_cmd_toggle_demo_mode(true);
            telegram_send_message(r.message);
            processed = true;
        }
        else if (strcasecmp(msg, "/demo_off") == 0) {
            ai_cmd_result_t r = ai_cmd_toggle_demo_mode(false);
            telegram_send_message(r.message);
            processed = true;
        }
        else if (strcasecmp(msg, "/save") == 0) {
            ai_cmd_result_t r = ai_cmd_save_settings();
            telegram_send_message(r.message);
            processed = true;
        }
        else if (strcasecmp(msg, "/launch") == 0 || strcasecmp(msg, "launch") == 0) {
            ecu_data_t data;
            ecu_data_get_copy(&data);

            bool c1 = (data.gear_lever_val == 12);
            bool c2 = (data.vehicle_speed < 1.0f);
            bool c3 = (data.pedal_position > 80.0f);
            bool c4 = (data.brake_status == 3);
            bool c5 = (data.tcu_torque_intervention);
            int met = c1 + c2 + c3 + c4 + c5;

            char lc_msg[512];
            snprintf(lc_msg, sizeof(lc_msg),
                "🏁 *Launch Control Diagnostics* (%d/5)\n\n"
                "%s 1. Selector: %s (%d)\n"
                "%s 2. Speed: %.0f km/h\n"
                "%s 3. Throttle: %.0f%%\n"
                "%s 4. Brake: %s (%d)\n"
                "%s 5. TCU Torque: %s\n\n"
                "RPM: %.0f | MAP: %.0f kPa\n"
                "Status: %s",
                met,
                c1 ? "✅" : "❌", c1 ? "Sport" : "N/A", data.gear_lever_val,
                c2 ? "✅" : "❌", data.vehicle_speed,
                c3 ? "✅" : "❌", data.pedal_position,
                c4 ? "✅" : "❌", c4 ? "Pressed" : "Released", data.brake_status,
                c5 ? "✅" : "❌", c5 ? "Active" : "Inactive",
                data.engine_rpm, data.map_kpa,
                met == 5 ? "🟢 LAUNCH ACTIVE" : (met > 0 ? "🟡 PRELAUNCH" : "⚪ STANDBY"));
            telegram_send_message(lc_msg);
            processed = true;
        }

        if (processed) {
            // Already handled natively above
        } else if (strncmp(msg, "/lua ", 5) == 0) {
          const char *lua_code = msg + 5;
          ESP_LOGI(TAG, "Telegram -> Lua Editor: %s", lua_code);
          
          if (example_lvgl_lock(500)) {
              ui_Screen6_set_lua_text(lua_code);
              ui_Screen7_append_text("[TG] Lua код загружен в редактор");
              example_lvgl_unlock();
          }
          
          /* Optionally auto-execute */
          if (strncmp(msg, "/lua!", 5) == 0) {
            extern esp_err_t lua_manager_execute(const char *script);
            lua_manager_execute(lua_code);
          }
          
          telegram_send_message("✅ Lua код загружен в редактор");
        } else {
          /* Normal text -> AI terminal (Screen 7) */
          if (example_lvgl_lock(500)) {
              char cmd_line[512];
              snprintf(cmd_line, sizeof(cmd_line), "[TG] > %s", msg);
              ui_Screen7_append_text(cmd_line);
              example_lvgl_unlock();
          }
          
          ai_manager_send_text_query(msg);
        }
      }
      cJSON_Delete(root);
    }
  }

  xSemaphoreGive(s_tg_bridge_sem);
  snprintf(output, output_size, "OK");
  return ESP_OK;
}



static const claw_cap_descriptor_t s_bridge_descriptor = {
    .id = "dashboard_tg_bridge",
    .name = "dashboard_tg_bridge",
    .family = "dashboard",
    .description = "Bridge: forwards Telegram messages to Dashboard AI",
    .kind = CLAW_CAP_KIND_CALLABLE,
    .cap_flags = 0, /* not callable by LLM, internal only */
    .input_schema_json =
        "{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"}}}",
    .execute = dashboard_tg_bridge_execute,
};

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void telegram_send_message(const char *msg) {
  if (!msg)
    return;

  /* Check if configured */
  if (strlen(TELEGRAM_BOT_TOKEN) < 10 ||
      strstr(TELEGRAM_BOT_TOKEN, "YOUR_BOT")) {
    ESP_LOGE(TAG, "Telegram not configured. Skipping message.");
    return;
  }

  if (!telegram_running) {
    ESP_LOGW(TAG, "Telegram not running yet, message queued for later.");
    return;
  }

  ESP_LOGI(TAG, "Sending Telegram message to chat ID: %s", TELEGRAM_CHAT_ID);
  esp_err_t err = cap_im_tg_send_text(TELEGRAM_CHAT_ID, msg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to send Telegram message: %s", esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "Telegram message sent OK");
  }
}

esp_err_t telegram_init(void) {
  if (telegram_running)
    return ESP_OK;

  /* Validate configuration */
  if (strlen(TELEGRAM_BOT_TOKEN) < 10 ||
      strstr(TELEGRAM_BOT_TOKEN, "YOUR_BOT")) {
    ESP_LOGW(TAG, "Telegram not configured, skipping init");
    return ESP_OK;
  }

  esp_err_t err;

  /* Step 1: Initialize Event Router (if not already init) */
  ESP_LOGI(TAG, "Ensuring ESP-Claw Event Router is ready...");
  
  /* Create the rules file if it doesn't exist */
  struct stat st;
  if (stat("/sdcard/SYSTEM", &st) != 0) mkdir("/sdcard/SYSTEM", 0755);
  if (stat("/sdcard/SYSTEM/RULES", &st) != 0) mkdir("/sdcard/SYSTEM/RULES", 0755);
  FILE *f = fopen("/sdcard/SYSTEM/RULES/rt_rules", "r");
  if (!f) {
      f = fopen("/sdcard/SYSTEM/RULES/rt_rules", "w");
      if (f) {
          fprintf(f, "[]");
          fclose(f);
      }
  } else {
      fclose(f);
  }

  claw_event_router_config_t router_cfg = {
      .rules_path = "/sdcard/SYSTEM/RULES/rt_rules",
      .max_rules = 16,
      .max_actions_per_rule = 4,
      .cap_output_size = 1024,
      .event_queue_len = 6,   /* max allowed by claw_event_router pending table */
      .task_stack_size = 8192,
      .task_priority = 5,
      .task_core = tskNO_AFFINITY,
      .core_submit_timeout_ms = 5000,
      .core_receive_timeout_ms = 5000,
      .default_route_messages_to_agent = false,
  };
  
  err = claw_event_router_init(&router_cfg);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "claw_event_router_init failed: %s", esp_err_to_name(err));
    return err;
  }

  /* Step 2: Register our bridge capability */
  ESP_LOGI(TAG, "Registering dashboard_tg_bridge capability...");
  err = claw_cap_register(&s_bridge_descriptor);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to register bridge cap: %s", esp_err_to_name(err));
    return err;
  }

  /* Step 3: Configure cap_im_tg (already registered by app_capabilities_init) */
  ESP_LOGI(TAG, "Configuring ESP-Claw Telegram module...");
  err = cap_im_tg_set_token(TELEGRAM_BOT_TOKEN);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "cap_im_tg_set_token failed: %s", esp_err_to_name(err));
    return err;
  }

  /* Enable inbound file attachments (saved to SD card) */
  cap_im_tg_attachment_config_t att_cfg = {
      .storage_root_dir = "/sdcard/SYSTEM/TELEGRAM",
      .max_inbound_file_bytes = 2 * 1024 * 1024,
      .enable_inbound_attachments = true,
  };
  cap_im_tg_set_attachment_config(&att_cfg);

  /* Step 4: Add an event-router rule to forward Telegram messages to our bridge */
  ESP_LOGI(TAG, "Adding Telegram -> Dashboard routing rule...");
  const char *rule_json =
      "{"
      "  \"id\": \"tg_to_dashboard\","
      "  \"enabled\": true,"
      "  \"consume_on_match\": true,"
      "  \"description\": \"Forward Telegram text to Dashboard AI\","
      "  \"match\": {"
      "    \"event_type\": \"message\","
      "    \"source_cap\": \"tg_gateway\","
      "    \"channel\": \"telegram\""
      "  },"
      "  \"actions\": [{"
      "    \"type\": \"call_cap\","
      "    \"caller\": \"\","
      "    \"cap\": \"dashboard_tg_bridge\","
      "    \"input\": {\"text\": \"{{event.text}}\"}"
      "  }]"
      "}";
  err = claw_event_router_add_rule_json(rule_json);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Note: routing rule addition returned %s", esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "Routing rule 'tg_to_dashboard' added successfully");
  }

  /* Step 5: Start the event router */
  err = claw_event_router_start();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "claw_event_router_start failed: %s", esp_err_to_name(err));
    return err;
  }

  /* Step 6: Start cap_im_tg polling */
  ESP_LOGI(TAG, "Starting ESP-Claw Telegram polling...");
  err = cap_im_tg_start();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "cap_im_tg_start failed: %s", esp_err_to_name(err));
    return err;
  }

  telegram_running = true;
  ESP_LOGI(TAG, "ESP-Claw Telegram integration active!");
  return ESP_OK;
}

