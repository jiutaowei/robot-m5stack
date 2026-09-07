/* SPDX-License-Identifier: MIT */
#ifndef UI_MEETING_SCREEN_H
#define UI_MEETING_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "../joystick/joystick_basic.h"

#define MODE_MEETING (3)

extern lv_obj_t *meeting_screen;
extern lv_obj_t *meeting_status_label;

void create_meeting_screen(void);
void update_meeting_screen(uint8_t command);
void ui_meeting_screen_destroy(void);

#ifdef __cplusplus
}
#endif

#endif
