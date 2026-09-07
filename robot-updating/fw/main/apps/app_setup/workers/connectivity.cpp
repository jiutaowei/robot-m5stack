/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include <src/misc/lv_area.h>
#include <src/misc/lv_text.h>
#include <stackchan/stackchan.h>
#include <ArduinoJson.hpp>
#include <mooncake_log.h>
#include <hal/hal.h>
#include <wifi_manager.h>
#include <ssid_manager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <memory>

using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace setup_workers;
using namespace stackchan;

static std::string _tag = "Setup-Connectivity";

// 任务 20：配网退出标志改用文件作用域静态原子量，事件回调零捕获，
// 即使 NotifyEvent 已拷贝回调后 worker 才析构（注销竞态窗口），
// 回调也仅写静态标志，不会悬空访问 worker 成员。
static std::atomic<bool> s_hotspot_exit_seen{false};

WifiSetupWorker::WifiSetupWorker()
{
    _state       = State::AppDownload;
    _last_state  = State::None;
    _is_first_in = true;

    // Create default avatar
    auto avatar = std::make_unique<avatar::DefaultAvatar>();
    avatar->init(lv_screen_active(), &lv_font_montserrat_24);
    avatar->leftEye().setVisible(false);
    avatar->rightEye().setVisible(false);
    avatar->mouth().setVisible(false);
    GetStackChan().attachAvatar(std::move(avatar));
}

WifiSetupWorker::~WifiSetupWorker()
{
    GetHAL().onAppConfigEvent.disconnect(_app_config_signal_id);
    GetStackChan().resetAvatar();
}

void WifiSetupWorker::update()
{
    cleanup_ui();
    update_state();
}

void WifiSetupWorker::update_state()
{
    switch (_state) {
        case State::AppDownload: {
            if (_is_first_in) {
                _is_first_in = false;

                auto& data = _state_app_download_data;

                data.panel = std::make_unique<Container>(lv_screen_active());
                data.panel->setBgColor(lv_color_hex(0xEDF4FF));
                data.panel->align(LV_ALIGN_CENTER, 0, 0);
                data.panel->setBorderWidth(0);
                data.panel->setSize(320, 240);
                data.panel->setRadius(0);

                data.title = std::make_unique<Label>(lv_screen_active());
                data.title->setTextFont(&lv_font_montserrat_20);
                data.title->setTextColor(lv_color_hex(0x7E7B9C));
                data.title->align(LV_ALIGN_TOP_MID, 0, 0);
                data.title->setText("APP SETUP");

                data.info = std::make_unique<Label>(lv_screen_active());
                data.info->setTextFont(&lv_font_montserrat_14);
                data.info->setTextColor(lv_color_hex(0x26206A));
                data.info->align(LV_ALIGN_TOP_MID, 0, 27);
                data.info->setTextAlign(LV_TEXT_ALIGN_CENTER);
                data.info->setText("Install \"StackChan World\" app\nand login to your M5Stack account");

                std::string qrcode_text = "https://apps.apple.com/us/app/stackchan-world/id6756086326";
                data.qrcode_ios         = std::make_unique<Qrcode>(lv_screen_active());
                data.qrcode_ios->setSize(80);
                data.qrcode_ios->setDarkColor(lv_color_hex(0x221C5B));
                data.qrcode_ios->setLightColor(lv_color_hex(0xEDF4FF));
                data.qrcode_ios->update(qrcode_text);
                data.qrcode_ios->align(LV_ALIGN_CENTER, -65, -12);

                qrcode_text         = "https://play.google.com/store/apps/details?id=com.m5stack.stackchan";
                data.qrcode_android = std::make_unique<Qrcode>(lv_screen_active());
                data.qrcode_android->setSize(80);
                data.qrcode_android->setDarkColor(lv_color_hex(0x221C5B));
                data.qrcode_android->setLightColor(lv_color_hex(0xEDF4FF));
                data.qrcode_android->update(qrcode_text);
                data.qrcode_android->align(LV_ALIGN_CENTER, 65, -12);

                data.label_ios = std::make_unique<Label>(lv_screen_active());
                data.label_ios->setTextFont(&lv_font_montserrat_14);
                data.label_ios->setTextColor(lv_color_hex(0x26206A));
                data.label_ios->align(LV_ALIGN_CENTER, -65, 47);
                data.label_ios->setText("App Store\n(iOS)");
                data.label_ios->setTextAlign(LV_TEXT_ALIGN_CENTER);

                data.label_android = std::make_unique<Label>(lv_screen_active());
                data.label_android->setTextFont(&lv_font_montserrat_14);
                data.label_android->setTextColor(lv_color_hex(0x26206A));
                data.label_android->align(LV_ALIGN_CENTER, 65, 47);
                data.label_android->setText("Play Store\n(Android)");
                data.label_android->setTextAlign(LV_TEXT_ALIGN_CENTER);

                data.btn_next = std::make_unique<Button>(lv_screen_active());
                apply_button_common_style(*data.btn_next);
                data.btn_next->align(LV_ALIGN_CENTER, 72, 91);
                data.btn_next->setSize(112, 42);
                data.btn_next->label().setText("Next");
                data.btn_next->onClick().connect([this]() { _state_app_download_data.next_clicked = true; });

                data.btn_quit = std::make_unique<Button>(lv_screen_active());
                apply_button_common_style(*data.btn_quit);
                data.btn_quit->align(LV_ALIGN_CENTER, -72, 91);
                data.btn_quit->setSize(112, 42);
                data.btn_quit->setBgColor(lv_color_hex(0xD4D9E0));
                data.btn_quit->label().setText("Back");
                data.btn_quit->label().setTextColor(lv_color_hex(0x525064));
                data.btn_quit->onClick().connect([this]() { _state_app_download_data.quit_clicked = true; });
            }

            if (_state_app_download_data.quit_clicked) {
                _is_done = true;
            }

            if (_state_app_download_data.next_clicked) {
                switch_state(State::WaitAppConnection);
            }

            // Check events
            if (_last_app_config_event != AppConfigEvent::None) {
                if (_last_app_config_event == AppConfigEvent::AppConnected) {
                    switch_state(State::AppConnected);
                }
                _last_app_config_event = AppConfigEvent::None;
            }

            break;
        }
        case State::WaitAppConnection: {
            if (_is_first_in) {
                _is_first_in = false;

                // Start app config server
                _app_config_signal_id =
                    GetHAL().onAppConfigEvent.connect([this](AppConfigEvent event) { _last_app_config_event = event; });

                GetHAL().startAppConfigServer();

                auto& data = _state_wait_app_connection_data;

                data.panel = std::make_unique<Container>(lv_screen_active());
                data.panel->setBgColor(lv_color_hex(0xEDF4FF));
                data.panel->align(LV_ALIGN_CENTER, 0, 0);
                data.panel->setBorderWidth(0);
                data.panel->setSize(320, 240);
                data.panel->setRadius(0);

                data.btn_id = std::make_unique<Button>(lv_screen_active());
                apply_button_common_style(*data.btn_id);
                data.btn_id->align(LV_ALIGN_CENTER, 0, -20);
                data.btn_id->setSize(262, 52);
                data.btn_id->onClick().connect([]() {
                    auto& avatar = GetStackChan().avatar();
                    avatar.clearDecorators();
                    avatar.addDecorator(std::make_unique<avatar::HeartDecorator>(lv_screen_active(), 3000));
                });
                data.btn_id->label().setText(fmt::format("ID: {}", GetHAL().getFactoryMacString()));

                data.info = std::make_unique<Label>(lv_screen_active());
                data.info->setTextFont(&lv_font_montserrat_24);
                data.info->setTextColor(lv_color_hex(0x26206A));
                data.info->align(LV_ALIGN_BOTTOM_MID, 0, -26);
                data.info->setTextAlign(LV_TEXT_ALIGN_CENTER);
                data.info->setText("Look for me in the app\nto start setup.");

                auto& avatar = GetStackChan().avatar();
                avatar.clearDecorators();
                avatar.addDecorator(std::make_unique<avatar::HeartDecorator>(lv_screen_active(), 3000));
            }

            // Check events
            if (_last_app_config_event != AppConfigEvent::None) {
                if (_last_app_config_event == AppConfigEvent::AppConnected) {
                    switch_state(State::AppConnected);
                }
                _last_app_config_event = AppConfigEvent::None;
            }

            break;
        }
        case State::AppConnected: {
            if (_is_first_in) {
                _is_first_in = false;

                auto& avatar = GetStackChan().avatar();
                avatar.leftEye().setVisible(true);
                avatar.rightEye().setVisible(true);
                avatar.mouth().setVisible(true);
                avatar.setSpeech("Ready to Configure ~");

                GetStackChan().addModifier(std::make_unique<TimedEmotionModifier>(avatar::Emotion::Happy, 4000));
                GetStackChan().addModifier(std::make_unique<BreathModifier>());
                GetStackChan().addModifier(std::make_unique<BlinkModifier>());
                GetStackChan().addModifier(std::make_unique<SpeakingModifier>(2000, 180, false));
            }

            // Check events
            if (_last_app_config_event != AppConfigEvent::None) {
                if (_last_app_config_event == AppConfigEvent::AppDisconnected) {
                    switch_state(State::WaitAppConnection);
                } else if (_last_app_config_event == AppConfigEvent::TryWifiConnect) {
                    auto& avatar = GetStackChan().avatar();
                    avatar.setSpeech("Verifying...");
                    GetStackChan().addModifier(std::make_unique<SpeakingModifier>(2000, 180, false));
                } else if (_last_app_config_event == AppConfigEvent::WifiConnectFailed) {
                    GetStackChan().addModifier(std::make_unique<TimedEmotionModifier>(avatar::Emotion::Sad, 4000));
                    GetStackChan().addModifier(
                        std::make_unique<TimedSpeechModifier>("Connect Failed. Try again?", 6000));
                    GetStackChan().addModifier(std::make_unique<SpeakingModifier>(3000, 180, false));
                } else if (_last_app_config_event == AppConfigEvent::WifiConnected) {
                    switch_state(State::Done);
                }
                _last_app_config_event = AppConfigEvent::None;
            }

            break;
        }
        case State::Done: {
            if (_is_first_in) {
                _is_first_in = false;

                auto& avatar = GetStackChan().avatar();
                avatar.leftEye().setVisible(true);
                avatar.rightEye().setVisible(true);
                avatar.mouth().setVisible(true);
                avatar.setEmotion(avatar::Emotion::Happy);

                GetStackChan().addModifier(std::make_unique<SpeakingModifier>(1500, 180, false));

                _state_done_data.reboot_count = 4;
            }

            if (GetHAL().millis() - _last_tick > 1000) {
                _last_tick = GetHAL().millis();
                if (_state_done_data.reboot_count > 0) {
                    _state_done_data.reboot_count--;
                    auto& avatar = GetStackChan().avatar();
                    avatar.setSpeech(fmt::format("Done!  Reboot in {}s.", _state_done_data.reboot_count));
                } else {
                    mclog::tagInfo(_tag, "rebooting...");
                    GetHAL().delay(100);
                    GetHAL().reboot();
                }
            }

            break;
        }
        default:
            break;
    }
}

void WifiSetupWorker::cleanup_ui()
{
    if (_last_state == State::None) {
        return;
    }

    switch (_last_state) {
        case State::AppDownload: {
            _state_app_download_data.reset();
            break;
        }
        case State::WaitAppConnection: {
            _state_wait_app_connection_data.reset();
            break;
        }
        case State::AppConnected: {
            GetStackChan().avatar().setSpeech("");
            GetStackChan().clearModifiers();
            break;
        }
        case State::Done: {
            break;
        }
        default:
            break;
    }

    _last_state = State::None;
}

void WifiSetupWorker::switch_state(State newState)
{
    _last_state  = _state;
    _state       = newState;
    _is_first_in = true;
}

// ==================== HotspotSetupWorker（SoftAP 热点配网） ====================

HotspotSetupWorker::HotspotSetupWorker()
{
    mclog::tagInfo(_tag, "hotspot setup worker start");

    // 事件回调（任务 20 修复）：只置原子标志，绝不在回调上下文同步调
    // StartStation。实测 backtrace：/submit → config_exit_task →
    // StopConfigAp（esp_wifi_stop）→ NotifyEvent(ConfigModeExit) →
    // 同步 StartStation → WifiStation::Start() 的 esp_wifi_set_band_mode
    // 在驱动刚 stop 未 start 时返回 ESP_ERR_WIFI_NOT_STARTED(0x3002) →
    // ESP_ERROR_CHECK abort → 提交后重启循环。驱动 stop 完成需时间，
    // 故改由 update() 轮询侧在独立任务异步拉起 station。
    // 回调零捕获（只写文件作用域静态原子标志）：析构注销与 NotifyEvent
    // 拷贝回调之间存在竞态窗口，零捕获保证竞态下也不悬空。
    s_hotspot_exit_seen = false;
    WifiManager::GetInstance().SetEventCallback([](WifiEvent event, const std::string&) {
        if (event == WifiEvent::ConfigModeExit) {
            s_hotspot_exit_seen = true;
        }
    });
}

HotspotSetupWorker::~HotspotSetupWorker()
{
    // 先注销事件回调，避免后续异步任务（hotspot_stop_ap / config_exit_task）
    // 触发回调时访问已析构的 this
    WifiManager::GetInstance().SetEventCallback(nullptr);

    // 关键：必须在独立任务里停 SoftAP，不能在 LVGL 线程调用 esp_wifi 操作。
    // 否则 home_indicator 触发 close() 链 → app_setup::onClose() 析构本 worker
    // 时，StopConfigAp 的 httpd_stop + esp_wifi_stop 会阻塞/并发访问驱动，
    // 出现"闪退"。同步停 AP 也避免下轮启动时残留旧 AP 状态。
    if (WifiManager::GetInstance().IsConfigMode()) {
        mclog::tagInfo(_tag, "hotspot setup: stopping AP on teardown");
        xTaskCreate([](void*) {
            WifiManager::GetInstance().StopConfigAp();
            vTaskDelete(NULL);
        }, "hotspot_stop_ap", 8192, nullptr, 5, nullptr);
    }
    destroy_ui();
}

void HotspotSetupWorker::build_ui()
{
    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setBgColor(lv_color_hex(0xEDF4FF));
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setBorderWidth(0);
    _panel->setSize(320, 240);
    _panel->setRadius(0);

    _label_title = std::make_unique<Label>(lv_screen_active());
    _label_title->setTextFont(&lv_font_montserrat_20);
    _label_title->setTextColor(lv_color_hex(0x7E7B9C));
    _label_title->align(LV_ALIGN_TOP_MID, 0, 4);
    _label_title->setText("Hotspot Setup");

    _label_hint = std::make_unique<Label>(lv_screen_active());
    _label_hint->setTextFont(&lv_font_montserrat_14);
    _label_hint->setTextColor(lv_color_hex(0x26206A));
    _label_hint->align(LV_ALIGN_TOP_MID, 0, 32);
    _label_hint->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_hint->setText("Connect your phone to this Wi-Fi:");

    _label_ssid = std::make_unique<Label>(lv_screen_active());
    _label_ssid->setTextFont(&lv_font_montserrat_20);
    _label_ssid->setTextColor(lv_color_hex(0x26206A));
    _label_ssid->align(LV_ALIGN_TOP_MID, 0, 54);
    _label_ssid->setText("Starting hotspot...");

    _label_url = std::make_unique<Label>(lv_screen_active());
    _label_url->setTextFont(&lv_font_montserrat_14);
    _label_url->setTextColor(lv_color_hex(0x26206A));
    _label_url->align(LV_ALIGN_TOP_MID, 0, 84);
    _label_url->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_url->setText("then open http://192.168.4.1\nin your phone browser");

    _label_status = std::make_unique<Label>(lv_screen_active());
    _label_status->setTextFont(&lv_font_montserrat_14);
    _label_status->setTextColor(lv_color_hex(0x7E7B9C));
    _label_status->align(LV_ALIGN_TOP_MID, 0, 128);
    _label_status->setTextAlign(LV_TEXT_ALIGN_CENTER);
    // 已知坑：APSTA 跨信道首次关联可能失败，提示重试
    _label_status->setText("If it fails, please try again.");

    _btn_back = std::make_unique<Button>(lv_screen_active());
    apply_button_common_style(*_btn_back);
    _btn_back->align(LV_ALIGN_BOTTOM_MID, 0, -14);
    _btn_back->setSize(112, 42);
    _btn_back->setBgColor(lv_color_hex(0xD4D9E0));
    _btn_back->label().setText("Back");
    _btn_back->label().setTextColor(lv_color_hex(0x525064));
    _btn_back->onClick().connect([this]() { _back_clicked = true; });
}

void HotspotSetupWorker::destroy_ui()
{
    _btn_back.reset();
    _label_status.reset();
    _label_url.reset();
    _label_ssid.reset();
    _label_hint.reset();
    _label_title.reset();
    _panel.reset();
}

void HotspotSetupWorker::update()
{
    auto& wifi = WifiManager::GetInstance();

    switch (_state) {
        case State::Start: {
            // 非阻塞进入 SoftAP 配网（自动停 station）；Wi-Fi 连接超时
            // 时框架也会自动进入同一模式，此处为主动入口。
            // 关键：mooncake 阶段 xiaozhi 尚未启动，WifiBoard::StartNetwork
            // 未执行过，WifiManager 处于未初始化态，此时 StartConfigAp 会
            // 直接 return 导致热点永远起不来。Initialize 幂等，可安全补调。
            //
            // 关键修复（2026-08-08）：Initialize + StartConfigAp 移到独立任务，
            // 避免在 LVGL 线程执行 esp_wifi 初始化/起 AP 阻塞 LVGL。
            _ap_start_ms = GetHAL().millis();
            xTaskCreate([](void*) {
                auto& w = WifiManager::GetInstance();
                if (!w.IsInitialized()) {
                    WifiManagerConfig cfg;
                    cfg.ssid_prefix = "Mibao";
                    if (!w.Initialize(cfg)) {
                        mclog::tagError(_tag, "wifi manager init failed");
                    }
                }
                w.StartConfigAp();
                vTaskDelete(NULL);
            }, "hotspot_start_ap", 8192, nullptr, 5, nullptr);
            build_ui();
            _state = State::Waiting;
            break;
        }
        case State::Waiting: {
            if (_back_clicked) {
                // 独立任务停 AP，避免 LVGL 线程执行 esp_wifi 操作崩溃
                xTaskCreate([](void*) {
                    WifiManager::GetInstance().StopConfigAp();
                    vTaskDelete(NULL);
                }, "hotspot_stop_ap", 8192, nullptr, 5, nullptr);
                _is_done = true;
                break;
            }
            // 等 AP 真正就绪：IsConfigMode 只是意图标志，需同时校验 SSID
            // 非空（WifiConfigurationAp::Start 完成后才可取到）。
            if (!_ssid_shown && wifi.IsConfigMode()) {
                auto ssid = wifi.GetApSsid();
                if (!ssid.empty()) {
                    _ssid_shown = true;
                    _label_ssid->setText(ssid);
                    _label_url->setText(fmt::format("then open {}\nin your phone browser",
                                                    wifi.GetApWebUrl()));
                    mclog::tagInfo(_tag, "hotspot ready, ssid: {}", ssid);
                } else if (GetHAL().millis() - _ap_start_ms > 10000U) {
                    // 超时未就绪（初始化失败/驱动异常），提示重试
                    _label_ssid->setText("Failed to start");
                    _label_status->setText("Hotspot not ready.\nPlease go back and retry.");
                    mclog::tagError(_tag, "hotspot not ready after 10s");
                }
            }
            // 手机提交凭证 → /submit 延迟触发退出 → StopConfigAp →
            // ConfigModeExit 事件置 s_hotspot_exit_seen。此处推进 UI 状态；station
            // 由 StartingStation 状态延迟 2s 后在独立任务拉起（驱动
            // esp_wifi_stop 需时间完成，过早 set_band_mode 会 abort，
            // 见构造函数注释的任务 20 修复说明）。
            if (_ssid_shown && (s_hotspot_exit_seen || !wifi.IsConfigMode())) {
                _state           = State::StartingStation;
                _finish_start_ms = GetHAL().millis();
                _label_status->setText("Saved. Starting Wi-Fi...");
            }
            break;
        }
        case State::StartingStation: {
            if (_back_clicked) {
                _is_done = true;
                break;
            }
            // 已配网凭证 SsidManager::AddSsid 持久化在 NVS。
            // 关键（任务 20）：延迟 2s 等 esp_wifi_stop 彻底完成，再在
            // 独立高栈任务拉起 station（不能在回调/LVGL 线程：回调同步调
            // 用会因驱动未重启 ESP_ERR_WIFI_NOT_STARTED abort；LVGL 线程
            // 调 esp_wifi 会阻塞 UI 触发看门狗）。对齐原生 TryWifiConnect：
            // 先判已存凭证再 StartStation。WifiManager::StartStation 内部
            // station_active_ 幂等保护。
            if (GetHAL().millis() - _finish_start_ms > 2000U) {
                if (!wifi.IsConnected() && !wifi.IsConfigMode()) {
                    if (!SsidManager::GetInstance().GetSsidList().empty()) {
                        mclog::tagInfo(_tag, "hotspot setup: starting station after AP teardown");
                        xTaskCreate([](void*) {
                            WifiManager::GetInstance().StartStation();
                            vTaskDelete(NULL);
                        }, "hotspot_start_sta", 16384, nullptr, 5, nullptr);
                    } else {
                        mclog::tagWarn(_tag, "hotspot setup: no saved ssid, skip station");
                    }
                }
                _state           = State::Finishing;
                _finish_start_ms = GetHAL().millis();
            }
            break;
        }
        case State::Finishing: {
            // 返回键必须立即生效，不能等超时（否则用户以为卡死）
            if (_back_clicked) {
                _is_done = true;
                break;
            }
            if (wifi.IsConnected()) {
                _label_status->setText(fmt::format("Connected: {}", wifi.GetSsid()));
                // 短暂展示结果后返回菜单（退出时 app_setup 会 warm-reboot）
                if (GetHAL().millis() - _finish_start_ms > 4000U) {
                    _is_done = true;
                }
            } else if (GetHAL().millis() - _finish_start_ms > 45000U) {
                // 超时窗口与原生对齐（WifiBoard 连接超时 60s）：WifiStation
                // 首次 scan+connect 后最多自动重连 5 次，两轮以上尝试耗时
                // 可超 15s，之前 15s 就判失败会误导用户。
                _label_status->setText("Not connected yet.\nPlease try again.");
            } else if (GetHAL().millis() - _finish_start_ms > 5000U) {
                _label_status->setText("Connecting, please wait...");
            }
            break;
        }
        default:
            break;
    }
}
