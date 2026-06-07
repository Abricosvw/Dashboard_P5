#ifndef UI_SCREEN9_H
#define UI_SCREEN9_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "../ui.h"

extern lv_obj_t *ui_Screen9;

// Coolant Pump UI elements
extern lv_obj_t *ui_Pump_Speed_Bar;
extern lv_obj_t *ui_Pump_Speed_Label;
extern lv_obj_t *ui_Pump_Mode_Auto_Btn;
extern lv_obj_t *ui_Pump_Mode_Man_Btn;
extern lv_obj_t *ui_Pump_Switch;
extern lv_obj_t *ui_Pump_Slider;
extern lv_obj_t *ui_Pump_Slider_Label;

// Electric Fan UI elements
extern lv_obj_t *ui_Fan_Speed_Bar;
extern lv_obj_t *ui_Fan_Speed_Label;
extern lv_obj_t *ui_Fan_Mode_Auto_Btn;
extern lv_obj_t *ui_Fan_Mode_Man_Btn;
extern lv_obj_t *ui_Fan_Switch;
extern lv_obj_t *ui_Fan_Slider;
extern lv_obj_t *ui_Fan_Slider_Label;

// Header Labels
extern lv_obj_t *ui_Screen9_CLT_Val;
extern lv_obj_t *ui_Screen9_IAT_Val;

// Lifecycle & Update methods
void ui_Screen9_screen_init(void);
void ui_Screen9_screen_destroy(void);
void ui_Screen9_update(void);

// Actual speed getters (for Lua engine / CAN output)
int ui_Screen9_get_actual_pump_speed(void);
int ui_Screen9_get_actual_fan_speed(void);

// Bidirectional Getters & Setters for Screen 9 States & Maps
bool ui_Screen9_get_pump_is_auto(void);
void ui_Screen9_set_pump_is_auto(bool is_auto);
bool ui_Screen9_get_pump_manual_on(void);
void ui_Screen9_set_pump_manual_on(bool manual_on);
int ui_Screen9_get_pump_manual_speed(void);
void ui_Screen9_set_pump_manual_speed(int speed);

bool ui_Screen9_get_fan_is_auto(void);
void ui_Screen9_set_fan_is_auto(bool is_auto);
bool ui_Screen9_get_fan_manual_on(void);
void ui_Screen9_set_fan_manual_on(bool manual_on);
int ui_Screen9_get_fan_manual_speed(void);
void ui_Screen9_set_fan_manual_speed(int speed);

int ui_Screen9_get_pump_map_temp(int idx);
void ui_Screen9_set_pump_map_temp(int idx, int temp);
int ui_Screen9_get_pump_map_speed(int idx);
void ui_Screen9_set_pump_map_speed(int idx, int speed);

int ui_Screen9_get_fan_map_temp(int idx);
void ui_Screen9_set_fan_map_temp(int idx, int temp);
int ui_Screen9_get_fan_map_speed(int idx);
void ui_Screen9_set_fan_map_speed(int idx, int speed);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* UI_SCREEN9_H */
