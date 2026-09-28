/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "view/view.h"
#include <apps/app_setup/workers/workers.h>
#include <mooncake.h>
#include <mooncake_templates.h>
#include <cstdint>
#include <memory>

class AppLauncher : public mooncake::templates::AppLauncherBase {
public:
    AppLauncher() = default;
    ~AppLauncher() override;

    void onLauncherCreate() override;
    void onLauncherOpen() override;
    void onLauncherRunning() override;
    void onLauncherClose() override;
    void onLauncherDestroy() override;

private:
    std::unique_ptr<view::LauncherView> _view;
    std::unique_ptr<view::Screensaver> _screensaver;
    std::unique_ptr<setup_workers::WorkerBase> _startup_worker;
    uint32_t _screensaver_timecount = 0;
    bool _startup_checked           = false;
    bool _config_pulled_            = false;

    void create_launcher_view();
    void screensaver_update();
    // 在独立任务里拉起 Wi-Fi station 连接已保存的网络（不阻塞 LVGL 线程）。
    // 开机时调用；配网界面结束后也必须再调一次（见 onLauncherRunning 注释）。
    void start_wifi_auto_connect();
};
