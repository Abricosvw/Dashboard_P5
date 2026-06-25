#include "lua_manager.h"
#include "esp_log.h"
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "can_manager.h" // For getting CAN data
#include "ui/settings_config.h" // For brightness etc
#include "ui/screens/ui_Screen6.h" // For setting terminal text
#include "ui/screens/ui_Screen9.h" // For pump/fan speed getters
#include "ui/screens/ui_Screen10.h" // For wastegate/bov getters/setters

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include "ecu_data.h"

static const char *TAG = "LUA_MGR";
static lua_State *L = NULL;
static SemaphoreHandle_t lua_mutex = NULL;
static char *g_background_script = NULL;
static TaskHandle_t lua_bg_task_handle = NULL;
static int g_tick_rate_hz = 10;
static bool g_bg_script_loaded = false;
typedef struct {
    int bus;
    uint32_t id;
    int callback_ref;
} can_rx_filter_t;

static can_rx_filter_t g_can_rx_filters[16];
static int g_can_rx_filter_count = 0;

// --- Includes for Bindings ---
#include "can_logger.h"
#include "ui/ui_screen_manager.h"
#include "ui/ui_layout_manager.h"
#include "driver/gpio.h"
#include "telegram_manager.h"

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
        extern void ui_Screen7_set_status(const char *text);
        ui_Screen7_set_status(status_buf);
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
    if (screen_id >= 1 && screen_id <= 10) {
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

static int l_telegram_send(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    ESP_LOGI(TAG, "LUA requested Telegram send: %s", msg);
    telegram_send_message(msg);
    return 0;
}

// --- rusEFI CAN Bindings ---

static int l_txCan(lua_State *L) {
    // txCan(bus, ID, isExt, payload_table)
    int bus = luaL_checkinteger(L, 1);
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
    
    ESP_LOGD(TAG, "txCan: Bus=%d, ID=0x%lX, dlc=%d", bus, (unsigned long)msg.identifier, msg.data_length_code);
    
    twai_handle_t handle = (bus == 2) ? g_can2_handle : g_can1_handle;
    esp_err_t err = ESP_OK;
    if (handle != NULL) {
        err = twai_transmit_v2(handle, &msg, pdMS_TO_TICKS(10));
    } else {
        err = ESP_ERR_INVALID_STATE;
    }
    
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Lua txCan failed: %s", esp_err_to_name(err));
    } else {
        // Log to UI sniffer as TX
        extern void ui_process_real_can_message_bus(uint8_t bus_id, uint32_t id, uint8_t *data, uint8_t dlc, bool is_tx);
        if (example_lvgl_lock(10)) {
            ui_process_real_can_message_bus(bus, msg.identifier, msg.data, msg.data_length_code, true); // true = TX
            example_lvgl_unlock();
        }
    }
    
    return 0;
}

static int l_canRxAdd(lua_State *L) {
    // canRxAdd(id) or canRxAdd(id, callback) or canRxAdd(bus, id) or canRxAdd(bus, id, callback)
    int num_args = lua_gettop(L);
    int bus = 1;
    uint32_t id = 0;
    int callback_arg_idx = 0;
    
    if (num_args == 1) {
        id = luaL_checkinteger(L, 1);
    } else if (num_args == 2) {
        if (lua_isfunction(L, 2)) {
            id = luaL_checkinteger(L, 1);
            callback_arg_idx = 2;
        } else {
            bus = luaL_checkinteger(L, 1);
            id = luaL_checkinteger(L, 2);
        }
    } else if (num_args >= 3) {
        bus = luaL_checkinteger(L, 1);
        id = luaL_checkinteger(L, 2);
        if (lua_isfunction(L, 3)) {
            callback_arg_idx = 3;
        }
    }
    
    int callback_ref = LUA_NOREF;
    if (callback_arg_idx > 0) {
        lua_pushvalue(L, callback_arg_idx);
        callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    
    int found_idx = -1;
    for (int i = 0; i < g_can_rx_filter_count; i++) {
        if (g_can_rx_filters[i].bus == bus && g_can_rx_filters[i].id == id) {
            found_idx = i;
            break;
        }
    }
    
    if (found_idx != -1) {
        // Overwrite
        if (g_can_rx_filters[found_idx].callback_ref != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, g_can_rx_filters[found_idx].callback_ref);
        }
        g_can_rx_filters[found_idx].callback_ref = callback_ref;
        ESP_LOGI(TAG, "Lua updated CAN filter ID: 0x%lX (Bus %d)", (unsigned long)id, bus);
    } else {
        // Add new
        if (g_can_rx_filter_count < 16) {
            g_can_rx_filters[g_can_rx_filter_count].bus = bus;
            g_can_rx_filters[g_can_rx_filter_count].id = id;
            g_can_rx_filters[g_can_rx_filter_count].callback_ref = callback_ref;
            g_can_rx_filter_count++;
            ESP_LOGI(TAG, "Lua registered CAN filter ID: 0x%lX (Bus %d, callback=%d)", (unsigned long)id, bus, callback_ref != LUA_NOREF);
        } else {
            ESP_LOGW(TAG, "Lua CAN filter limit reached!");
            if (callback_ref != LUA_NOREF) {
                luaL_unref(L, LUA_REGISTRYINDEX, callback_ref);
            }
        }
    }
    return 0;
}

static int l_getBitRange(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    int bitIndex = luaL_checkinteger(L, 2);
    int bitWidth = luaL_checkinteger(L, 3);
    
    size_t len = lua_rawlen(L, 1);
    int byteIndex = bitIndex >> 3;
    int shift = bitIndex - byteIndex * 8;
    
    uint64_t val = 0;
    for (int i = 0; i < 5; i++) {
        int idx = byteIndex + i;
        if (idx < len) {
            lua_rawgeti(L, 1, idx + 1);
            uint8_t b = (uint8_t)lua_tointeger(L, -1);
            lua_pop(L, 1);
            val |= ((uint64_t)b) << (i * 8);
        }
    }
    
    uint64_t mask = (1ULL << bitWidth) - 1ULL;
    if (bitWidth >= 64) {
        mask = ~0ULL;
    }
    
    uint64_t result = (val >> shift) & mask;
    lua_pushinteger(L, result);
    return 1;
}

static int l_setLuaGauge(lua_State *L) {
    int gauge_id = luaL_checkinteger(L, 1);
    float value = luaL_checknumber(L, 2);
    
    typedef struct {
        int id;
        float val;
    } gauge_update_ctx_t;
    
    void update_gauge_tx(ecu_data_t *state, void *ctx) {
        gauge_update_ctx_t *g = (gauge_update_ctx_t *)ctx;
        switch (g->id) {
            case GAUGE_MAP: state->map_kpa = g->val; break;
            case GAUGE_WASTEGATE: state->wg_set_percent = g->val; break;
            case GAUGE_TPS: state->tps_position = g->val; break;
            case GAUGE_RPM: state->engine_rpm = g->val; break;
            case GAUGE_BOOST: state->wg_pos_percent = g->val; break;
            case GAUGE_OIL_PRESS: state->oil_pressure = g->val; break;
            case GAUGE_OIL_TEMP: state->oil_temp = g->val; break;
            case GAUGE_WATER_TEMP: state->clt_temp = g->val; break;
            case GAUGE_BATTERY: state->battery_voltage = g->val; break;
            case GAUGE_PEDAL: state->abs_pedal_pos = g->val; break;
            case GAUGE_WG_POS: state->wg_pos_percent = g->val; break;
            case GAUGE_BOV: state->bov_percent = g->val; break;
            case GAUGE_IAT: state->iat_temp = g->val; break;
            case GAUGE_SPEED: state->vehicle_speed = g->val; break;
            case GAUGE_TRANS_TEMP: state->trans_temp = g->val; break;
            case GAUGE_AFR: state->afr_val = g->val; break;
            case GAUGE_EGT: state->egt_temp = g->val; break;
            case GAUGE_KNOCK_RETARD: state->knock_retard = g->val; break;
            case GAUGE_AMBIENT_TEMP: state->ambient_temp = g->val; break;
            default:
                if (g->id == 1) state->tps_position = g->val;
                else if (g->id == 2) state->engine_rpm = g->val;
                else if (g->id == 3) state->clt_temp = g->val;
                else if (g->id == 4) state->iat_temp = g->val;
                break;
        }
    }
    
    gauge_update_ctx_t ctx = { .id = gauge_id, .val = value };
    ecu_data_update_transaction(update_gauge_tx, &ctx);
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

static int l_get_pump_speed(lua_State *L) {
    lua_pushinteger(L, ui_Screen9_get_actual_pump_speed());
    return 1;
}

static int l_get_fan_speed(lua_State *L) {
    lua_pushinteger(L, ui_Screen9_get_actual_fan_speed());
    return 1;
}

static int l_get_pump_mode(lua_State *L) {
    lua_pushboolean(L, ui_Screen9_get_pump_is_auto());
    return 1;
}

static int l_set_pump_mode(lua_State *L) {
    bool is_auto = lua_toboolean(L, 1);
    ui_Screen9_set_pump_is_auto(is_auto);
    return 0;
}

static int l_get_pump_state(lua_State *L) {
    lua_pushboolean(L, ui_Screen9_get_pump_manual_on());
    return 1;
}

static int l_set_pump_state(lua_State *L) {
    bool manual_on = lua_toboolean(L, 1);
    ui_Screen9_set_pump_manual_on(manual_on);
    return 0;
}

static int l_get_pump_manual_speed(lua_State *L) {
    lua_pushinteger(L, ui_Screen9_get_pump_manual_speed());
    return 1;
}

static int l_set_pump_manual_speed(lua_State *L) {
    int speed = luaL_checkinteger(L, 1);
    ui_Screen9_set_pump_manual_speed(speed);
    return 0;
}

static int l_get_fan_mode(lua_State *L) {
    lua_pushboolean(L, ui_Screen9_get_fan_is_auto());
    return 1;
}

static int l_set_fan_mode(lua_State *L) {
    bool is_auto = lua_toboolean(L, 1);
    ui_Screen9_set_fan_is_auto(is_auto);
    return 0;
}

static int l_get_fan_state(lua_State *L) {
    lua_pushboolean(L, ui_Screen9_get_fan_manual_on());
    return 1;
}

static int l_set_fan_state(lua_State *L) {
    bool manual_on = lua_toboolean(L, 1);
    ui_Screen9_set_fan_manual_on(manual_on);
    return 0;
}

static int l_get_fan_manual_speed(lua_State *L) {
    lua_pushinteger(L, ui_Screen9_get_fan_manual_speed());
    return 1;
}

static int l_set_fan_manual_speed(lua_State *L) {
    int speed = luaL_checkinteger(L, 1);
    ui_Screen9_set_fan_manual_speed(speed);
    return 0;
}

static int l_get_pump_map_temp(lua_State *L) {
    int idx = luaL_checkinteger(L, 1);
    lua_pushinteger(L, ui_Screen9_get_pump_map_temp(idx));
    return 1;
}

static int l_set_pump_map_temp(lua_State *L) {
    int idx = luaL_checkinteger(L, 1);
    int temp = luaL_checkinteger(L, 2);
    ui_Screen9_set_pump_map_temp(idx, temp);
    return 0;
}

static int l_get_pump_map_speed(lua_State *L) {
    int idx = luaL_checkinteger(L, 1);
    lua_pushinteger(L, ui_Screen9_get_pump_map_speed(idx));
    return 1;
}

static int l_set_pump_map_speed(lua_State *L) {
    int idx = luaL_checkinteger(L, 1);
    int speed = luaL_checkinteger(L, 2);
    ui_Screen9_set_pump_map_speed(idx, speed);
    return 0;
}

static int l_get_fan_map_temp(lua_State *L) {
    int idx = luaL_checkinteger(L, 1);
    lua_pushinteger(L, ui_Screen9_get_fan_map_temp(idx));
    return 1;
}

static int l_set_fan_map_temp(lua_State *L) {
    int idx = luaL_checkinteger(L, 1);
    int temp = luaL_checkinteger(L, 2);
    ui_Screen9_set_fan_map_temp(idx, temp);
    return 0;
}

static int l_get_fan_map_speed(lua_State *L) {
    int idx = luaL_checkinteger(L, 1);
    lua_pushinteger(L, ui_Screen9_get_fan_map_speed(idx));
    return 1;
}

static int l_set_fan_map_speed(lua_State *L) {
    int idx = luaL_checkinteger(L, 1);
    int speed = luaL_checkinteger(L, 2);
    ui_Screen9_set_fan_map_speed(idx, speed);
    return 0;
}

// --- Screen 10 (Boost & Blow-off Control) Bindings ---

static int l_get_wg_actual_pos(lua_State *L) {
    lua_pushinteger(L, ui_Screen10_get_actual_wg_pos());
    return 1;
}

static int l_get_wg_mode(lua_State *L) {
    lua_pushboolean(L, ui_Screen10_get_wg_is_auto());
    return 1;
}

static int l_set_wg_mode(lua_State *L) {
    bool is_auto = lua_toboolean(L, 1);
    ui_Screen10_set_wg_is_auto(is_auto);
    return 0;
}

static int l_get_wg_manual_pos(lua_State *L) {
    lua_pushinteger(L, ui_Screen10_get_wg_manual_pos());
    return 1;
}

static int l_set_wg_manual_pos(lua_State *L) {
    int pos = luaL_checkinteger(L, 1);
    ui_Screen10_set_wg_manual_pos(pos);
    return 0;
}

static int l_get_wg_map_rpm(lua_State *L) {
    int idx = luaL_checkinteger(L, 1);
    lua_pushinteger(L, ui_Screen10_get_wg_map_rpm(idx));
    return 1;
}

static int l_set_wg_map_rpm(lua_State *L) {
    int idx = luaL_checkinteger(L, 1);
    int rpm = luaL_checkinteger(L, 2);
    ui_Screen10_set_wg_map_rpm(idx, rpm);
    return 0;
}

static int l_get_wg_map_pos(lua_State *L) {
    int idx = luaL_checkinteger(L, 1);
    lua_pushinteger(L, ui_Screen10_get_wg_map_pos(idx));
    return 1;
}

static int l_set_wg_map_pos(lua_State *L) {
    int idx = luaL_checkinteger(L, 1);
    int pos = luaL_checkinteger(L, 2);
    ui_Screen10_set_wg_map_pos(idx, pos);
    return 0;
}

static int l_get_bov_actual_state(lua_State *L) {
    lua_pushinteger(L, ui_Screen10_get_actual_bov_state());
    return 1;
}

static int l_get_bov_mode(lua_State *L) {
    lua_pushboolean(L, ui_Screen10_get_bov_is_auto());
    return 1;
}

static int l_set_bov_mode(lua_State *L) {
    bool is_auto = lua_toboolean(L, 1);
    ui_Screen10_set_bov_is_auto(is_auto);
    return 0;
}

static int l_get_bov_manual_open(lua_State *L) {
    lua_pushboolean(L, ui_Screen10_get_bov_manual_open());
    return 1;
}

static int l_set_bov_manual_open(lua_State *L) {
    bool open = lua_toboolean(L, 1);
    ui_Screen10_set_bov_manual_open(open);
    return 0;
}

static int l_get_bov_tps_threshold(lua_State *L) {
    lua_pushinteger(L, ui_Screen10_get_bov_tps_threshold());
    return 1;
}

static int l_set_bov_tps_threshold(lua_State *L) {
    int val = luaL_checkinteger(L, 1);
    ui_Screen10_set_bov_tps_threshold(val);
    return 0;
}

static int l_get_bov_press_threshold(lua_State *L) {
    lua_pushinteger(L, ui_Screen10_get_bov_press_threshold());
    return 1;
}

static int l_set_bov_press_threshold(lua_State *L) {
    int val = luaL_checkinteger(L, 1);
    ui_Screen10_set_bov_press_threshold(val);
    return 0;
}

static int l_get_bov_open_duration(lua_State *L) {
    lua_pushinteger(L, ui_Screen10_get_bov_open_duration());
    return 1;
}

static int l_set_bov_open_duration(lua_State *L) {
    int val = luaL_checkinteger(L, 1);
    ui_Screen10_set_bov_open_duration(val);
    return 0;
}

static int l_get_bov_stat_enabled(lua_State *L) {
    lua_pushboolean(L, ui_Screen10_get_bov_stat_enabled());
    return 1;
}

static int l_set_bov_stat_enabled(lua_State *L) {
    bool enabled = lua_toboolean(L, 1);
    ui_Screen10_set_bov_stat_enabled(enabled);
    return 0;
}

static int l_get_bov_stat_ratio(lua_State *L) {
    lua_pushinteger(L, ui_Screen10_get_bov_stat_ratio());
    return 1;
}

static int l_set_bov_stat_ratio(lua_State *L) {
    int ratio = luaL_checkinteger(L, 1);
    ui_Screen10_set_bov_stat_ratio(ratio);
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
    {"telegram_send", l_telegram_send, "telegram_send(\"msg\") - Sends a message via Telegram bot."},
    {"txCan", l_txCan, "txCan(bus, id, isExt, payload) - Transmit CAN frame (rusEFI)."},
    {"canRxAdd", l_canRxAdd, "canRxAdd([bus], id, [callback]) - Subscribe Lua to CAN ID (rusEFI)."},
    {"setTickRate", l_setTickRate, "setTickRate(hz) - Set background tick rate (rusEFI)."},
    {"mcu_standby", l_mcu_standby, "mcu_standby() - Simulated standby (rusEFI)."},
    {"setLuaGauge", l_setLuaGauge, "setLuaGauge(id, value) - Update dashboard gauge value."},
    {"getBitRange", l_getBitRange, "getBitRange(data, bitIndex, bitWidth) - Get bit field from array (rusEFI)."},
    {"get_pump_speed", l_get_pump_speed, "get_pump_speed() - Returns intercooler pump speed (0-100%)."},
    {"get_fan_speed", l_get_fan_speed, "get_fan_speed() - Returns electric fan speed (0-100%)."},
    {"get_pump_mode", l_get_pump_mode, "get_pump_mode() - Returns true if pump is in AUTO mode."},
    {"set_pump_mode", l_set_pump_mode, "set_pump_mode(is_auto) - Sets pump mode to AUTO (true) or MANUAL (false)."},
    {"get_pump_state", l_get_pump_state, "get_pump_state() - Returns true if pump is manual ON."},
    {"set_pump_state", l_set_pump_state, "set_pump_state(on) - Sets pump manual ON (true) or OFF (false)."},
    {"get_pump_manual_speed", l_get_pump_manual_speed, "get_pump_manual_speed() - Returns pump manual speed (0-100%)."},
    {"set_pump_manual_speed", l_set_pump_manual_speed, "set_pump_manual_speed(spd) - Sets pump manual speed (0-100%)."},
    {"get_fan_mode", l_get_fan_mode, "get_fan_mode() - Returns true if fan is in AUTO mode."},
    {"set_fan_mode", l_set_fan_mode, "set_fan_mode(is_auto) - Sets fan mode to AUTO (true) or MANUAL (false)."},
    {"get_fan_state", l_get_fan_state, "get_fan_state() - Returns true if fan is manual ON."},
    {"set_fan_state", l_set_fan_state, "set_fan_state(on) - Sets fan manual ON (true) or OFF (false)."},
    {"get_fan_manual_speed", l_get_fan_manual_speed, "get_fan_manual_speed() - Returns fan manual speed (0-100%)."},
    {"set_fan_manual_speed", l_set_fan_manual_speed, "set_fan_manual_speed(spd) - Sets fan manual speed (0-100%)."},
    {"get_pump_map_temp", l_get_pump_map_temp, "get_pump_map_temp(idx) - Gets pump map temperature at index idx (0-9)."},
    {"set_pump_map_temp", l_set_pump_map_temp, "set_pump_map_temp(idx, temp) - Sets pump map temperature at index idx (0-9)."},
    {"get_pump_map_speed", l_get_pump_map_speed, "get_pump_map_speed(idx) - Gets pump map speed at index idx (0-9)."},
    {"set_pump_map_speed", l_set_pump_map_speed, "set_pump_map_speed(idx, speed) - Sets pump map speed at index idx (0-9)."},
    {"get_fan_map_temp", l_get_fan_map_temp, "get_fan_map_temp(idx) - Gets fan map temp at index idx (0-9)."},
    {"set_fan_map_temp", l_set_fan_map_temp, "set_fan_map_temp(idx, temp) - Sets fan map temp at index idx (0-9)."},
    {"get_fan_map_speed", l_get_fan_map_speed, "get_fan_map_speed(idx) - Gets fan map speed at index idx (0-9)."},
    {"set_fan_map_speed", l_set_fan_map_speed, "set_fan_map_speed(idx, speed) - Sets fan map speed at index idx (0-9)."},
    {"get_wg_actual_pos", l_get_wg_actual_pos, "get_wg_actual_pos() - Returns actual wastegate VGT position (0-100%)."},
    {"get_wg_mode", l_get_wg_mode, "get_wg_mode() - Returns true if wastegate VGT is in AUTO mode."},
    {"set_wg_mode", l_set_wg_mode, "set_wg_mode(auto) - Sets wastegate VGT mode to AUTO (true) or MANUAL (false)."},
    {"get_wg_manual_pos", l_get_wg_manual_pos, "get_wg_manual_pos() - Returns wastegate manual target position (0-100%)."},
    {"set_wg_manual_pos", l_set_wg_manual_pos, "set_wg_manual_pos(pos) - Sets wastegate manual target position (0-100%)."},
    {"get_wg_map_rpm", l_get_wg_map_rpm, "get_wg_map_rpm(idx) - Gets wastegate map RPM at index idx (0-9)."},
    {"set_wg_map_rpm", l_set_wg_map_rpm, "set_wg_map_rpm(idx, rpm) - Sets wastegate map RPM at index idx (0-9)."},
    {"get_wg_map_pos", l_get_wg_map_pos, "get_wg_map_pos(idx) - Gets wastegate map position at index idx (0-9)."},
    {"set_wg_map_pos", l_set_wg_map_pos, "set_wg_map_pos(idx, pos) - Sets wastegate map position at index idx (0-9)."},
    {"get_bov_actual_state", l_get_bov_actual_state, "get_bov_actual_state() - Returns actual blow-off solenoid state (0=closed, 100=open)."},
    {"get_bov_mode", l_get_bov_mode, "get_bov_mode() - Returns true if blow-off is in AUTO mode."},
    {"set_bov_mode", l_set_bov_mode, "set_bov_mode(auto) - Sets blow-off mode to AUTO (true) or MANUAL (false)."},
    {"get_bov_manual_open", l_get_bov_manual_open, "get_bov_manual_open() - Returns true if blow-off is manually open."},
    {"set_bov_manual_open", l_set_bov_manual_open, "set_bov_manual_open(open) - Manually opens (true) or closes (false) blow-off solenoid."},
    {"get_bov_tps_threshold", l_get_bov_tps_threshold, "get_bov_tps_threshold() - Gets blow-off TPS drop threshold (%)."},
    {"set_bov_tps_threshold", l_set_bov_tps_threshold, "set_bov_tps_threshold(val) - Sets blow-off TPS drop threshold (%)."},
    {"get_bov_press_threshold", l_get_bov_press_threshold, "get_bov_press_threshold() - Gets blow-off overboost threshold (kPa)."},
    {"set_bov_press_threshold", l_set_bov_press_threshold, "set_bov_press_threshold(val) - Sets blow-off overboost threshold (kPa)."},
    {"get_bov_open_duration", l_get_bov_open_duration, "get_bov_open_duration() - Gets blow-off open duration (in 100ms units)."},
    {"set_bov_open_duration", l_set_bov_open_duration, "set_bov_open_duration(val) - Sets blow-off open duration (in 100ms units)."},
    {"get_bov_stat_enabled", l_get_bov_stat_enabled, "get_bov_stat_enabled() - Returns true if stationary path is enabled."},
    {"set_bov_stat_enabled", l_set_bov_stat_enabled, "set_bov_stat_enabled(enabled) - Toggles stationary path on/off."},
    {"get_bov_stat_ratio", l_get_bov_stat_ratio, "get_bov_stat_ratio() - Gets stationary pressure ratio threshold (multiplied by 100)."},
    {"set_bov_stat_ratio", l_set_bov_stat_ratio, "set_bov_stat_ratio(ratio) - Sets stationary pressure ratio threshold (multiplied by 100)."},
};
static const int num_bindings = sizeof(g_lua_bindings) / sizeof(g_lua_bindings[0]);

static char g_help_text_buffer[8192] = {0};

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
    
    if (xSemaphoreTake(lua_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < g_can_rx_filter_count; i++) {
            if (g_can_rx_filters[i].id == id) {
                int cb_ref = g_can_rx_filters[i].callback_ref;
                if (cb_ref != LUA_NOREF) {
                    lua_rawgeti(L, LUA_REGISTRYINDEX, cb_ref);
                    if (lua_isfunction(L, -1)) {
                        lua_pushinteger(L, g_can_rx_filters[i].bus);
                        lua_pushinteger(L, id);
                        lua_pushinteger(L, dlc);
                        
                        lua_newtable(L);
                        for (int d = 0; d < dlc; d++) {
                            lua_pushinteger(L, data[d]);
                            lua_rawseti(L, -2, d + 1);
                        }
                        
                        if (lua_pcall(L, 4, 0, 0) != LUA_OK) {
                            const char *err = lua_tostring(L, -1);
                            ESP_LOGE(TAG, "Lua CAN callback error: %s", err);
                            lua_pop(L, 1);
                        }
                    } else {
                        lua_pop(L, 1);
                    }
                } else {
                    lua_getglobal(L, "onCanRx");
                    if (lua_isfunction(L, -1)) {
                        lua_pushinteger(L, g_can_rx_filters[i].bus);
                        lua_pushinteger(L, id);
                        lua_pushinteger(L, dlc);
                        
                        lua_newtable(L);
                        for (int d = 0; d < dlc; d++) {
                            lua_pushinteger(L, data[d]);
                            lua_rawseti(L, -2, d + 1);
                        }
                        
                        if (lua_pcall(L, 4, 0, 0) != LUA_OK) {
                            const char *err = lua_tostring(L, -1);
                            ESP_LOGE(TAG, "Lua global onCanRx error: %s", err);
                            lua_pop(L, 1);
                        }
                    } else {
                        lua_pop(L, 1);
                    }
                }
            }
        }
        xSemaphoreGive(lua_mutex);
    }
}

// --- Core API ---

static const char *DEFAULT_LUA_SCRIPT = 
    "setTickRate(10)\n"
    "tpsSensor = Sensor.new('tps1')\n"
    "rpmSensor = Sensor.new('rpm')\n"
    "cltSensor = Sensor.new('clt')\n"
    "iatSensor = Sensor.new('Iat')\n\n"
    "local current_rpm = 0\n"
    "local current_tps = 0\n"
    "local current_clt = 90\n"
    "local current_iat = 40\n"
    "local last_tps_for_bov = 0\n"
    "local bov_open_timer = 0\n\n"
    "-- MRE Link Monitoring\n"
    "local mre_link_ok = false\n"
    "local mre_rx_count = 0\n"
    "local mre_last_rx_tick = 0\n"
    "local tick_counter = 0\n"
    "local BOOT_GRACE_TICKS = 300  -- 30 sec at 10Hz\n"
    "local MRE_TIMEOUT_TICKS = 30  -- 3 sec at 10Hz\n"
    "local mre_warned = false\n\n"
    "function interpolate10(input_val, map_x_fn, map_y_fn)\n"
    "    if input_val <= map_x_fn(0) then return map_y_fn(0) end\n"
    "    if input_val >= map_x_fn(9) then return map_y_fn(9) end\n"
    "    for i = 0, 8 do\n"
    "        local x0 = map_x_fn(i)\n"
    "        local x1 = map_x_fn(i + 1)\n"
    "        if input_val >= x0 and input_val <= x1 then\n"
    "            local y0 = map_y_fn(i)\n"
    "            local y1 = map_y_fn(i + 1)\n"
    "            if x1 == x0 then return y0 end\n"
    "            return y0 + (input_val - x0) * (y1 - y0) / (x1 - x0)\n"
    "        end\n"
    "    end\n"
    "    return map_y_fn(9)\n"
    "end\n\n"
    "function onVehicleMotor1(bus, id, dlc, data)\n"
    "    current_tps = getBitRange(data, 40, 8) * 0.40\n"
    "    -- Using 0.39 scale as defined in the user's MRE configuration\n"
    "    current_rpm = getBitRange(data, 16, 16) * 0.39\n"
    "    \n"
    "    tpsSensor:set(current_tps)\n"
    "    rpmSensor:set(current_rpm)\n"
    "end\n\n"
    "function onVehicleMotor2(bus, id, dlc, data)\n"
    "    current_clt = getBitRange(data, 8, 8) * 0.75 - 48\n"
    "    cltSensor:set(current_clt)\n"
    "end\n\n"
    "function onVehicleMotor3(bus, id, dlc, data)\n"
    "    current_iat = getBitRange(data, 8, 8) * 0.75 - 48\n"
    "    iatSensor:set(current_iat)\n"
    "end\n\n"
    "function onMreStatus(bus, id, dlc, data)\n"
    "    mre_rx_count = mre_rx_count + 1\n"
    "    mre_last_rx_tick = tick_counter\n"
    "    if not mre_link_ok then\n"
    "        mre_link_ok = true\n"
    "        mre_warned = false\n"
    "        log('MRE Link ONLINE (rx #' .. mre_rx_count .. ')')\n"
    "    end\n"
    "    -- Read actual actuator positions from MRE\n"
    "    if dlc >= 2 then\n"
    "        setLuaGauge(12, data[1])  -- WG actual pos\n"
    "        setLuaGauge(13, data[2])  -- BOV actual state\n"
    "    end\n"
    "end\n\n"
    "canRxAdd(1, 0x280, onVehicleMotor1)\n"
    "canRxAdd(1, 0x288, onVehicleMotor2)\n"
    "canRxAdd(1, 0x380, onVehicleMotor3)\n"
    "canRxAdd(1, 0x601, onMreStatus)\n\n"
    "function onTick()\n"
    "    tick_counter = tick_counter + 1\n\n"
    "    -- Check MRE link timeout (only after boot grace period)\n"
    "    if tick_counter > BOOT_GRACE_TICKS then\n"
    "        if mre_link_ok and (tick_counter - mre_last_rx_tick) > MRE_TIMEOUT_TICKS then\n"
    "            mre_link_ok = false\n"
    "            log('MRE Link LOST - no 0x601 for 3 sec')\n"
    "        end\n"
    "        if not mre_link_ok and not mre_warned then\n"
    "            mre_warned = true\n"
    "            show_warning('MRE ECU Offline - check CAN wiring')\n"
    "        end\n"
    "    end\n\n"
    "    -- Calculate targets (works with or without MRE)\n"
    "    local wg_target = 0\n"
    "    if get_wg_mode() == false then\n"
    "        wg_target = get_wg_manual_pos()\n"
    "    else\n"
    "        wg_target = interpolate10(current_rpm, get_wg_map_rpm, get_wg_map_pos)\n"
    "    end\n"
    "    local bov_target = 0\n"
    "    if get_bov_mode() == false then\n"
    "        bov_target = get_bov_manual_open() and 100 or 0\n"
    "    else\n"
    "        local tps_drop = last_tps_for_bov - current_tps\n"
    "        last_tps_for_bov = current_tps\n"
    "        if tps_drop >= get_bov_tps_threshold() then\n"
    "            bov_open_timer = get_bov_open_duration()\n"
    "        end\n"
    "        if bov_open_timer > 0 then\n"
    "            bov_target = 100\n"
    "            bov_open_timer = bov_open_timer - 1\n"
    "        else\n"
    "            bov_target = 0\n"
    "        end\n"
    "    end\n"
    "    local pump_target = 0\n"
    "    if get_pump_mode() == false then\n"
    "        pump_target = get_pump_state() and get_pump_manual_speed() or 0\n"
    "    else\n"
    "        pump_target = interpolate10(current_clt, get_pump_map_temp, get_pump_map_speed)\n"
    "    end\n"
    "    local fan_target = 0\n"
    "    if get_fan_mode() == false then\n"
    "        fan_target = get_fan_state() and get_fan_manual_speed() or 0\n"
    "    else\n"
    "        fan_target = interpolate10(current_clt, get_fan_map_temp, get_fan_map_speed)\n"
    "    end\n\n"
    "    -- Send command to MRE: [wg, bov, pump, fan, 0xAA, seq, link_status]\n"
    "    local seq = tick_counter % 256\n"
    "    local link_byte = mre_link_ok and 1 or 0\n"
    "    txCan(1, 0x600, 0, { math.floor(wg_target), math.floor(bov_target),\n"
    "        math.floor(pump_target), math.floor(fan_target), 0xAA, seq, link_byte })\n\n"
    "    -- Update dashboard gauges locally (works without MRE)\n"
    "    setLuaGauge(1, math.floor(wg_target))\n"
    "end\n";

#include "esp_heap_caps.h"

static void *lua_alloc_psram(void *ud, void *ptr, size_t osize, size_t nsize) {
    (void)ud;
    (void)osize;
    if (nsize == 0) {
        if (ptr) {
            heap_caps_free(ptr);
        }
        return NULL;
    }
    return heap_caps_realloc(ptr, nsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

esp_err_t lua_manager_init(void) {
    if (L) return ESP_OK;

    ESP_LOGI(TAG, "Initializing Lua Engine in PSRAM...");
    
    lua_mutex = xSemaphoreCreateMutex();
    if (!lua_mutex) {
        ESP_LOGE(TAG, "Failed to create Lua mutex");
        return ESP_FAIL;
    }

    L = lua_newstate(lua_alloc_psram, NULL, 0);
    if (!L) {
        ESP_LOGE(TAG, "Failed to create Lua state");
        return ESP_FAIL;
    }

    luaL_openlibs(L);
    lua_bind_dashboard_functions();
    
    // Inject rusEFI compatibility helper classes (e.g. Sensor class)
    const char *emulation_script = 
        "Sensor = {}\n"
        "Sensor.__index = Sensor\n"
        "function Sensor.new(name)\n"
        "    local self = setmetatable({}, Sensor)\n"
        "    self.name = name\n"
        "    return self\n"
        "end\n"
        "function Sensor:set(value)\n"
        "    local ln = string.lower(self.name)\n"
        "    if ln == \"tps1\" or ln == \"tps\" then\n"
        "        setLuaGauge(2, value)\n"\
        "    elseif ln == \"rpm\" then\n"
        "        setLuaGauge(3, value)\n"\
        "    elseif ln == \"clt\" or ln == \"coolant\" then\n"
        "        setLuaGauge(8, value)\n"\
        "    elseif ln == \"iat\" then\n"
        "        setLuaGauge(19, value)\n"\
        "    elseif ln == \"acceleratorpedal\" then\n"
        "        setLuaGauge(11, value)\n"\
        "    elseif ln == \"map\" then\n"
        "        setLuaGauge(0, value)\n"\
        "    else\n"
        "        log(\"Sensor \" .. self.name .. \" set: \" .. tostring(value))\n"
        "    end\n"
        "end\n";
        
    if (luaL_dostring(L, emulation_script) != LUA_OK) {
        ESP_LOGE(TAG, "Failed to load Sensor emulation script: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    
    ESP_LOGI(TAG, "Lua Engine Initialized.");
    
    // Persistent Script Autoload from SD Card, with embedded default fallback
    FILE *f = fopen("/sdcard/SYSTEM/LUA/boot_script.lua", "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        char *buffer = malloc(size + 1);
        if (buffer) {
            size_t read_bytes = fread(buffer, 1, size, f);
            buffer[read_bytes] = '\0';
            fclose(f);
            
            ESP_LOGI(TAG, "Persistent Lua script loaded from SD Card (%ld bytes).", (long)read_bytes);
            lua_manager_save_background_script(buffer);
            free(buffer);
        } else {
            fclose(f);
            ESP_LOGE(TAG, "Failed to allocate memory for loading script from SD. Loading embedded default.");
            lua_manager_save_background_script(DEFAULT_LUA_SCRIPT);
        }
    } else {
        ESP_LOGI(TAG, "No persistent Lua script found on SD card. Loading embedded default dashboard script.");
        lua_manager_save_background_script(DEFAULT_LUA_SCRIPT);
    }
    
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
            extern void ui_Screen6_set_lua_text(const char *text);
            if (example_lvgl_lock(500)) {
                ui_Screen6_set_lua_text(err_msg);
                example_lvgl_unlock();
            }
            
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
    
    if (xSemaphoreTake(lua_mutex, portMAX_DELAY) == pdTRUE) {
        if (g_background_script) {
            free(g_background_script);
        }
        g_background_script = strdup(script);
        g_bg_script_loaded = false;
        g_tick_rate_hz = 10;
        
        // Unref all callbacks in registry
        for (int i = 0; i < g_can_rx_filter_count; i++) {
            if (g_can_rx_filters[i].callback_ref != LUA_NOREF) {
                luaL_unref(L, LUA_REGISTRYINDEX, g_can_rx_filters[i].callback_ref);
                g_can_rx_filters[i].callback_ref = LUA_NOREF;
            }
        }
        g_can_rx_filter_count = 0;
        
        xSemaphoreGive(lua_mutex);
    }
    
    // Save to SD card persistently
    struct stat st = {0};
    if (stat("/sdcard/SYSTEM", &st) != 0) mkdir("/sdcard/SYSTEM", 0755);
    if (stat("/sdcard/SYSTEM/LUA", &st) != 0) mkdir("/sdcard/SYSTEM/LUA", 0755);
    
    FILE *f = fopen("/sdcard/SYSTEM/LUA/boot_script.lua", "w");
    if (f) {
        fprintf(f, "%s", script);
        fclose(f);
        ESP_LOGI(TAG, "Saved script persistently to SD card.");
    } else {
        ESP_LOGW(TAG, "Could not write script to SD card (maybe not mounted).");
    }
    
    if (!lua_bg_task_handle) {
        xTaskCreatePinnedToCore(lua_background_task, "lua_bg_task", 8192, NULL, 4, &lua_bg_task_handle, 1);
    }
    
    ESP_LOGI(TAG, "Background script saved and router active.");
    return ESP_OK;
}

const char* lua_manager_get_background_script(void) {
    return g_background_script;
}

const char* lua_manager_get_default_script(void) {
    return DEFAULT_LUA_SCRIPT;
}
