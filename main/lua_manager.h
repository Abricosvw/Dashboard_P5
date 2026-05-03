#ifndef LUA_MANAGER_H
#define LUA_MANAGER_H

#include <esp_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the ESP-Claw Lua environment
esp_err_t lua_manager_init(void);

// Execute a Lua script (run once)
esp_err_t lua_manager_execute(const char *script);

// Save and run a Lua script in the background
esp_err_t lua_manager_save_background_script(const char *script);

// Get the dynamically generated help text for available Lua functions
const char* lua_manager_get_help_text(void);

// Bindings for Dashboard_P5
void lua_bind_dashboard_functions(void);

// Pass incoming CAN message to Lua Engine if filter matches
void lua_manager_handle_can_rx(uint32_t id, const uint8_t *data, uint8_t dlc);

#ifdef __cplusplus
}
#endif

#endif // LUA_MANAGER_H
