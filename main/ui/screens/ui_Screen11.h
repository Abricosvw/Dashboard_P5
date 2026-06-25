#ifndef UI_SCREEN11_H
#define UI_SCREEN11_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "../ui.h"

extern lv_obj_t *ui_Screen11;

// Lifecycle & Update methods
void ui_Screen11_screen_init(void);
void ui_Screen11_screen_destroy(void);
void ui_Screen11_update(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* UI_SCREEN11_H */
