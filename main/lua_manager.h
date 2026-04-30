#ifndef LUA_MANAGER_H
#define LUA_MANAGER_H

#include <esp_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the ESP-Claw Lua environment
esp_err_t lua_manager_init(void);

// Execute a Lua script
esp_err_t lua_manager_execute(const char *script);

// Bindings for Dashboard_P5
void lua_bind_dashboard_functions(void);

#ifdef __cplusplus
}
#endif

#endif // LUA_MANAGER_H
