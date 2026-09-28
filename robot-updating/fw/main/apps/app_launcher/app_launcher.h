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
    // 自动配网：开机后连不上任何已保存的 Wi-Fi 时，等待一段时间自动打开配网热点
    uint32_t _launch_ms           = 0;
    bool _auto_prov_triggered     = false;

    void create_launcher_view();
    void screensaver_update();
};
