#ifndef UI_UPDATES_H
#define UI_UPDATES_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "ecu_data.h"
#include "lvgl.h"

// This function is called periodically by the LVGL task.
// It reads the latest data from the global ECU data struct
// and updates all the gauge widgets on all screens.
void update_all_gauges(void);
void ui_updates_set_demo_mode(bool enabled);

void update_gauge(gauge_id_t id, lv_obj_t *arc, lv_obj_t *label, float value,
                  const char *default_fmt, float warn_thr, float crit_thr,
                  bool invert_logic, lv_color_t normal_color);

#ifdef __cplusplus
}
#endif

#endif // UI_UPDATES_H
