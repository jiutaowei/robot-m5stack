/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <lvgl.h>
#include <smooth_lvgl.hpp>
#include <functional>
#include <vector>
#include <memory>

namespace view {

/**
 * @brief
 *
 */
class LauncherView {
public:
    ~LauncherView();

    enum State_t {
        STATE_STARTUP,
        STATE_NORMAL,
    };

    std::function<void(int appID)> onAppClicked;

    void init(std::vector<mooncake::AppProps_t> appPorps);
    void update();

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Container>> _icon_panels;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Image>> _icon_images;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Container>> _lr_indicator_panels;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Image>> _lr_indicators_images;

    std::unique_ptr<uitk::AnimateVector2> _startup_anim;

    int _clicked_app_id = -1;
    // 显示列表（去 launcher 后）中每个图标对应的 appID，用于 warm-boot 自动打开
    std::vector<int> _display_app_ids;
    State_t _state      = STATE_STARTUP;

    void handle_state_startup();
    void handle_state_normal();
};

/**
 * @brief
 *
 * 米宝眨眼屏保：
 * - 黑底 + 居中 150×150 米宝脸
 * - 4s 睁眼 + 200ms 闭眼，循环
 * - 屏保逻辑由 lv_timer 驱动，update() 是空操作（接口保留）
 */
class Screensaver {
public:
    ~Screensaver();

    void init();
    void update();

private:
    std::unique_ptr<uitk::lvgl_cpp::ScreenActive> _prev_screen;
    std::unique_ptr<uitk::lvgl_cpp::Screen> _screen;
    std::unique_ptr<uitk::lvgl_cpp::Image> _img;
    lv_timer_t* _blink_timer = nullptr;
    bool _is_closed = false;

    void show_open();
    void show_closed();
    static void blink_timer_cb(lv_timer_t* t);
};

}  // namespace view
