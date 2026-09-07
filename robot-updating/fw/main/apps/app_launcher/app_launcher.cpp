/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_launcher.h"
#include <hal/hal.h>
#include <hal/mibao_config.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <stackchan/stackchan.h>
#include <ssid_manager.h>
#include <wifi_manager.h>
#include <apps/common/common.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdint>

using namespace mooncake;

// 独立任务：开机后从后台服务器拉取设备配置（upload_url/iot_url/ota_url）。
// 必须放在独立 FreeRTOS 任务里执行，不能在 LVGL 线程里直接做 HTTP，
// 否则会长时间占用主线程导致卡顿/看门狗复位。
static void config_pull_task(void* arg)
{
    mclog::tagInfo("AppLauncher", "pulling device config from server...");
    const bool ok = mibao::pullDeviceConfigFromServer();
    if (ok) {
        mclog::tagInfo("AppLauncher", "device config pulled successfully");
    } else {
        mclog::tagInfo("AppLauncher", "failed to pull device config");
    }
    vTaskDelete(nullptr);
}

void AppLauncher::onLauncherCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");

    // 打开自己
    open();
}

void AppLauncher::onLauncherOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    LvglLockGuard lock;

    if (!_startup_checked && SsidManager::GetInstance().GetSsidList().empty()) {
        // 官方 StackChan-MCP 配网流程移植：开机无已保存 SSID → 自动进入
        // 浏览器 SoftAP 配网模式（手机连热点 → 192.168.4.1 → 选 Wi-Fi）。
        // 配网完成后自动纯 STA 连接，成功后回到 launcher。等价于官方
        // WifiBoard::TryWifiConnect 的 "无 SSID 则 StartConfigMode" 分支。
        mclog::tagInfo(getAppInfo().name, "no saved ssid, auto enter hotspot provisioning");
        _startup_worker = std::make_unique<setup_workers::HotspotSetupWorker>();
        view::create_status_bar(0x2DBE8D, 0x0B2530);
    } else {
        create_launcher_view();

        // 重启后自动连接已保存的 Wi-Fi：mooncake 模式不走 xiaozhi 基类的
        // WifiBoard::TryWifiConnect（它只在 xiaozhi 启动时运行），若不在此主动
        // 拉起 station，设备重启后 WiFi 不会自动连接，导致用户以为"配网丢失"
        // 而被迫重新配网。WifiManager::Initialize 幂等，StartStation 内部有
        // station_active_ 幂等保护，可安全重复调用。
        if (!WifiManager::GetInstance().IsConnected() && !WifiManager::GetInstance().IsConfigMode()) {
            if (xTaskCreate([](void*) {
                                auto& w = WifiManager::GetInstance();
                                if (!w.IsInitialized()) {
                                    WifiManagerConfig cfg;
                                    cfg.ssid_prefix = "Mibao";
                                    if (!w.Initialize(cfg)) {
                                        mclog::tagError("AppLauncher", "wifi manager init failed");
                                    }
                                }
                                if (!SsidManager::GetInstance().GetSsidList().empty()) {
                                    mclog::tagInfo("AppLauncher", "auto-connect saved wifi");
                                    w.StartStation();
                                }
                                vTaskDelete(nullptr);
                            },
                            "wifi_auto_connect", 16384, nullptr, 5, nullptr) != pdPASS) {
                mclog::tagError(getAppInfo().name, "failed to create wifi auto-connect task");
            }
        }

        // 开机后若已有保存的 Wi-Fi 且已配置后台 upload_url 或 ota_url，则在独立高栈任务中
        // 拉取服务器端配置（upload_url/iot_url/ota_url）。无论成败只尝试一次，
        // 成功/失败都只打日志，不阻塞 UI。
        if (!_config_pulled_ && !SsidManager::GetInstance().GetSsidList().empty() &&
            (!mibao::getUploadUrl().empty() || !mibao::getOtaUrl().empty())) {
            _config_pulled_ = true;  // 提前置位，避免重复创建任务
            if (xTaskCreate(config_pull_task, "config_pull", 8192, nullptr, 10, nullptr) != pdPASS) {
                mclog::tagError(getAppInfo().name, "failed to create config pull task");
            }
        }
    }
}

void AppLauncher::onLauncherRunning()
{
    LvglLockGuard lock;

    if (_startup_worker) {
        _startup_worker->update();
        if (_startup_worker->isDone()) {
            _startup_worker.reset();
            _startup_checked = true;
            create_launcher_view();
        }
    } else {
        _view->update();
        screensaver_update();
    }

    GetStackChan().update();
    view::update_status_bar();
}

void AppLauncher::onLauncherClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;

    // 屏保必须在 LVGL 锁内释放：Screensaver 析构会 _prev_screen->load()（lv_screen_load），
    // 若拖到 AppLauncher 析构（uninstallAllApps 无锁）再释放会与 LVGL 任务死锁，
    // 触发 task_wdt（唤醒语音进对话时复现）。
    _screensaver.reset();
    _view.reset();
    view::destroy_status_bar();
}

void AppLauncher::onLauncherDestroy()
{
    mclog::tagInfo(getAppInfo().name, "on destroy");
}

AppLauncher::~AppLauncher()
{
    mclog::tagInfo(getAppInfo().name, "destructor");

    // launcher 的 LVGL 对象（屏保 + 主视图）必须在 LVGL 锁内释放。
    // uninstallAllApps() 直接 reset AbilityManager，Ability 对象析构不经过
    // onLauncherClose()/onLauncherDestroy()；若在此处（无锁）释放 LVGL 对象，
    // lv_obj_delete 会与 LVGL 任务冲突，触发 Guru Meditation
    // （唤醒语音进对话 / MCP open_meeting 重启时复现）。
    LvglLockGuard lock;
    _screensaver.reset();
    _view.reset();
}

void AppLauncher::create_launcher_view()
{
    _view = std::make_unique<view::LauncherView>();
    _view->init(getAppProps());
    // The launcher is the normal home screen, so it owns the always-on system
    // status bar just like the other apps do.
    view::create_status_bar(0x2DBE8D, 0x0B2530);

    // Force the first frame after boot.  On a hardware reset the LCD keeps
    // its previous framebuffer contents, and relying only on the LVGL task's
    // next periodic refresh can leave the display showing its solid boot
    // background even though all page objects have already been created.
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(nullptr);

    _view->onAppClicked = [&](int appID) {
        mclog::tagInfo(getAppInfo().name, "handle open app, app id: {}", appID);
        openApp(appID);
    };
}

void AppLauncher::screensaver_update()
{
    const uint32_t SCREENSAVER_TIMEOUT_MS = 30000;

    uint32_t idle_time = lv_display_get_inactive_time(NULL);
    if (idle_time >= SCREENSAVER_TIMEOUT_MS) {
        if (!_screensaver) {
            _screensaver = std::make_unique<view::Screensaver>();
            _screensaver->init();
        }
    } else if (_screensaver) {
        _screensaver.reset();
    }

    // Update in 30ms interval
    if (_screensaver && GetHAL().millis() - _screensaver_timecount > 30) {
        _screensaver_timecount = GetHAL().millis();
        _screensaver->update();
    }
}
