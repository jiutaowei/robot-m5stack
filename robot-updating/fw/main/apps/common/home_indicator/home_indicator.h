/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <lvgl.h>
#include <functional>

namespace view {

void create_home_indicator(std::function<void(void)> onGoHome, uint32_t colorButton = 0xB8D3FD,
                           uint32_t colorBorder = 0x26206A, lv_obj_t* parent = lv_screen_active());
void update_home_indicator();
bool is_home_indicator_created();
void destroy_home_indicator();

// 滑动抑制：顶部下滑/底部上滑手势检测到滑动位移时调用，
// 供各 App 在消费按钮点击标志前丢弃"滑动误触"产生的点击。
void mark_swipe_detected();
bool swipe_recently_detected(std::uint32_t within_ms);

// 边缘滑动返回（类手机全面屏手势）：
// 从屏幕左缘向右滑 或 从右缘向左滑 触发 onBack。
// App 在 onOpen 注册、onClose 清除（clear_edge_back_callback）。
void set_edge_back_callback(std::function<void(void)> onBack);
void clear_edge_back_callback();

}  // namespace view
