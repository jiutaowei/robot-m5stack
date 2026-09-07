/* SPDX-License-Identifier: MIT */
#include "ui_meeting_screen.h"
#include "../lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

lv_obj_t *meeting_screen = NULL;
lv_obj_t *meeting_status_label = NULL;

void create_meeting_screen(void)
{
    while (!lvgl_port_lock()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    meeting_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(meeting_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(meeting_screen);
    lv_label_set_text(title, "Meeting Mode");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);

    lv_obj_t *hint = lv_label_create(meeting_screen);
    lv_label_set_text(hint, "BtnB short: Start/Pause\nBtnB long: End");
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);

    meeting_status_label = lv_label_create(meeting_screen);
    lv_label_set_text(meeting_status_label, "Ready");
    lv_obj_align(meeting_status_label, LV_ALIGN_BOTTOM_MID, 0, -25);
    lv_obj_set_style_text_font(meeting_status_label, &lv_font_montserrat_14, 0);
    lvgl_port_unlock();
}

void update_meeting_screen(uint8_t command)
{
    if (meeting_status_label == NULL || !lv_obj_is_valid(meeting_status_label)) {
        return;
    }
    while (!lvgl_port_lock()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (command == REMOTE_COMMAND_END) {
        lv_label_set_text(meeting_status_label, "End command sent");
    } else if (command == REMOTE_COMMAND_PRIMARY) {
        lv_label_set_text(meeting_status_label, "Start/Pause command sent");
    } else {
        lv_label_set_text(meeting_status_label, "Ready");
    }
    lvgl_port_unlock();
}

void ui_meeting_screen_destroy(void)
{
    meeting_screen = NULL;
    meeting_status_label = NULL;
}
