/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * 米宝一号屏保：黑底 + 150×150 米宝脸（居中）+ 4s 睁眼 / 200ms 闭眼
 *
 * 资源：mibao_look_open_150.bin（睁眼） / mibao_look_close_150.bin（闭眼）
 * 源 PNG：mibao/看你_睁眼.png / mibao/看你_闭眼.png
 * 转换：main/assets/png_to_mibao_bin.py（150×150 ARGB8888）
 *
 * 实现要点：
 * - 不再继承 DvdScreensaver，屏保只是 LVGL 浮动层
 * - 眨眼中断由 lv_timer 驱动，每 4s 切到闭眼图，启 200ms 单次 timer 切回睁眼
 * - update() 是空操作（接口保留，app_launcher.cpp 仍按 30ms 节奏调用）
 */
#include "view.h"
#include <assets/assets.h>
#include <lvgl.h>
#include <cstdint>
#include <memory>

using namespace view;
using namespace uitk;
using namespace uitk::lvgl_cpp;

namespace {

// 资源 dsc 缓存：mibao_look_open_150.bin / mibao_look_close_150.bin
// （150×150 ARGB8888，由 main/assets/png_to_mibao_bin.py 生成）
const lv_image_dsc_t* get_mibao_open_dsc()
{
    static const auto img = assets::get_image("mibao_look_open_150.bin");
    return &img;
}

const lv_image_dsc_t* get_mibao_close_dsc()
{
    static const auto img = assets::get_image("mibao_look_close_150.bin");
    return &img;
}

// 屏保布局常量
constexpr int32_t kScreenW   = 320;
constexpr int32_t kScreenH   = 240;
constexpr int32_t kMibaoSize = 150;
constexpr int32_t kMibaoX    = (kScreenW - kMibaoSize) / 2;  // 85
constexpr int32_t kMibaoY    = (kScreenH - kMibaoSize) / 2;  // 45
constexpr uint32_t kBgColor  = 0x000000;

constexpr uint32_t kBlinkPeriodMs = 4000;  // 睁眼持续时间
constexpr uint32_t kCloseDurationMs = 200; // 闭眼持续时间

}  // namespace

Screensaver::~Screensaver()
{
    if (_blink_timer) {
        lv_timer_delete(_blink_timer);
        _blink_timer = nullptr;
    }
    if (_prev_screen) {
        _prev_screen->load();
    }
}

void Screensaver::show_open()
{
    if (_img) {
        _img->setSrc(get_mibao_open_dsc());
    }
    _is_closed = false;
}

void Screensaver::show_closed()
{
    if (_img) {
        _img->setSrc(get_mibao_close_dsc());
    }
    _is_closed = true;
}

void Screensaver::blink_timer_cb(lv_timer_t* t)
{
    auto* self = static_cast<Screensaver*>(lv_timer_get_user_data(t));
    if (!self) {
        return;
    }
    self->show_closed();

    // 200ms 单次 timer 切回睁眼
    auto* close_timer = lv_timer_create(
        +[](lv_timer_t* ct) {
            auto* s = static_cast<Screensaver*>(lv_timer_get_user_data(ct));
            if (s) {
                s->show_open();
            }
        },
        kCloseDurationMs, self);
    if (close_timer) {
        lv_timer_set_repeat_count(close_timer, 1);
    }
}

void Screensaver::init()
{
    _prev_screen = std::make_unique<ScreenActive>();

    _screen = std::make_unique<Screen>();
    _screen->setBgColor(lv_color_hex(kBgColor));
    _screen->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _screen->setPadding(0, 0, 0, 0);
    _screen->load();

    _img = std::make_unique<Image>(_screen->get());
    _img->setSrc(get_mibao_open_dsc());
    _img->setPos(kMibaoX, kMibaoY);
    _img->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _is_closed = false;

    // 4s 周期的"眨眼中断"。lv_timer_create 默认就是无限循环（repeat_count = -1），
    // 不需要再调用 lv_timer_set_repeat_count。
    _blink_timer = lv_timer_create(blink_timer_cb, kBlinkPeriodMs, this);
}

void Screensaver::update()
{
    // 屏保逻辑由 lv_timer 驱动，update 是空操作。
    // 保留接口是因为 app_launcher.cpp 仍按 30ms 节奏调用。
}
