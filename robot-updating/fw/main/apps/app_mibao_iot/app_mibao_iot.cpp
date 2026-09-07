/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_mibao_iot.h"
#include "view/view.h"
#include <mooncake.h>
#include <mooncake_log.h>
#include <hal/hal.h>
#include <apps/common/common.h>
#include <assets/assets.h>

using namespace mooncake;

AppMibaoIot::AppMibaoIot() {
    // 米宝主题色（与 app_meeting 一致）
    setAppInfo().name = "物联网";
    // 阶段 1.3：启用米宝图标（新资源名，不覆盖原生 icon）
    static auto icon = assets::get_image("mibao_iot_150.bin");
    setAppInfo().icon = (void*)&icon;
    static std::uint32_t theme_color = 0x2DBE8D;
    setAppInfo().userData = (void*)&theme_color;
}

void AppMibaoIot::onCreate() {
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppMibaoIot::onOpen() {
    mclog::tagInfo(getAppInfo().name, "on open");

    _close_requested = false;

    LvglLockGuard lock;

    // 构建 view
    MibaoIotView::build([this]() { _close_requested = true; });

    // 状态栏
    view::create_status_bar(0x2DBE8D, 0x0B2530);
}

void AppMibaoIot::onRunning() {
    LvglLockGuard lock;
    view::update_status_bar();

    // 轮询后台 HTTP 任务结果并刷新设备按键/toast（已在 LVGL 锁内）
    MibaoIotView::update();

    if (_close_requested) {
        _close_requested = false;
        close();
    }
}

void AppMibaoIot::onClose() {
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;
    MibaoIotView::destroy();
    view::destroy_status_bar();
}
