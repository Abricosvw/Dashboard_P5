import re

with open('main/ui/screens/ui_Screen7.c', 'r', encoding='utf-8') as f:
    s7 = f.read()

new_layout = """
  // --- Terminal area ---
  int terminal_h = 360; // 360px height

  ui_Screen7_Terminal = lv_textarea_create(ui_Screen7);
  lv_obj_set_size(ui_Screen7_Terminal, SCR_W - 16, terminal_h);
  lv_obj_set_pos(ui_Screen7_Terminal, 8, TERMINAL_TOP);
  lv_textarea_set_text(ui_Screen7_Terminal, "");
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
  int input_y = btn_term_y + BTN_ROW_H + 4;

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

  // --- Lua Editor Area ---
  int lua_y = input_y + INPUT_H + 4;
  int lua_h = 320;

  ui_Screen7_LuaEditor = lv_textarea_create(ui_Screen7);
  lv_obj_set_size(ui_Screen7_LuaEditor, SCR_W - 16, lua_h);
  lv_obj_set_pos(ui_Screen7_LuaEditor, 8, lua_y);
  lv_obj_set_style_bg_color(ui_Screen7_LuaEditor, lv_color_hex(0x000000), 0);
  lv_obj_set_style_text_color(ui_Screen7_LuaEditor, lv_color_hex(0x00FF88), 0);
  lv_obj_set_style_text_font(ui_Screen7_LuaEditor, &montserrat_20_en_ru, 0);
  lv_obj_set_style_border_color(ui_Screen7_LuaEditor, lv_color_hex(CLR_BORDER), 0);
  lv_obj_set_style_border_width(ui_Screen7_LuaEditor, 1, 0);
  lv_obj_set_style_radius(ui_Screen7_LuaEditor, 0, 0);
  lv_obj_set_style_pad_all(ui_Screen7_LuaEditor, 12, 0);
  
  lv_obj_set_style_bg_color(ui_Screen7_LuaEditor, lv_color_hex(0xFFFFFF), LV_PART_CURSOR);
  lv_obj_set_style_bg_opa(ui_Screen7_LuaEditor, LV_OPA_COVER, LV_PART_CURSOR);
  
  lv_textarea_set_text(ui_Screen7_LuaEditor, 
    "setTickRate(2)\\n"
    "local counter = 0\\n"
    "canRxAdd(0x123)\\n"
    "function onTick()\\n"
    "    counter = counter + 1\\n"
    "    log(\\"onTick work! Counter: \\" .. counter)\\n"
    "    txCan(1, 0x600, 0, {counter, 0xAA, 0xBB})\\n"
    "end\\n"
    "function onCanRx(bus, id, dlc, data)\\n"
    "    log(\\"Wow, got CAN frame: \\" .. id)\\n"
    "end\\n");
    
  lv_textarea_set_cursor_click_pos(ui_Screen7_LuaEditor, true);
  lv_obj_add_event_cb(ui_Screen7_LuaEditor, on_input_focused, LV_EVENT_FOCUSED, NULL);
  lv_obj_add_event_cb(ui_Screen7_LuaEditor, on_input_defocused, LV_EVENT_DEFOCUSED, NULL);

  // --- Quick action buttons row ---
  int btn_lua_y = lua_y + lua_h + 4;

  lv_obj_t *btn_run = create_quick_btn(ui_Screen7, "Run Rule", lua_run_event_cb, btn_w);
  lv_obj_set_pos(btn_run, 8, btn_lua_y);
  lv_obj_set_style_bg_color(btn_run, lv_color_hex(0x00D4FF), 0);

  lv_obj_t *btn_save = create_quick_btn(ui_Screen7, "Save", lua_save_event_cb, btn_w);
  lv_obj_set_pos(btn_save, 8 + (btn_w + 4) * 1, btn_lua_y);
  lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x00FF88), 0);
  lv_obj_set_style_text_color(lv_obj_get_child(btn_save, 0), lv_color_black(), 0);

  lv_obj_t *btn_help = create_quick_btn(ui_Screen7, "Help", lua_help_event_cb, btn_w);
  lv_obj_set_pos(btn_help, 8 + (btn_w + 4) * 2, btn_lua_y);
  lv_obj_set_style_bg_color(btn_help, lv_color_hex(0xFFCC00), 0);
  lv_obj_set_style_text_color(lv_obj_get_child(btn_help, 0), lv_color_black(), 0);

  lv_obj_t *btn_gpio = create_quick_btn(ui_Screen7, "GPIO", gpio_map_event_cb, btn_w);
  lv_obj_set_pos(btn_gpio, 8 + (btn_w + 4) * 3, btn_lua_y);
  lv_obj_set_style_bg_color(btn_gpio, lv_color_hex(0xFF00FF), 0);
  lv_obj_set_style_text_color(lv_obj_get_child(btn_gpio, 0), lv_color_white(), 0);

  lv_obj_t *btn_tg = create_quick_btn(ui_Screen7, "TG", telegram_help_event_cb, btn_w);
  lv_obj_set_pos(btn_tg, 8 + (btn_w + 4) * 4, btn_lua_y);
  lv_obj_set_style_bg_color(btn_tg, lv_color_hex(0x0088FF), 0);
  lv_obj_set_style_text_color(lv_obj_get_child(btn_tg, 0), lv_color_white(), 0);

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
  ui_Screen7_append_text("=== Open Claw AI Terminal ===");
  ui_Screen7_append_text("ESP-Claw Framework Active");
  ui_Screen7_append_text("Type a command or use quick buttons.");
  ui_Screen7_append_text("");

  ESP_LOGI(TAG, "Screen 7 - Open Claw initialized");
}
"""

start_idx = s7.find('  // --- Terminal area ---')
end_idx = s7.find('void ui_Screen7_screen_destroy(void) {')

if start_idx != -1 and end_idx != -1:
    s7 = s7[:start_idx] + new_layout.strip() + '\n}\n\n' + s7[end_idx:]
    with open('main/ui/screens/ui_Screen7.c', 'w', encoding='utf-8') as f:
        f.write(s7)
    print("Screen 7 layout updated.")
else:
    print("Could not find start or end markers.")
