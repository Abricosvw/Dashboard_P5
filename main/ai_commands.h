// AI Commands Header - Voice command handlers for Gemini Function Calling
#ifndef AI_COMMANDS_H
#define AI_COMMANDS_H

#include <stdbool.h>
#include <stdint.h>

// Command result structure
typedef struct {
  bool success;
  char message[256];
} ai_cmd_result_t;

// Command handlers
ai_cmd_result_t ai_cmd_switch_screen(int screen_number);
ai_cmd_result_t ai_cmd_toggle_gauge(const char *gauge_name, bool enable);
ai_cmd_result_t ai_cmd_search_can_id(uint32_t can_id);
ai_cmd_result_t ai_cmd_set_brightness(int percent);
ai_cmd_result_t ai_cmd_get_status(void);
ai_cmd_result_t ai_cmd_toggle_demo_mode(bool enable);
ai_cmd_result_t ai_cmd_save_settings(void);

// Execute command from Gemini function call
ai_cmd_result_t ai_execute_function_call(const char *function_name,
                                         const char *args_json);

#endif // AI_COMMANDS_H
