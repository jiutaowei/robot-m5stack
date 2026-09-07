/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_prov_test.h"
#include <apps/common/common.h>
#include <assets/assets.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <smooth_lvgl.hpp>
#include <wifi_manager.h>
#include <ssid_manager.h>
#include <atomic>
#include <cstdint>

using namespace mooncake;

LV_FONT_DECLARE(mibao_zh_font_16);
LV_FONT_DECLARE(lv_font_montserrat_14);

namespace {

constexpr std::uint32_t kAccentColor     = 0x2DBE8D;
constexpr std::uint32_t kAccentDarkColor = 0x155D4A;
constexpr std::uint32_t kBgColor         = 0x101417;
constexpr std::uint32_t kInkColor        = 0x273238;
constexpr std::uint32_t kPanelSoftColor  = 0xF7EFE3;

// 硬编码测试凭证：只用于验证 "纯 STA 能否连上 jinhetech"。
constexpr const char* kTestSsid     = "jinhetech";
constexpr const char* kTestPassword = "Jinhetech1@#";
constexpr std::uint32_t kTimeoutMs  = 25000;

}  // namespace

// 进度标记：prov_task 每步更新，onRunning 据此显示当前进度，
// 崩溃前屏幕会停在最后成功的步骤，便于定位闪退点。
static std::atomic<int> g_step{0};
static std::atomic<bool> g_connected{false};

// 在独立高栈任务中初始化 WiFi + 启动纯 STA 连接。
// 关键：不能在 LVGL 主线程持锁期间调用 esp_wifi 初始化/连接，
// 否则 WiFi 驱动操作会阻塞 LVGL 任务，表现为"点击后闪退"。
// 独立任务配大栈，也避免 WiFi 初始化深调用链导致的栈溢出。
static void prov_task(void* arg)
{
    auto& wifi = WifiManager::GetInstance();
    if (!wifi.IsInitialized()) {
        WifiManagerConfig cfg;
        cfg.ssid_prefix = "Mibao";
        // 缩短扫描间隔，尽快扫到 jinhetech 并发起连接
        cfg.station_scan_min_interval_seconds = 2;
        cfg.station_scan_max_interval_seconds = 10;
        g_step = 1;  // initializing wifi
        mclog::tagInfo("ProvTest", "initializing wifi...");
        bool ok = wifi.Initialize(cfg);
        mclog::tagInfo("ProvTest", "wifi init result={}", ok);
        if (!ok) {
            g_step = 10;  // init failed
            vTaskDelete(NULL);
            return;
        }
    }
    g_step = 2;  // adding ssid
    SsidManager::GetInstance().AddSsid(kTestSsid, kTestPassword);
    mclog::tagInfo("ProvTest", "adding ssid done");

    g_step = 3;  // starting station
    mclog::tagInfo("ProvTest", "starting station, ssid={}", kTestSsid);
    wifi.StartStation();
    g_step = 4;  // station started
    mclog::tagInfo("ProvTest", "station started");
    vTaskDelete(NULL);
}

AppProvisionTest::AppProvisionTest()
{
    setAppInfo().name = "配网测试";
    static auto icon  = assets::get_image("mibao_work_150.bin");
    setAppInfo().icon = (void*)&icon;
    static std::uint32_t theme_color = 0x2DBE8D;
    setAppInfo().userData            = (void*)&theme_color;
}

void AppProvisionTest::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppProvisionTest::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    _started         = false;
    _close_requested = false;
    _start_ms        = GetHAL().millis();
    g_step           = 0;
    g_connected      = false;

    LvglLockGuard lock;
    createUi();
    view::create_home_indicator([this]() { _close_requested = true; }, kAccentColor, kAccentDarkColor);
    view::create_status_bar(kAccentColor, kAccentDarkColor);

    // 在独立任务里初始化 WiFi + 连接，避免阻塞 LVGL 主线程
    if (xTaskCreate(prov_task, "prov_task", 16384, nullptr, 5, nullptr) != pdPASS) {
        mclog::tagError(getAppInfo().name, "failed to create prov_task");
    }
}

void AppProvisionTest::onRunning()
{
    if (_close_requested) {
        _close_requested = false;
        close();
        return;
    }

    LvglLockGuard lock;

    // 初始化由独立 prov_task 完成，这里按 g_step 显示进度 + 轮询连接状态
    auto& wifi = WifiManager::GetInstance();
    if (wifi.IsConnected()) {
        g_connected = true;
        lv_label_set_text(_status_label,
                          fmt::format("CONNECTED!\nSSID: {}\nIP:    {}",
                                      wifi.GetSsid(), wifi.GetIpAddress())
                              .c_str());
    } else if (g_step == 10) {
        lv_label_set_text(_status_label, "WIFI INIT FAILED\n(see serial log)");
    } else if (GetHAL().millis() - _start_ms > kTimeoutMs) {
        lv_label_set_text(_status_label,
                          "FAILED: not connected in 25s\n"
                          "Check jinhetech 2.4G / antenna\n"
                          "Press home to retry later");
    } else {
        // 显示当前进度步骤，定位闪退/卡住点
        const char* text = "Starting...";
        switch (g_step.load()) {
            case 1: text = "Init wifi..."; break;
            case 2: text = "Add ssid..."; break;
            case 3: text = "Start station..."; break;
            case 4: text = "Connecting to jinhetech..."; break;
            default: break;
        }
        lv_label_set_text(_status_label, text);
    }

    view::update_home_indicator();
    view::update_status_bar();
}

void AppProvisionTest::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;
    destroyUi();
    view::destroy_home_indicator();
    view::destroy_status_bar();
}

void AppProvisionTest::createUi()
{
    _root = lv_obj_create(lv_screen_active());
    lv_obj_set_size(_root, 320, 240);
    lv_obj_set_style_bg_color(_root, lv_color_hex(kBgColor), 0);
    lv_obj_set_style_border_width(_root, 0, 0);
    lv_obj_set_style_radius(_root, 0, 0);
    lv_obj_set_style_pad_all(_root, 0, 0);
    lv_obj_center(_root);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

    // 标题条
    lv_obj_t* title_bar = lv_obj_create(_root);
    lv_obj_set_size(title_bar, 320, 28);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(kAccentColor), 0);
    lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_set_style_radius(title_bar, 0, 0);
    lv_obj_set_style_pad_all(title_bar, 4, 0);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(title_bar);
    lv_label_set_text(title, "配网测试");
    lv_obj_set_style_text_font(title, &mibao_zh_font_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // 状态面板
    lv_obj_t* panel = lv_obj_create(_root);
    lv_obj_set_size(panel, 288, 140);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 8);
    lv_obj_set_style_bg_color(panel, lv_color_hex(kPanelSoftColor), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 10, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    _status_label = lv_label_create(panel);
    lv_label_set_text(_status_label, "Starting...");
    lv_obj_set_style_text_font(_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_status_label, lv_color_hex(kInkColor), 0);
    lv_label_set_long_mode(_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_status_label, 264);
    lv_obj_align(_status_label, LV_ALIGN_TOP_LEFT, 0, 0);
}

void AppProvisionTest::destroyUi()
{
    if (_root != nullptr) {
        lv_obj_delete(_root);
        _root         = nullptr;
        _status_label = nullptr;
    }
}