/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "common.h"
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <hal/hal.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace setup_workers {

/**
 * @brief
 *
 */
class WorkerBase {
public:
    virtual ~WorkerBase() = default;

    virtual void update()
    {
    }

    bool isDone() const
    {
        return _is_done;
    }

protected:
    bool _is_done = false;
};

/**
 * @brief
 *
 */
class ZeroCalibrationWorker : public WorkerBase {
public:
    ZeroCalibrationWorker();
    void update() override;

private:
    std::unique_ptr<WorkerBase> _page_tips;
    std::unique_ptr<WorkerBase> _page_calibration;
};

/**
 * @brief
 *
 */
class ServoTestWorker : public WorkerBase {
public:
    ServoTestWorker();
    void update() override;

private:
    std::unique_ptr<WorkerBase> _page_tips;
    std::unique_ptr<WorkerBase> _page_test;
    std::unique_ptr<WorkerBase> _page_done;
};

/**
 * @brief
 *
 */
class WifiSetupWorker : public WorkerBase {
public:
    WifiSetupWorker();
    ~WifiSetupWorker();
    void update() override;

private:
    enum class State {
        None,
        AppDownload,
        WaitAppConnection,
        AppConnected,
        Done,
    };

    State _state      = State::AppDownload;
    State _last_state = State::None;

    uint32_t _last_tick = 0;
    bool _is_first_in   = false;

    AppConfigEvent _last_app_config_event = AppConfigEvent::None;
    int _app_config_signal_id             = -1;

    struct StateAppDownloadData {
        std::unique_ptr<uitk::lvgl_cpp::Container> panel;
        std::unique_ptr<uitk::lvgl_cpp::Label> title;
        std::unique_ptr<uitk::lvgl_cpp::Qrcode> qrcode_ios;
        std::unique_ptr<uitk::lvgl_cpp::Qrcode> qrcode_android;
        std::unique_ptr<uitk::lvgl_cpp::Label> label_ios;
        std::unique_ptr<uitk::lvgl_cpp::Label> label_android;
        std::unique_ptr<uitk::lvgl_cpp::Button> btn_next;
        std::unique_ptr<uitk::lvgl_cpp::Button> btn_quit;
        std::unique_ptr<uitk::lvgl_cpp::Label> info;
        bool next_clicked = false;
        bool quit_clicked = false;

        void reset()
        {
            panel.reset();
            title.reset();
            qrcode_ios.reset();
            qrcode_android.reset();
            label_ios.reset();
            label_android.reset();
            btn_next.reset();
            btn_quit.reset();
            info.reset();
            next_clicked = false;
            quit_clicked = false;
        }
    };
    StateAppDownloadData _state_app_download_data;

    struct StateWaitAppConnectionData {
        std::unique_ptr<uitk::lvgl_cpp::Container> panel;
        std::unique_ptr<uitk::lvgl_cpp::Button> btn_id;
        std::unique_ptr<uitk::lvgl_cpp::Label> info;

        void reset()
        {
            panel.reset();
            btn_id.reset();
            info.reset();
        }
    };
    StateWaitAppConnectionData _state_wait_app_connection_data;

    struct StateDoneData {
        int reboot_count = 0;
    };
    StateDoneData _state_done_data;

    void update_state();
    void cleanup_ui();
    void switch_state(State newState);
};

/**
 * @brief SoftAP 热点配网 worker
 *
 * 主动进入框架内置的 SoftAP 配网模式（WifiManager::StartConfigAp，
 * CONFIG_USE_HOTSPOT_WIFI_PROVISIONING）：屏幕显示热点 SSID 与
 * http://192.168.4.1 提示，轮询配网完成（AP 自动停止）后等待
 * station 重连并提示结果。已知 APSTA 跨信道首次关联可能失败，
 * UI 文案提示重试。
 */
class HotspotSetupWorker : public WorkerBase {
public:
    HotspotSetupWorker();
    ~HotspotSetupWorker();
    void update() override;

private:
    enum class State {
        Start,            // 触发 StartConfigAp
        Waiting,          // 等待手机配网（显示 SSID / Web URL）
        StartingStation,  // AP 已停，等待 1.5s 后显式 StartStation
        Finishing,        // station 已拉起，等待连接结果
    };

    State _state                   = State::Start;
    bool _ssid_shown               = false;
    bool _back_clicked             = false;
    std::uint32_t _finish_start_ms = 0;
    std::uint32_t _ap_start_ms     = 0;

    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_title;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_hint;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_ssid;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_url;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_status;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_back;

    void build_ui();
    void destroy_ui();
};

/**
 * @brief 屏幕直连 Wi-Fi（无需手机）
 *
 * 在机器人自己的屏幕上：扫描周边 AP → 点选 → 虚拟键盘输密码 → 连接 → 显示结果。
 * 扫描/连接全部在独立 FreeRTOS 任务里做（LVGL 线程只画界面），
 * 凭据写入 SsidManager（NVS "wifi"），重启后由框架自动重连。
 * 「手机配网」按钮回退到 HotspotSetupWorker（SoftAP 配网）。
 */
struct WifiJobCtx;  // 定义在 on_screen_wifi.cpp

class OnScreenWifiWorker : public WorkerBase {
public:
    OnScreenWifiWorker();
    ~OnScreenWifiWorker();
    void update() override;

private:
    enum class Page {
        Scan,        // 扫描列表
        Password,    // 输入密码
        Connecting,  // 连接中
        Result,      // 成功/失败
    };

    Page _page = Page::Scan;

    // 关键：LVGL 事件回调里绝对不能销毁控件。点列表项/按钮时若在回调内直接
    // 切页面，会 delete 正在派发事件的那个对象 → use-after-free → panic 重启。
    // 所以回调只写一个「待办」，真正的切页/起任务都在 update() 里做。
    enum class Pending {
        None,
        ToScan,
        ToPassword,
        ConnectSaved,  // 点的是已保存的网络：直接拿 NVS 里的密码连，不再问用户
        Connect,
        Hotspot,
        Finish,
    };
    Pending _pending = Pending::None;
    std::string _pending_ssid;
    std::string _pending_pwd;

    std::shared_ptr<WifiJobCtx> _job;
    std::unique_ptr<HotspotSetupWorker> _hotspot;  // 「手机配网」回退

    bool _list_built       = false;
    bool _scan_requested   = false;
    bool _connect_started  = false;
    bool _connect_ok       = false;
    bool _was_saved_before = false;
    std::uint32_t _result_at_ms = 0;
    std::uint32_t _last_scroll_ms = 0;
    std::string _sel_ssid;
    std::string _last_pwd;  // 失败重试时用（输入框会随页面销毁）
    // 「已保存的网络不问密码」相关状态：
    bool _using_saved_pwd = false;  // 本次连接用的是 NVS 里保存的密码
    std::string _pwd_hint;          // 密码页的提示语（自动连接失败时说明原因）
    std::string _prefill_pwd;       // 密码页预填上次用过的密码，方便改错字

    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_title;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_status;
    std::unique_ptr<uitk::lvgl_cpp::Container> _list;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Button>> _rows;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_password_for;
    std::unique_ptr<uitk::lvgl_cpp::TextArea> _ta_password;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_primary;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_secondary;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_back;
    lv_obj_t* _keyboard = nullptr;

    void build_scan_page();
    void build_password_page();
    void build_status_page(const char* title, const std::string& detail, bool show_retry);
    void clear_pages();

    void request_scan();
    void request_connect(const std::string& password);
    void start_hotspot_fallback();

    void rebuild_rows();
    void sync_from_job();
};

/**
 * @brief
 *
 */
class RgbTestWorker : public WorkerBase {
public:
    RgbTestWorker();
    ~RgbTestWorker();

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Button>> _buttons;
};

/**
 * @brief
 *
 */
class MicTestWorker : public WorkerBase {
public:
    MicTestWorker();
    ~MicTestWorker();
    void update() override;

private:
    void update_button_text();
    void update_button_state();
    void update_button_color();
    void update_waveform();

    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Chart> _chart_waveform;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_test;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_back;

    MicTestStatus _status        = MicTestStatus::Done;
    bool _test_flag              = false;
    bool _back_flag              = false;
    bool _is_testing             = false;
    int _waveform_series         = -1;
    uint8_t _original_volume     = 80;
    uint32_t _last_waveform_tick = 0;
    std::string _error_message;
    std::vector<int16_t> _waveform_frame;
};

/**
 * @brief
 *
 */
class StartupWorker : public WorkerBase {
public:
    class PageStartup {
    public:
        PageStartup();

        bool isSkipClicked() const
        {
            return _is_skip_clicked;
        }

        bool isStartClicked() const
        {
            return _is_start_clicked;
        }

    private:
        std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
        std::unique_ptr<uitk::lvgl_cpp::Label> _info;
        std::unique_ptr<uitk::lvgl_cpp::Button> _btn_skip;
        std::unique_ptr<uitk::lvgl_cpp::Button> _btn_start;

        bool _is_skip_clicked  = false;
        bool _is_start_clicked = false;
    };

    StartupWorker();
    ~StartupWorker();
    void update() override;

private:
    std::unique_ptr<PageStartup> _page_startup;
    std::unique_ptr<ServoTestWorker> _worker_servo_test;
    std::unique_ptr<WifiSetupWorker> _worker_wifi;
};

/**
 * @brief
 *
 */
class FwVersionWorker : public WorkerBase {
public:
    FwVersionWorker();
    ~FwVersionWorker();
    void update() override;

private:
    uint32_t _last_tick = 0;
};

/**
 * @brief 米宝服务器地址配置（upload_url / iot_url，读写 NVS "mibao"）
 *
 */
class MibaoUrlWorker : public WorkerBase {
public:
    MibaoUrlWorker();
    ~MibaoUrlWorker();
    void update() override;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_title;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_ota_title;
    std::unique_ptr<uitk::lvgl_cpp::TextArea> _ta_ota;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_upload_title;
    std::unique_ptr<uitk::lvgl_cpp::TextArea> _ta_upload;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_iot_title;
    std::unique_ptr<uitk::lvgl_cpp::TextArea> _ta_iot;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_ai_title;
    lv_obj_t* _switch_ai = nullptr;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_cancel;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_save;
    lv_obj_t* _keyboard = nullptr;

    bool _save_flag   = false;
    bool _cancel_flag = false;
};

/**
 * @brief
 *
 */
class SystemUpdateWorker : public WorkerBase {
public:
    SystemUpdateWorker();
    ~SystemUpdateWorker();
    void update() override;
};

/**
 * @brief
 *
 */
class BrightnessSetupWorker : public WorkerBase {
public:
    BrightnessSetupWorker();
    ~BrightnessSetupWorker();
    void update() override;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_brightness;
    std::unique_ptr<uitk::lvgl_cpp::Slider> _slider;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_confirm;
    uint8_t _original_brightness = 0;
    int32_t _target_brightness   = -1;
    bool _confirmed              = false;
};

/**
 * @brief
 *
 */
class VolumeSetupWorker : public WorkerBase {
public:
    VolumeSetupWorker();
    ~VolumeSetupWorker();
    void update() override;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_volume;
    std::unique_ptr<uitk::lvgl_cpp::Slider> _slider;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_confirm;
    std::vector<uint8_t> _volume_levels;
    uint8_t _original_volume = 0;
    int32_t _target_volume   = -1;
    bool _confirmed          = false;
};

/**
 * @brief
 *
 */
class XiaozhiPowerSavingWorker : public WorkerBase {
public:
    XiaozhiPowerSavingWorker();
    void update() override;

private:
    void update_idle_label();

    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel_idle_shutdown;
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel_charging;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_idle_title;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_idle_value;
    std::unique_ptr<uitk::lvgl_cpp::Slider> _slider_idle_shutdown;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_charging_title;
    std::unique_ptr<uitk::lvgl_cpp::Switch> _switch_charging;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_confirm;

    XiaozhiConfig_t _config;
    std::vector<uint32_t> _idle_shutdown_levels;
    int32_t _pending_idle_index = -1;
    bool _confirm_flag          = false;
};

/**
 * @brief
 *
 */
class XiaozhiGeneralWorker : public WorkerBase {
public:
    XiaozhiGeneralWorker();
    void update() override;

private:
    void update_idle_motion_label();

    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel_general;
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel_startup;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_idle_motion_title;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_idle_motion_value;
    std::unique_ptr<uitk::lvgl_cpp::Slider> _slider_idle_motion;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_startup_title;
    std::unique_ptr<uitk::lvgl_cpp::Switch> _switch_start_ai_on_boot;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_confirm;

    XiaozhiConfig_t _config;
    std::vector<uint8_t> _idle_motion_levels;
    int32_t _pending_idle_motion_index = -1;
    bool _confirm_flag                 = false;
};

/**
 * @brief
 *
 */
class TimezoneWorker : public WorkerBase {
public:
    TimezoneWorker();
    ~TimezoneWorker();
    void update() override;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Roller> _roller;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_confirm;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label;
    bool _confirm_flag = false;
};

/**
 * @brief
 *
 */
class FactoryResetWorker : public WorkerBase {
public:
    FactoryResetWorker(std::function<void()> beforeResetAction = {});
    ~FactoryResetWorker();
    void update() override;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_title;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_info;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_cancel;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_confirm;

    int _confirm_count = 0;
    bool _cancel_flag  = false;
    bool _confirm_flag = false;
    std::function<void()> _before_reset_action;

    void update_ui();
};

/**
 * @brief
 *
 */
class AccountWorker : public WorkerBase {
public:
    class PanelInfo {
    public:
        PanelInfo(lv_obj_t* parent, int posY, std::string_view title, std::string_view info);

    private:
        std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
        std::unique_ptr<uitk::lvgl_cpp::Label> _label_title;
        std::unique_ptr<uitk::lvgl_cpp::Label> _label_info;
    };

    class PageAccount {
    public:
        PageAccount(std::string_view username, std::string_view deviceName);

        bool isUnbindClicked() const
        {
            return _is_unbind_clicked;
        }

        bool isQuitClicked() const
        {
            return _is_quit_clicked;
        }

    private:
        std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
        std::unique_ptr<uitk::lvgl_cpp::Label> _label_title;
        std::unique_ptr<PanelInfo> _panel_username;
        std::unique_ptr<PanelInfo> _panel_device_name;
        std::unique_ptr<uitk::lvgl_cpp::Button> _btn_unbind;
        std::unique_ptr<uitk::lvgl_cpp::Button> _btn_quit;

        bool _is_unbind_clicked = false;
        bool _is_quit_clicked   = false;
    };

    AccountWorker();
    ~AccountWorker();
    void update() override;

private:
    std::unique_ptr<PageAccount> _page_account;
    std::unique_ptr<FactoryResetWorker> _worker_reset;
};

}  // namespace setup_workers
