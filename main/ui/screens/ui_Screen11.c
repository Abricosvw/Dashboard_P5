#include "ui_Screen11.h"
#include "../ui.h"
#include "../ui_screen_manager.h"
#include "diagnostic_tester.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "SCREEN11";

// Root screen pointer
lv_obj_t *ui_Screen11 = NULL;

// Header live parameters
static lv_obj_t *ui_Screen11_Status_Label = NULL;
static lv_obj_t *ui_Screen11_Progress_Bar = NULL;
static lv_obj_t *ui_Screen11_Progress_Label = NULL;
static lv_obj_t *ui_Screen11_Logs_Textarea = NULL;

// Color palette
#define CLR_BG 0x0A0F1A
#define CLR_PANEL 0x111827
#define CLR_BORDER 0x334155
#define CLR_CYAN 0x00D4FF
#define CLR_GOLD 0xFFD700
#define CLR_RED 0xFF3333
#define CLR_GREEN 0x00FF88
#define CLR_BTN_BG 0x1E293B
#define CLR_BTN_ACTIVE 0x0F172A
#define CLR_TEXT_DIM 0x94A3B8
#define CLR_TEXT_WHITE 0xF1F5F9

// Fonts declared in ui.h
LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(montserrat_20_en_ru);

// Event callbacks
static void scan_btn_cb(lv_event_t *e) {
  ESP_LOGI(TAG, "Scan button clicked");
  diagnostic_tester_start_scan();
}

static void clear_btn_cb(lv_event_t *e) {
  ESP_LOGI(TAG, "Clear faults button clicked");
  diagnostic_tester_start_clear();
}

static void clear_logs_btn_cb(lv_event_t *e) {
  ESP_LOGI(TAG, "Clear logs button clicked");
  diagnostic_tester_clear_logs();
}

void ui_Screen11_screen_init(void) {
  ESP_LOGI(TAG, "Initializing Screen 11 (VAG Diagnostic Scanner)...");

  // --- ROOT SCREEN ---
  ui_Screen11 = lv_obj_create(NULL);
  lv_obj_set_size(ui_Screen11, 720, 1280);
  lv_obj_clear_flag(ui_Screen11, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(ui_Screen11, lv_color_hex(CLR_BG), 0);
  lv_obj_set_style_bg_opa(ui_Screen11, LV_OPA_COVER, 0);

  // --- HEADER PANEL ---
  lv_obj_t *header_panel = lv_obj_create(ui_Screen11);
  lv_obj_set_size(header_panel, 700, 80);
  lv_obj_set_pos(header_panel, 10, 10);
  lv_obj_set_style_bg_color(header_panel, lv_color_hex(CLR_PANEL), 0);
  lv_obj_set_style_bg_opa(header_panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(header_panel, lv_color_hex(CLR_BORDER), 0);
  lv_obj_set_style_border_width(header_panel, 1, 0);
  lv_obj_set_style_radius(header_panel, 10, 0);
  lv_obj_clear_flag(header_panel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(header_panel);
  lv_label_set_text(title, "VAG DIAGNOSTIC SCANNER");
  lv_obj_set_style_text_color(title, lv_color_hex(CLR_CYAN), 0);
  lv_obj_set_style_text_font(title, &montserrat_20_en_ru, 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

  // --- STATUS & PROGRESS PANEL ---
  lv_obj_t *status_panel = lv_obj_create(ui_Screen11);
  lv_obj_set_size(status_panel, 700, 120);
  lv_obj_set_pos(status_panel, 10, 100);
  lv_obj_set_style_bg_color(status_panel, lv_color_hex(CLR_PANEL), 0);
  lv_obj_set_style_bg_opa(status_panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(status_panel, lv_color_hex(CLR_BORDER), 0);
  lv_obj_set_style_border_width(status_panel, 1, 0);
  lv_obj_set_style_radius(status_panel, 12, 0);
  lv_obj_clear_flag(status_panel, LV_OBJ_FLAG_SCROLLABLE);

  ui_Screen11_Status_Label = lv_label_create(status_panel);
  lv_label_set_text(ui_Screen11_Status_Label, "STATUS: IDLE (READY)");
  lv_obj_set_style_text_color(ui_Screen11_Status_Label,
                              lv_color_hex(CLR_TEXT_WHITE), 0);
  lv_obj_set_style_text_font(ui_Screen11_Status_Label, &lv_font_montserrat_14,
                             0);
  lv_obj_align(ui_Screen11_Status_Label, LV_ALIGN_TOP_LEFT, 15, 15);

  ui_Screen11_Progress_Label = lv_label_create(status_panel);
  lv_label_set_text(ui_Screen11_Progress_Label, "0%");
  lv_obj_set_style_text_color(ui_Screen11_Progress_Label,
                              lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(ui_Screen11_Progress_Label, &lv_font_montserrat_14,
                             0);
  lv_obj_align(ui_Screen11_Progress_Label, LV_ALIGN_TOP_RIGHT, -15, 15);

  ui_Screen11_Progress_Bar = lv_bar_create(status_panel);
  lv_obj_set_size(ui_Screen11_Progress_Bar, 670, 16);
  lv_obj_align(ui_Screen11_Progress_Bar, LV_ALIGN_BOTTOM_MID, 0, -15);
  lv_bar_set_range(ui_Screen11_Progress_Bar, 0, 100);
  lv_obj_set_style_bg_color(ui_Screen11_Progress_Bar, lv_color_hex(0x1F293B),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui_Screen11_Progress_Bar, lv_color_hex(CLR_CYAN),
                            LV_PART_INDICATOR);
  lv_bar_set_value(ui_Screen11_Progress_Bar, 0, LV_ANIM_OFF);

  // --- LOG TERMINAL PANEL ---
  lv_obj_t *terminal_panel = lv_obj_create(ui_Screen11);
  lv_obj_set_size(terminal_panel, 700, 830);
  lv_obj_set_pos(terminal_panel, 10, 230);
  lv_obj_set_style_bg_color(terminal_panel, lv_color_hex(CLR_PANEL), 0);
  lv_obj_set_style_bg_opa(terminal_panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(terminal_panel, lv_color_hex(CLR_BORDER), 0);
  lv_obj_set_style_border_width(terminal_panel, 1, 0);
  lv_obj_set_style_radius(terminal_panel, 12, 0);
  lv_obj_clear_flag(terminal_panel, LV_OBJ_FLAG_SCROLLABLE);

  ui_Screen11_Logs_Textarea = lv_textarea_create(terminal_panel);
  lv_obj_set_size(ui_Screen11_Logs_Textarea, 680, 800);
  lv_obj_align(ui_Screen11_Logs_Textarea, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(ui_Screen11_Logs_Textarea, lv_color_hex(0x05070C),
                            0);
  lv_obj_set_style_bg_opa(ui_Screen11_Logs_Textarea, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(ui_Screen11_Logs_Textarea,
                              lv_color_hex(CLR_GREEN), 0);
  lv_obj_set_style_text_font(ui_Screen11_Logs_Textarea, &lv_font_montserrat_12,
                             0);
  lv_obj_set_style_border_width(ui_Screen11_Logs_Textarea, 0, 0);
  lv_obj_set_style_radius(ui_Screen11_Logs_Textarea, 6, 0);
  lv_obj_set_style_pad_all(ui_Screen11_Logs_Textarea, 10, 0);

  // Hide cursor styling
  lv_obj_set_style_bg_opa(ui_Screen11_Logs_Textarea, LV_OPA_TRANSP,
                          LV_PART_CURSOR);
  lv_obj_set_style_border_opa(ui_Screen11_Logs_Textarea, LV_OPA_TRANSP,
                              LV_PART_CURSOR);

  lv_textarea_set_cursor_click_pos(ui_Screen11_Logs_Textarea, false);
  lv_textarea_set_placeholder_text(
      ui_Screen11_Logs_Textarea,
      "Scanner terminal logs will appear here...\nConnect the device to the "
      "Powertrain CAN bus and click 'AUTO SCAN'.");

  // --- ACTION PANEL ---
  lv_obj_t *action_panel = lv_obj_create(ui_Screen11);
  lv_obj_set_size(action_panel, 700, 110);
  lv_obj_set_pos(action_panel, 10, 1070);
  lv_obj_set_style_bg_color(action_panel, lv_color_hex(CLR_PANEL), 0);
  lv_obj_set_style_bg_opa(action_panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(action_panel, lv_color_hex(CLR_BORDER), 0);
  lv_obj_set_style_border_width(action_panel, 1, 0);
  lv_obj_set_style_radius(action_panel, 12, 0);
  lv_obj_clear_flag(action_panel, LV_OBJ_FLAG_SCROLLABLE);

  // Auto Scan button
  lv_obj_t *scan_btn = lv_btn_create(action_panel);
  lv_obj_set_size(scan_btn, 200, 48);
  lv_obj_align(scan_btn, LV_ALIGN_LEFT_MID, 15, 0);
  lv_obj_set_style_bg_color(scan_btn, lv_color_hex(CLR_CYAN), 0);
  lv_obj_set_style_radius(scan_btn, 8, 0);
  lv_obj_set_style_shadow_width(scan_btn, 0, 0);
  lv_obj_add_event_cb(scan_btn, scan_btn_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *scan_lbl = lv_label_create(scan_btn);
  lv_label_set_text(scan_lbl, "AUTO SCAN");
  lv_obj_set_style_text_color(scan_lbl, lv_color_black(), 0);
  lv_obj_set_style_text_font(scan_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(scan_lbl);

  // Clear Faults button
  lv_obj_t *clear_btn = lv_btn_create(action_panel);
  lv_obj_set_size(clear_btn, 200, 48);
  lv_obj_align(clear_btn, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(clear_btn, lv_color_hex(0xE76F51),
                            0); // Warm orange/coral
  lv_obj_set_style_radius(clear_btn, 8, 0);
  lv_obj_set_style_shadow_width(clear_btn, 0, 0);
  lv_obj_add_event_cb(clear_btn, clear_btn_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *clear_lbl = lv_label_create(clear_btn);
  lv_label_set_text(clear_lbl, "CLEAR FAULTS");
  lv_obj_set_style_text_color(clear_lbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(clear_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(clear_lbl);

  // Clear Logs button
  lv_obj_t *clear_logs_btn = lv_btn_create(action_panel);
  lv_obj_set_size(clear_logs_btn, 200, 48);
  lv_obj_align(clear_logs_btn, LV_ALIGN_RIGHT_MID, -15, 0);
  lv_obj_set_style_bg_color(clear_logs_btn, lv_color_hex(CLR_BTN_BG), 0);
  lv_obj_set_style_radius(clear_logs_btn, 8, 0);
  lv_obj_set_style_shadow_width(clear_logs_btn, 0, 0);
  lv_obj_add_event_cb(clear_logs_btn, clear_logs_btn_cb, LV_EVENT_CLICKED,
                      NULL);

  lv_obj_t *clear_logs_lbl = lv_label_create(clear_logs_btn);
  lv_label_set_text(clear_logs_lbl, "CLEAR LOGS");
  lv_obj_set_style_text_color(clear_logs_lbl, lv_color_hex(CLR_TEXT_DIM), 0);
  lv_obj_set_style_text_font(clear_logs_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(clear_logs_lbl);

  // --- NAVIGATION BUTTONS ---
  ui_create_standard_navigation_buttons(ui_Screen11);

  ESP_LOGI(TAG, "Screen 11 (VAG Diagnostic Scanner) successfully initialized.");
}

void ui_Screen11_screen_destroy(void) {
  ESP_LOGI(TAG, "Destroying ui_Screen11...");
  if (ui_Screen11) {
    lv_obj_del(ui_Screen11);
    ui_Screen11 = NULL;
  }
}

void ui_Screen11_update(void) {
  if (!ui_Screen11)
    return;

  tester_state_t state = diagnostic_tester_get_state();
  int progress = diagnostic_tester_get_progress();
  const char *logs = diagnostic_tester_get_logs();

  // Update Status label & color
  if (ui_Screen11_Status_Label) {
    switch (state) {
    case TESTER_IDLE:
      lv_label_set_text(ui_Screen11_Status_Label, "STATUS: IDLE (READY)");
      lv_obj_set_style_text_color(ui_Screen11_Status_Label,
                                  lv_color_hex(CLR_TEXT_WHITE), 0);
      break;
    case TESTER_SCANNING:
      lv_label_set_text(ui_Screen11_Status_Label,
                        "STATUS: SCANNING POWERTRAIN BUS...");
      lv_obj_set_style_text_color(ui_Screen11_Status_Label,
                                  lv_color_hex(CLR_GOLD), 0);
      break;
    case TESTER_CLEARING:
      lv_label_set_text(ui_Screen11_Status_Label,
                        "STATUS: CLEARING FAULT CODES...");
      lv_obj_set_style_text_color(ui_Screen11_Status_Label,
                                  lv_color_hex(0xE76F51), 0);
      break;
    case TESTER_FINISHED:
      lv_label_set_text(ui_Screen11_Status_Label, "STATUS: COMPLETED");
      lv_obj_set_style_text_color(ui_Screen11_Status_Label,
                                  lv_color_hex(CLR_GREEN), 0);
      break;
    }
  }

  // Update Progress Bar & Percentage
  if (ui_Screen11_Progress_Bar) {
    lv_bar_set_value(ui_Screen11_Progress_Bar, progress, LV_ANIM_OFF);
  }
  if (ui_Screen11_Progress_Label) {
    lv_label_set_text_fmt(ui_Screen11_Progress_Label, "%d%%", progress);
  }

  // Update Logs Textarea (efficient delta update)
  if (ui_Screen11_Logs_Textarea) {
    static char last_logs[4096] = {0};
    if (strcmp(last_logs, logs) != 0) {
      lv_textarea_set_text(ui_Screen11_Logs_Textarea, logs);
      lv_textarea_set_cursor_pos(ui_Screen11_Logs_Textarea,
                                 LV_TEXTAREA_CURSOR_LAST);
      strncpy(last_logs, logs, sizeof(last_logs));
    }
  }
}
