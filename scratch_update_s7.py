import re

with open('main/ui/screens/ui_Screen7.c', 'r', encoding='utf-8') as f:
    s7 = f.read()

# 1. Add LuaEditor to destroy function
s7 = s7.replace('  ui_Screen7_Input = NULL;\n', '  ui_Screen7_Input = NULL;\n  ui_Screen7_LuaEditor = NULL;\n')

# 2. Add the Lua editor callbacks to the top
callbacks = """
// ---------- New Callbacks ----------
static void lua_run_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    ESP_LOGI(TAG, "Run ESP-Claw Lua Rule");
    if(ui_Screen7_LuaEditor) {
      const char * code = lv_textarea_get_text(ui_Screen7_LuaEditor);
      extern esp_err_t lua_manager_execute(const char *script);
      lua_manager_execute(code);
    }
  }
}

static void lua_save_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    ESP_LOGI(TAG, "Save ESP-Claw Rule");
    if(ui_Screen7_LuaEditor) {
      const char * code = lv_textarea_get_text(ui_Screen7_LuaEditor);
      extern esp_err_t lua_manager_save_background_script(const char *script);
      lua_manager_save_background_script(code);
      ui_Screen7_set_status("Rule Saved!");
    }
  }
}

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
      "ESP32-P4 GPIO Map:\\n\\n"
      "#FF0000 0-3: JTAG/System (DO NOT USE)#\\n"
      "#00FF00 FREE PINS: 6, 22-32, 34-38, 45-52#\\n"
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
    lv_label_set_text(map_text, "#0088FF Telegram Messenger Setup:#\\nSee ai_config.h");
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
"""

s7 = s7.replace('// ---------- Callbacks ----------', callbacks + '\n// ---------- Callbacks ----------')

# 3. Add set_lua_text function
set_lua_text = """
void ui_Screen7_set_lua_text(const char *text) {
  if (ui_Screen7_LuaEditor && text) {
    lv_textarea_set_text(ui_Screen7_LuaEditor, text);
  }
}
"""
s7 = s7.replace('// ---------- Callbacks ----------', set_lua_text + '\n// ---------- Callbacks ----------')

with open('main/ui/screens/ui_Screen7.c', 'w', encoding='utf-8') as f:
    f.write(s7)

print("Callbacks added.")
