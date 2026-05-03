#include "lua_manager.h"
#include "esp_log.h"
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "can_manager.h" // For getting CAN data
#include "ui/settings_config.h" // For brightness etc
#include "ui/screens/ui_Screen6.h" // For setting terminal text

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include "ecu_data.h"

static const char *TAG = "LUA_MGR";
static lua_State *L = NULL;
static SemaphoreHandle_t lua_mutex = NULL;
static char *g_background_script = NULL;
static TaskHandle_t lua_bg_task_handle = NULL;
static int g_tick_rate_hz = 10;
static bool g_bg_script_loaded = false;
static uint32_t g_can_rx_filters[16];
static int g_can_rx_filter_count = 0;

// --- Includes for Bindings ---
#include "can_logger.h"
#include "ui/ui_screen_manager.h"
#include "ui/ui_layout_manager.h"
#include "driver/gpio.h"

extern lv_obj_t *ui_Button_Run_Rule;
extern lv_obj_t *ui_Button_Save_Rule;
extern lv_obj_t *ui_Button_Help_Rule;
extern bool example_lvgl_lock(int timeout_ms);
extern void example_lvgl_unlock(void);

// --- ECU Bindings ---

static int l_get_rpm(lua_State *L) {
    ecu_data_t data;
    ecu_data_get_copy(&data);
    lua_pushnumber(L, data.engine_rpm);
    return 1;
}

static int l_get_engine_temp(lua_State *L) {
    ecu_data_t data;
    ecu_data_get_copy(&data);
    lua_pushnumber(L, data.clt_temp);
    return 1;
}

static int l_set_fan_speed(lua_State *L) {
    int speed = luaL_checkinteger(L, 1);
    ESP_LOGI(TAG, "Virtual Fan Speed set to: %d%%", speed);
    return 0;
}

static int l_show_warning(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    ESP_LOGW(TAG, "LUA WARNING: %s", msg);
    data_stream_add_entry(msg, LOG_WARNING);
    
    char status_buf[256];
    snprintf(status_buf, sizeof(status_buf), "WARNING: %s", msg);
    if(example_lvgl_lock(100)) {
        extern void ui_Screen6_set_ai_info(const char *text);
        ui_Screen6_set_ai_info(status_buf);
        example_lvgl_unlock();
    }
    return 0;
}

static int l_log_message(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    ESP_LOGI("LUA_SCRIPT", "%s", msg);
    return 0;
}

// --- New System Bindings ---

static int l_start_can_log(lua_State *L) {
    ESP_LOGI(TAG, "LUA: Starting CAN Logger");
    can_logger_start();
    return 0;
}

static int l_stop_can_log(lua_State *L) {
    ESP_LOGI(TAG, "LUA: Stopping CAN Logger");
    can_logger_stop();
    return 0;
}

static int l_switch_screen(lua_State *L) {
    int screen_id = luaL_checkinteger(L, 1);
    if (screen_id >= 1 && screen_id <= 8) {
        if (example_lvgl_lock(100)) {
            ui_switch_to_screen((screen_id_t)(screen_id - 1));
            example_lvgl_unlock();
        }
    }
    return 0;
}

static void defer_click_task(void *pvParameter) {
    int btn_id = (int)pvParameter;
    vTaskDelay(pdMS_TO_TICKS(100)); // Defer to prevent Lua Mutex deadlock
    if (example_lvgl_lock(500)) {
        if (btn_id == 1 && ui_Button_Run_Rule) lv_event_send(ui_Button_Run_Rule, LV_EVENT_CLICKED, NULL);
        else if (btn_id == 2 && ui_Button_Save_Rule) lv_event_send(ui_Button_Save_Rule, LV_EVENT_CLICKED, NULL);
        else if (btn_id == 3 && ui_Button_Help_Rule) lv_event_send(ui_Button_Help_Rule, LV_EVENT_CLICKED, NULL);
        example_lvgl_unlock();
    }
    vTaskDelete(NULL);
}

static int l_click_btn_run(lua_State *L) {
    xTaskCreatePinnedToCore(defer_click_task, "click_run", 2048, (void*)1, 5, NULL, 1);
    return 0;
}

static int l_click_btn_save(lua_State *L) {
    xTaskCreatePinnedToCore(defer_click_task, "click_save", 2048, (void*)2, 5, NULL, 1);
    return 0;
}

static int l_click_btn_help(lua_State *L) {
    xTaskCreatePinnedToCore(defer_click_task, "click_help", 2048, (void*)3, 5, NULL, 1);
    return 0;
}

static int l_set_gauge_visible(lua_State *L) {
    int gauge_id = luaL_checkinteger(L, 1);
    bool visible = lua_toboolean(L, 2);
    
    system_settings_t *settings = system_settings_get();
    switch (gauge_id) {
        case 0: settings->show_map = visible; break;
        case 1: settings->show_wastegate = visible; break;
        case 2: settings->show_tps = visible; break;
        case 3: settings->show_rpm = visible; break;
        case 4: settings->show_boost = visible; break;
        case 5: settings->show_tcu = visible; break;
        // Map other IDs if needed...
        default: ESP_LOGW(TAG, "Unknown gauge ID %d", gauge_id); break;
    }
    system_settings_save(settings);
    
    if (example_lvgl_lock(100)) {
        ui_update_global_layout();
        example_lvgl_unlock();
    }
    return 0;
}

// --- GPIO Safety Filter ---
static bool is_pin_safe(int pin) {
    const int safe_pins[] = {6, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 34, 35, 36, 37, 38, 45, 46, 47, 48, 49, 50, 51, 52};
    for (int i = 0; i < sizeof(safe_pins)/sizeof(safe_pins[0]); i++) {
        if (pin == safe_pins[i]) return true;
    }
    return false;
}

static int l_gpio_set(lua_State *L) {
    int pin = luaL_checkinteger(L, 1);
    int level = luaL_checkinteger(L, 2);
    
    if (!is_pin_safe(pin)) {
        ESP_LOGE(TAG, "LUA GPIO ERROR: Pin %d is protected by the system!", pin);
        return 0;
    }
    
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, level);
    ESP_LOGI(TAG, "LUA GPIO: Set Pin %d to %d", pin, level);
    return 0;
}

static int l_gpio_get(lua_State *L) {
    int pin = luaL_checkinteger(L, 1);
    
    if (!is_pin_safe(pin)) {
        ESP_LOGE(TAG, "LUA GPIO ERROR: Pin %d is protected by the system!", pin);
        lua_pushinteger(L, 0);
        return 1;
    }
    
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    int level = gpio_get_level(pin);
    lua_pushinteger(L, level);
    return 1;
}

// --- rusEFI CAN Bindings ---

static int l_txCan(lua_State *L) {
    // txCan(bus, ID, isExt, payload_table)
    // bus is ignored on this hardware since we only have one TWAI interface
    uint32_t id = luaL_checkinteger(L, 2);
    int isExt = luaL_checkinteger(L, 3);
    
    luaL_checktype(L, 4, LUA_TTABLE); // Payload must be a table
    
    twai_message_t msg = {0};
    msg.identifier = id;
    msg.extd = (isExt != 0) ? 1 : 0;
    
    size_t len = lua_rawlen(L, 4);
    if (len > 8) len = 8;
    msg.data_length_code = len;
    
    for (size_t i = 0; i < len; i++) {
        lua_rawgeti(L, 4, i + 1); // Lua tables are 1-indexed
        msg.data[i] = (uint8_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    
    ESP_LOGI(TAG, "txCan Debug: ID=0x%lX, extd=%d, dlc=%d", msg.identifier, msg.extd, msg.data_length_code);
    
    esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(10));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Lua txCan failed: %s", esp_err_to_name(err));
    }
    
    return 0;
}

static int l_canRxAdd(lua_State *L) {
    // canRxAdd(id) or canRxAdd(bus, id)
    uint32_t id = 0;
    if (lua_gettop(L) >= 2) {
        id = luaL_checkinteger(L, 2);
    } else {
        id = luaL_checkinteger(L, 1);
    }
    
    if (g_can_rx_filter_count < 16) {
        g_can_rx_filters[g_can_rx_filter_count++] = id;
        ESP_LOGI(TAG, "Lua registered CAN filter ID: 0x%lX", id);
    } else {
        ESP_LOGW(TAG, "Lua CAN filter limit reached!");
    }
    return 0;
}

static int l_setTickRate(lua_State *L) {
    int hz = luaL_checkinteger(L, 1);
    if (hz < 1) hz = 1;
    if (hz > 1000) hz = 1000;
    g_tick_rate_hz = hz;
    return 0;
}

static int l_mcu_standby(lua_State *L) {
    ESP_LOGI(TAG, "Lua requested mcu_standby (not fully supported)");
    vTaskDelay(pdMS_TO_TICKS(1000));
    return 0;
}

// --- Dynamic Bindings & Help Generation ---
typedef struct {
    const char *name;
    lua_CFunction func;
    const char *help_desc;
} lua_binding_t;

static const lua_binding_t g_lua_bindings[] = {
    {"get_rpm", l_get_rpm, "get_rpm() - Returns the current Engine RPM as a number."},
    {"get_engine_temp", l_get_engine_temp, "get_engine_temp() - Returns coolant temp in Celsius."},
    {"set_fan_speed", l_set_fan_speed, "set_fan_speed(speed) - Sets virtual fan speed (0-100%)."},
    {"show_warning", l_show_warning, "show_warning(\"msg\") - Shows a popup warning on the UI."},
    {"log", l_log_message, "log(\"msg\") - Prints a message to the ESP32 serial console."},
    {"start_can_log", l_start_can_log, "start_can_log() - Starts recording CAN data to SD card."},
    {"stop_can_log", l_stop_can_log, "stop_can_log() - Stops CAN data recording."},
    {"switch_screen", l_switch_screen, "switch_screen(id) - Switches the UI to screen ID (1-8)."},
    {"click_btn_run", l_click_btn_run, "click_btn_run() - Simulates clicking 'Run Rule'."},
    {"click_btn_save", l_click_btn_save, "click_btn_save() - Simulates clicking 'Save to ESP-Claw'."},
    {"click_btn_help", l_click_btn_help, "click_btn_help() - Simulates clicking 'Help'."},
    {"set_gauge_visible", l_set_gauge_visible, "set_gauge_visible(id, visible) - Shows/hides gauge (e.g. 3=RPM)."},
    {"gpio_set", l_gpio_set, "gpio_set(pin, level) - Sets safe pin to 0 or 1."},
    {"gpio_get", l_gpio_get, "gpio_get(pin) - Returns state of safe pin (0 or 1)."},
    {"txCan", l_txCan, "txCan(bus, id, isExt, payload) - Transmit CAN frame (rusEFI)."},
    {"canRxAdd", l_canRxAdd, "canRxAdd([bus], id) - Subscribe Lua to CAN ID (rusEFI)."},
    {"setTickRate", l_setTickRate, "setTickRate(hz) - Set background tick rate (rusEFI)."},
    {"mcu_standby", l_mcu_standby, "mcu_standby() - Simulated standby (rusEFI)."},
};
static const int num_bindings = sizeof(g_lua_bindings) / sizeof(g_lua_bindings[0]);

static char g_help_text_buffer[1024] = {0};

const char* lua_manager_get_help_text(void) {
    if (g_help_text_buffer[0] == '\0') {
        // Generate it once
        strcpy(g_help_text_buffer, "Available ESP-Claw Lua Functions:\n\n");
        for (int i = 0; i < num_bindings; i++) {
            strcat(g_help_text_buffer, "-> ");
            strcat(g_help_text_buffer, g_lua_bindings[i].help_desc);
            strcat(g_help_text_buffer, "\n");
        }
    }
    return g_help_text_buffer;
}

void lua_bind_dashboard_functions(void) {
    if (!L) return;
    for (int i = 0; i < num_bindings; i++) {
        lua_register(L, g_lua_bindings[i].name, g_lua_bindings[i].func);
    }
}

// --- Background Task ---
static void lua_background_task(void *pvParameters) {
    ESP_LOGI(TAG, "Lua Background Event Router Task Started");
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (1) {
        int delay_ms = 1000 / (g_tick_rate_hz > 0 ? g_tick_rate_hz : 10);
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(delay_ms));
        
        if (g_background_script && L && lua_mutex) {
            if (xSemaphoreTake(lua_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                if (!g_bg_script_loaded) {
                    if (luaL_dostring(L, g_background_script) != LUA_OK) {
                        const char *err = lua_tostring(L, -1);
                        ESP_LOGE(TAG, "Background Lua load error: %s", err);
                        lua_pop(L, 1);
                    } else {
                        ESP_LOGI(TAG, "Background Lua script loaded successfully.");
                    }
                    g_bg_script_loaded = true;
                }
                
                // Call onTick() if defined
                lua_getglobal(L, "onTick");
                if (lua_isfunction(L, -1)) {
                    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
                        const char *err = lua_tostring(L, -1);
                        ESP_LOGE(TAG, "Lua onTick error: %s", err);
                        lua_pop(L, 1);
                    }
                } else {
                    lua_pop(L, 1); // Pop the non-function value
                }
                
                xSemaphoreGive(lua_mutex);
            }
        }
    }
}

// --- CAN Event Dispatcher ---
void lua_manager_handle_can_rx(uint32_t id, const uint8_t *data, uint8_t dlc) {
    if (!L || !lua_mutex || !g_bg_script_loaded) return;
    
    // Check filters
    bool match = false;
    for (int i = 0; i < g_can_rx_filter_count; i++) {
        if (g_can_rx_filters[i] == id) {
            match = true;
            break;
        }
    }
    if (!match) return;
    
    if (xSemaphoreTake(lua_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        lua_getglobal(L, "onCanRx");
        if (lua_isfunction(L, -1)) {
            lua_pushinteger(L, 1); // bus
            lua_pushinteger(L, id); // id
            lua_pushinteger(L, dlc); // dlc
            
            // data array
            lua_newtable(L);
            for (int i = 0; i < dlc; i++) {
                lua_pushinteger(L, data[i]);
                lua_rawseti(L, -2, i + 1); // 1-indexed
            }
            
            if (lua_pcall(L, 4, 0, 0) != LUA_OK) {
                const char *err = lua_tostring(L, -1);
                ESP_LOGE(TAG, "Lua onCanRx error: %s", err);
                lua_pop(L, 1);
            }
        } else {
            lua_pop(L, 1); // pop non-function
        }
        xSemaphoreGive(lua_mutex);
    }
}

// --- Core API ---

esp_err_t lua_manager_init(void) {
    if (L) return ESP_OK;

    ESP_LOGI(TAG, "Initializing Lua Engine...");
    
    lua_mutex = xSemaphoreCreateMutex();
    if (!lua_mutex) {
        ESP_LOGE(TAG, "Failed to create Lua mutex");
        return ESP_FAIL;
    }

    L = luaL_newstate();
    if (!L) {
        ESP_LOGE(TAG, "Failed to create Lua state");
        return ESP_FAIL;
    }

    luaL_openlibs(L);
    lua_bind_dashboard_functions();
    
    ESP_LOGI(TAG, "Lua Engine Initialized.");
    return ESP_OK;
}

esp_err_t lua_manager_execute(const char *script) {
    if (!L || !lua_mutex) {
        ESP_LOGE(TAG, "Lua not initialized");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Executing Lua script (Manual Run)...");
    
    if (xSemaphoreTake(lua_mutex, portMAX_DELAY) == pdTRUE) {
        if (luaL_dostring(L, script) != LUA_OK) {
            const char *err = lua_tostring(L, -1);
            ESP_LOGE(TAG, "Lua execution error: %s", err);
            
            char err_msg[256];
            snprintf(err_msg, sizeof(err_msg), "Error: %s", err);
            ui_Screen6_set_lua_terminal_text(err_msg);
            
            lua_pop(L, 1);
            xSemaphoreGive(lua_mutex);
            return ESP_FAIL;
        }
        xSemaphoreGive(lua_mutex);
    }
    
    ESP_LOGI(TAG, "Lua script executed successfully.");
    return ESP_OK;
}

esp_err_t lua_manager_save_background_script(const char *script) {
    if (!script) return ESP_ERR_INVALID_ARG;
    
    if (g_background_script) {
        free(g_background_script);
    }
    g_background_script = strdup(script);
    g_bg_script_loaded = false;
    g_tick_rate_hz = 10;
    g_can_rx_filter_count = 0;
    
    if (!lua_bg_task_handle) {
        xTaskCreatePinnedToCore(lua_background_task, "lua_bg_task", 8192, NULL, 4, &lua_bg_task_handle, 1);
    }
    
    ESP_LOGI(TAG, "Background script saved and router active.");
    return ESP_OK;
}
