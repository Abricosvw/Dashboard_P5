#ifndef UI_SCREEN12_H
#define UI_SCREEN12_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "../ui.h"

extern lv_obj_t *ui_Screen12;

void ui_Screen12_screen_init(void);
void ui_Screen12_screen_destroy(void);
void ui_Screen12_update(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* UI_SCREEN12_H */
