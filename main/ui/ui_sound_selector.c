#include "audio_manager.h"
#include "lvgl.h"
#include "ui/settings_config.h"
#include <dirent.h>
#include <esp_log.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_SOUND_SEL";

static lv_obj_t *popup_overlay = NULL;
static lv_obj_t *file_list = NULL;

static void close_popup_cb(lv_event_t *e) {
  if (popup_overlay) {
    lv_obj_del(popup_overlay);
    popup_overlay = NULL;
  }
}

static void file_select_cb(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_target(e);
  lv_obj_t *label = NULL;

  // In LVGL list buttons, child 0 is usually the icon, child 1 is the label
  uint32_t child_cnt = lv_obj_get_child_cnt(btn);
  for (uint32_t i = 0; i < child_cnt; i++) {
    lv_obj_t *child = lv_obj_get_child(btn, i);
    if (lv_obj_check_type(child, &lv_label_class)) {
      label = child;
      break;
    }
  }

  if (!label) {
    ESP_LOGE(TAG, "Could not find label in list button");
    return;
  }

  const char *filename = lv_label_get_text(label);
  char full_path[128];
  snprintf(full_path, sizeof(full_path), "/sdcard/SYSTEM/SOUND/%s", filename);

  ESP_LOGI(TAG, "Selected sound: %s", full_path);

  // Update settings
  settings_set_boot_sound_path(full_path);
  trigger_settings_save();

  // Play preview
  audio_play_wav(full_path);
}

void ui_show_sound_selector(void) {
  if (popup_overlay)
    return;

  // Create a semi-transparent background overlay
  popup_overlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(popup_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(popup_overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(popup_overlay, LV_OPA_70, 0);
  lv_obj_clear_flag(popup_overlay, LV_OBJ_FLAG_SCROLLABLE);

  // Create the main popup container
  lv_obj_t *container = lv_obj_create(popup_overlay);
  lv_obj_set_size(container, 600, 800);
  lv_obj_center(container);
  lv_obj_set_style_bg_color(container, lv_color_hex(0x222222), 0);
  lv_obj_set_style_border_color(container, lv_color_hex(0x00D4FF), 0);
  lv_obj_set_style_border_width(container, 2, 0);
  lv_obj_set_style_pad_all(container, 20, 0);

  // Title
  lv_obj_t *title = lv_label_create(container);
  lv_label_set_text(title, "Select Startup Sound");
  lv_obj_set_style_text_color(title, lv_color_hex(0x00D4FF), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

  // File List
  file_list = lv_list_create(container);
  lv_obj_set_size(file_list, LV_PCT(100), 600);
  lv_obj_align(file_list, LV_ALIGN_TOP_MID, 0, 50);
  lv_obj_set_style_bg_color(file_list, lv_color_hex(0x111111), 0);
  lv_obj_set_style_text_color(file_list, lv_color_white(), 0);

  // Scan SD card for .wav files
  const char *path = "/sdcard/SYSTEM/SOUND";
  DIR *dir = opendir(path);
  if (dir) {
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
      if (ent->d_type == DT_REG &&
          (strstr(ent->d_name, ".WAV") || strstr(ent->d_name, ".wav"))) {
        lv_obj_t *btn =
            lv_list_add_btn(file_list, LV_SYMBOL_AUDIO, ent->d_name);
        lv_obj_add_event_cb(btn, file_select_cb, LV_EVENT_CLICKED, NULL);

        // Highlight if currently selected
        const char *current = settings_get_boot_sound_path();
        if (current && strstr(current, ent->d_name)) {
          lv_obj_set_style_bg_color(btn, lv_color_hex(0x00AA00), 0);
        }
      }
    }
    closedir(dir);
  } else {
    lv_list_add_text(file_list, "No sound files found\n(/sdcard/SYSTEM/SOUND)");
  }

  // Close Button
  lv_obj_t *close_btn = lv_btn_create(container);
  lv_obj_set_size(close_btn, 200, 50);
  lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x555555), 0);
  lv_obj_add_event_cb(close_btn, close_popup_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *close_label = lv_label_create(close_btn);
  lv_label_set_text(close_label, "CLOSE");
  lv_obj_center(close_label);
}
