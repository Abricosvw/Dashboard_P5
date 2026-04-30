#include "lua_manager.h"
#include "esp_log.h"
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "can_manager.h" // For getting CAN data
#include "ui/settings_config.h" // For brightness etc
#include "ui/screens/ui_Screen6.h" // For setting terminal text

static const char *TAG = "LUA_MGR";
static lua_State *L = NULL;

// Example Lua binding: get_can_value(can_id)
static int l_get_can_value(lua_State *L) {
    int can_id = luaL_checkinteger(L, 1);
    // Dummy return for now, since can_manager may not have an exact fetch method
    // In reality, you'd fetch from your CAN cache
    int value = 0; // Replace with actual CAN data lookup
    lua_pushinteger(L, value);
    return 1;
}

// Example Lua binding: log(msg)
static int l_log_message(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    ESP_LOGI("LUA_SCRIPT", "%s", msg);
    return 0;
}

void lua_bind_dashboard_functions(void) {
    if (!L) return;
    lua_register(L, "get_can_value", l_get_can_value);
    lua_register(L, "log", l_log_message);
}

esp_err_t lua_manager_init(void) {
    if (L) return ESP_OK;

    ESP_LOGI(TAG, "Initializing Lua Engine...");
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
    if (!L) {
        ESP_LOGE(TAG, "Lua not initialized");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Executing Lua script:\n%s", script);
    
    if (luaL_dostring(L, script) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        ESP_LOGE(TAG, "Lua execution error: %s", err);
        
        // Output error to the UI terminal
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "Error: %s", err);
        ui_Screen6_set_lua_terminal_text(err_msg);
        
        lua_pop(L, 1);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Lua script executed successfully.");
    return ESP_OK;
}
