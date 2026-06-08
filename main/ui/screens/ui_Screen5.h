// ECU Dashboard Screen 5 - ECU Data Gauges (Page 2)
#ifndef UI_SCREEN5_H
#define UI_SCREEN5_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

void ui_Screen5_screen_init(void);
void ui_Screen5_screen_destroy(void);
void ui_Screen5_screen_destroy(void);
void ui_Screen5_update_animations(bool demo_enabled);
void ui_Screen5_update_layout(void);
extern lv_obj_t *ui_Screen5;

extern lv_obj_t *ui_Arc_Eng_TQ_Act;
extern lv_obj_t *ui_Label_Eng_TQ_Act_Value;

extern lv_obj_t *ui_Arc_Limit_TQ;
extern lv_obj_t *ui_Label_Limit_TQ_Value;

// New Gauges
extern lv_obj_t *ui_Arc_IAT;
extern lv_obj_t *ui_Label_IAT_Value;
extern lv_obj_t *ui_Arc_Speed;
extern lv_obj_t *ui_Label_Speed_Value;
extern lv_obj_t *ui_Arc_Trans_Temp;
extern lv_obj_t *ui_Label_Trans_Temp_Value;
extern lv_obj_t *ui_Arc_AFR;
extern lv_obj_t *ui_Label_AFR_Value;
extern lv_obj_t *ui_Arc_EGT;
extern lv_obj_t *ui_Label_EGT_Value;
extern lv_obj_t *ui_Arc_Knock_Retard;
extern lv_obj_t *ui_Label_Knock_Retard_Value;
extern lv_obj_t *ui_Arc_Boost_Act;
extern lv_obj_t *ui_Label_Boost_Act_Value;

// Arc visibility control
void ui_Screen5_update_arc_visibility(int arc_index, bool visible);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
