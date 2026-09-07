/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <mooncake.h>
#include <mooncake_log.h>
#include <esp_log.h>
#include <cstdint>
#include <functional>
#include <smooth_ui_toolkit.hpp>
#include <smooth_lvgl.hpp>
#include <assets/assets.h>
#include <hal/hal.h>
#include "home_indicator.h"
#include <memory>
#include <lvgl.h>

using namespace smooth_ui_toolkit;
using namespace smooth_ui_toolkit::lvgl_cpp;

/**
 * @brief
 *
 */
class HomeGesture {
public:
    std::function<void(void)> onGesture;

    HomeGesture() : _is_tracking(false), _last_state(LV_INDEV_STATE_REL)
    {
    }

    void init()
    {
        _is_tracking      = false;
        _last_state       = LV_INDEV_STATE_REL;
        _screen_height    = 240;
        _bottom_threshold = 20;  // 距离底部 20 像素内触发
        _swipe_min_dist   = 50;  // 向上滑动至少 50 像素才触发
    }

    void update()
    {
        lv_indev_t* indev = GetHAL().lvTouchpad;
        if (!indev) {
            return;
        }

        lv_indev_state_t state = lv_indev_get_state(indev);
        lv_point_t curr_point;
        lv_indev_get_point(indev, &curr_point);

        // 1. 按下瞬间 (Transition: Released -> Pressed)
        if (state == LV_INDEV_STATE_PR && _last_state == LV_INDEV_STATE_REL) {
            // 只有在按下那一刻就在底部，才标记为追踪开始
            if (curr_point.y >= (_screen_height - _bottom_threshold) && curr_point.y >= 0) {
                _start_point = curr_point;
                _is_tracking = true;
            } else {
                _is_tracking = false;  // 按下位置不对，此次滑动全程忽略
            }
        }
        // 1.5 按住期间（滑动进行中）：只要从起点产生了明显位移就立即标记"滑动中"。
        // 这是关键：按钮的 CLICKED 在抬起时派发，而 App 的 onRunning 在
        // update_home_indicator 之前检查 swipe_recently_detected——
        // 若只在抬起瞬间 mark，同帧内 mark 还没发生，抑制不生效，
        // 从底部上滑的手势会误触到开始/历史等底部按钮。
        else if (state == LV_INDEV_STATE_PR && _is_tracking) {
            int delta_y = _start_point.y - curr_point.y;  // 向上滑为正
            int delta_x = abs(curr_point.x - _start_point.x);
            if (delta_y > 20 || delta_x > 20) {
                view::mark_swipe_detected();
            }
        }
        // 2. 抬起瞬间 (Transition: Pressed -> Released)
        else if (state == LV_INDEV_STATE_REL && _last_state == LV_INDEV_STATE_PR) {
            if (_is_tracking) {
                int delta_y = _start_point.y - curr_point.y;  // 向上滑为正
                int delta_x = abs(curr_point.x - _start_point.x);

                // 判断标准：向上位移足够，且角度偏垂直
                if (delta_y > _swipe_min_dist && delta_y > delta_x) {
                    ESP_LOGI("HomeGesture", "up swipe detected, delta_y=%d", delta_y);
                    if (onGesture) {
                        onGesture();
                    }
                }
                // 无论是否达到触发阈值，只要从底部按下的滑动产生了明显位移，
                // 就标记"滑动中"，供 App 丢弃误触的按钮点击。
                if (delta_y > 20 || delta_x > 20) {
                    view::mark_swipe_detected();
                }
                _is_tracking = false;  // 重置追踪状态
            }
        }

        _last_state = state;  // 更新状态机
    }

private:
    bool _is_tracking;
    lv_indev_state_t _last_state;  // 记录上一帧的状态
    lv_point_t _start_point;
    int _screen_height;
    int _bottom_threshold;
    int _swipe_min_dist;
};

/**
 * @brief
 *
 */
class HomeButton {
public:
    HomeButton(lv_obj_t* parent, uint32_t colorButton, uint32_t colorBorder)
    {
        _btn = std::make_unique<Button>(parent);
        _btn->setSize(76, 36);
        _btn->align(LV_ALIGN_BOTTOM_RIGHT, -8, -8);
        _btn->setBgColor(lv_color_hex(colorButton));
        _btn->setBorderWidth(1);
        _btn->setBorderColor(lv_color_hex(colorBorder));
        _btn->setShadowWidth(0);
        _btn->setRadius(12);
        _btn->addFlag(LV_OBJ_FLAG_FLOATING);
        _btn->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _btn->onClick().connect([&]() { _is_clicked = true; });

        _icon = std::make_unique<Image>(_btn->get());
        _icon->align(LV_ALIGN_CENTER, 0, 0);
        _icon_home = assets::get_image("icon_home.bin");
        _icon->setSrc(&_icon_home);
        _icon->setScale(128);
        _icon->setImageRecolorOpa(LV_OPA_COVER);
        _icon->setImageRecolor(lv_color_hex(colorBorder));
    }

    void update()
    {
        _btn->moveForeground();
    }

    void show()
    {
        _btn->moveForeground();
        _btn->setHidden(false);
    }

    void hide()
    {
        _btn->setHidden(true);
    }

    bool isHidden() const
    {
        return lv_obj_has_flag(_btn->get(), LV_OBJ_FLAG_HIDDEN);
    }

    bool isClicked()
    {
        if (_is_clicked) {
            _is_clicked = false;
            return true;
        }
        return false;
    }

private:
    std::unique_ptr<Button> _btn;
    std::unique_ptr<Image> _icon;
    bool _is_clicked     = false;
    lv_image_dsc_t _icon_home;
};

/**
 * @brief
 *
 */
class HomeIndicator {
public:
    std::function<void(void)> onGoHome;

    void init(lv_obj_t* parent, uint32_t colorButton, uint32_t colorBorder)
    {
        _home_button = std::make_unique<HomeButton>(parent, colorButton, colorBorder);
        // 默认隐藏，从底部上滑显示（对齐官方 StackChan 交互）
        _home_button->hide();

        _gesture.onGesture = [this]() {
            if (_home_button->isHidden()) {
                _home_button->show();
            } else {
                _home_button->hide();
            }
        };
        _gesture.init();
    }

    void update()
    {
        _home_button->update();
        _gesture.update();
        check_go_home();
    }

private:
    std::unique_ptr<HomeButton> _home_button;
    HomeGesture _gesture;

    void check_go_home()
    {
        if (_home_button->isClicked()) {
            if (onGoHome) {
                onGoHome();
            }
        }
    }
};

/**
 * @brief 边缘滑动返回手势（类手机全面屏）：
 * - 从屏幕左缘（x<=20）按下并向右滑 -> 右滑返回
 * - 从屏幕右缘（x>=300）按下并向左滑 -> 左滑返回
 *
 * 与 HomeGesture 独立：HomeGesture 只管底部上滑（home），
 * 本类只管左右边缘滑动（back）。用 GetHAL().lvTouchpad 轮询。
 */
class EdgeBackGesture {
public:
    std::function<void(void)> onBack;

    void init()
    {
        _is_tracking      = false;
        _last_state       = LV_INDEV_STATE_REL;
        _edge_threshold   = 20;   // 距左右边缘 20px 内按下才追踪
        _swipe_min_dist   = 45;   // 水平滑动至少 45px 才触发
    }

    void update()
    {
        lv_indev_t* indev = GetHAL().lvTouchpad;
        if (!indev) {
            return;
        }
        lv_indev_state_t state = lv_indev_get_state(indev);
        lv_point_t curr_point;
        lv_indev_get_point(indev, &curr_point);

        // 按下瞬间：仅在左/右边缘按下时开始追踪
        if (state == LV_INDEV_STATE_PR && _last_state == LV_INDEV_STATE_REL) {
            int hor_res = 320;
            lv_display_t* disp = lv_display_get_default();
            if (disp != nullptr) {
                hor_res = lv_display_get_horizontal_resolution(disp);
            }
            _edge_side = 0;  // 0=未知 1=左缘 2=右缘
            if (curr_point.x <= _edge_threshold) {
                _edge_side = 1;  // 左缘
            } else if (curr_point.x >= (hor_res - _edge_threshold)) {
                _edge_side = 2;  // 右缘
            }
            if (_edge_side != 0) {
                _start_point = curr_point;
                _is_tracking = true;
            } else {
                _is_tracking = false;
            }
        }
        // 按住滑动中：明显位移即标记滑动（抑制底部按钮误触）
        else if (state == LV_INDEV_STATE_PR && _is_tracking) {
            int delta_x = abs(curr_point.x - _start_point.x);
            int delta_y = abs(curr_point.y - _start_point.y);
            if (delta_x > 20 || delta_y > 20) {
                view::mark_swipe_detected();
            }
        }
        // 抬起瞬间：判定是否水平滑动触发返回
        else if (state == LV_INDEV_STATE_REL && _last_state == LV_INDEV_STATE_PR) {
            if (_is_tracking) {
                int delta_x = curr_point.x - _start_point.x;  // 右滑为正
                int delta_y = abs(curr_point.y - _start_point.y);
                bool triggered = false;
                if (_edge_side == 1 && delta_x > _swipe_min_dist && delta_x > delta_y) {
                    // 左缘 -> 向右滑
                    triggered = true;
                } else if (_edge_side == 2 && (-delta_x) > _swipe_min_dist && abs(delta_x) > delta_y) {
                    // 右缘 -> 向左滑
                    triggered = true;
                }
                if (delta_x > 20 || delta_y > 20) {
                    view::mark_swipe_detected();
                }
                if (triggered && onBack) {
                    ESP_LOGI("EdgeBackGesture", "edge back swipe detected, side=%d dx=%d", _edge_side, delta_x);
                    onBack();
                }
                _is_tracking = false;
            }
        }

        _last_state = state;
    }

private:
    bool _is_tracking;
    lv_indev_state_t _last_state;
    int _edge_side;
    lv_point_t _start_point;
    int _edge_threshold;
    int _swipe_min_dist;
};

/**
 * @brief
 *
 */
namespace view {

static std::unique_ptr<HomeIndicator> _home_indicator;
static std::unique_ptr<EdgeBackGesture> _edge_back_gesture;

// 滑动抑制状态：最近一次检测到滑动的时间戳（ms）
static volatile std::uint32_t _last_swipe_ms = 0;

void mark_swipe_detected()
{
    _last_swipe_ms = GetHAL().millis();
}

bool swipe_recently_detected(std::uint32_t within_ms)
{
    if (_last_swipe_ms == 0) {
        return false;
    }
    return (GetHAL().millis() - _last_swipe_ms) < within_ms;
}

void set_edge_back_callback(std::function<void(void)> onBack)
{
    if (!_edge_back_gesture) {
        _edge_back_gesture           = std::make_unique<EdgeBackGesture>();
        _edge_back_gesture->init();
    }
    _edge_back_gesture->onBack = std::move(onBack);
}

void clear_edge_back_callback()
{
    if (_edge_back_gesture) {
        _edge_back_gesture->onBack = nullptr;
    }
}

void create_home_indicator(std::function<void(void)> onGoHome, uint32_t colorButton, uint32_t colorBorder,
                           lv_obj_t* parent)
{
    _home_indicator           = std::make_unique<HomeIndicator>();
    _home_indicator->onGoHome = onGoHome;
    _home_indicator->init(parent, colorButton, colorBorder);
    // 边缘滑动返回手势随 home indicator 一起初始化
    if (!_edge_back_gesture) {
        _edge_back_gesture = std::make_unique<EdgeBackGesture>();
        _edge_back_gesture->init();
    }
}

void update_home_indicator()
{
    if (_home_indicator) {
        _home_indicator->update();
    }
    if (_edge_back_gesture) {
        _edge_back_gesture->update();
    }
}

bool is_home_indicator_created()
{
    return _home_indicator != nullptr;
}

void destroy_home_indicator()
{
    _home_indicator.reset();
    _edge_back_gesture.reset();
}

}  // namespace view
