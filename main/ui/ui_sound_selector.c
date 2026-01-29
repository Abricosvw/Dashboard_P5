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
static lv_obj_t *volume_slider = NULL;
static int current_file_index = -1;

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

  // Update index
  if (file_list) {
    uint32_t count = lv_obj_get_child_cnt(file_list);
    for (uint32_t i = 0; i < count; i++) {
      if (lv_obj_get_child(file_list, i) == btn) {
        current_file_index = i;
        break;
      }
    }
  }

  // Play preview
  audio_play_wav(full_path);
}

static void volume_event_cb(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  int vol = (int)lv_slider_get_value(slider);
  audio_set_volume(vol);
}

static void playback_control_cb(lv_event_t *e) {
  const char *cmd = (const char *)lv_event_get_user_data(e);
  if (!cmd)
    return;

  if (strcmp(cmd, "play") == 0) {
    audio_play_wav(settings_get_boot_sound_path());
  } else if (strcmp(cmd, "pause") == 0) {
    audio_pause();
  } else if (strcmp(cmd, "resume") == 0) { // Not used by icon but useful
    audio_resume();
  } else if (strcmp(cmd, "stop") == 0) {
    audio_stop();
  } else if (strcmp(cmd, "prev") == 0 || strcmp(cmd, "next") == 0) {
    if (!file_list)
      return;
    uint32_t count = lv_obj_get_child_cnt(file_list);
    if (count == 0)
      return;

    if (strcmp(cmd, "prev") == 0) {
      current_file_index =
          (current_file_index <= 0) ? count - 1 : current_file_index - 1;
    } else {
      current_file_index =
          (current_file_index >= (int)count - 1) ? 0 : current_file_index + 1;
    }

    lv_obj_t *btn = lv_obj_get_child(file_list, current_file_index);
    if (btn) {
      lv_event_send(btn, LV_EVENT_CLICKED, NULL);
      lv_obj_scroll_to_view(btn, LV_ANIM_ON);
    }
  }
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

  // Controls Header
  lv_obj_t *header_cont = lv_obj_create(container);
  lv_obj_set_size(header_cont, LV_PCT(100), 140);
  lv_obj_align(header_cont, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_opa(header_cont, 0, 0);
  lv_obj_set_style_border_width(header_cont, 0, 0);
  lv_obj_set_flex_flow(header_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(header_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  // Buttons Row
  lv_obj_t *btns_row = lv_obj_create(header_cont);
  lv_obj_set_size(btns_row, LV_PCT(100), 70);
  lv_obj_set_style_bg_opa(btns_row, 0, 0);
  lv_obj_set_style_border_width(btns_row, 0, 0);
  lv_obj_set_flex_flow(btns_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btns_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_gap(btns_row, 15, 0);

  const char *symbols[] = {LV_SYMBOL_PREV, LV_SYMBOL_PLAY, LV_SYMBOL_PAUSE,
                           LV_SYMBOL_STOP, LV_SYMBOL_NEXT};
  const char *cmds[] = {"prev", "play", "pause", "stop", "next"};

  for (int i = 0; i < 5; i++) {
    lv_obj_t *b = lv_btn_create(btns_row);
    lv_obj_set_size(b, 70, 60);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x333333), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, symbols[i]);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, playback_control_cb, LV_EVENT_CLICKED,
                        (void *)cmds[i]);
  }

  // Volume Row
  lv_obj_t *vol_row = lv_obj_create(header_cont);
  lv_obj_set_size(vol_row, LV_PCT(100), 50);
  lv_obj_set_style_bg_opa(vol_row, 0, 0);
  lv_obj_set_style_border_width(vol_row, 0, 0);
  lv_obj_set_flex_flow(vol_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(vol_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_gap(vol_row, 10, 0);

  lv_obj_t *vol_icon = lv_label_create(vol_row);
  lv_label_set_text(vol_icon, LV_SYMBOL_VOLUME_MAX);
  lv_obj_set_style_text_color(vol_icon, lv_color_hex(0x00D4FF), 0);

  volume_slider = lv_slider_create(vol_row);
  lv_obj_set_width(volume_slider, 400);
  lv_slider_set_range(volume_slider, 0, 100);
  lv_slider_set_value(volume_slider, 50, LV_ANIM_OFF);
  lv_obj_add_event_cb(volume_slider, volume_event_cb, LV_EVENT_VALUE_CHANGED,
                      NULL);
  lv_obj_set_style_bg_color(volume_slider, lv_color_hex(0x00D4FF),
                            LV_PART_INDICATOR);

  // File List
  file_list = lv_list_create(container);
  lv_obj_set_size(file_list, LV_PCT(100), 530);
  lv_obj_align(file_list, LV_ALIGN_TOP_MID, 0, 150);
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
