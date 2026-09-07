/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_setup.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <stackchan/stackchan.h>
#include <apps/common/common.h>

using namespace mooncake;
using namespace view;
using namespace setup_workers;

AppSetup::AppSetup()
{
    // 配置 App 名
    setAppInfo().name = "SETUP";
    // 配置 App 图标
    static auto icon  = assets::get_image("icon_setup.bin");
    setAppInfo().icon = (void*)&icon;
    // 配置 App 主题颜色
    static uint32_t theme_color = 0xB3B3B3;
    setAppInfo().userData       = (void*)&theme_color;
}

void AppSetup::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
    // open();
}

void AppSetup::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    // Reset state
    _destroy_menu    = false;
    _need_warm_reset = false;

    _menu_sections = {
        {
            "Wi-Fi",
            {{"Change Wi-Fi",
              [&]() {
                  _destroy_menu    = true;
                  _need_warm_reset = true;
                  _worker          = std::make_unique<WifiSetupWorker>();
              }},
             // 主动进入框架内置 SoftAP 热点配网（手机浏览器配网）
             {"Hotspot Setup",
              [&]() {
                  _destroy_menu    = true;
                  // _need_warm_reset 留 false：worker 析构已同步停 AP，
                  // 配网结果 NVS 落盘；用户按 home 退出时直接回到 launcher，
                  // 不再触发 4s 后 warm-reboot，避免"闪退"感与重复重启。
                  _need_warm_reset = false;
                  _worker          = std::make_unique<HotspotSetupWorker>();
              }}},
        },
        {
            "Device",
            {{"Brightness",
              [&]() {
                  _destroy_menu = true;
                  _worker       = std::make_unique<BrightnessSetupWorker>();
              }},
             {"Volume",
              [&]() {
                  _destroy_menu = true;
                  _worker       = std::make_unique<VolumeSetupWorker>();
              }},
             {"Timezone",
              [&]() {
                  _destroy_menu = true;
                  _worker       = std::make_unique<TimezoneWorker>();
              }}},
        },
        {
            "Hardware Test",
            {{"Microphone",
              [&]() {
                  _destroy_menu = true;
                  _worker       = std::make_unique<MicTestWorker>();
              }},
             {"RGB Strip",
              [&]() {
                  _destroy_menu = true;
                  _worker       = std::make_unique<RgbTestWorker>();
              }}},
        },
        {
            "Account",
            {{"Unbind & Reset",
              [&]() {
                  _destroy_menu    = true;
                  _need_warm_reset = true;
                  _worker          = std::make_unique<AccountWorker>();
              }}},
        },
        {
            "Firmware",
            {
                {fmt::format("Version:  {}", common::FirmwareVersion),
                 [&]() {
                     // 去掉 10 连点彩蛋，直接查看版本详情
                     _destroy_menu = true;
                     _worker       = std::make_unique<FwVersionWorker>();
                 }},
                {"Check for Updates",
                 [&]() {
                     _destroy_menu    = true;
                     _need_warm_reset = true;
                     _worker          = std::make_unique<SystemUpdateWorker>();
                 }},
                //  {"Factory Reset",
                //   [&]() {
                //       _destroy_menu = true;
                //       _worker       = std::make_unique<FactoryResetWorker>();
                //   }}
            },
        },
        {
            "Mibao",
            {{"Server URLs",
              [&]() {
                  _destroy_menu = true;
                  _worker       = std::make_unique<MibaoUrlWorker>();
              }}},
        },
    };

    LvglLockGuard lock;

    _menu_page = std::make_unique<view::SelectMenuPage>(_menu_sections);

    view::create_home_indicator([&]() { close(); });
    view::create_status_bar();
}

void AppSetup::onRunning()
{
    LvglLockGuard lock;

    if (_menu_page) {
        _menu_page->update();
    }

    if (_destroy_menu) {
        _menu_page.reset();
        _destroy_menu = false;
    }

    if (_worker) {
        _worker->update();
        if (_worker->isDone()) {
            _worker.reset();
            _menu_page = std::make_unique<view::SelectMenuPage>(_menu_sections);
        }
    }

    GetStackChan().update();

    view::update_home_indicator();
    view::update_status_bar();
}

void AppSetup::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;

    _menu_sections.clear();
    _menu_page.reset();
    _worker.reset();

    view::destroy_home_indicator();
    view::destroy_status_bar();

    if (_need_warm_reset) {
        // warm-reboot 目标 = launcher 显示列表索引（getAppProps 排除 launcher 自身）：
        // 0=对话 1=会议录音 2=个人灵感 3=物联网 4=设置。回到设置页自身（列表末尾）。
        GetHAL().requestWarmReboot(4);
    }
}
