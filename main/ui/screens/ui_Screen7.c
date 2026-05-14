// ==========================================================================
// Screen 7 - Open Claw AI Terminal
// Full ESP-Claw AI interface with terminal, command input, and quick actions
// Portrait 736x1280 (ESP32-P4)
// ==========================================================================
#include "ui_Screen7.h"
#include "../ui.h"
#include "ai_manager.h"
#include "ui_screen_manager.h"
#include <esp_log.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "SCREEN7";



// Screen object
lv_obj_t *ui_Screen7 = NULL;

// Terminal components
lv_obj_t *ui_Screen7_Terminal = NULL;  // Main scrollable terminal output
lv_obj_t *ui_Screen7_Input = NULL;    // Text input area
static lv_obj_t *keyboard = NULL;     // On-screen keyboard
static lv_obj_t *status_label = NULL; // Status bar at top
static lv_obj_t *title_label = NULL;  // Title

// Quick action buttons
static lv_obj_t *btn_send = NULL;

// Colors
#define CLR_BG          0x0A0F1A
#define CLR_PANEL       0x111827
#define CLR_ACCENT      0x00D4FF
#define CLR_TERMINAL_BG 0x0D1117
#define CLR_TERMINAL_FG 0x00FF88
#define CLR_INPUT_BG    0x1A1F2E
#define CLR_BTN_BG      0x1E293B
#define CLR_BTN_ACTIVE  0x0E7490
#define CLR_TEXT_DIM     0x94A3B8
#define CLR_TEXT_WHITE   0xF1F5F9
#define CLR_SEND         0x059669
#define CLR_BORDER       0x334155

// Layout constants (portrait 736x1280)
#define SCR_W 736
#define SCR_H 1280
#define TITLE_H 50
#define STATUS_H 28
#define TERMINAL_TOP (TITLE_H + 4)  // starts right below title bar
#define BTN_ROW_H 44
#define INPUT_H 44
#define NAV_H 60
#define BOTTOM_MARGIN 8
#define KEYBOARD_H 280

// Terminal buffer
#define TERMINAL_BUF_SIZE 4096
static char terminal_buf[TERMINAL_BUF_SIZE];
static bool keyboard_visible = false;

// ---------- Forward declarations ----------
static void send_command(void);
static void on_send_clicked(lv_event_t *e);
static void on_clear_clicked(lv_event_t *e);
static void on_status_clicked(lv_event_t *e);
static void on_skills_clicked(lv_event_t *e);
static void on_schedule_clicked(lv_event_t *e);
static void on_files_clicked(lv_event_t *e);
static void on_input_focused(lv_event_t *e);
static void on_input_defocused(lv_event_t *e);
static void on_kb_ready(lv_event_t *e);
static void popup_close_cb(lv_event_t *e);
static void gpio_map_event_cb(lv_event_t *e);
static void telegram_help_event_cb(lv_event_t *e);

// ---------- Helpers ----------

static lv_obj_t *create_quick_btn(lv_obj_t *parent, const char *text,
                                   lv_event_cb_t cb, int w) {
  lv_obj_t *btn = lv_obj_create(parent);
  lv_obj_set_size(btn, w, BTN_ROW_H);
  lv_obj_set_style_bg_color(btn, lv_color_hex(CLR_BTN_BG), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btn, 0, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(CLR_BORDER), 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);

  // Pressed style
  lv_obj_set_style_bg_color(btn, lv_color_hex(CLR_BTN_ACTIVE), LV_STATE_PRESSED);

  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_ACCENT), 0);
  lv_obj_set_style_text_font(lbl, &montserrat_20_en_ru, 0);
  lv_obj_center(lbl);

  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
  return btn;
}

// ---------- Public API ----------

void ui_Screen7_append_text(const char *text) {
  if (!text)
    return;

  size_t cur_len = strlen(terminal_buf);
  size_t add_len = strlen(text);

  // If buffer would overflow, trim from the beginning
  if (cur_len + add_len + 2 >= TERMINAL_BUF_SIZE) {
    // Keep last half of buffer
    size_t keep = TERMINAL_BUF_SIZE / 2;
    if (cur_len > keep) {
      memmove(terminal_buf, terminal_buf + (cur_len - keep), keep);
      terminal_buf[keep] = '\0';
      cur_len = keep;
    }
  }

  // Append new text
  strncat(terminal_buf, text, TERMINAL_BUF_SIZE - cur_len - 1);
  if (text[add_len - 1] != '\n') {
    strncat(terminal_buf, "\n", TERMINAL_BUF_SIZE - strlen(terminal_buf) - 1);
  }

  if (ui_Screen7_Terminal) {
    lv_textarea_set_text(ui_Screen7_Terminal, terminal_buf);
    // Auto-scroll to bottom
    lv_textarea_set_cursor_pos(ui_Screen7_Terminal, LV_TEXTAREA_CURSOR_LAST);
  }
}

void ui_Screen7_clear_terminal(void) {
  if (!ui_Screen7_Terminal)
    return;
  terminal_buf[0] = '\0';
  lv_textarea_set_text(ui_Screen7_Terminal, "");
  ESP_LOGI(TAG, "Terminal cleared");
}

void ui_Screen7_set_status(const char *status) {
  if (status_label && status) {
    lv_label_set_text(status_label, status);
  }
}




// ---------- New Callbacks ----------


static void popup_close_cb(lv_event_t *e) {
  lv_obj_t *popup = lv_event_get_user_data(e);
  if (popup) {
    lv_obj_del(popup);
  }
}

static void lua_help_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_t *popup = lv_obj_create(ui_Screen7);
    lv_obj_set_size(popup, 600, 400);
    lv_obj_center(popup);
    lv_obj_set_style_bg_color(popup, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(popup, 2, 0);
    lv_obj_set_style_border_color(popup, lv_color_hex(0x00D4FF), 0);
    
    lv_obj_t *help_text = lv_label_create(popup);
    extern const char* lua_manager_get_help_text(void);
    lv_label_set_text(help_text, lua_manager_get_help_text());
    lv_obj_set_style_text_color(help_text, lv_color_white(), 0);
    lv_obj_set_style_text_font(help_text, &montserrat_20_en_ru, 0);
    lv_label_set_long_mode(help_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(help_text, 560);
    
    lv_obj_t *close_btn = lv_btn_create(popup);
    lv_obj_set_size(close_btn, 120, 40);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xFF3333), 0);
    lv_obj_add_event_cb(close_btn, popup_close_cb, LV_EVENT_CLICKED, popup);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "Close");
    lv_obj_center(close_lbl);
  }
}

static void gpio_map_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_t *popup = lv_obj_create(ui_Screen7);
    lv_obj_set_size(popup, 600, 450);
    lv_obj_center(popup);
    lv_obj_set_style_bg_color(popup, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(popup, 2, 0);
    lv_obj_set_style_border_color(popup, lv_color_hex(0x00FF88), 0);
    
    lv_obj_t *map_text = lv_label_create(popup);
    lv_label_set_recolor(map_text, true);
    lv_label_set_text(map_text, 
      "ESP32-P4 GPIO Map:\n\n"
      "#FF0000 0-3: JTAG/System (DO NOT USE)#\n"
      "#00FF00 FREE PINS: 6, 22-32, 34-38, 45-52#\n"
    );
    lv_obj_set_style_text_color(map_text, lv_color_white(), 0);
    lv_obj_set_style_text_font(map_text, &montserrat_20_en_ru, 0);
    
    lv_obj_t *close_btn = lv_btn_create(popup);
    lv_obj_set_size(close_btn, 120, 40);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xFF3333), 0);
    lv_obj_add_event_cb(close_btn, popup_close_cb, LV_EVENT_CLICKED, popup);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "Close");
    lv_obj_center(close_lbl);
  }
}

static void telegram_help_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_t *popup = lv_obj_create(ui_Screen7);
    lv_obj_set_size(popup, 600, 450);
    lv_obj_center(popup);
    lv_obj_set_style_bg_color(popup, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(popup, 2, 0);
    lv_obj_set_style_border_color(popup, lv_color_hex(0x0088FF), 0);
    
    lv_obj_t *map_text = lv_label_create(popup);
    lv_label_set_recolor(map_text, true);
    lv_label_set_text(map_text, "#0088FF Telegram Messenger Setup:#\nSee ai_config.h");
    lv_obj_set_style_text_color(map_text, lv_color_white(), 0);
    lv_obj_set_style_text_font(map_text, &montserrat_20_en_ru, 0);
    
    lv_obj_t *close_btn = lv_btn_create(popup);
    lv_obj_set_size(close_btn, 120, 40);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xFF3333), 0);
    lv_obj_add_event_cb(close_btn, popup_close_cb, LV_EVENT_CLICKED, popup);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "Close");
    lv_obj_center(close_lbl);
  }
}

// ---------- Callbacks ----------


static void send_command(void) {
  if (!ui_Screen7_Input)
    return;

  const char *text = lv_textarea_get_text(ui_Screen7_Input);
  if (!text || text[0] == '\0')
    return;

  // Show command in terminal
  char cmd_line[512];
  snprintf(cmd_line, sizeof(cmd_line), "> %s", text);
  ui_Screen7_append_text(cmd_line);

  // Send to AI
  ESP_LOGI(TAG, "Sending command: %s", text);
  ai_manager_send_text_query(text);

  // Clear input
  lv_textarea_set_text(ui_Screen7_Input, "");

  // Update status
  ui_Screen7_set_status("Processing...");
}

static void on_send_clicked(lv_event_t *e) {
  (void)e;
  send_command();
}

static void on_clear_clicked(lv_event_t *e) {
  (void)e;
  ui_Screen7_clear_terminal();
  ui_Screen7_append_text("[Terminal cleared]");
}

static void on_status_clicked(lv_event_t *e) {
  (void)e;
  ai_manager_send_text_query("system status report");
  ui_Screen7_set_status("Requesting status...");
}

static void on_skills_clicked(lv_event_t *e) {
  (void)e;
  ai_manager_send_text_query("list all available skills");
  ui_Screen7_set_status("Loading skills...");
}

static void on_schedule_clicked(lv_event_t *e) {
  (void)e;
  ai_manager_send_text_query("scheduler list");
  ui_Screen7_set_status("Loading schedules...");
}

static void on_files_clicked(lv_event_t *e) {
  (void)e;
  ai_manager_send_text_query("list files on SD card /sdcard");
  ui_Screen7_set_status("Listing files...");
}

static void on_input_focused(lv_event_t *e) {
  lv_obj_t *ta = lv_event_get_target(e);

  if (!keyboard) {
    keyboard = lv_keyboard_create(ui_Screen7);
    lv_obj_set_size(keyboard, SCR_W, KEYBOARD_H);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_LEFT, 0, -NAV_H);
    lv_keyboard_set_textarea(keyboard, ta);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(CLR_PANEL), 0);
    lv_obj_set_style_shadow_width(keyboard, 0, 0);
    lv_obj_add_event_cb(keyboard, on_kb_ready, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, on_kb_ready, LV_EVENT_CANCEL, NULL);
    keyboard_visible = true;
  }

  if (keyboard_visible)
    return;

  keyboard_visible = true;
  lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);

  // Shrink terminal to make room for keyboard
  int terminal_h =
      SCR_H - TERMINAL_TOP - INPUT_H - BTN_ROW_H - KEYBOARD_H - NAV_H - 24;
  if (terminal_h < 200)
    terminal_h = 200;
  lv_obj_set_height(ui_Screen7_Terminal, terminal_h);

  ESP_LOGI(TAG, "Keyboard shown");
}

static void on_input_defocused(lv_event_t *e) {
  (void)e;
  if (!keyboard || !keyboard_visible)
    return;

  keyboard_visible = false;
  lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);

  // Restore terminal height
  int terminal_h = SCR_H - TERMINAL_TOP - INPUT_H - BTN_ROW_H - NAV_H - 24;
  lv_obj_set_height(ui_Screen7_Terminal, terminal_h);

  ESP_LOGI(TAG, "Keyboard hidden");
}

static void on_kb_ready(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) {
    send_command();
    on_input_defocused(NULL);
  } else if (code == LV_EVENT_CANCEL) {
    on_input_defocused(NULL);
  }
}

// ---------- Screen Init ----------

void ui_Screen7_screen_init(void) {
  terminal_buf[0] = '\0';
  keyboard_visible = false;

  // --- Root screen ---
  ui_Screen7 = lv_obj_create(NULL);
  lv_obj_set_size(ui_Screen7, SCR_W, SCR_H);
  lv_obj_clear_flag(ui_Screen7, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(ui_Screen7, lv_color_hex(CLR_BG), 0);
  lv_obj_set_style_bg_opa(ui_Screen7, LV_OPA_COVER, 0);

  // --- Title bar ---
  lv_obj_t *title_bar = lv_obj_create(ui_Screen7);
  lv_obj_set_size(title_bar, SCR_W, TITLE_H);
  lv_obj_set_pos(title_bar, 0, 0);
  lv_obj_set_style_bg_color(title_bar, lv_color_hex(CLR_PANEL), 0);
  lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(title_bar, 0, 0);
  lv_obj_set_style_border_width(title_bar, 0, 0);
  lv_obj_set_style_shadow_width(title_bar, 0, 0);
  lv_obj_set_style_pad_all(title_bar, 0, 0);
  lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

  // Accent line under title
  lv_obj_t *accent_line = lv_obj_create(title_bar);
  lv_obj_set_size(accent_line, SCR_W, 3);
  lv_obj_align(accent_line, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_bg_color(accent_line, lv_color_hex(CLR_ACCENT), 0);
  lv_obj_set_style_bg_opa(accent_line, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(accent_line, 0, 0);
  lv_obj_set_style_border_width(accent_line, 0, 0);
  lv_obj_set_style_shadow_width(accent_line, 0, 0);

  title_label = lv_label_create(title_bar);
  lv_label_set_text(title_label, LV_SYMBOL_SETTINGS " Open Claw");
  lv_obj_set_style_text_color(title_label, lv_color_hex(CLR_ACCENT), 0);
  lv_obj_set_style_text_font(title_label, &montserrat_20_en_ru, 0);
  lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 16, -2);



// --- Terminal area ---
  int terminal_h = 776; // Expanded height since Lua editor is moved

  ui_Screen7_Terminal = lv_textarea_create(ui_Screen7);
  lv_obj_set_size(ui_Screen7_Terminal, SCR_W - 16, terminal_h);
  lv_obj_set_pos(ui_Screen7_Terminal, 8, TERMINAL_TOP);
  lv_textarea_set_text(ui_Screen7_Terminal, terminal_buf);
  lv_textarea_set_cursor_click_pos(ui_Screen7_Terminal, false);
  lv_obj_clear_flag(ui_Screen7_Terminal, LV_OBJ_FLAG_CLICKABLE);

  // Terminal styling
  lv_obj_set_style_bg_color(ui_Screen7_Terminal, lv_color_hex(CLR_TERMINAL_BG), 0);
  lv_obj_set_style_bg_opa(ui_Screen7_Terminal, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(ui_Screen7_Terminal, lv_color_hex(CLR_TERMINAL_FG), 0);
  lv_obj_set_style_text_font(ui_Screen7_Terminal, &montserrat_20_en_ru, 0);
  lv_obj_set_style_border_color(ui_Screen7_Terminal, lv_color_hex(CLR_BORDER), 0);
  lv_obj_set_style_border_width(ui_Screen7_Terminal, 1, 0);
  lv_obj_set_style_radius(ui_Screen7_Terminal, 0, 0);
  lv_obj_set_style_shadow_width(ui_Screen7_Terminal, 0, 0);
  lv_obj_set_style_pad_all(ui_Screen7_Terminal, 8, 0);

  // --- Terminal quick action buttons row ---
  int btn_term_y = TERMINAL_TOP + terminal_h + 4;
  int btn_w = (SCR_W - 16 - 5 * 4) / 5;

  lv_obj_t *btn_clear2 = create_quick_btn(ui_Screen7, LV_SYMBOL_TRASH " Clr",
                                           on_clear_clicked, btn_w);
  lv_obj_set_pos(btn_clear2, 8, btn_term_y);

  lv_obj_t *btn_status2 = create_quick_btn(ui_Screen7, LV_SYMBOL_CHARGE " Stat",
                                            on_status_clicked, btn_w);
  lv_obj_set_pos(btn_status2, 8 + (btn_w + 4) * 1, btn_term_y);

  lv_obj_t *btn_skills2 = create_quick_btn(ui_Screen7, LV_SYMBOL_LIST " Skls",
                                            on_skills_clicked, btn_w);
  lv_obj_set_pos(btn_skills2, 8 + (btn_w + 4) * 2, btn_term_y);

  lv_obj_t *btn_sched2 = create_quick_btn(ui_Screen7, LV_SYMBOL_REFRESH " Scd",
                                           on_schedule_clicked, btn_w);
  lv_obj_set_pos(btn_sched2, 8 + (btn_w + 4) * 3, btn_term_y);

  lv_obj_t *btn_files2 = create_quick_btn(ui_Screen7, LV_SYMBOL_DIRECTORY " Fls",
                                           on_files_clicked, btn_w);
  lv_obj_set_pos(btn_files2, 8 + (btn_w + 4) * 4, btn_term_y);

  // --- Input row (text field + send button) ---
  int input_y = btn_term_y + BTN_ROW_H + 50;

  ui_Screen7_Input = lv_textarea_create(ui_Screen7);
  lv_obj_set_size(ui_Screen7_Input, SCR_W - 100, INPUT_H);
  lv_obj_set_pos(ui_Screen7_Input, 8, input_y);
  lv_textarea_set_placeholder_text(ui_Screen7_Input, "Введите запрос для ИИ...");
  lv_textarea_set_one_line(ui_Screen7_Input, true);
  lv_textarea_set_max_length(ui_Screen7_Input, 256);

  // Input styling
  lv_obj_set_style_bg_color(ui_Screen7_Input, lv_color_hex(CLR_INPUT_BG), 0);
  lv_obj_set_style_bg_opa(ui_Screen7_Input, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(ui_Screen7_Input, lv_color_hex(CLR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(ui_Screen7_Input, &montserrat_20_en_ru, 0);
  lv_obj_set_style_border_color(ui_Screen7_Input, lv_color_hex(0x00D4FF), 0);
  lv_obj_set_style_border_width(ui_Screen7_Input, 1, 0);
  lv_obj_set_style_radius(ui_Screen7_Input, 0, 0);
  lv_obj_set_style_shadow_width(ui_Screen7_Input, 0, 0);
  lv_obj_set_style_pad_left(ui_Screen7_Input, 10, 0);

  lv_obj_add_event_cb(ui_Screen7_Input, on_input_focused, LV_EVENT_FOCUSED, NULL);
  lv_obj_add_event_cb(ui_Screen7_Input, on_input_defocused, LV_EVENT_DEFOCUSED, NULL);

  // Send button
  btn_send = lv_obj_create(ui_Screen7);
  lv_obj_set_size(btn_send, 80, INPUT_H);
  lv_obj_set_pos(btn_send, SCR_W - 88, input_y);
  lv_obj_set_style_bg_color(btn_send, lv_color_hex(CLR_SEND), 0);
  lv_obj_set_style_bg_opa(btn_send, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btn_send, 0, 0);
  lv_obj_set_style_border_width(btn_send, 0, 0);
  lv_obj_set_style_shadow_width(btn_send, 0, 0);
  lv_obj_set_style_pad_all(btn_send, 0, 0);
  lv_obj_clear_flag(btn_send, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btn_send, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(btn_send, on_send_clicked, LV_EVENT_CLICKED, NULL);

  lv_obj_t *send_lbl = lv_label_create(btn_send);
  lv_label_set_text(send_lbl, LV_SYMBOL_RIGHT " Send");
  lv_obj_set_style_text_color(send_lbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(send_lbl, &montserrat_20_en_ru, 0);
  lv_obj_center(send_lbl);


  // --- Keyboard ---
  // Lazy initialized in on_input_focused to prevent boot-time LVGL mask calculation crash

  // --- Swipe navigation ---
  lv_obj_add_event_cb(ui_Screen7, ui_screen_swipe_event_cb, LV_EVENT_GESTURE, NULL);

  // --- Navigation buttons ---
  ui_create_standard_navigation_buttons(ui_Screen7);

  // --- Status label between nav arrows (BOTTOM_MID, above nav) ---
  status_label = lv_label_create(ui_Screen7);
  lv_label_set_text(status_label, "AI: System Ready (Gemini)");
  lv_obj_set_size(status_label, 560, 30);
  lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -30);
  lv_obj_set_style_text_color(status_label, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(status_label, &montserrat_20_en_ru, 0);
  lv_label_set_long_mode(status_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);

  // --- Welcome message ---
  if (strlen(terminal_buf) == 0) {
    ui_Screen7_append_text("=== Open Claw AI Terminal ===");
    ui_Screen7_append_text("ESP-Claw Framework Active");
    ui_Screen7_append_text("Type a command or use quick buttons.");
    ui_Screen7_append_text("");
  }

  ESP_LOGI(TAG, "Screen 7 - Open Claw initialized");
}

void ui_Screen7_screen_destroy(void) {
  keyboard = NULL;
  status_label = NULL;
  title_label = NULL;
  btn_send = NULL;
  ui_Screen7_Terminal = NULL;
  ui_Screen7_Input = NULL;

  if (ui_Screen7) {
    lv_obj_del(ui_Screen7);
    ui_Screen7 = NULL;
  }
  ESP_LOGI(TAG, "Screen 7 destroyed");
}

void ui_Screen7_update_layout(void) {
  // No dynamic layout updates needed for now
}
