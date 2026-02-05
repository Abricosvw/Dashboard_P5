#include "ui_wifi_settings.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "main_gui.h"
#include "ui.h"
#include "wifi_controller.h"
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_WIFI_SEL";

static lv_obj_t *popup_overlay = NULL;
static lv_obj_t *wifi_list = NULL;
static lv_obj_t *kb = NULL;
static lv_obj_t *pwd_ta = NULL;
static char selected_ssid[33] = {0};
static wifi_scan_result_t scan_results[40]; // Move to static to save stack
static bool is_scanning = false;
static uint8_t current_kb_lang = 0; // 0=EN, 1=RU

// Control maps define button widths (lower 4 bits = width ratio)
// Russian keyboard: Row1=13 btns, Row2=11 btns, Row3=11 btns, Row4=4 btns
static const lv_btnmatrix_ctrl_t kb_ctrl_ru[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, // Row 1: 12 chars + backspace(wider)
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2,       // Row 2: 10 chars + enter(wider)
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2,       // Row 3: 10 chars + enter(wider)
    2, 1, 5, 1 // Row 4: EN(wider) + arrows + space(wide)
};

// English keyboard control map: Row1=11 btns, Row2=10 btns, Row3=11 btns,
// Row4=4 btns
static const lv_btnmatrix_ctrl_t kb_ctrl_en[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, // Row 1: 10 chars + backspace(wider)
    1, 1, 1, 1, 1, 1, 1, 1, 1, 2,    // Row 2: 9 chars + enter(wider)
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // Row 3: 11 chars
    2, 1, 5, 1                       // Row 4: RU(wider) + arrows + space(wide)
};

// Russian Cyrillic keyboard map (ЙЦУКЕН layout) - lowercase
static const char *kb_map_ru[] = {"й",
                                  "ц",
                                  "у",
                                  "к",
                                  "е",
                                  "н",
                                  "г",
                                  "ш",
                                  "щ",
                                  "з",
                                  "х",
                                  "ъ",
                                  LV_SYMBOL_BACKSPACE,
                                  "\n",
                                  "ф",
                                  "ы",
                                  "в",
                                  "а",
                                  "п",
                                  "р",
                                  "о",
                                  "л",
                                  "д",
                                  "ж",
                                  "э",
                                  "\n",
                                  "я",
                                  "ч",
                                  "с",
                                  "м",
                                  "и",
                                  "т",
                                  "ь",
                                  "б",
                                  "ю",
                                  ".",
                                  LV_SYMBOL_NEW_LINE,
                                  "\n",
                                  "EN",
                                  LV_SYMBOL_LEFT,
                                  " ",
                                  LV_SYMBOL_RIGHT,
                                  ""};

// Russian Cyrillic keyboard map - uppercase
static const char *kb_map_ru_uc[] = {"Й",
                                     "Ц",
                                     "У",
                                     "К",
                                     "Е",
                                     "Н",
                                     "Г",
                                     "Ш",
                                     "Щ",
                                     "З",
                                     "Х",
                                     "Ъ",
                                     LV_SYMBOL_BACKSPACE,
                                     "\n",
                                     "Ф",
                                     "Ы",
                                     "В",
                                     "А",
                                     "П",
                                     "Р",
                                     "О",
                                     "Л",
                                     "Д",
                                     "Ж",
                                     "Э",
                                     "\n",
                                     "Я",
                                     "Ч",
                                     "С",
                                     "М",
                                     "И",
                                     "Т",
                                     "Ь",
                                     "Б",
                                     "Ю",
                                     ".",
                                     LV_SYMBOL_NEW_LINE,
                                     "\n",
                                     "EN",
                                     LV_SYMBOL_LEFT,
                                     " ",
                                     LV_SYMBOL_RIGHT,
                                     ""};

// English QWERTY keyboard map - lowercase
static const char *kb_map_en[] = {"q",
                                  "w",
                                  "e",
                                  "r",
                                  "t",
                                  "y",
                                  "u",
                                  "i",
                                  "o",
                                  "p",
                                  LV_SYMBOL_BACKSPACE,
                                  "\n",
                                  "a",
                                  "s",
                                  "d",
                                  "f",
                                  "g",
                                  "h",
                                  "j",
                                  "k",
                                  "l",
                                  LV_SYMBOL_NEW_LINE,
                                  "\n",
                                  "z",
                                  "x",
                                  "c",
                                  "v",
                                  "b",
                                  "n",
                                  "m",
                                  ".",
                                  ",",
                                  "?",
                                  "!",
                                  "\n",
                                  "RU",
                                  LV_SYMBOL_LEFT,
                                  " ",
                                  LV_SYMBOL_RIGHT,
                                  ""};

// English QWERTY keyboard map - uppercase
static const char *kb_map_en_uc[] = {"Q",
                                     "W",
                                     "E",
                                     "R",
                                     "T",
                                     "Y",
                                     "U",
                                     "I",
                                     "O",
                                     "P",
                                     LV_SYMBOL_BACKSPACE,
                                     "\n",
                                     "A",
                                     "S",
                                     "D",
                                     "F",
                                     "G",
                                     "H",
                                     "J",
                                     "K",
                                     "L",
                                     LV_SYMBOL_NEW_LINE,
                                     "\n",
                                     "Z",
                                     "X",
                                     "C",
                                     "V",
                                     "B",
                                     "N",
                                     "M",
                                     ".",
                                     ",",
                                     "?",
                                     "!",
                                     "\n",
                                     "RU",
                                     LV_SYMBOL_LEFT,
                                     " ",
                                     LV_SYMBOL_RIGHT,
                                     ""};

// Keyboard value changed callback - handles EN/RU button clicks on keyboard
static void kb_value_cb(lv_event_t *e) {
  lv_obj_t *keyboard = lv_event_get_target(e);
  uint16_t btn_id = lv_btnmatrix_get_selected_btn(keyboard);
  if (btn_id == LV_BTNMATRIX_BTN_NONE)
    return;

  const char *txt = lv_btnmatrix_get_btn_text(keyboard, btn_id);
  if (!txt)
    return;

  // Check if EN or RU button was clicked
  if (strcmp(txt, "EN") == 0 || strcmp(txt, "RU") == 0) {
    current_kb_lang = !current_kb_lang; // Toggle 0<->1

    if (current_kb_lang == 1) {
      // Switch to Russian
      lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER, kb_map_ru,
                          kb_ctrl_ru);
      lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER, kb_map_ru_uc,
                          kb_ctrl_ru);
      ESP_LOGI(TAG, "Switched to Russian keyboard");
    } else {
      // Switch to English
      lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER, kb_map_en,
                          kb_ctrl_en);
      lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER, kb_map_en_uc,
                          kb_ctrl_en);
      ESP_LOGI(TAG, "Switched to English keyboard");
    }
  }
}

static void ssid_select_cb(lv_event_t *e);
static void update_wifi_list_cb(void) {
  if (!popup_overlay || !wifi_list) {
    ESP_LOGW(TAG, "WiFi popup or list already destroyed, skipping update");
    return;
  }

  ESP_LOGI(TAG, "Populating wifi list with lock held...");
  lv_obj_clean(wifi_list);

  for (int i = 0; i < 40 && scan_results[i].ssid[0] != '\0'; i++) {
    lv_obj_t *btn = lv_list_add_btn(wifi_list, NULL, scan_results[i].ssid);
    if (!btn)
      continue;
    lv_obj_add_event_cb(btn, ssid_select_cb, LV_EVENT_CLICKED, NULL);

    char rssi_str[32];
    snprintf(rssi_str, sizeof(rssi_str), "%d dBm", scan_results[i].rssi);
    lv_obj_t *r_label = lv_label_create(btn);
    if (r_label) {
      lv_label_set_text(r_label, rssi_str);
      lv_obj_align(r_label, LV_ALIGN_RIGHT_MID, -10, 0);
    }
  }

  if (wifi_list) {
    lv_obj_invalidate(wifi_list);
  }
  ESP_LOGI(TAG, "Wifi list population complete");
}

static void wifi_scan_task(void *pvParameters) {
  ESP_LOGI(TAG, "Starting async wifi scan...");
  int count = wifi_controller_scan(scan_results, 40);
  ESP_LOGI(TAG, "Async scan returned %d networks", count);

  // Cap results and ensure null termination if necessary (though memset in
  // controller helps)
  if (count < 40) {
    memset(&scan_results[count], 0, sizeof(wifi_scan_result_t) * (40 - count));
  }

  // Use main_gui's lock to update UI safely
  if (example_lvgl_lock(500)) {
    if (popup_overlay && wifi_list) {
      update_wifi_list_cb();
    } else {
      ESP_LOGW(TAG, "Popup closed during scan, update skipped");
    }
    example_lvgl_unlock();
  } else {
    ESP_LOGE(TAG, "Failed to get LVGL lock to update WiFi list!");
  }

  is_scanning = false;
  vTaskDelete(NULL);
}

static void close_popup_cb(lv_event_t *e) {
  if (popup_overlay) {
    lv_obj_del(popup_overlay);
    popup_overlay = NULL;
    wifi_list = NULL; // Crucial: clear reference after deletion
    kb = NULL;
    pwd_ta = NULL;
  }
}

static void connect_click_cb(lv_event_t *e) {
  const char *pwd = lv_textarea_get_text(pwd_ta);
  ESP_LOGI(TAG, "Connecting to %s", selected_ssid);

  // Save WiFi credentials to NVS
  nvs_handle_t nvs_handle;
  if (nvs_open("wifi_creds", NVS_READWRITE, &nvs_handle) == ESP_OK) {
    nvs_set_str(nvs_handle, "ssid", selected_ssid);
    nvs_set_str(nvs_handle, "password", pwd);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "WiFi credentials saved to NVS");
  }

  wifi_controller_connect_to_ap(selected_ssid, pwd);

  close_popup_cb(NULL);
}

static void cancel_pwd_cb(lv_event_t *e) {
  lv_obj_t *pwd_cont = (lv_obj_t *)lv_event_get_user_data(e);
  if (pwd_cont) {
    lv_obj_del(pwd_cont);
  }
  if (kb) {
    lv_obj_del(kb);
    kb = NULL;
  }
}

static void kb_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) {
    ESP_LOGI(TAG, "Keyboard: Enter pressed, connecting...");
    connect_click_cb(NULL);
  }
  // Note: We no longer handle LV_EVENT_DELETE here to avoid recursive deletion
}

static void ssid_select_cb(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_target(e);
  lv_obj_t *label = NULL;

  uint32_t child_cnt = lv_obj_get_child_cnt(btn);
  for (uint32_t i = 0; i < child_cnt; i++) {
    lv_obj_t *child = lv_obj_get_child(btn, i);
    if (lv_obj_check_type(child, &lv_label_class)) {
      label = child;
      break;
    }
  }

  if (label) {
    strncpy(selected_ssid, lv_label_get_text(label), sizeof(selected_ssid) - 1);

    // Open Password Dialog
    lv_obj_t *pwd_cont = lv_obj_create(popup_overlay);
    lv_obj_set_size(pwd_cont, 500, 400);
    lv_obj_center(pwd_cont);
    lv_obj_set_style_bg_color(pwd_cont, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_color(pwd_cont, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_border_width(pwd_cont, 2, 0);

    lv_obj_t *title = lv_label_create(pwd_cont);
    lv_label_set_text_fmt(title, "Connect to: %s", selected_ssid);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);

    pwd_ta = lv_textarea_create(pwd_cont);
    lv_textarea_set_password_mode(pwd_ta, false); // Show password while typing
    lv_textarea_set_one_line(pwd_ta, true);
    lv_textarea_set_placeholder_text(pwd_ta, "Enter WiFi password");
    lv_obj_set_width(pwd_ta, LV_PCT(90));
    lv_obj_align(pwd_ta, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_add_state(pwd_ta, LV_STATE_FOCUSED);

    kb = lv_keyboard_create(popup_overlay);
    lv_obj_set_style_text_font(kb, &montserrat_20_en_ru, 0); // Apply EN/RU font
    lv_obj_set_size(kb, LV_PCT(100), LV_PCT(30));
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, pwd_ta);
    lv_obj_set_style_text_font(pwd_ta, &montserrat_20_en_ru,
                               0); // Apply to textarea too

    // Set initial keyboard layout to English with proper control maps
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, kb_map_en, kb_ctrl_en);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, kb_map_en_uc,
                        kb_ctrl_en);
    current_kb_lang = 0; // Start with EN

    // Add callbacks for language toggle and standard keyboard events
    lv_obj_add_event_cb(kb, kb_value_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *connect_btn = lv_btn_create(pwd_cont);
    lv_obj_set_size(connect_btn, 150, 50);
    lv_obj_align(connect_btn, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    lv_obj_set_style_bg_color(connect_btn, lv_color_hex(0x00FF88), 0);
    lv_obj_add_event_cb(connect_btn, connect_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l1 = lv_label_create(connect_btn);
    lv_label_set_text(l1, "CONNECT");
    lv_obj_center(l1);
    lv_obj_set_style_text_color(l1, lv_color_black(), 0);

    lv_obj_t *cancel_btn = lv_btn_create(pwd_cont);
    lv_obj_set_size(cancel_btn, 150, 50);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x666666), 0);
    lv_obj_add_event_cb(cancel_btn, cancel_pwd_cb, LV_EVENT_CLICKED, pwd_cont);

    lv_obj_t *l2 = lv_label_create(cancel_btn);
    lv_label_set_text(l2, "CANCEL");
    lv_obj_center(l2);
  }
}

static void refresh_scan_cb(lv_event_t *e) {
  if (!wifi_list) {
    ESP_LOGW(TAG, "wifi_list is NULL, cannot refresh");
    return;
  }

  if (is_scanning) {
    ESP_LOGW(TAG, "Already scanning...");
    return;
  }

  ESP_LOGI(TAG, "Cleaning wifi list and showing scanning status...");
  lv_obj_clean(wifi_list);
  lv_list_add_text(wifi_list, "Scanning... Please wait.");

  is_scanning = true;
  xTaskCreatePinnedToCore(wifi_scan_task, "wifi_scan", 4096, NULL, 4, NULL, 0);
}

void ui_show_wifi_settings(void) {
  if (popup_overlay)
    return;

  popup_overlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(popup_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(popup_overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(popup_overlay, LV_OPA_70, 0);
  lv_obj_clear_flag(popup_overlay, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *container = lv_obj_create(popup_overlay);
  lv_obj_set_size(container, 600, 900);
  lv_obj_center(container);
  lv_obj_set_style_bg_color(container, lv_color_hex(0x222222), 0);
  lv_obj_set_style_border_color(container, lv_color_hex(0x00FF88), 0);
  lv_obj_set_style_border_width(container, 2, 0);
  lv_obj_set_style_pad_all(container, 20, 0);

  lv_obj_t *title = lv_label_create(container);
  lv_label_set_text(title, "WIFI SETTINGS");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x00FF88), 0);

  // WiFi Status Panel - Shows both STA and AP modes
  lv_obj_t *status_panel = lv_obj_create(container);
  lv_obj_set_size(status_panel, LV_PCT(100),
                  120); // Increased height for both modes
  lv_obj_align(status_panel, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_set_style_bg_color(status_panel, lv_color_hex(0x1a1a2e), 0);
  lv_obj_set_style_border_color(status_panel, lv_color_hex(0x00D4FF), 0);
  lv_obj_set_style_border_width(status_panel, 1, 0);
  lv_obj_set_style_pad_all(status_panel, 10, 0);
  lv_obj_clear_flag(status_panel, LV_OBJ_FLAG_SCROLLABLE);

  // Get current WiFi status
  wifi_controller_info_t wifi_info;
  wifi_controller_get_info(&wifi_info);

  // STA Mode Status (Internet connection)
  lv_obj_t *sta_label = lv_label_create(status_panel);
  if (wifi_info.ssid[0] != '\0' && wifi_info.ip[0] != '\0') {
    lv_label_set_text_fmt(sta_label,
                          LV_SYMBOL_WIFI " STA: %s\n   IP: %s  RSSI: %d dBm",
                          wifi_info.ssid, wifi_info.ip, wifi_info.rssi);
    lv_obj_set_style_text_color(sta_label, lv_color_hex(0x00FF88), 0);
  } else if (wifi_info.ssid[0] != '\0') {
    lv_label_set_text_fmt(sta_label, LV_SYMBOL_WIFI " STA: Connecting to %s...",
                          wifi_info.ssid);
    lv_obj_set_style_text_color(sta_label, lv_color_hex(0xFFAA00), 0);
  } else {
    lv_label_set_text(sta_label, LV_SYMBOL_WIFI " STA: Not Connected");
    lv_obj_set_style_text_color(sta_label, lv_color_hex(0xFF6666), 0);
  }
  lv_obj_align(sta_label, LV_ALIGN_TOP_LEFT, 0, 0);

  // AP Mode Status (Hotspot for phone)
  lv_obj_t *ap_label = lv_label_create(status_panel);
  lv_label_set_text(
      ap_label, LV_SYMBOL_CALL
      " AP: ESP32P4_Dashboard\n   IP: 192.168.4.1 (Connect here!)");
  lv_obj_set_style_text_color(ap_label, lv_color_hex(0x00D4FF), 0);
  lv_obj_align(ap_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  lv_obj_t *refresh_btn = lv_btn_create(container);
  lv_obj_set_size(refresh_btn, 150, 50);
  lv_obj_align(refresh_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(0x444444), 0);
  lv_obj_add_event_cb(refresh_btn, refresh_scan_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *re_label = lv_label_create(refresh_btn);
  lv_label_set_text(re_label, LV_SYMBOL_REFRESH " SCAN");
  lv_obj_center(re_label);

  wifi_list = lv_list_create(container);
  lv_obj_set_size(wifi_list, LV_PCT(100),
                  500); // Adjusted for larger status panel
  lv_obj_align(wifi_list, LV_ALIGN_TOP_MID, 0,
               170); // Moved down for larger status panel
  lv_obj_set_style_bg_color(wifi_list, lv_color_hex(0x111111), 0);
  lv_obj_set_style_text_color(wifi_list, lv_color_white(), 0);

  lv_obj_t *close_btn = lv_btn_create(container);
  lv_obj_set_size(close_btn, 150, 50);
  lv_obj_align(close_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x555555), 0);
  lv_obj_add_event_cb(close_btn, close_popup_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *close_label = lv_label_create(close_btn);
  lv_label_set_text(close_label, "CLOSE");
  lv_obj_center(close_label);

  refresh_scan_cb(NULL);
}
