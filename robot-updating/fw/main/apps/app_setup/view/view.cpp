/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <hal/hal.h>

using namespace view;
using namespace uitk::lvgl_cpp;

namespace {

// 菜单列表滑动防误触：手指按下后移动超过阈值（视为滑动/滚动），
// 刚滑完的一小段时间内不触发菜单项点击，避免滚动时误进子页面。
constexpr int32_t kMenuSwipeThreshold = 16;
constexpr uint32_t kMenuSwipeSuppressMs = 400;

uint32_t g_menu_last_swipe_ms = 0;
bool g_menu_pressing          = false;
lv_point_t g_menu_press_start{0, 0};

}  // namespace

SelectMenuPage::SelectMenuPage(std::vector<MenuSection> sections) : _sections(std::move(sections))
{
    _pannel = std::make_unique<uitk::lvgl_cpp::Container>(lv_screen_active());
    _pannel->setSize(320, 240);
    _pannel->setBgColor(lv_color_hex(0xEDF4FF));
    _pannel->setPadding(30, 72, 0, 0);
    _pannel->setBorderWidth(0);
    _pannel->setRadius(0);
    _pannel->setScrollDir(LV_DIR_VER);
    _pannel->setScrollbarMode(LV_SCROLLBAR_MODE_ACTIVE);

    int cursor_y = 10;

    for (int i = 0; i < _sections.size(); ++i) {
        const auto& section = _sections[i];
        create_selection_label(20, cursor_y, section.title);
        cursor_y += 24 + 12;

        for (int j = 0; j < section.items.size(); ++j) {
            const auto& item = section.items[j];
            create_item_button(cursor_y, item, i, j);
            cursor_y += 48 + 12;
        }
        cursor_y += 8;
    }
    cursor_y += 20;
}

void SelectMenuPage::update()
{
    if (_pending_section_index >= 0 && _pending_item_index >= 0) {
        if (_pending_section_index < _sections.size()) {
            auto& section = _sections[_pending_section_index];
            if (_pending_item_index < section.items.size()) {
                auto& item = section.items[_pending_item_index];
                if (item.onClick) {
                    item.onClick();
                }
            }
        }
        _pending_section_index = -1;
        _pending_item_index    = -1;
    }
}

void SelectMenuPage::create_selection_label(int x, int y, std::string_view text)
{
    auto label = std::make_unique<uitk::lvgl_cpp::Label>(*_pannel);
    label->setText(text);
    label->setTextFont(&lv_font_montserrat_16);
    label->setTextColor(lv_color_hex(0x6A6882));
    label->setPos(x, y);
    _labels.push_back(std::move(label));
}

void SelectMenuPage::create_item_button(int y, const MenuItem& item, int section_idx, int item_idx)
{
    auto btn = std::make_unique<uitk::lvgl_cpp::Button>(*_pannel);
    btn->setSize(282, 48);
    btn->align(LV_ALIGN_TOP_MID, 0, y);
    btn->setBgColor(lv_color_hex(0xB8D3FD));
    btn->setBorderWidth(0);
    btn->setShadowWidth(0);
    btn->setRadius(18);

    btn->label().setText(item.label);
    btn->label().setTextFont(&lv_font_montserrat_24);
    btn->label().setTextColor(lv_color_hex(0x26206A));
    btn->label().align(LV_ALIGN_CENTER, 0, 0);
    btn->label().setWidth(256);
    btn->label().setTextAlign(LV_TEXT_ALIGN_CENTER);
    btn->label().setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

    if (item.onClick) {
        // 滑动防误触：手指移动超过阈值就算滑动，滑动结束后短暂时间内忽略点击
        lv_obj_add_event_cb(btn->get(),
                            [](lv_event_t*) {
                                g_menu_pressing        = true;
                                g_menu_press_start     = lv_point_t{0, 0};
                                if (lv_indev_t* indev = lv_indev_active(); indev != nullptr) {
                                    lv_indev_get_point(indev, &g_menu_press_start);
                                }
                            },
                            LV_EVENT_PRESSED, nullptr);
        lv_obj_add_event_cb(btn->get(),
                            [](lv_event_t*) {
                                if (!g_menu_pressing) {
                                    return;
                                }
                                lv_indev_t* indev = lv_indev_active();
                                if (indev == nullptr) {
                                    return;
                                }
                                lv_point_t p;
                                lv_indev_get_point(indev, &p);
                                if (LV_ABS(p.x - g_menu_press_start.x) > kMenuSwipeThreshold ||
                                    LV_ABS(p.y - g_menu_press_start.y) > kMenuSwipeThreshold) {
                                    g_menu_pressing      = false;
                                    g_menu_last_swipe_ms = GetHAL().millis();
                                }
                            },
                            LV_EVENT_PRESSING, nullptr);
        lv_obj_add_event_cb(btn->get(),
                            [](lv_event_t*) { g_menu_pressing = false; }, LV_EVENT_RELEASED, nullptr);

        btn->onClick().connect([this, section_idx, item_idx]() {
            if (GetHAL().millis() - g_menu_last_swipe_ms < kMenuSwipeSuppressMs) {
                return;  // 滑动刚结束，算误触
            }
            _pending_section_index = section_idx;
            _pending_item_index    = item_idx;
        });
    }
    _buttons.push_back(std::move(btn));
}
