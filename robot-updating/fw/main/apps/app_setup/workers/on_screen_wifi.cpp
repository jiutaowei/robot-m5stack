/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * 米宝一号「屏幕直连 Wi-Fi」：不依赖手机，在机器人自己的屏幕上扫描 / 选网 /
 * 输密码 / 连接 / 看结果。
 *
 * 设计约束（沿用本项目既有的踩坑纪律）：
 *  - 扫描与连接都在独立 FreeRTOS 任务里做，LVGL 线程只负责画界面；
 *    esp_wifi_scan_start(block=true) 绝不能跑在 LVGL 线程或事件回调里。
 *  - 任务只持有 shared_ptr<WifiJobCtx>，不持有 worker 指针 —— worker 被
 *    提前析构也不会悬垂（析构时置 cancel 并等任务退出，最多 2s）。
 *  - 凭据写入 SsidManager（NVS "wifi"），重启后由框架自动重连；
 *    连接失败且本次是新写入的凭据会被移除，避免设备一直拿错密码重试。
 *  - 「手机配网」按钮回退到 HotspotSetupWorker（SoftAP 配网），
 *    保证无手机以外的兜底路径始终可达。
 */
#include "workers.h"
#include <hal/hal.h>
#include <hal/mibao_config.h>
#include <mooncake_log.h>
#include <ssid_manager.h>
#include <wifi_manager.h>

#include <lvgl.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

LV_FONT_DECLARE(mibao_zh_font);
LV_FONT_DECLARE(mibao_zh_font_16);

using namespace uitk;
using namespace uitk::lvgl_cpp;

namespace setup_workers {

namespace {

const char* _tag = "Setup-WifiOnScreen";

constexpr uint32_t kTextColor = 0x26206A;
constexpr uint32_t kHintColor = 0x7E7B9C;
constexpr uint32_t kRowColorA = 0xEDF4FF;
constexpr uint32_t kRowColorB = 0xDCE8FF;



enum class JobPhase {
    Idle,
    Scanning,
    ScanDone,
    Connecting,
    Connected,
    Failed,
};

struct ApEntry {
    std::string ssid;
    int rssi      = -127;
    bool secured  = false;
};

// 文件级静态：同一时刻只会存在一个 OnScreenWifiWorker，
// 且 LVGL 事件回调必须是无可捕获 lambda，状态放这里最省事。
lv_obj_t* g_keyboard = nullptr;

// 滑动防误触（与 mibao_url.cpp 同款）：按下记起点，移动超阈值算滑动，
// 该次点击不生效。比用容器的 SCROLL 事件更可靠 —— 后者在布局/滚动条
// 变化时也会触发，容易把正常点击误判成滑动而"点了没反应"。
constexpr int32_t kSwipeThreshold = 16;
struct TapGuard {
    bool pressed = false;
    bool moved   = false;
    lv_point_t start{0, 0};
};
TapGuard g_tap;

lv_point_t current_point()
{
    lv_point_t p{0, 0};
    if (lv_indev_t* indev = lv_indev_active(); indev != nullptr) {
        lv_indev_get_point(indev, &p);
    }
    return p;
}

}  // namespace

/**
 * 任务与 UI 之间共享的上下文。任务只碰这个结构体和系统单例。
 */
struct WifiJobCtx {
    std::mutex mtx;
    std::atomic<bool> busy{false};
    std::atomic<bool> cancel{false};
    std::atomic<int> phase{static_cast<int>(JobPhase::Idle)};

    std::vector<ApEntry> aps;
    std::string target_ssid;
    std::string target_pwd;
    std::string ip;
    std::string error;
    bool was_saved_before = false;
};

namespace {

std::string rssi_text(int rssi)
{
    if (rssi <= -127) {
        return "--";
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", rssi);
    return buf;
}

std::string sta_ip_address()
{
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == nullptr) {
        return "";
    }
    esp_netif_ip_info_t info = {};
    if (esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0) {
        return "";
    }
    char buf[16];
    snprintf(buf, sizeof(buf), IPSTR, IP2STR(&info.ip));
    return buf;
}

// ==================== 扫描任务 ====================

void wifi_scan_task(void* arg)
{
    auto* holder = static_cast<std::shared_ptr<WifiJobCtx>*>(arg);
    auto ctx     = *holder;
    delete holder;

    ctx->busy = true;
    auto& wifi = WifiManager::GetInstance();
    std::vector<ApEntry> found;

    WifiManagerConfig cfg;
    cfg.ssid_prefix = "Mibao";
    // 注意：不要改 station_scan_*_interval_seconds —— 它内部按「秒 × 1e6」
    // 存进 int32 微秒，3600s 会溢出成负数（实测日志 "next scan in -694 seconds"）。

    if (!wifi.Initialize(cfg)) {
        ctx->error = "Wi-Fi 初始化失败";
        ctx->phase = static_cast<int>(JobPhase::Failed);
        ctx->busy  = false;
        vTaskDelete(nullptr);
        return;
    }

    // 扫描必须独占驱动：框架 WifiStation 的 SCAN_DONE 处理器会抢先把 scan
    // 结果取走（esp_wifi_scan_get_ap_records 是「谁先取谁得到」），
    // 若与它并发扫描就会拿到 0 个 AP。所以先停掉框架 station（它会注销
    // 自己的事件处理器），扫完再把驱动交回，避免设备停在"离线"状态。
    const bool was_connected = wifi.IsConnected();
    const bool in_config_ap = wifi.IsConfigMode();
    bool we_started_driver  = false;

    if (!in_config_ap) {
        wifi.StopStation();  // 未激活时是 no-op
        esp_wifi_set_mode(WIFI_MODE_STA);
        if (esp_wifi_start() == ESP_OK) {
            we_started_driver = true;
        }
    } else {
        // 配网 AP 是 APSTA，驱动已启动，直接扫（该路径下手机配网页自身的
        // 扫描只在有人打开页面时触发，冲突概率低）
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    for (int attempt = 0; attempt < 5 && !ctx->cancel; attempt++) {
        esp_err_t err = esp_wifi_scan_start(nullptr, true);
        if (err != ESP_OK) {
            mclog::tagWarn(_tag, "scan attempt {} failed: {}", attempt + 1, esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(600));
            continue;
        }

        uint16_t num = 0;
        esp_wifi_scan_get_ap_num(&num);
        if (num == 0) {
            mclog::tagWarn(_tag, "scan attempt {}: 0 ap", attempt + 1);
            vTaskDelay(pdMS_TO_TICKS(600));
            continue;
        }

        std::vector<wifi_ap_record_t> recs(num);
        uint16_t got = num;
        if (esp_wifi_scan_get_ap_records(&got, recs.data()) != ESP_OK) {
            continue;
        }
        for (uint16_t i = 0; i < got; i++) {
            if (recs[i].ssid[0] == '\0') {
                continue;
            }
            ApEntry entry;
            entry.ssid    = reinterpret_cast<const char*>(recs[i].ssid);
            entry.rssi    = recs[i].rssi;
            entry.secured = recs[i].authmode != WIFI_AUTH_OPEN;

            // 同名 AP 只留信号最强的那条
            auto same = std::find_if(found.begin(), found.end(), [&entry](const ApEntry& x) {
                return x.ssid == entry.ssid;
            });
            if (same == found.end()) {
                found.push_back(entry);
            } else if (entry.rssi > same->rssi) {
                *same = entry;
            }
        }
        if (!found.empty()) {
            break;
        }
    }

    // 交回驱动：原本已联网的设备（用户在设置里换网）不能因为扫描而离线
    if (we_started_driver) {
        esp_wifi_stop();
    }
    if (was_connected && !ctx->cancel) {
        wifi.StartStation();
    }

    std::sort(found.begin(), found.end(), [](const ApEntry& a, const ApEntry& b) {
        return a.rssi > b.rssi;
    });

    {
        std::lock_guard<std::mutex> lock(ctx->mtx);
        ctx->aps = std::move(found);
    }
    std::string summary;
    for (const auto& ap : ctx->aps) {
        summary += ap.ssid + "(" + rssi_text(ap.rssi) + ") ";
    }
    mclog::tagInfo(_tag, "scan done, {} ap(s): {}", ctx->aps.size(), summary);
    ctx->phase = static_cast<int>(JobPhase::ScanDone);
    ctx->busy  = false;
    vTaskDelete(nullptr);
}

// ==================== 连接任务 ====================

void wifi_connect_task(void* arg)
{
    auto* holder = static_cast<std::shared_ptr<WifiJobCtx>*>(arg);
    auto ctx     = *holder;
    delete holder;

    ctx->busy = true;

    auto& wifi = WifiManager::GetInstance();
    if (!wifi.IsInitialized()) {
        WifiManagerConfig cfg;
        cfg.ssid_prefix = "Mibao";
        wifi.Initialize(cfg);
    }

    // 1) 凭据落 NVS（重启后框架自动重连靠它）
    SsidManager::GetInstance().AddSsid(ctx->target_ssid, ctx->target_pwd);

    // 2) 走框架既有连接路径：StartStation 会触发一次扫描，SCAN_DONE 处理器
    //    从 SsidManager 匹配到刚写入的凭据就发起连接（这条路径配网页已在用，
    //    实测最稳）。不要再自己 esp_wifi_set_config —— 与框架扫描并发时
    //    会返回 ESP_ERR_WIFI_STATE，两条路径互相打架还会导致 reason:4 掉线。
    if (!wifi.IsConfigMode()) {
        if (!wifi.IsConnected()) {
            wifi.StartStation();  // 已激活时内部直接返回
        }
        vTaskDelay(pdMS_TO_TICKS(300));
        // 手动补一次扫描；若框架那次扫描还在进行会返回 ESP_ERR_WIFI_STATE，
        // 无所谓 —— 它自己的 SCAN_DONE 照样会完成匹配。
        esp_err_t scan_err = esp_wifi_scan_start(nullptr, false);
        if (scan_err != ESP_OK) {
            mclog::tagInfo(_tag, "kick scan skipped ({}), framework scan will match", esp_err_to_name(scan_err));
        }
    } else {
        // 配网 AP 模式（APSTA）下框架 station 未激活，直接连
        wifi_config_t wifi_config = {};
        strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), ctx->target_ssid.c_str(),
                sizeof(wifi_config.sta.ssid) - 1);
        strncpy(reinterpret_cast<char*>(wifi_config.sta.password), ctx->target_pwd.c_str(),
                sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.listen_interval = 10;
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        esp_wifi_connect();
    }

    // 3) 等真正拿到 IP（只"关联成功"不算，DHCP 完成才算连上；最多 24s）
    bool connected = false;
    for (int i = 0; i < 120 && !ctx->cancel; i++) {
        if (!sta_ip_address().empty()) {
            connected = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (ctx->cancel) {
        ctx->busy = false;
        vTaskDelete(nullptr);
        return;
    }

    if (connected) {
        ctx->ip    = sta_ip_address();
        ctx->error.clear();
        ctx->phase = static_cast<int>(JobPhase::Connected);
        const std::string actual = wifi.GetSsid();
        mclog::tagInfo(_tag, "connected to {} ip={} (framework ssid='{}')", ctx->target_ssid, ctx->ip,
                       actual);
        if (!actual.empty() && actual != ctx->target_ssid) {
            // 极少数情况：目标 AP 与另一条已保存的 AP 同时在信号范围内，
            // 框架按扫描顺序挑中了另一条。如实显示，别让用户以为连错了网。
            ctx->error = "（注意：框架连到了 " + actual + "）";
        }

        // 刚配好网的设备 NVS 里没有服务器地址，若不在这里发现一次，
        // 紧接着进入 AI 对话时 xiaozhi 会回落到编译期默认的公网小智云
        // （api.tenclass.net），回答会变成通用助手的英文/泛化内容。
        // 这里用长一点超时并重试，确保把本地服务器地址写进 NVS。
        bool found = false;
        for (int attempt = 0; attempt < 3 && !found && !ctx->cancel; attempt++) {
            found = mibao::refreshServerAddressFromDiscovery(2500);
            if (!found) {
                mclog::tagWarn(_tag, "server discovery attempt {} failed", attempt + 1);
            }
        }
        mclog::tagInfo(_tag, "server discovery done: found={}, ota_url='{}'", found, mibao::getOtaUrl());
    } else {
        ctx->phase = static_cast<int>(JobPhase::Failed);
        ctx->error = "连不上，请检查密码或信号";
        mclog::tagError(_tag, "connect to {} failed", ctx->target_ssid);
        // 别把错密码留在 NVS 里让设备无限重试
        if (!ctx->was_saved_before) {
            auto& list = SsidManager::GetInstance().GetSsidList();
            for (size_t i = 0; i < list.size(); i++) {
                if (list[i].ssid == ctx->target_ssid) {
                    SsidManager::GetInstance().RemoveSsid(static_cast<int>(i));
                    mclog::tagInfo(_tag, "removed failed credential {}", ctx->target_ssid);
                    break;
                }
            }
        }
    }

    ctx->busy = false;
    vTaskDelete(nullptr);
}

}  // namespace

namespace {

/** 这条网络是否已经存过凭据（存过就不再让用户重输密码，直接用）。 */
bool ssid_is_saved(const std::string& ssid) {
    for (const auto& item : SsidManager::GetInstance().GetSsidList()) {
        if (item.ssid == ssid) {
            return true;
        }
    }
    return false;
}

/** 取已保存的密码；没有则返回空串。 */
std::string saved_password_of(const std::string& ssid) {
    for (const auto& item : SsidManager::GetInstance().GetSsidList()) {
        if (item.ssid == ssid) {
            return item.password;
        }
    }
    return std::string();
}

}  // namespace

// ==================== Worker ====================

OnScreenWifiWorker::OnScreenWifiWorker()
{
    mclog::tagInfo(_tag, "create");
    _job = std::make_shared<WifiJobCtx>();

    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setSize(320, 240);
    _panel->setAlign(LV_ALIGN_CENTER);
    _panel->setBgColor(lv_color_hex(0xFFFFFF));
    _panel->setBorderWidth(0);
    _panel->setRadius(0);
    _panel->setPaddingAll(0);
    _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    // 虚拟键盘挂在屏幕上（不随页面销毁），默认隐藏
    g_tap     = TapGuard{};
    g_keyboard = lv_keyboard_create(lv_screen_active());
    lv_obj_set_width(g_keyboard, 320);
    lv_obj_align(g_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(
        g_keyboard,
        [](lv_event_t* e) {
            lv_obj_t* kb = lv_event_get_target_obj(e);
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
            lv_obj_t* ta = lv_keyboard_get_textarea(kb);
            if (ta != nullptr) {
                lv_obj_send_event(ta, LV_EVENT_DEFOCUSED, nullptr);
                lv_obj_remove_state(ta, LV_STATE_FOCUSED);
            }
        },
        LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(
        g_keyboard,
        [](lv_event_t* e) {
            lv_obj_t* kb = lv_event_get_target_obj(e);
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        },
        LV_EVENT_CANCEL, nullptr);

    build_scan_page();
    request_scan();
}

OnScreenWifiWorker::~OnScreenWifiWorker()
{
    mclog::tagInfo(_tag, "destroy");

    if (_job) {
        _job->cancel = true;
        // 等任务退出（任务只在取消点之间做 200~800ms 的短等待）
        for (int i = 0; i < 40 && _job->busy.load(); i++) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    _hotspot.reset();

    g_keyboard = nullptr;
    if (_keyboard != nullptr) {
        lv_obj_del(_keyboard);
        _keyboard = nullptr;
    }

    clear_pages();
    _panel.reset();
}

void OnScreenWifiWorker::clear_pages()
{
    _rows.clear();
    _btn_primary.reset();
    _btn_secondary.reset();
    _btn_back.reset();
    _ta_password.reset();
    _label_password_for.reset();
    _list.reset();
    _label_status.reset();
    _label_title.reset();
}

void OnScreenWifiWorker::build_scan_page()
{
    clear_pages();
    _page = Page::Scan;

    // 顶部 28px 是状态栏（launcher/setup 都会创建），内容从 y=34 起
    _label_title = std::make_unique<Label>(_panel->get());
    _label_title->setText("连接 Wi-Fi");
    _label_title->setTextColor(lv_color_hex(kTextColor));
    _label_title->setTextFont(&mibao_zh_font_16);
    _label_title->align(LV_ALIGN_TOP_LEFT, 12, 34);

    _label_status = std::make_unique<Label>(_panel->get());
    _label_status->setText("正在扫描…");
    _label_status->setTextColor(lv_color_hex(kHintColor));
    _label_status->setTextFont(&mibao_zh_font_16);
    _label_status->align(LV_ALIGN_TOP_RIGHT, -12, 34);

    _list = std::make_unique<Container>(_panel->get());
    _list->setSize(300, 128);
    _list->align(LV_ALIGN_TOP_MID, 0, 56);
    _list->setBgColor(lv_color_hex(0xFFFFFF));
    _list->setBorderWidth(0);
    _list->setRadius(0);
    _list->setPaddingAll(0);
    _list->setScrollDir(LV_DIR_VER);
    _list->setScrollbarMode(LV_SCROLLBAR_MODE_AUTO);

    _btn_back = std::make_unique<Button>(_panel->get());
    _btn_back->setSize(72, 34);
    _btn_back->align(LV_ALIGN_BOTTOM_LEFT, 8, -6);
    apply_button_common_style(*_btn_back);
    _btn_back->label().setTextFont(&mibao_zh_font_16);
    _btn_back->label().setText("返回");
    _btn_back->onClick().connect([this]() { _pending = Pending::Finish; });

    _btn_secondary = std::make_unique<Button>(_panel->get());
    _btn_secondary->setSize(96, 34);
    _btn_secondary->align(LV_ALIGN_BOTTOM_MID, 0, -6);
    apply_button_common_style(*_btn_secondary);
    _btn_secondary->label().setTextFont(&mibao_zh_font_16);
    _btn_secondary->label().setText("手机配网");
    _btn_secondary->onClick().connect([this]() { _pending = Pending::Hotspot; });

    _btn_primary = std::make_unique<Button>(_panel->get());
    _btn_primary->setSize(88, 34);
    _btn_primary->align(LV_ALIGN_BOTTOM_RIGHT, -8, -6);
    apply_button_common_style(*_btn_primary);
    _btn_primary->label().setTextFont(&mibao_zh_font_16);
    _btn_primary->label().setText("重新扫描");
    _btn_primary->onClick().connect([this]() { request_scan(); });

    _list_built = false;
}

void OnScreenWifiWorker::build_password_page()
{
    clear_pages();
    _page = Page::Password;

    _label_title = std::make_unique<Label>(_panel->get());
    _label_title->setText("输入 Wi-Fi 密码");
    _label_title->setTextColor(lv_color_hex(kTextColor));
    _label_title->setTextFont(&mibao_zh_font_16);
    _label_title->align(LV_ALIGN_TOP_MID, 0, 34);

    _label_password_for = std::make_unique<Label>(_panel->get());
    _label_password_for->setText(_sel_ssid.c_str());
    _label_password_for->setTextColor(lv_color_hex(kTextColor));
    _label_password_for->setTextFont(&mibao_zh_font_16);
    _label_password_for->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    _label_password_for->setWidth(296);
    _label_password_for->align(LV_ALIGN_TOP_MID, 0, 56);

    _ta_password = std::make_unique<TextArea>(_panel->get());
    _ta_password->setWidth(288);
    _ta_password->setHeight(34);
    _ta_password->setOneLine(true);
    _ta_password->setBgColor(lv_color_hex(0xF4F4F8));
    _ta_password->setBorderWidth(1);
    _ta_password->setBorderColor(lv_color_hex(0xC8C8D4));
    _ta_password->setRadius(8);
    _ta_password->setTextFont(&lv_font_montserrat_16);
    _ta_password->setPlaceholderText("密码，开放网络留空");
    _ta_password->align(LV_ALIGN_TOP_MID, 0, 78);
    // 自动连接失败退回来时预填上次的密码：桌面设备上能直接看见并改错字
    if (!_prefill_pwd.empty()) {
        lv_textarea_set_text(_ta_password->get(), _prefill_pwd.c_str());
    }
    _prefill_pwd.clear();
    // 密码不遮挡：桌面设备上看得见更不容易输错
    lv_obj_add_event_cb(
        _ta_password->get(),
        [](lv_event_t* e) {
            if (g_keyboard == nullptr) {
                return;
            }
            lv_keyboard_set_textarea(g_keyboard, lv_event_get_target_obj(e));
            lv_obj_clear_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
            mclog::tagInfo(_tag, "password field tapped, keyboard shown");
        },
        LV_EVENT_CLICKED, nullptr);

    _label_status = std::make_unique<Label>(_panel->get());
    // 默认提示；自动连接失败退回来时显示失败原因
    _label_status->setText(_pwd_hint.empty() ? "点输入框弹键盘，键盘 ✓ 收起" : _pwd_hint.c_str());
    _label_status->setTextColor(lv_color_hex(kHintColor));
    _label_status->setTextFont(&mibao_zh_font_16);
    _label_status->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_status->setWidth(292);
    _label_status->align(LV_ALIGN_TOP_MID, 0, 120);
    _pwd_hint.clear();  // 只对这一次进入密码页生效

    _btn_back = std::make_unique<Button>(_panel->get());
    _btn_back->setSize(96, 32);
    _btn_back->align(LV_ALIGN_BOTTOM_LEFT, 16, -8);
    apply_button_common_style(*_btn_back);
    _btn_back->label().setTextFont(&mibao_zh_font_16);
    _btn_back->label().setText("取消");
    _btn_back->onClick().connect([this]() { _pending = Pending::ToScan; });

    _btn_primary = std::make_unique<Button>(_panel->get());
    _btn_primary->setSize(96, 32);
    _btn_primary->align(LV_ALIGN_BOTTOM_RIGHT, -16, -8);
    apply_button_common_style(*_btn_primary);
    _btn_primary->label().setTextFont(&mibao_zh_font_16);
    _btn_primary->label().setText("连接");
    _btn_primary->onClick().connect([this]() {
        // 只读取输入 + 置待办：切页/起连接任务都在 update() 里做
        _pending_pwd = _ta_password ? lv_textarea_get_text(_ta_password->get()) : "";
        _pending     = Pending::Connect;
    });
}

void OnScreenWifiWorker::build_status_page(const char* title, const std::string& detail, bool show_retry)
{
    clear_pages();

    _label_title = std::make_unique<Label>(_panel->get());
    _label_title->setText(title);
    _label_title->setTextColor(lv_color_hex(kTextColor));
    _label_title->setTextFont(&mibao_zh_font_16);
    _label_title->align(LV_ALIGN_TOP_MID, 0, 62);

    _label_status = std::make_unique<Label>(_panel->get());
    _label_status->setText(detail.c_str());
    _label_status->setTextColor(lv_color_hex(kHintColor));
    _label_status->setTextFont(&mibao_zh_font_16);
    _label_status->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_status->setWidth(292);
    _label_status->align(LV_ALIGN_TOP_MID, 0, 98);

    if (show_retry) {
        _btn_secondary = std::make_unique<Button>(_panel->get());
        _btn_secondary->setSize(104, 34);
        _btn_secondary->align(LV_ALIGN_BOTTOM_LEFT, 22, -22);
        apply_button_common_style(*_btn_secondary);
        _btn_secondary->label().setTextFont(&mibao_zh_font_16);
        _btn_secondary->label().setText("重试");
        // 输入框已随页面销毁，用记住的密码重试
        _btn_secondary->onClick().connect([this]() {
            _pending_pwd = _last_pwd;
            _pending     = Pending::Connect;
        });
    }

    _btn_back = std::make_unique<Button>(_panel->get());
    _btn_back->setSize(104, 34);
    if (show_retry) {
        _btn_back->align(LV_ALIGN_BOTTOM_RIGHT, -22, -22);
    } else {
        _btn_back->align(LV_ALIGN_BOTTOM_MID, 0, -22);
    }
    apply_button_common_style(*_btn_back);
    _btn_back->label().setTextFont(&mibao_zh_font_16);
    _btn_back->label().setText("返回");
    _btn_back->onClick().connect([this]() { _pending = Pending::ToScan; });
}

void OnScreenWifiWorker::rebuild_rows()
{
    _rows.clear();
    if (!_list) {
        return;
    }
    lv_obj_clean(_list->get());

    std::vector<ApEntry> aps;
    if (_job) {
        std::lock_guard<std::mutex> lock(_job->mtx);
        aps = _job->aps;
    }

    if (_label_status) {
        if (aps.empty()) {
            _label_status->setText("未找到网络，点「重新扫描」");
        } else {
            _label_status->setText((std::to_string(aps.size()) + " 个网络").c_str());
        }
    }

    int y = 0;
    for (size_t i = 0; i < aps.size(); i++) {
        auto btn = std::make_unique<Button>(_list->get());
        btn->setSize(292, 36);
        btn->align(LV_ALIGN_TOP_MID, 0, y);
        btn->setBgColor(lv_color_hex((i % 2) ? kRowColorB : kRowColorA));
        btn->setBorderWidth(0);
        btn->setShadowWidth(0);
        btn->setRadius(8);
        btn->label().setTextFont(&mibao_zh_font_16);
        btn->label().setTextColor(lv_color_hex(kTextColor));
        btn->label().setText((aps[i].ssid + "   " + rssi_text(aps[i].rssi) + "dBm   " +
                              (aps[i].secured ? "加密" : "开放"))
                                 .c_str());

        const std::string ssid = aps[i].ssid;
        const int rssi         = aps[i].rssi;
        btn->onClick().connect([this, ssid, rssi]() {
            if (g_tap.moved) {
                g_tap.moved = false;
                mclog::tagInfo(_tag, "row click suppressed (was a swipe): {}", ssid);
                return;  // 滑动列表后的误触
            }
            mclog::tagInfo(_tag, "row clicked: {} ({}dBm)", ssid, rssi);
            // 不能在回调里切页（会删掉正在派发事件的这个按钮）
            _pending_ssid = ssid;
            // 已保存过的网络直接用 NVS 里的密码连，不再每次都让用户重输；
            // 只有连不上时才退回密码页让他重输（见 sync_from_job 的 Failed 分支）。
            _pending = ssid_is_saved(ssid) ? Pending::ConnectSaved : Pending::ToPassword;
        });
        // 按钮级滑动判定
        lv_obj_add_event_cb(
            btn->get(),
            [](lv_event_t*) {
                g_tap.pressed = true;
                g_tap.moved   = false;
                g_tap.start   = current_point();
            },
            LV_EVENT_PRESSED, nullptr);
        lv_obj_add_event_cb(
            btn->get(),
            [](lv_event_t*) {
                if (!g_tap.pressed || g_tap.moved) {
                    return;
                }
                const lv_point_t p = current_point();
                if (LV_ABS(p.x - g_tap.start.x) > kSwipeThreshold ||
                    LV_ABS(p.y - g_tap.start.y) > kSwipeThreshold) {
                    g_tap.moved = true;
                }
            },
            LV_EVENT_PRESSING, nullptr);
        lv_obj_add_event_cb(
            btn->get(),
            [](lv_event_t*) { g_tap.pressed = false; },
            LV_EVENT_RELEASED, nullptr);

        _rows.push_back(std::move(btn));
        y += 40;
    }
}

void OnScreenWifiWorker::request_scan()
{
    if (!_job) {
        return;
    }
    if (_job->busy.load()) {
        mclog::tagWarn(_tag, "scan already running, ignore rescan");
        return;
    }
    if (_label_status) {
        _label_status->setText("正在扫描…");
    }
    mclog::tagInfo(_tag, "scan requested");
    _list_built       = false;
    _connect_started  = false;
    _job->cancel      = false;
    _job->error.clear();
    _job->phase       = static_cast<int>(JobPhase::Scanning);

    auto* holder = new std::shared_ptr<WifiJobCtx>(_job);
    if (xTaskCreate(wifi_scan_task, "wifi_scan", 8192, holder, 5, nullptr) != pdPASS) {
        delete holder;
        if (_label_status) {
            _label_status->setText("任务创建失败");
        }
        mclog::tagError(_tag, "failed to create scan task");
    }
}

void OnScreenWifiWorker::request_connect(const std::string& password)
{
    if (!_job || _sel_ssid.empty()) {
        return;
    }

    _job->target_ssid = _sel_ssid;
    _job->target_pwd  = password;
    _last_pwd         = password;
    _job->error.clear();

    // 记录这条凭据是否本来就存在（失败时只清理本次新写入的）
    _was_saved_before = false;
    for (const auto& item : SsidManager::GetInstance().GetSsidList()) {
        if (item.ssid == _sel_ssid) {
            _was_saved_before = true;
            break;
        }
    }
    _job->was_saved_before = _was_saved_before;
    _job->cancel           = false;
    _job->phase            = static_cast<int>(JobPhase::Connecting);

    _connect_started = true;
    _connect_ok      = false;
    _page            = Page::Connecting;
    build_status_page("正在连接…",
                      _sel_ssid + (_using_saved_pwd ? "（已保存密码）" : ""),
                      false);

    if (g_keyboard != nullptr) {
        lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
    }

    auto* holder = new std::shared_ptr<WifiJobCtx>(_job);
    if (xTaskCreate(wifi_connect_task, "wifi_connect", 8192, holder, 5, nullptr) != pdPASS) {
        delete holder;
        build_status_page("连接失败", "任务创建失败", true);
        _page = Page::Result;
        mclog::tagError(_tag, "failed to create connect task");
    }
}

void OnScreenWifiWorker::start_hotspot_fallback()
{
    mclog::tagInfo(_tag, "fallback to hotspot setup");
    if (_job) {
        _job->cancel = true;
        for (int i = 0; i < 40 && _job->busy.load(); i++) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    clear_pages();
    if (_panel) {
        _panel->addFlag(LV_OBJ_FLAG_HIDDEN);
    }
    _hotspot = std::make_unique<HotspotSetupWorker>();
}

void OnScreenWifiWorker::sync_from_job()
{
    if (!_job) {
        return;
    }
    const auto phase = static_cast<JobPhase>(_job->phase.load());

    if (_page == Page::Scan) {
        if (phase == JobPhase::ScanDone && !_list_built) {
            _list_built = true;
            rebuild_rows();
        } else if (phase == JobPhase::Failed) {
            _page = Page::Result;
            build_status_page("Wi-Fi 初始化失败", _job->error.empty() ? "请重启设备" : _job->error, true);
        }
    } else if (_page == Page::Connecting) {
        if (phase == JobPhase::Connected) {
            _connect_ok   = true;
            _page         = Page::Result;
            _result_at_ms = GetHAL().millis();
            std::string detail = _sel_ssid;
            if (!_job->ip.empty()) {
                detail += "  " + _job->ip;
            }
            if (!_job->error.empty()) {
                detail += "\n" + _job->error;  // 例如"框架连到了另一条已保存的网"
            }
            build_status_page("连接成功", detail, false);
        } else if (phase == JobPhase::Failed) {
            _connect_ok   = false;
            _page         = Page::Result;
            _result_at_ms = GetHAL().millis();
            if (_using_saved_pwd) {
                // 用保存的密码没连上（多半是密码变了 / 换过密码）：
                // 直接回到密码页让他重输，而不是只丢一句「连接失败」。
                _prefill_pwd = _last_pwd;
                _pwd_hint    = "连不上，请重新输入密码";
                _page        = Page::Password;
                _result_at_ms = 0;
                mclog::tagInfo(_tag, "auto connect with saved password failed, ask user to retype");
                build_password_page();
            } else {
                build_status_page("连接失败", _sel_ssid + "\n" + _job->error, true);
            }
        }
    }
}

void OnScreenWifiWorker::update()
{
    // 回退到手机配网后，本 worker 全权代理它
    if (_hotspot) {
        _hotspot->update();
        if (_hotspot->isDone()) {
            _hotspot.reset();
            _is_done = true;
        }
        return;
    }

    // 处理事件回调里挂下的待办（切页 / 起任务 / 结束），
    // 保证任何控件销毁都发生在 LVGL 事件派发之外。
    if (_pending != Pending::None) {
        const Pending action = _pending;
        _pending             = Pending::None;

        switch (action) {
            case Pending::ToScan:
                mclog::tagInfo(_tag, "ui -> scan page");
                if (g_keyboard != nullptr) {
                    lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
                }
                build_scan_page();
                _list_built = true;  // 列表数据还在，直接重建行
                rebuild_rows();
                break;
            case Pending::ToPassword:
                mclog::tagInfo(_tag, "ui -> password page for {}", _pending_ssid);
                _sel_ssid        = _pending_ssid;
                _using_saved_pwd = false;  // 用户手输，失败时走原有「连接失败」页
                _pwd_hint.clear();
                _prefill_pwd.clear();
                build_password_page();
                break;
            case Pending::ConnectSaved:
                mclog::tagInfo(_tag, "ui -> auto connect saved network {}", _pending_ssid);
                _sel_ssid        = _pending_ssid;
                _using_saved_pwd = true;
                request_connect(saved_password_of(_sel_ssid));
                break;
            case Pending::Connect:
                mclog::tagInfo(_tag, "ui -> connect {} (pwd len {})", _sel_ssid, _pending_pwd.size());
                _using_saved_pwd = false;  // 用户手输的密码，别标成「已保存密码」
                request_connect(_pending_pwd);
                break;
            case Pending::Hotspot:
                mclog::tagInfo(_tag, "ui -> phone hotspot fallback");
                start_hotspot_fallback();
                break;
            case Pending::Finish:
                mclog::tagInfo(_tag, "ui -> finish (back)");
                _is_done = true;
                break;
            case Pending::None:
                break;
        }
        return;
    }

    sync_from_job();

    // 连接成功后自动收尾，回到 launcher（有附加提示时多留一会儿给人看）。
    // 必须等连接任务真正结束（里面还要做服务器地址发现），否则 launcher 会
    // 过早进 xiaozhi，可能落到公网默认服务器。
    if (_page == Page::Result && _connect_ok && _result_at_ms != 0) {
        const uint32_t hold_ms = (_job && !_job->error.empty()) ? 3500 : 1500;
        const bool task_done   = (!_job || !_job->busy.load());
        if (task_done && GetHAL().millis() - _result_at_ms > hold_ms) {
            _is_done = true;
        }
    }
}

}  // namespace setup_workers
