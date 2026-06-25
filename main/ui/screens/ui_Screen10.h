#ifndef UI_SCREEN10_H
#define UI_SCREEN10_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "../ui.h"

extern lv_obj_t *ui_Screen10;

// Header live parameters
extern lv_obj_t *ui_Screen10_Boost_Val;
extern lv_obj_t *ui_Screen10_RPM_Val;

// Wastegate VGT elements
extern lv_obj_t *ui_Wg_Actual_Bar;
extern lv_obj_t *ui_Wg_Actual_Label;
extern lv_obj_t *ui_Wg_Mode_Auto_Btn;
extern lv_obj_t *ui_Wg_Mode_Man_Btn;
extern lv_obj_t *ui_Wg_Slider;
extern lv_obj_t *ui_Wg_Slider_Label;

// Blow-off Solenoid elements
extern lv_obj_t *ui_Bov_State_Label;
extern lv_obj_t *ui_Bov_Led;
extern lv_obj_t *ui_Bov_Mode_Auto_Btn;
extern lv_obj_t *ui_Bov_Mode_Man_Btn;
extern lv_obj_t *ui_Bov_Switch;

// Auto Triggers (ME7 LDUVST parameters)
extern lv_obj_t *ui_Bov_Tps_Slider;
extern lv_obj_t *ui_Bov_Tps_Slider_Label;
extern lv_obj_t *ui_Bov_Press_Slider;
extern lv_obj_t *ui_Bov_Press_Slider_Label;
extern lv_obj_t *ui_Bov_Dur_Slider;
extern lv_obj_t *ui_Bov_Dur_Slider_Label;
extern lv_obj_t *ui_Bov_Stat_Switch;
extern lv_obj_t *ui_Bov_Stat_Ratio_Slider;
extern lv_obj_t *ui_Bov_Stat_Ratio_Label;

// Lifecycle & Update methods
void ui_Screen10_screen_init(void);
void ui_Screen10_screen_destroy(void);
void ui_Screen10_update(void);

// Bidirectional Getters & Setters for external engine / Lua bindings
bool ui_Screen10_get_wg_is_auto(void);
void ui_Screen10_set_wg_is_auto(bool is_auto);
int ui_Screen10_get_wg_manual_pos(void);
void ui_Screen10_set_wg_manual_pos(int pos);

int ui_Screen10_get_wg_map_rpm(int idx);
void ui_Screen10_set_wg_map_rpm(int idx, int rpm);
int ui_Screen10_get_wg_map_pos(int idx);
void ui_Screen10_set_wg_map_pos(int idx, int pos);
int ui_Screen10_get_actual_wg_pos(void);

bool ui_Screen10_get_bov_is_auto(void);
void ui_Screen10_set_bov_is_auto(bool is_auto);
bool ui_Screen10_get_bov_manual_open(void);
void ui_Screen10_set_bov_manual_open(bool open);

int ui_Screen10_get_bov_tps_threshold(void);
void ui_Screen10_set_bov_tps_threshold(int val);
int ui_Screen10_get_bov_press_threshold(void);
void ui_Screen10_set_bov_press_threshold(int val);
int ui_Screen10_get_bov_open_duration(void);
void ui_Screen10_set_bov_open_duration(int val_ms);

bool ui_Screen10_get_bov_stat_enabled(void);
void ui_Screen10_set_bov_stat_enabled(bool enabled);
int ui_Screen10_get_bov_stat_ratio(void);
void ui_Screen10_set_bov_stat_ratio(int val_x100);
int ui_Screen10_get_actual_bov_state(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* UI_SCREEN10_H */
