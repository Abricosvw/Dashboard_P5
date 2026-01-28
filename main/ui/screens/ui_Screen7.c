// ==========================================================================
// Screen 7 - Snake Game
// FULLY OPTIMIZED FOR ESP32-P4 + LVGL 9
// No canvas, no radius (anti-aliasing is too slow), minimal objects
// ==========================================================================
#include "ui_Screen7.h"
#include "../ui.h"
#include "esp_random.h"
#include "ui_screen_manager.h"
#include <esp_log.h>

lv_obj_t *ui_Screen7;
static lv_timer_t *game_timer = NULL;
static lv_obj_t *label_status;
static lv_obj_t *label_score;

// Simple grid - Optimized for Portrait 720x1280
#define GRID_W 18
#define GRID_H 32
#define CELL_SIZE 32
#define GAME_OFFSET_X ((736 - GRID_W * CELL_SIZE) / 2)
#define GAME_OFFSET_Y ((1280 - GRID_H * CELL_SIZE) / 2)

// Snake data
#define MAX_LEN 30
static int snake_x[MAX_LEN];
static int snake_y[MAX_LEN];
static int snake_len = 3;
static int dir_x = 1, dir_y = 0;
static int food_x, food_y;
static bool game_over = false;
static int score = 0;

static lv_obj_t *head_obj = NULL;
static lv_obj_t *food_obj = NULL;
static lv_obj_t *body_objs[MAX_LEN];
static lv_obj_t *game_container = NULL;

static void spawn_food(void) {
  bool valid;
  int attempts = 0;
  do {
    valid = true;
    food_x = esp_random() % GRID_W;
    food_y = esp_random() % GRID_H;
    for (int i = 0; i < snake_len && attempts < 50; i++) {
      if (food_x == snake_x[i] && food_y == snake_y[i]) {
        valid = false;
        break;
      }
    }
    attempts++;
  } while (!valid && attempts < 50);

  if (food_obj) {
    lv_obj_set_pos(food_obj, food_x * CELL_SIZE + 4, food_y * CELL_SIZE + 4);
    lv_obj_clear_flag(food_obj, LV_OBJ_FLAG_HIDDEN);
  }
}

static void update_display(void) {
  if (head_obj) {
    lv_obj_set_pos(head_obj, snake_x[0] * CELL_SIZE + 4,
                   snake_y[0] * CELL_SIZE + 4);
  }

  for (int i = 1; i < snake_len && i < MAX_LEN; i++) {
    if (!body_objs[i]) {
      body_objs[i] = lv_obj_create(game_container);
      lv_obj_set_size(body_objs[i], CELL_SIZE - 8, CELL_SIZE - 8);
      lv_obj_set_style_bg_color(body_objs[i], lv_color_hex(0x00AA00), 0);
      lv_obj_set_style_radius(body_objs[i], 0, 0); // NO RADIUS!
      lv_obj_set_style_border_width(body_objs[i], 0, 0);
      lv_obj_set_style_shadow_width(body_objs[i], 0, 0);
    }
    lv_obj_set_pos(body_objs[i], snake_x[i] * CELL_SIZE + 4,
                   snake_y[i] * CELL_SIZE + 4);
    lv_obj_clear_flag(body_objs[i], LV_OBJ_FLAG_HIDDEN);
  }

  for (int i = snake_len; i < MAX_LEN; i++) {
    if (body_objs[i]) {
      lv_obj_add_flag(body_objs[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static void reset_game(void) {
  snake_len = 3;
  snake_x[0] = GRID_W / 2;
  snake_y[0] = GRID_H / 2;
  snake_x[1] = GRID_W / 2 - 1;
  snake_y[1] = GRID_H / 2;
  snake_x[2] = GRID_W / 2 - 2;
  snake_y[2] = GRID_H / 2;
  dir_x = 1;
  dir_y = 0;
  score = 0;
  game_over = false;

  spawn_food();
  update_display();

  if (label_status)
    lv_label_set_text(label_status, "Tap screen to change direction");
  if (label_score)
    lv_label_set_text(label_score, "Score: 0");
}

static void touch_cb(lv_event_t *e) {
  if (game_over) {
    reset_game();
    return;
  }

  lv_point_t p;
  lv_indev_get_point(lv_indev_get_act(), &p);

  int dx = p.x - 368; // Center X of 736
  int dy = p.y - 640; // Center Y of 1280

  if (abs(dx) > abs(dy)) {
    if (dx > 0 && dir_x == 0) {
      dir_x = 1;
      dir_y = 0;
    } else if (dx < 0 && dir_x == 0) {
      dir_x = -1;
      dir_y = 0;
    }
  } else {
    if (dy > 0 && dir_y == 0) {
      dir_x = 0;
      dir_y = 1;
    } else if (dy < 0 && dir_y == 0) {
      dir_x = 0;
      dir_y = -1;
    }
  }
}

static void game_loop(lv_timer_t *timer) {
  if (lv_scr_act() != ui_Screen7)
    return;
  if (game_over)
    return;

  int new_x = snake_x[0] + dir_x;
  int new_y = snake_y[0] + dir_y;

  if (new_x < 0 || new_x >= GRID_W || new_y < 0 || new_y >= GRID_H) {
    game_over = true;
    lv_label_set_text(label_status, "GAME OVER! Tap to restart");
    return;
  }

  for (int i = 0; i < snake_len; i++) {
    if (new_x == snake_x[i] && new_y == snake_y[i]) {
      game_over = true;
      lv_label_set_text(label_status, "GAME OVER! Tap to restart");
      return;
    }
  }

  for (int i = snake_len; i > 0; i--) {
    snake_x[i] = snake_x[i - 1];
    snake_y[i] = snake_y[i - 1];
  }
  snake_x[0] = new_x;
  snake_y[0] = new_y;

  if (new_x == food_x && new_y == food_y) {
    if (snake_len < MAX_LEN - 1)
      snake_len++;
    score += 10;
    spawn_food();
    char buf[32];
    snprintf(buf, sizeof(buf), "Score: %d", score);
    lv_label_set_text(label_score, buf);
  }

  update_display();
}

void ui_Screen7_update_layout(void) {}

void ui_Screen7_screen_init(void) {
  for (int i = 0; i < MAX_LEN; i++)
    body_objs[i] = NULL;

  ui_Screen7 = lv_obj_create(NULL);
  lv_obj_set_size(ui_Screen7, 736, 1280);
  lv_obj_clear_flag(ui_Screen7, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(ui_Screen7, lv_color_hex(0x001020), 0);
  lv_obj_add_event_cb(ui_Screen7, touch_cb, LV_EVENT_CLICKED, NULL);

  // Game container - NO RADIUS to avoid slow AA calculations
  game_container = lv_obj_create(ui_Screen7);
  lv_obj_set_size(game_container, GRID_W * CELL_SIZE, GRID_H * CELL_SIZE);
  lv_obj_set_pos(game_container, GAME_OFFSET_X, GAME_OFFSET_Y);
  lv_obj_set_style_bg_color(game_container, lv_color_hex(0x000800), 0);
  lv_obj_set_style_border_color(game_container, lv_color_hex(0x00FF44), 0);
  lv_obj_set_style_border_width(game_container, 2, 0);
  lv_obj_set_style_radius(game_container, 0, 0); // NO RADIUS!
  lv_obj_set_style_shadow_width(game_container, 0, 0);
  lv_obj_set_style_pad_all(game_container, 0, 0);
  lv_obj_clear_flag(game_container, LV_OBJ_FLAG_SCROLLABLE);

  // Food - simple square, NO RADIUS
  food_obj = lv_obj_create(game_container);
  lv_obj_set_size(food_obj, CELL_SIZE - 8, CELL_SIZE - 8);
  lv_obj_set_style_bg_color(food_obj, lv_color_hex(0xFF2222), 0);
  lv_obj_set_style_radius(food_obj, 0, 0); // NO RADIUS!
  lv_obj_set_style_border_width(food_obj, 0, 0);
  lv_obj_set_style_shadow_width(food_obj, 0, 0);

  // Snake head - simple square, NO RADIUS
  head_obj = lv_obj_create(game_container);
  lv_obj_set_size(head_obj, CELL_SIZE - 8, CELL_SIZE - 8);
  lv_obj_set_style_bg_color(head_obj, lv_color_hex(0x00FF00), 0);
  lv_obj_set_style_radius(head_obj, 0, 0); // NO RADIUS!
  lv_obj_set_style_border_width(head_obj, 0, 0);
  lv_obj_set_style_shadow_width(head_obj, 0, 0);

  // Labels
  label_status = lv_label_create(ui_Screen7);
  lv_label_set_text(label_status, "Tap to change direction");
  lv_obj_set_style_text_color(label_status, lv_color_hex(0x00FF88), 0);
  lv_obj_set_style_text_font(label_status, &lv_font_montserrat_20, 0);
  lv_obj_align(label_status, LV_ALIGN_TOP_MID, 0, 10);

  label_score = lv_label_create(ui_Screen7);
  lv_label_set_text(label_score, "Score: 0");
  lv_obj_set_style_text_color(label_score, lv_color_white(), 0);
  lv_obj_set_style_text_font(label_score, &lv_font_montserrat_24, 0);
  lv_obj_align(label_score, LV_ALIGN_TOP_RIGHT, -20, 10);

  // Navigation
  ui_create_standard_navigation_buttons(ui_Screen7);

  reset_game();
  game_timer = lv_timer_create(game_loop, 250, NULL); // 4 FPS

  ESP_LOGI("SCREEN7", "Snake game initialized (no radius, optimized)");
}

void ui_Screen7_screen_destroy(void) {
  if (game_timer) {
    lv_timer_del(game_timer);
    game_timer = NULL;
  }
  for (int i = 0; i < MAX_LEN; i++)
    body_objs[i] = NULL;
  head_obj = NULL;
  food_obj = NULL;
  game_container = NULL;
  if (ui_Screen7) {
    lv_obj_del(ui_Screen7);
    ui_Screen7 = NULL;
  }
}
