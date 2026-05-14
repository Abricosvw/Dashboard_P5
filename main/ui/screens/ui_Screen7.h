// ==========================================================================
// Screen 7 - Open Claw AI Terminal
// Full ESP-Claw AI interface with terminal, Lua editor, and quick actions
// Portrait 736x1280 (ESP32-P4)
// ==========================================================================
#ifndef _UI_SCREEN7_H
#define _UI_SCREEN7_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../ui.h"

extern lv_obj_t *ui_Screen7;

// Terminal text areas
extern lv_obj_t *ui_Screen7_Terminal;    // Main AI terminal output
extern lv_obj_t *ui_Screen7_Input;       // AI text input
extern lv_obj_t *ui_Screen7_LuaEditor;  // Lua code editor

void ui_Screen7_screen_init(void);
void ui_Screen7_screen_destroy(void);
void ui_Screen7_update_layout(void);

// Public API for other modules to interact with the terminal
void ui_Screen7_append_text(const char *text);
void ui_Screen7_clear_terminal(void);
void ui_Screen7_set_status(const char *status);
void ui_Screen7_set_lua_text(const char *text);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
