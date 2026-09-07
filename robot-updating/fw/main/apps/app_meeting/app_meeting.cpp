/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

#include "app_meeting.h"

#include <apps/common/common.h>
#include <assets/assets.h>
#include <board.h>
#include <hal/board/hal_bridge.h>
#include <hal/hal.h>
#include <hal/mibao_config.h>
#include <hal/mibao_voice_confirm.h>
#include <hal/mibao_wake_word.h>
#include <audio/audio_codec.h>
#include <mooncake_log.h>
#include <settings.h>

#include <cJSON.h>

#include "wav_writer.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <dirent.h>
#include <vector>

#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

using namespace mooncake;

// Generated from Puhui with precisely the meeting UI's characters:
// LVGL format, 4bpp and --no-compress.  Unlike the generic CJK subset, it
// includes every Chinese character displayed by this app.
LV_FONT_DECLARE(meeting_zh_font);
// 历史/删除等新增文字不在 meeting 精简字体里，复用米宝全量 16px 字体
LV_FONT_DECLARE(mibao_zh_font_16);

namespace {

constexpr std::uint32_t kAccentColor = 0x2DBE8D;
constexpr std::uint32_t kAccentDarkColor = 0x155D4A;
constexpr std::uint32_t kBgColor = 0x101417;
constexpr std::uint32_t kPanelSoftColor = 0xF7EFE3;
constexpr std::uint32_t kInkColor = 0x273238;
constexpr std::uint32_t kDangerColor = 0xE64B4B;

class SdCardAccessGuard {
public:
    SdCardAccessGuard() { hal_bridge::board_begin_sdcard_access(); }
    ~SdCardAccessGuard() { hal_bridge::board_end_sdcard_access(); }
};

void set_label(lv_obj_t* label, const std::string& text) {
    if (label != nullptr) {
        lv_label_set_text(label, text.c_str());
    }
}

lv_obj_t* create_label(lv_obj_t* parent, const char* text, const lv_font_t* font,
                       std::uint32_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return label;
}

lv_obj_t* create_button(lv_obj_t* parent, const char* text, std::uint32_t color,
                        const lv_font_t* font) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_size(button, 76, 36);
    lv_obj_set_style_radius(button, 12, 0);  // 与文件管理按钮圆角一致
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(label);
    return button;
}

bool ensure_dir(const char* path) {
    struct stat info {};
    if (stat(path, &info) == 0) {
        return (info.st_mode & S_IFDIR) != 0;
    }
    return mkdir(path, 0775) == 0 || errno == EEXIST;
}

// ==================== 录音后台上传（HTTP multipart，chunked 流式） ====================
//
// 框架的 Http 接口 SetContent 只能整体带 body；不带 content 的 POST 会自动
// 走 Transfer-Encoding: chunked，因此用 Write() 从 SD 流式分块发送，避免
// 整读 9.6MB 大文件占用内存。SD 读取通过 SdCardAccessGuard 与 LVGL 串行。

constexpr std::size_t kUploadChunkSize = 4096;
constexpr int kUploadTimeoutMs = 60000;

std::string uploadDeviceId() {
    std::uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char id[32] = {0};
    std::snprintf(id, sizeof(id), "mibao-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2],
                  mac[3], mac[4], mac[5]);
    return id;
}

/**
 * @brief 从服务器响应 JSON 中提取纪要（notes 字段），写入录音同目录 .txt。
 *
 * 响应格式：{"success":true,"type":"meeting","device_id":"...",
 *            "transcript":"...","notes":"..."}
 * 提取失败（无 notes / JSON 解析失败 / 写卡失败）只记日志，不影响上传结果。
 */
void saveMeetingNotesFromResponse(const std::string& recording_path,
                                  const std::string& response_body) {
    try {
        cJSON* root = cJSON_Parse(response_body.c_str());
        if (root == nullptr) {
            mclog::tagWarn("MeetingUpload", "notes: response is not valid JSON");
            return;
        }
        cJSON* notes = cJSON_GetObjectItemCaseSensitive(root, "notes");
        if (notes == nullptr || !cJSON_IsString(notes) || notes->valuestring == nullptr ||
            strlen(notes->valuestring) == 0) {
            cJSON_Delete(root);
            mclog::tagWarn("MeetingUpload", "notes: no notes field in response");
            return;
        }
        std::string notes_text = notes->valuestring;
        cJSON_Delete(root);

        // 录音路径 meeting_YYYYMMDD_HHMMSS.wav -> meeting_YYYYMMDD_HHMMSS.txt
        std::string txt_path = recording_path;
        const auto dot = txt_path.rfind('.');
        if (dot != std::string::npos) {
            txt_path = txt_path.substr(0, dot);
        }
        txt_path += ".txt";

        {
            SdCardAccessGuard sd_guard;
            FILE* f = std::fopen(txt_path.c_str(), "wb");
            if (f == nullptr) {
                mclog::tagError("MeetingUpload", "notes: open failed: path={}, errno={}",
                                txt_path, errno);
                return;
            }
            const bool ok =
                std::fwrite(notes_text.data(), 1U, notes_text.size(), f) == notes_text.size();
            std::fclose(f);
            if (ok) {
                mclog::tagInfo("MeetingUpload", "notes saved: path={}, len={}", txt_path,
                               notes_text.size());
            } else {
                mclog::tagError("MeetingUpload", "notes: write failed: path={}", txt_path);
            }
        }
    } catch (const std::exception& e) {
        mclog::tagError("MeetingUpload", "notes: exception: {}", e.what());
    }
}

bool uploadRecordingFile(const stackchan::meeting::UploadSession& session) {
    // 通用上传（multipart 流式 + 响应解析写 .txt 纪要）已提取到 hal_bridge，
    // 会议录音与文件管理共用同一实现，避免两处重复。
    return hal_bridge::upload_recording_for_notes(session.url, session.path, session.file_name,
                                                  session.type);
}

// FreeRTOS task 入口：arg 为堆上 shared_ptr 副本的所有权指针，task 持有会话
// 引用计数，app 先销毁也不影响上传跑完。
void uploadRecordingTask(void* arg) {
    std::unique_ptr<std::shared_ptr<stackchan::meeting::UploadSession>> holder(
        static_cast<std::shared_ptr<stackchan::meeting::UploadSession>*>(arg));
    const bool ok = uploadRecordingFile(**holder);
    (*holder)->success.store(ok);
    (*holder)->finished.store(true);
    vTaskDelete(NULL);
}

}  // namespace

AppMeeting::AppMeeting(std::string app_name, std::string title, std::string root_dir,
                       std::string file_prefix, std::string icon_bin)
    : _app_title(std::move(title)), _root_dir(std::move(root_dir)), _file_prefix(std::move(file_prefix)) {
    _ui_font = &meeting_zh_font;
    setAppInfo().name = app_name;
    // launcher 图标走米宝资源（新资源名，不覆盖原生 icon），实例成员保证地址稳定
    _icon_dsc = assets::get_image(icon_bin.c_str());
    setAppInfo().icon = (void*)&_icon_dsc;
    setAppInfo().userData = (void*)&_theme_color;
}

void AppMeeting::onCreate() {
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppMeeting::onOpen() {
    mclog::tagInfo(getAppInfo().name, "on open");

    // The remote controller uses channel 1 by default. Keep ESP-NOW alive
    // while the meeting app is active instead of requiring the ESP-NOW app.
    GetHAL().startEspNow(1);

    _model.reset();
    _recording_started_ms = 0;
    _final_duration_ms = 0;
    _recorded_sample_count = 0;
    _audio_source.reset();
    _recording_file = nullptr;
    _recording_path.clear();
    // 开机时的一次性挂载可能因 SD 上电慢/共享 SPI 总线瞬态竞争而失败；
    // 打开 app 时惰性重挂（有次数上限），避免“永久未挂载”。
    if (!hal_bridge::board_sdcard_is_mounted()) {
        hal_bridge::board_sdcard_try_remount();
    }
    _storage_status = hal_bridge::board_sdcard_is_mounted() ? "存储：就绪" : "存储：未挂载";
    _recording_save_failed = false;
    _close_requested = false;
    // 新一轮打开不等待上一轮后台上传；task 持有会话 shared_ptr，可自行跑完
    _upload_session.reset();
    _upload_consumed = false;
    // 历史界面状态重置
    _ui_mode = UiMode::Main;
    _history_requested = false;
    _convert_requested = false;
    _convert_done_refresh = false;
    _back_requested = false;
    _playback_requested = false;
    _confirm_visible = false;
    _delete_confirmed = false;
    _delete_cancelled = false;
    _confirm_path.clear();
    _playback_request_path.clear();
    _history_files.clear();

    {
        LvglLockGuard lock;
        createUi();
        // Closing an app from the indicator callback would re-enter onClose()
        // while this app owns LvglLockGuard.  Defer it until the lock is released.
        view::create_home_indicator([this]() { _close_requested = true; }, kAccentColor, kAccentDarkColor);
        view::create_status_bar(kAccentColor, kAccentDarkColor);
        // 边缘左/右滑 = 返回（播放页回列表 / 列表回主界面；个人灵感首页=退出）
        view::set_edge_back_callback([this]() { requestBack(); });
        refreshUi();
    }
    // 个人灵感：首页直接显示纪要列表（不进录音主界面）。
    // 放在 LvglLockGuard 之外调用（enterHistoryList 内部会自己加锁）。
    if (_list_txt_notes) {
        enterHistoryList();
    }

    // 语音打开确认：MCP 工具（self.mibao.open_meeting）设置的 voice_meeting 标记
    // 表示本 app 由"米宝米宝"对话语音打开，需播报确认语并等待"是/否"。
    // 在 LvglLockGuard 作用域之外执行（内部会阻塞听语音），
    // 因此放到锁释放之后。
    runVoiceConfirmIfRequested();
}

void AppMeeting::onRunning() {
    EspNowRemoteCommandEvent remote_event;
    while (GetHAL().pollEspNowRemoteCommand(remote_event)) {
        handleRemoteCommand(remote_event.command);
    }

    // 历史界面动作（触摸线程只置标记，主循环串行处理）
    // 滑动召唤（底部上滑 home / 顶部下滑状态栏）期间丢弃误触的按钮点击。
    const bool swipe_active = view::swipe_recently_detected(400);

    if (!swipe_active && _history_requested.exchange(false) && _ui_mode == UiMode::Main) {
        enterHistoryList();
    } else {
        _history_requested = false;
    }
    if (!swipe_active && _convert_requested.exchange(false) && !_convert_running.load()) {
        startConvertAll();
    } else {
        _convert_requested = false;
    }
    // 批量转换完成：若当前在纪要/录音列表页，重扫刷新
    if (_convert_done_refresh.exchange(false) && _ui_mode == UiMode::HistoryList) {
        enterHistoryList();
    }
    if (!swipe_active && _back_requested.exchange(false)) {
        if (_ui_mode == UiMode::Playback) {
            stopPlayback();  // 回放 -> 历史列表
        } else if (_ui_mode == UiMode::HistoryList) {
            if (_list_txt_notes) {
                // 个人灵感首页就是纪要列表：返回 = 退出应用
                _close_requested = true;
            } else {
                exitHistoryList();
            }
        }
    } else {
        _back_requested = false;
    }
    if (!swipe_active && _playback_requested.exchange(false) && !_playback_request_path.empty()) {
        if (_ui_mode == UiMode::HistoryList || _ui_mode == UiMode::Main) {
            // 个人灵感：列表是纪要 .txt，点击查看内容而不是播放
            if (_list_txt_notes && _playback_request_path.size() >= 4 &&
                _playback_request_path.substr(_playback_request_path.size() - 4) == ".txt") {
                showNotesDialog(_playback_request_path);
                _playback_request_path.clear();
            } else {
                startPlayback(_playback_request_path);
                _playback_request_path.clear();
            }
        }
    } else {
        _playback_requested = false;
        _playback_request_path.clear();
    }
    if (!swipe_active && _playback_toggle_requested.exchange(false) && _ui_mode == UiMode::Playback) {
        _playback_paused = !_playback_paused;
        {
            LvglLockGuard lock;
            updatePlaybackToggleButton();
        }
    } else {
        _playback_toggle_requested = false;
    }
    if (_delete_cancelled.exchange(false)) {
        hideConfirmDialog();
    }
    if (_confirm_visible.exchange(false)) {
        showConfirmDialog(_confirm_path);
    }
    if (_delete_confirmed.exchange(false)) {
        const std::string path = _confirm_path;
        hideConfirmDialog();
        doDeleteFile(path);
    }

    if (_model.state() == stackchan::meeting::MeetingState::Recording) {
        captureRecordingFrame();
    }

    // 历史录音回放推进
    if (_ui_mode == UiMode::Playback) {
        playbackTick();
    }

    // 后台上传结果轮询：状态/文案更新放在 UI 线程
    pollUploadResult();

    {
        LvglLockGuard lock;
        refreshUi();
        view::update_home_indicator();
        view::update_status_bar();
    }

    if (_close_requested) {
        _close_requested = false;
        close();
    }
}

void AppMeeting::onClose() {
    mclog::tagInfo(getAppInfo().name, "on close");
    stopMeetingTasks();
    hideConfirmDialog();

    LvglLockGuard lock;
    view::clear_edge_back_callback();
    destroyUi();
    view::destroy_home_indicator();
    view::destroy_status_bar();
}

void AppMeeting::createUi() {
    _root = lv_obj_create(lv_screen_active());
    lv_obj_set_size(_root, 320, 240);
    lv_obj_set_style_bg_color(_root, lv_color_hex(kBgColor), 0);
    lv_obj_set_style_border_width(_root, 0, 0);
    lv_obj_set_style_radius(_root, 0, 0);
    lv_obj_set_style_pad_all(_root, 0, 0);
    lv_obj_center(_root);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

    _info_panel = lv_obj_create(_root);
    lv_obj_set_size(_info_panel, 296, 56);
    lv_obj_align(_info_panel, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_set_style_bg_color(_info_panel, lv_color_hex(kPanelSoftColor), 0);
    lv_obj_set_style_bg_opa(_info_panel, LV_OPA_80, 0);
    lv_obj_set_style_border_width(_info_panel, 0, 0);
    lv_obj_set_style_radius(_info_panel, 8, 0);
    lv_obj_set_style_pad_all(_info_panel, 7, 0);
    lv_obj_clear_flag(_info_panel, LV_OBJ_FLAG_SCROLLABLE);

    _title_label = create_label(_info_panel, _app_title.c_str(), _ui_font, kInkColor);
    lv_obj_set_width(_title_label, 116);
    lv_obj_align(_title_label, LV_ALIGN_TOP_LEFT, 0, -1);

    _recording_dot = lv_obj_create(_info_panel);
    lv_obj_set_size(_recording_dot, 10, 10);
    lv_obj_set_style_radius(_recording_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(_recording_dot, lv_color_hex(kDangerColor), 0);
    lv_obj_set_style_border_width(_recording_dot, 0, 0);
    lv_obj_align(_recording_dot, LV_ALIGN_TOP_RIGHT, 0, 2);

    _state_label = create_label(_info_panel, "就绪", _ui_font, kAccentDarkColor);
    lv_obj_set_width(_state_label, 98);
    lv_obj_align(_state_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    _duration_label = create_label(_info_panel, "00:00", _ui_font, kInkColor);
    lv_obj_set_width(_duration_label, 72);
    lv_obj_set_style_text_align(_duration_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(_duration_label, LV_ALIGN_TOP_RIGHT, -18, -2);

    _network_label = create_label(_info_panel, "无线：--", _ui_font, kInkColor);
    lv_obj_set_width(_network_label, 76);
    lv_obj_align(_network_label, LV_ALIGN_BOTTOM_LEFT, 100, 0);

    _storage_label = create_label(_info_panel, "存储：就绪", _ui_font, kInkColor);
    lv_obj_set_width(_storage_label, 96);
    lv_obj_set_style_text_align(_storage_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(_storage_label, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    // 「最近录音」卡片：中下方独立卡片，展示最近一次保存的录音文件（#2）
    // 高度加大到 70 解决文件名与标题重叠（#4）
    _recent_panel = lv_obj_create(_root);
    lv_obj_set_size(_recent_panel, 296, 70);
    lv_obj_align(_recent_panel, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_set_style_bg_color(_recent_panel, lv_color_hex(kPanelSoftColor), 0);
    lv_obj_set_style_bg_opa(_recent_panel, LV_OPA_80, 0);
    lv_obj_set_style_border_width(_recent_panel, 0, 0);
    lv_obj_set_style_radius(_recent_panel, 8, 0);
    lv_obj_set_style_pad_all(_recent_panel, 6, 0);
    lv_obj_clear_flag(_recent_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_recent_panel, LV_OBJ_FLAG_CLICKABLE);
    // 点击「最近录音」卡片 -> 进入详情页播放
    lv_obj_add_event_cb(
        _recent_panel,
        [](lv_event_t* event) {
            auto* app = static_cast<AppMeeting*>(lv_event_get_user_data(event));
            if (app && !app->_recent_record_path.empty()) {
                app->_playback_request_path = app->_recent_record_path;
                app->_playback_requested = true;
            }
        },
        LV_EVENT_CLICKED, this);

    // 标题：左上，固定行高 22
    _recent_title_label = create_label(_recent_panel, "最近录音", _ui_font, kAccentDarkColor);
    lv_obj_set_width(_recent_title_label, 100);
    lv_obj_align(_recent_title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    // 时长：右上，固定行高 22
    _recent_duration_label = create_label(_recent_panel, "--:--", _ui_font, kInkColor);
    lv_obj_set_width(_recent_duration_label, 80);
    lv_obj_set_style_text_align(_recent_duration_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(_recent_duration_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    // 文件名：在标题下方独立一行，宽度 280 加省略号处理长文件名
    _recent_file_label = create_label(_recent_panel, "暂无", _ui_font, kInkColor);
    lv_obj_set_width(_recent_file_label, 280);
    lv_obj_set_style_text_align(_recent_file_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(_recent_file_label, LV_LABEL_LONG_DOT);
    lv_obj_align(_recent_file_label, LV_ALIGN_BOTTOM_LEFT, 0, 2);

    _controls_panel = lv_obj_create(_root);
    // 3 个按钮从左到右紧凑排列：3×54 + 2×4 = 170 宽（不是 228 两端拉开）
    lv_obj_set_size(_controls_panel, 170, 36);
    lv_obj_align(_controls_panel, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lv_obj_set_style_bg_opa(_controls_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_controls_panel, 0, 0);
    lv_obj_set_style_pad_all(_controls_panel, 0, 0);
    lv_obj_set_style_pad_column(_controls_panel, 4, 0);  // 与文件管理按钮间距一致
    lv_obj_set_flex_flow(_controls_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_controls_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(_controls_panel, LV_OBJ_FLAG_SCROLLABLE);

    // 主按钮：开始 -> 暂停 -> 继续 -> 暂停 ...（点击切换，见 handlePrimaryAction）
    _primary_button = create_button(_controls_panel, "开始", kAccentColor, _ui_font);
    lv_obj_set_size(_primary_button, 54, 36);
    _primary_button_label = lv_obj_get_child(_primary_button, 0);
    lv_obj_add_event_cb(
        _primary_button,
        [](lv_event_t* event) {
            auto* app = static_cast<AppMeeting*>(lv_event_get_user_data(event));
            app->handlePrimaryAction();
        },
        LV_EVENT_CLICKED, this);

    // 「结束」单击即结束并保存，无二次确认
    _end_button = create_button(_controls_panel, "结束", kDangerColor, _ui_font);
    lv_obj_set_size(_end_button, 54, 36);
    _end_button_label = lv_obj_get_child(_end_button, 0);
    lv_obj_add_event_cb(
        _end_button,
        [](lv_event_t* event) {
            auto* app = static_cast<AppMeeting*>(lv_event_get_user_data(event));
            app->handleEndAction();
        },
        LV_EVENT_CLICKED, this);

    // 「历史」：查看历史录音（结束按钮右侧）
    _history_button = create_button(_controls_panel, "历史", kAccentDarkColor, &mibao_zh_font_16);
    lv_obj_set_size(_history_button, 54, 36);
    _history_button_label = lv_obj_get_child(_history_button, 0);
    lv_obj_add_event_cb(
        _history_button,
        [](lv_event_t* event) {
            auto* app = static_cast<AppMeeting*>(lv_event_get_user_data(event));
            app->requestHistory();
        },
        LV_EVENT_CLICKED, this);

    // ---- 历史面板（全屏覆盖，默认隐藏；参考会议录像 App） ----
    _history_panel = lv_obj_create(_root);
    lv_obj_remove_flag(_history_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(_history_panel, 320, 240);
    lv_obj_align(_history_panel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_history_panel, lv_color_hex(kBgColor), 0);
    lv_obj_set_style_bg_opa(_history_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_history_panel, 0, 0);
    lv_obj_set_style_radius(_history_panel, 0, 0);
    lv_obj_set_style_pad_all(_history_panel, 0, 0);
    lv_obj_add_flag(_history_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* history_title =
        create_label(_history_panel, _list_txt_notes ? "历史纪要" : "历史录音", &mibao_zh_font_16, 0xFFFFFF);
    lv_obj_set_width(history_title, 200);
    lv_obj_align(history_title, LV_ALIGN_TOP_LEFT, 12, 6);

    // 列表容器：可滚动，行按钮自绘（工程禁用了 lv_list）
    _history_list = lv_obj_create(_history_panel);
    lv_obj_set_size(_history_list, 296, 168);
    lv_obj_align(_history_list, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_color(_history_list, lv_color_hex(kPanelSoftColor), 0);
    lv_obj_set_style_border_width(_history_list, 0, 0);
    lv_obj_set_style_radius(_history_list, 8, 0);
    lv_obj_set_style_pad_all(_history_list, 6, 0);
    // 个人灵感（纪要列表首页）没有返回按钮，列表可以延伸到屏幕底部
    if (_list_txt_notes) {
        lv_obj_set_size(_history_list, 296, 190);
        lv_obj_align(_history_list, LV_ALIGN_TOP_MID, 0, 40);
    }
    lv_obj_set_flex_flow(_history_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_history_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    // 返回按钮（左下角，与右下角 home 键同排同尺寸）
    _history_back_btn = create_button(_history_panel, "返回", kAccentColor, &mibao_zh_font_16);
    lv_obj_set_size(_history_back_btn, 54, 36);
    lv_obj_set_style_radius(_history_back_btn, 12, 0);
    lv_obj_align(_history_back_btn, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lv_obj_add_event_cb(
        _history_back_btn,
        [](lv_event_t* event) {
            auto* app = static_cast<AppMeeting*>(lv_event_get_user_data(event));
            app->requestBack();
        },
        LV_EVENT_CLICKED, this);

    // ---- 播放器（详情页：进度条 + 播放/暂停 + 时间 + 删除） ----
    // 进度条（slider，可拖拽）：界面中央
    _playback_progress = lv_slider_create(_history_panel);
    lv_obj_set_size(_playback_progress, 280, 12);
    lv_obj_align(_playback_progress, LV_ALIGN_CENTER, 0, -24);
    lv_obj_set_style_bg_color(_playback_progress, lv_color_hex(0x3A4249), 0);
    lv_obj_set_style_bg_color(_playback_progress, lv_color_hex(kAccentColor), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_playback_progress, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_slider_set_range(_playback_progress, 0, 1000);
    lv_slider_set_value(_playback_progress, 0, LV_ANIM_OFF);
    lv_obj_add_flag(_playback_progress, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(
        _playback_progress,
        [](lv_event_t* event) {
            auto* app = static_cast<AppMeeting*>(lv_event_get_user_data(event));
            if (app && lv_event_get_code(event) == LV_EVENT_RELEASED) {
                // 拖拽结束 -> seek 到对应位置
                app->_playback_seek_value = (int)lv_slider_get_value(
                    static_cast<lv_obj_t*>(lv_event_get_target(event)));
                app->_playback_seek_requested = true;
            }
        },
        LV_EVENT_RELEASED, this);

    // 时间标签（进度条下方）
    _playback_time_label = create_label(_history_panel, "00:00 / 00:00", &mibao_zh_font_16, 0xFFFFFF);
    lv_obj_set_width(_playback_time_label, 200);
    lv_obj_align(_playback_time_label, LV_ALIGN_CENTER, 0, 4);
    lv_obj_add_flag(_playback_time_label, LV_OBJ_FLAG_HIDDEN);

    // 回放页返回按钮（左下角，与 home 键同排同尺寸）
    _playback_btn = create_button(_history_panel, "返回", kAccentColor, &mibao_zh_font_16);
    lv_obj_set_size(_playback_btn, 54, 36);
    lv_obj_set_style_radius(_playback_btn, 12, 0);
    lv_obj_align(_playback_btn, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lv_obj_add_flag(_playback_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(
        _playback_btn,
        [](lv_event_t* event) {
            auto* app = static_cast<AppMeeting*>(lv_event_get_user_data(event));
            app->requestBack();
        },
        LV_EVENT_CLICKED, this);

    // 播放/暂停切换按钮（底部居中，与返回/删除齐平）
    _playback_toggle_btn = create_button(_history_panel, "播放", kAccentColor, &mibao_zh_font_16);
    lv_obj_set_size(_playback_toggle_btn, 54, 36);
    lv_obj_set_style_radius(_playback_toggle_btn, 12, 0);
    lv_obj_align(_playback_toggle_btn, LV_ALIGN_BOTTOM_LEFT, 66, -8);
    lv_obj_add_flag(_playback_toggle_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(
        _playback_toggle_btn,
        [](lv_event_t* event) {
            auto* app = static_cast<AppMeeting*>(lv_event_get_user_data(event));
            if (app) {
                app->requestPlaybackToggle();
            }
        },
        LV_EVENT_CLICKED, this);

    // 回放页删除按钮（右下角，与返回/播放齐平）
    _playback_delete_btn = create_button(_history_panel, "删除", kDangerColor, &mibao_zh_font_16);
    lv_obj_set_size(_playback_delete_btn, 54, 36);
    lv_obj_set_style_radius(_playback_delete_btn, 12, 0);
    lv_obj_align(_playback_delete_btn, LV_ALIGN_BOTTOM_LEFT, 124, -8);
    lv_obj_add_flag(_playback_delete_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(
        _playback_delete_btn,
        [](lv_event_t* event) {
            auto* app = static_cast<AppMeeting*>(lv_event_get_user_data(event));
            app->requestDeleteConfirm(app->_playback_path);
        },
        LV_EVENT_CLICKED, this);

    // 回放页转纪要按钮（删除按钮右侧）：把当前播放的录音转成会议纪要
    _playback_convert_btn = create_button(_history_panel, "转纪要", kAccentDarkColor, &mibao_zh_font_16);
    lv_obj_set_size(_playback_convert_btn, 54, 36);
    lv_obj_set_style_radius(_playback_convert_btn, 12, 0);
    lv_obj_align(_playback_convert_btn, LV_ALIGN_BOTTOM_LEFT, 182, -8);
    lv_obj_add_flag(_playback_convert_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(
        _playback_convert_btn,
        [](lv_event_t* event) {
            auto* app = static_cast<AppMeeting*>(lv_event_get_user_data(event));
            if (view::swipe_recently_detected(400)) {
                return;  // 滑动唤出 home 时误触
            }
            app->requestConvertAll();
        },
        LV_EVENT_CLICKED, this);

    // ---- 删除确认弹窗（覆盖层 + 面板） ----
    _confirm_panel = lv_obj_create(lv_screen_active());
    lv_obj_remove_flag(_confirm_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(_confirm_panel, 320, 240);
    lv_obj_align(_confirm_panel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_confirm_panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_confirm_panel, LV_OPA_50, 0);
    lv_obj_set_style_border_width(_confirm_panel, 0, 0);
    lv_obj_set_style_radius(_confirm_panel, 0, 0);
    lv_obj_add_flag(_confirm_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* dialog = lv_obj_create(_confirm_panel);
    lv_obj_remove_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(dialog, 260, 120);
    lv_obj_align(dialog, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(0x1E2429), 0);
    lv_obj_set_style_border_width(dialog, 1, 0);
    lv_obj_set_style_border_color(dialog, lv_color_hex(0x3A4249), 0);
    lv_obj_set_style_radius(dialog, 10, 0);
    lv_obj_set_style_pad_all(dialog, 12, 0);

    _confirm_label = create_label(dialog, "删除这条录音？", &mibao_zh_font_16, kInkColor);
    lv_obj_set_width(_confirm_label, 230);
    lv_label_set_long_mode(_confirm_label, LV_LABEL_LONG_DOT);
    lv_obj_align(_confirm_label, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t* cancel_btn = create_button(dialog, "取消", 0x3A4249, &mibao_zh_font_16);
    lv_obj_set_size(cancel_btn, 100, 36);
    lv_obj_set_style_radius(cancel_btn, 10, 0);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 12, -12);
    lv_obj_add_event_cb(
        cancel_btn,
        [](lv_event_t* event) {
            auto* app = static_cast<AppMeeting*>(lv_event_get_user_data(event));
            app->requestDeleteCancel();
        },
        LV_EVENT_CLICKED, this);

    _confirm_delete_btn = create_button(dialog, "删除", kDangerColor, &mibao_zh_font_16);
    lv_obj_set_size(_confirm_delete_btn, 100, 36);
    lv_obj_set_style_radius(_confirm_delete_btn, 10, 0);
    lv_obj_align(_confirm_delete_btn, LV_ALIGN_BOTTOM_RIGHT, -12, -12);
    lv_obj_add_event_cb(
        _confirm_delete_btn,
        [](lv_event_t* event) {
            auto* app = static_cast<AppMeeting*>(lv_event_get_user_data(event));
            app->requestDeleteConfirmAction();
        },
        LV_EVENT_CLICKED, this);
}

void AppMeeting::destroyUi() {
    if (_root != nullptr) {
        lv_obj_delete(_root);
    }
    _root = nullptr;
    _title_label = nullptr;
    _state_label = nullptr;
    _duration_label = nullptr;
    _recording_dot = nullptr;
    _network_label = nullptr;
    _storage_label = nullptr;
    _info_panel = nullptr;
    _controls_panel = nullptr;
    _primary_button = nullptr;
    _primary_button_label = nullptr;
    _end_button = nullptr;
    _end_button_label = nullptr;
    _history_button = nullptr;
    _history_button_label = nullptr;
    _recent_panel = nullptr;
    _recent_title_label = nullptr;
    _recent_file_label = nullptr;
    _recent_duration_label = nullptr;
    _history_panel = nullptr;
    _history_list = nullptr;
    _history_back_btn = nullptr;
    _playback_btn = nullptr;
    _playback_delete_btn = nullptr;
    _playback_convert_btn = nullptr;
    _playback_progress = nullptr;
    _playback_toggle_btn = nullptr;
    _playback_time_label = nullptr;
    _confirm_panel = nullptr;
    _confirm_label = nullptr;
    _confirm_delete_btn = nullptr;
}

void AppMeeting::refreshUi() {
    if (_ui_mode != UiMode::Main) {
        // 历史界面：主界面文案/按钮刷新跳过（按钮已由 applyUiMode 隐藏）
        return;
    }
    set_label(_state_label, _model.stateLabel());
    // 录音中实时计时；暂停/结束时显示冻结时长（暂停时已把起点前移，
    // 但冻结值更直观：暂停期间时间不变）
    const auto rec_state = _model.state();
    const bool active = rec_state == stackchan::meeting::MeetingState::Recording;
    const std::uint32_t shown_ms = active ? elapsedRecordingMs() : _final_duration_ms;
    set_label(_duration_label, _model.formatDuration(shown_ms));
    set_label(_network_label, wifiLabel());
    set_label(_storage_label, _storage_status);
    // 每次刷新同步「最近录音」卡片（文件名 + 时长）
    if (_recent_file_label != nullptr) {
        set_label(_recent_file_label, _recent_record_name.empty() ? "暂无" : _recent_record_name);
    }
    if (_recent_duration_label != nullptr) {
        set_label(_recent_duration_label,
                  _recent_record_duration.empty() ? "--:--" : _recent_record_duration);
    }
    set_label(_primary_button_label, "开始");
    set_label(_end_button_label, "结束");

    // 主按钮按状态显示 开始/暂停/继续，并切换颜色
    const auto state = _model.state();
    if (_primary_button_label != nullptr) {
        const char* text = "开始";
        std::uint32_t color = kAccentColor;
        switch (state) {
            case stackchan::meeting::MeetingState::Recording:
                text = "暂停";
                color = kAccentColor;
                break;
            case stackchan::meeting::MeetingState::Paused:
                text = "继续";
                color = kAccentDarkColor;
                break;
            case stackchan::meeting::MeetingState::Completed:
            case stackchan::meeting::MeetingState::Error:
                text = "开始";
                color = kAccentColor;
                break;
            default:
                break;
        }
        set_label(_primary_button_label, text);
        if (_primary_button != nullptr) {
            lv_obj_set_style_bg_color(_primary_button, lv_color_hex(color), 0);
        }
    }

    const bool can_end = state == stackchan::meeting::MeetingState::Recording ||
                         state == stackchan::meeting::MeetingState::Paused ||
                         state == stackchan::meeting::MeetingState::Error ||
                         state == stackchan::meeting::MeetingState::Completed;
    if (_end_button != nullptr) {
        if (can_end) {
            lv_obj_clear_state(_end_button, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(_end_button, LV_STATE_DISABLED);
        }
    }
    // 历史按钮：录音/暂停中禁用，避免中途切走
    if (_history_button != nullptr) {
        if (state == stackchan::meeting::MeetingState::Recording ||
            state == stackchan::meeting::MeetingState::Paused) {
            lv_obj_add_state(_history_button, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(_history_button, LV_STATE_DISABLED);
        }
    }
    if (_recording_dot != nullptr) {
        if (_model.recordingIndicatorVisible()) {
            lv_obj_clear_flag(_recording_dot, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_recording_dot, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void AppMeeting::handlePrimaryAction() {
    if (_model.state() == stackchan::meeting::MeetingState::Completed ||
        _model.state() == stackchan::meeting::MeetingState::Error) {
        // A short press after a finished meeting starts a clean new recording.
        resetFinishedMeeting();
    }

    const auto previous = _model.state();
    if (previous == stackchan::meeting::MeetingState::Idle ||
        previous == stackchan::meeting::MeetingState::Error) {
        if (beginRecording()) {
            _model.primaryAction();
        } else {
            _model.markError();
        }
        return;
    }
    if (previous == stackchan::meeting::MeetingState::Recording ||
        previous == stackchan::meeting::MeetingState::Paused) {
        // 开始/暂停/继续切换：模型已支持 Recording <-> Paused，
        // captureRecordingFrame 只在 Recording 状态写入，暂停即停录。
        // 暂停时冻结已录时长，继续时把计时起点前移，时长从暂停处继续走。
        if (previous == stackchan::meeting::MeetingState::Recording) {
            _final_duration_ms = elapsedRecordingMs();  // 冻结暂停前的已录时长
        } else {
            // 继续：起点 = now - 已录时长，elapsed 接着暂停前继续
            _recording_started_ms = GetHAL().millis() - _final_duration_ms;
        }
        _model.primaryAction();
        return;
    }
    // 其它状态（Finalizing/Uploading/Processing）忽略点击。
}

void AppMeeting::handleEndAction() {
    const auto state = _model.state();
    if (state == stackchan::meeting::MeetingState::Error ||
        state == stackchan::meeting::MeetingState::Completed) {
        resetFinishedMeeting();
        return;
    }

    const bool can_end = state == stackchan::meeting::MeetingState::Recording ||
                         state == stackchan::meeting::MeetingState::Paused;
    if (!can_end) return;
    // 单击直接结束并保存，不再做两段式确认
    finalizeMeeting();
}

void AppMeeting::handleRemoteCommand(EspNowRemoteCommand command) {
    if (command == EspNowRemoteCommand::MeetingPrimary) {
        // Start, pause, or resume according to the current meeting state.
        handlePrimaryAction();
        return;
    }

    if (command == EspNowRemoteCommand::MeetingEnd) {
        const auto state = _model.state();
        if (state == stackchan::meeting::MeetingState::Recording ||
            state == stackchan::meeting::MeetingState::Paused) {
            finalizeMeeting();
        } else if (state == stackchan::meeting::MeetingState::Completed ||
                   state == stackchan::meeting::MeetingState::Error) {
            // A long press after completion only clears the completed meeting.
            resetFinishedMeeting();
        }
    }
}

void AppMeeting::resetFinishedMeeting() {
    stopMeetingTasks();
    _model.reset();
    _storage_status = hal_bridge::board_sdcard_is_mounted() ? "存储：就绪" : "存储：未挂载";
    _recording_path.clear();
    _recorded_sample_count = 0;
    _recording_save_failed = false;
    _final_duration_ms = 0;
    _upload_session.reset();
    _upload_consumed = false;
}

void AppMeeting::finalizeMeeting() {
    // 冻结最终时长并清零计时起点，结束后界面不再走秒
    _final_duration_ms = elapsedRecordingMs();
    _recording_started_ms = 0;
    _model.endMeeting();
    stopMeetingTasks();
    if (!_recording_save_failed && !_recording_path.empty()) {
        const auto slash = _recording_path.rfind('/');
        const std::string file_name =
            (slash == std::string::npos) ? _recording_path : _recording_path.substr(slash + 1);
        // 文件名 + 时长放入「最近录音」卡片（#2），顶部存储标签只保留状态、不再塞长文件名
        _storage_status = "存储：已保存";
        _recent_record_name = file_name;
        _recent_record_duration = _model.formatDuration(_final_duration_ms);
        _recent_record_path = _recording_path;
        // 配置了 upload_url 则后台上传（Finalizing→Uploading）；
        // 未配置则直接走 Processing→Completed。
        startUploadIfConfigured();
    } else {
        _model.markError();
    }
}

void AppMeeting::startUploadIfConfigured() {
    const std::string upload_url = mibao::getUploadUrl();
    if (upload_url.empty() || _recording_path.empty()) {
        // 未配置上传：本地保存即完成
        _model.markProcessing();
        _model.markCompleted();
        return;
    }

    auto session = std::make_shared<stackchan::meeting::UploadSession>();
    session->url = upload_url;
    session->path = _recording_path;
    const auto slash = session->path.rfind('/');
    session->file_name =
        (slash == std::string::npos) ? session->path : session->path.substr(slash + 1);
    // type 由录音文件前缀决定：个人灵感（personal_）→ personal，其余 → meeting
    session->type = (_file_prefix.find("personal") != std::string::npos) ? "personal" : "meeting";
    _upload_session = session;
    _upload_consumed = false;
    _model.markUploading();  // 状态标签自动显示「上传中」

    mclog::tagInfo(getAppInfo().name, "start background upload: path={}, type={}, url={}",
                   session->path, session->type, session->url);

    // TODO: 本批不做失败重试队列；上传失败仅 UI 提示，不影响本地文件。
    auto* holder = new std::shared_ptr<stackchan::meeting::UploadSession>(session);
    if (xTaskCreatePinnedToCore(&uploadRecordingTask, "rec_upload", 12288, holder, 4, nullptr,
                                1) != pdPASS) {
        delete holder;
        _storage_status = "上传失败(本地已保存)";
        _model.markError();
    }
}

void AppMeeting::pollUploadResult() {
    if (!_upload_session || _upload_consumed || !_upload_session->finished.load()) {
        return;
    }
    _upload_consumed = true;
    if (_upload_session->success.load()) {
        _storage_status = "已上传";
        _model.markProcessing();  // Uploading → Processing
        _model.markCompleted();   // Processing → Completed
    } else {
        // 上传失败不影响本地文件；本地已保存，仅 UI 提示
        _storage_status = "上传失败(本地已保存)";
        _model.markError();
    }
}

void AppMeeting::stopMeetingTasks() {
    finalizeRecordingFile();
    _audio_source.reset();
}

bool AppMeeting::beginRecording() {
    abortRecordingFile();
    _recorded_sample_count = 0;
    _recording_save_failed = false;
    _final_duration_ms = 0;
    _recording_path = makeRecordingPath();
    // 开始新录音：清空「最近录音」卡片（录完后再展示新音频）
    _recent_record_name.clear();
    _recent_record_duration.clear();
    _recent_record_path.clear();

    // 点“开始”时再给一次重挂机会（覆盖 onOpen 后才插卡/挂载才恢复的场景）
    if (!hal_bridge::board_sdcard_is_mounted()) {
        hal_bridge::board_sdcard_try_remount();
    }

    if (!hal_bridge::board_sdcard_is_mounted()) {
        _storage_status = "存储：未挂载";
        mclog::tagError(getAppInfo().name, "cannot start recording: SD card is not mounted");
        return false;
    }

    _audio_source = std::make_unique<stackchan::meeting::StackChanAudioSource>();
    if (!_audio_source->isAcquired()) {
        _audio_source.reset();
        _storage_status = "麦克风：忙";
        return false;
    }

    {
        SdCardAccessGuard sd_guard;
        if (!ensure_dir("/sdcard") || !ensure_dir(_root_dir.c_str())) {
            const int error_number = errno;
            _storage_status = "存储：目录错误";
            mclog::tagError(getAppInfo().name, "meeting storage unavailable: path={}, errno={}, error={}",
                            _root_dir, error_number, std::strerror(error_number));
            _audio_source.reset();
            return false;
        }
    }

    {
        SdCardAccessGuard sd_guard;
        errno = 0;
        // The recorder only writes samples and seeks back to rewrite the WAV
        // header.  It never reads the file, so use write-only create/truncate
        // mode; this is more compatible with the FAT VFS than wb+ on some
        // cards/driver combinations.
        _recording_file = std::fopen(_recording_path.c_str(), "wb");
    }

    if (_recording_file == nullptr) {
        const int error_number = errno;
        _storage_status = "存储：打开错误 " + std::to_string(error_number);
        mclog::tagError(getAppInfo().name, "open recording failed: path={}, errno={}, error={}",
                        _recording_path, error_number, std::strerror(error_number));
        _audio_source.reset();
        return false;
    }

    const auto header = stackchan::meeting::WritePlaceholderHeader();
    bool header_written = false;
    {
        SdCardAccessGuard sd_guard;
        header_written = std::fwrite(header.data(), 1U, header.size(), _recording_file) == header.size();
    }
    if (!header_written) {
        const int error_number = errno;
        _storage_status = "存储：写入失败";
        mclog::tagError(getAppInfo().name, "write wav header failed: path={}, errno={}, error={}",
                        _recording_path, error_number, std::strerror(error_number));
        _recording_save_failed = true;
        abortRecordingFile();
        _audio_source.reset();
        return false;
    }

    _recording_started_ms = GetHAL().millis();
    _storage_status = "存储：录音中";
    return true;
}

void AppMeeting::captureRecordingFrame() {
    if (!_audio_source || !_recording_file || _recording_save_failed) {
        return;
    }

    stackchan::meeting::AudioBlock block;
    if (!_audio_source->read(block)) {
        return;
    }

    if (!writeAudioBlock(block)) {
        _recording_save_failed = true;
        _storage_status = "存储：写入失败";
        _model.markError();
        stopMeetingTasks();
    }
}

bool AppMeeting::writeAudioBlock(const stackchan::meeting::AudioBlock& block) {
    SdCardAccessGuard sd_guard;
    for (std::int16_t sample : block.samples) {
        const std::uint8_t bytes[] = {
            static_cast<std::uint8_t>(sample),
            static_cast<std::uint8_t>(static_cast<std::uint16_t>(sample) >> 8U),
        };
        if (std::fwrite(bytes, 1U, sizeof(bytes), _recording_file) != sizeof(bytes)) {
            return false;
        }
    }

    _recorded_sample_count += block.samples.size();
    return true;
}

bool AppMeeting::finalizeRecordingFile() {
    if (_recording_file == nullptr) {
        return !_recording_save_failed;
    }

    const auto header = stackchan::meeting::FinalizeHeader(_recorded_sample_count);
    bool ok = false;
    {
        SdCardAccessGuard sd_guard;
        ok = std::fflush(_recording_file) == 0;
        ok = ok && std::fseek(_recording_file, 0, SEEK_SET) == 0;
        ok = ok && std::fwrite(header.data(), 1U, header.size(), _recording_file) == header.size();
        ok = ok && std::fflush(_recording_file) == 0;
        ok = std::fclose(_recording_file) == 0 && ok;
    }
    _recording_file = nullptr;

    if (ok) {
        _storage_status = "存储：已保存";
        mclog::tagInfo(getAppInfo().name, "recording saved: path={}, samples={}",
                       _recording_path, static_cast<unsigned long long>(_recorded_sample_count));
    } else {
        _storage_status = "存储：保存失败";
        _recording_save_failed = true;
    }
    return ok;
}

void AppMeeting::abortRecordingFile() {
    if (_recording_file != nullptr) {
        std::fclose(_recording_file);
        _recording_file = nullptr;
    }
}

std::uint32_t AppMeeting::elapsedRecordingMs() const {
    if (_recording_started_ms == 0U) return 0U;
    return GetHAL().millis() - _recording_started_ms;
}

std::string AppMeeting::wifiLabel() const {
    switch (GetHAL().getWifiStatus()) {
        case WifiStatus::High:
            return "无线：强";
        case WifiStatus::Medium:
            return "无线：中";
        case WifiStatus::Low:
            return "无线：弱";
        case WifiStatus::None:
            return "无线：关闭";
    }
    return "无线：--";
}

std::string AppMeeting::makeRecordingPath() const {
    // RTC 墙钟命名：<前缀>YYYYMMDD_HHMMSS.wav；
    // 系统时间未同步（早于 2024 年）时回退为开机时长命名，保证文件名唯一可用。
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm_buf{};
    localtime_r(&now, &tm_buf);
    char stamp[32] = {0};
    if (tm_buf.tm_year + 1900 >= 2024) {
        std::snprintf(stamp, sizeof(stamp), "%04d%02d%02d_%02d%02d%02d", tm_buf.tm_year + 1900,
                      tm_buf.tm_mon + 1, tm_buf.tm_mday, tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    } else {
        std::snprintf(stamp, sizeof(stamp), "%lu", static_cast<unsigned long>(GetHAL().millis()));
    }
    return _root_dir + "/" + _file_prefix + stamp + ".wav";
}

void AppMeeting::clearVoiceMeetingFlag() {
    Settings settings("mibao", true);
    settings.EraseKey("voice_meeting");
}

void AppMeeting::runVoiceConfirmIfRequested() {
    // 读取语音打开标记：MCP 工具 self.mibao.open_meeting 设置
    Settings settings("mibao", false);
    const bool requested = settings.GetInt("voice_meeting", 0) == 1;
    if (!requested) {
        return;
    }
    clearVoiceMeetingFlag();

    mclog::tagInfo(getAppInfo().name, "voice-open requested, start recording directly");

    // 语音打开会议 -> 直接开始录音。
    // （原 TTS 播报确认方案因 esp_tts 语音数据未打包进 assets 会崩溃，先简化为直接录制，
    //   符合"说打开录音就开始录"的预期；后续如需"是否开启"确认，再补语音数据。）
    {
        LvglLockGuard lock;
        handlePrimaryAction();
    }
}

// ---------------- 历史录音（列表 + 详情 + 删除） ----------------

void AppMeeting::requestPlaybackByIndex(size_t index) {
    // 短按 = 播放 WAV（或查看纪要 .txt）
    if (index < _history_files.size()) {
        _playback_request_path = _history_files[index];
        _playback_requested = true;
    }
}

void AppMeeting::startConvertAll() {
    if (_convert_running.load() || _list_txt_notes) {
        return;  // 个人灵感首页是纪要查看，不提供转纪要按钮
    }
    // 单文件转换：转当前播放的录音（.wav）
    const std::string path = _playback_path;
    if (path.empty() || !(path.size() >= 4 && path.substr(path.size() - 4) == ".wav")) {
        view::pop_a_toast("请先播放要转换的录音", view::ToastType::Warning);
        return;
    }
    // 已转过（有同名 .txt）则不重复转
    const std::string txt = path.substr(0, path.size() - 4) + ".txt";
    {
        SdCardAccessGuard guard;
        struct stat st {};
        if (stat(txt.c_str(), &st) == 0) {
            view::pop_a_toast("这份录音已转过纪要", view::ToastType::Info);
            return;
        }
    }
    if (mibao::getUploadUrl().empty()) {
        view::pop_a_toast("未配置上传地址", view::ToastType::Warning);
        return;
    }
    _convert_running.store(true);
    view::pop_a_toast("开始转纪要", view::ToastType::Info);
    // 后台任务：上传这一个录音，转写后写 SD 卡 .txt
    if (xTaskCreate(
            [](void* arg) {
                auto* app = static_cast<AppMeeting*>(arg);
                app->convertAllTask();
                app->_convert_running.store(false);
                vTaskDelete(nullptr);
            },
            "convert_one", 12288, this, 2, nullptr) != pdPASS) {
        _convert_running.store(false);
        view::pop_a_toast("转换任务启动失败", view::ToastType::Error);
    }
}

void AppMeeting::convertAllTask() {
    const std::string path = _playback_path;
    if (path.empty()) {
        view::pop_a_toast("没有待转换的录音", view::ToastType::Info);
        return;
    }

    stackchan::meeting::UploadSession session;
    session.url = mibao::getUploadUrl();
    session.path = path;
    const auto slash = path.rfind('/');
    session.file_name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    session.type = (_file_prefix.find("personal") != std::string::npos) ? "personal" : "meeting";
    const bool ok = uploadRecordingFile(session);  // 内部含响应解析 + 写 .txt
    view::pop_a_toast(ok ? "转换成功" : "转换失败", ok ? view::ToastType::Info : view::ToastType::Error);
    // 通知主循环刷新纪要列表
    _convert_done_refresh.store(true);
}

void AppMeeting::showNotesDialog(const std::string& path) {
    // 读取纪要 .txt 内容并在对话框展示
    std::string content;
    {
        SdCardAccessGuard guard;
        FILE* f = std::fopen(path.c_str(), "rb");
        if (f == nullptr) {
            view::pop_a_toast("无法打开纪要", view::ToastType::Error);
            return;
        }
        std::fseek(f, 0, SEEK_END);
        const long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (sz > 0 && sz < 8192) {
            content.resize(static_cast<size_t>(sz));
            const size_t got = std::fread(&content[0], 1, static_cast<size_t>(sz), f);
            content.resize(got);
        }
        std::fclose(f);
    }
    if (content.empty()) {
        view::pop_a_toast("纪要为空", view::ToastType::Warning);
        return;
    }

    LvglLockGuard lock;
    lv_obj_t* dlg = lv_obj_create(lv_screen_active());
    lv_obj_set_size(dlg, 300, 220);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(kPanelSoftColor), 0);
    lv_obj_set_style_border_width(dlg, 2, 0);
    lv_obj_set_style_border_color(dlg, lv_color_hex(0x2DBE8D), 0);
    lv_obj_set_style_radius(dlg, 8, 0);
    lv_obj_set_style_pad_all(dlg, 6, 0);

    lv_obj_t* title = create_label(dlg, "会议纪要", &mibao_zh_font_16, 0x2DBE8D);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    // 正文：独立可滚动容器（固定在标题下方、按钮上方），
    // 这样长纪要滚动时关闭按钮固定在底部不跟着滚。
    lv_obj_t* body_area = lv_obj_create(dlg);
    lv_obj_set_size(body_area, 280, 150);
    lv_obj_align(body_area, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_set_style_bg_opa(body_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body_area, 0, 0);
    lv_obj_set_style_pad_all(body_area, 2, 0);
    lv_obj_set_style_radius(body_area, 0, 0);
    lv_obj_set_scroll_dir(body_area, LV_DIR_VER);
    lv_obj_add_flag(body_area, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* body = lv_label_create(body_area);
    lv_label_set_text(body, content.c_str());
    lv_obj_set_style_text_font(body, &mibao_zh_font_16, 0);
    // 深色文字，浅色背景上清晰可读
    lv_obj_set_style_text_color(body, lv_color_hex(0x273238), 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, 270);

    lv_obj_t* btn_close = create_button(dlg, "关闭", 0x2DBE8D, &mibao_zh_font_16);
    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(btn_close,
                        [](lv_event_t* e) {
                            lv_obj_t* obj = lv_event_get_target_obj(e);
                            lv_obj_delete(lv_obj_get_parent(obj));
                        },
                        LV_EVENT_CLICKED, nullptr);

    // 外层对话框本身不滚动（正文在 body_area 里滚，按钮固定）
    lv_obj_clear_flag(dlg, LV_OBJ_FLAG_SCROLLABLE);
}

void AppMeeting::applyUiMode() {
    // 调用方需持有 LVGL 锁
    const bool history = _ui_mode == UiMode::HistoryList;
    const bool playback = _ui_mode == UiMode::Playback;
    if (_history_panel != nullptr) {
        if (history || playback) {
            lv_obj_clear_flag(_history_panel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_history_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_history_list != nullptr) {
        if (history) {
            lv_obj_clear_flag(_history_list, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_history_list, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_history_back_btn != nullptr) {
        // 个人灵感首页就是纪要列表，返回按钮多余（退出用 home 键）
        if (history && !_list_txt_notes) {
            lv_obj_clear_flag(_history_back_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_history_back_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_playback_btn != nullptr) {
        if (playback) {
            lv_obj_clear_flag(_playback_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_playback_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_playback_progress != nullptr) {
        if (playback) {
            lv_obj_clear_flag(_playback_progress, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_playback_progress, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_playback_toggle_btn != nullptr) {
        if (playback) {
            lv_obj_clear_flag(_playback_toggle_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_playback_toggle_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_playback_time_label != nullptr) {
        if (playback) {
            lv_obj_clear_flag(_playback_time_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_playback_time_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_playback_delete_btn != nullptr) {
        if (playback) {
            lv_obj_clear_flag(_playback_delete_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_playback_delete_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_playback_convert_btn != nullptr) {
        // 播放页"转纪要"：仅在播放录音(.wav)时显示；个人灵感播放纪要(.txt)时隐藏
        if (playback && !_list_txt_notes) {
            lv_obj_clear_flag(_playback_convert_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_playback_convert_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    const bool show_main = _ui_mode == UiMode::Main;
    if (_primary_button != nullptr) {
        if (show_main) {
            lv_obj_clear_flag(_primary_button, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_primary_button, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_end_button != nullptr) {
        if (show_main) {
            lv_obj_clear_flag(_end_button, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_end_button, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_history_button != nullptr) {
        if (show_main) {
            lv_obj_clear_flag(_history_button, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_history_button, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void AppMeeting::enterHistoryList() {
    // 1) 扫描 SD 卡（一次性在 SD 访问保护内完成，避免与摄像头/UI 争用）
    struct FileInfo {
        std::string path;
        std::string size_display;
    };
    std::vector<FileInfo> files;
    {
        SdCardAccessGuard sd_guard;
        DIR* dir = opendir(_root_dir.c_str());
        if (dir != nullptr) {
            while (struct dirent* ent = readdir(dir)) {
                const std::string name = ent->d_name;
                const std::string prefix = _file_prefix;
                // _list_txt_notes：个人灵感显示纪要 .txt（与录音同名前缀）；
                // 否则会议录音显示 .wav
                const std::string suffix = _list_txt_notes ? ".txt" : ".wav";
                if (name.size() > 4 && name.compare(0, prefix.size(), prefix) == 0 &&
                    name.compare(name.size() - 4, 4, suffix) == 0) {
                    FileInfo info;
                    info.path = _root_dir + "/" + name;
                    struct stat st {};
                    if (stat(info.path.c_str(), &st) == 0 && st.st_size > 0) {
                        char sizebuf[24] = {0};
                        if (st.st_size > 1024) {
                            std::snprintf(sizebuf, sizeof(sizebuf), "  %lu.%luKB",
                                          (unsigned long)(st.st_size / 1024),
                                          (unsigned long)((st.st_size % 1024) / 102));
                        }
                        info.size_display = sizebuf;
                    }
                    files.push_back(std::move(info));
                }
            }
            closedir(dir);
        }
    }
    // 文件名含时间戳，倒序 = 新的在前
    std::sort(files.begin(), files.end(),
              [](const FileInfo& a, const FileInfo& b) { return a.path > b.path; });
    _history_files.clear();
    for (const auto& f : files) {
        _history_files.push_back(f.path);
    }

    {
        LvglLockGuard lock;
        if (_history_list != nullptr) {
            lv_obj_clean(_history_list);
            if (files.empty()) {
                const char* empty_text = _list_txt_notes ? "暂无纪要" : "暂无录音";
                // 空状态用普通文本（深色字），不用按钮——按钮浅底浅字看不见
                auto* item = create_label(_history_list, empty_text, &mibao_zh_font_16, 0x273238);
                lv_obj_set_style_text_color(item, lv_color_hex(0x273238), 0);
                lv_obj_align(item, LV_ALIGN_TOP_MID, 0, 12);
            } else {
                for (size_t i = 0; i < files.size(); i++) {
                    const FileInfo& info = files[i];
                    // <前缀>YYYYMMDD_HHMMSS.wav -> "YY-MM-DD HH:MM"（含日期）
                    const size_t slash = info.path.rfind('/');
                    const std::string name =
                        (slash == std::string::npos) ? info.path : info.path.substr(slash + 1);
                    // 前缀长度后是 YYYYMMDD_HHMMSS（14 位时间戳）
                    const size_t ts = _file_prefix.size();
                    char display[64] = {0};
                    if (name.size() >= ts + 14) {
                        std::snprintf(display, sizeof(display), "%.2s-%.2s-%.2s %.2s:%.2s%s",
                                      name.c_str() + ts + 2, name.c_str() + ts + 4,
                                      name.c_str() + ts + 6, name.c_str() + ts + 9,
                                      name.c_str() + ts + 11, info.size_display.c_str());
                    } else {
                        std::snprintf(display, sizeof(display), "%s%s", name.c_str(),
                                      info.size_display.c_str());
                    }
                    // 高对比度：浅底 + 深色文字，避免与背景混淆
                    auto* item = create_button(_history_list, display, 0x2E353C, &mibao_zh_font_16);
                    lv_obj_set_size(item, 284, 36);
                    lv_obj_set_style_radius(item, 6, 0);
                    lv_obj_set_user_data(item, (void*)(intptr_t)i);
                    // 文字用纯白，提高可读性
                    if (auto* label = lv_obj_get_child(item, 0); label != nullptr) {
                        lv_obj_set_style_text_color(static_cast<lv_obj_t*>(label),
                                                    lv_color_hex(0xFFFFFF), 0);
                    }
                    // 按下记录起点（用于区分滑动与点击/长按）
                    lv_obj_add_event_cb(item, [](lv_event_t* event) {
                        auto* app = static_cast<AppMeeting*>(lv_event_get_user_data(event));
                        if (!app) {
                            return;
                        }
                        lv_indev_t* indev = lv_indev_active();
                        lv_point_t pt;
                        lv_indev_get_point(indev, &pt);
                        app->_press_start_x = pt.x;
                        app->_press_start_y = pt.y;
                        app->_press_moved = false;
                        app->_long_pressed = false;
                    }, LV_EVENT_PRESSED, this);
                    // 移动检测：位移超阈值视为滑动
                    lv_obj_add_event_cb(item, [](lv_event_t* event) {
                        auto* app = static_cast<AppMeeting*>(lv_event_get_user_data(event));
                        if (!app) {
                            return;
                        }
                        lv_indev_t* indev = lv_indev_active();
                        lv_point_t pt;
                        lv_indev_get_point(indev, &pt);
                        if (std::abs(pt.x - app->_press_start_x) > 12 ||
                            std::abs(pt.y - app->_press_start_y) > 12) {
                            app->_press_moved = true;
                        }
                    }, LV_EVENT_PRESSING, this);
                    // 短按播放
                    lv_obj_add_event_cb(item, [](lv_event_t* event) {
                        auto* app = static_cast<AppMeeting*>(lv_event_get_user_data(event));
                        lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(event));
                        if (app && obj) {
                            // 长按松手后也会发 CLICKED，此时已请求删除，忽略播放；
                            // 滑动（位移超阈值）也忽略
                            if (app->_long_pressed.exchange(false) || app->_press_moved.load()) {
                                return;
                            }
                            app->requestPlaybackByIndex((size_t)(intptr_t)lv_obj_get_user_data(obj));
                        }
                    }, LV_EVENT_CLICKED, this);
                    // 长按删除（抑制随之而来的 CLICKED；滑动不算长按）
                    lv_obj_add_event_cb(item, [](lv_event_t* event) {
                        auto* app = static_cast<AppMeeting*>(lv_event_get_user_data(event));
                        lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(event));
                        if (app && obj) {
                            if (app->_press_moved.load()) {
                                return;
                            }
                            app->_long_pressed = true;
                            const size_t index = (size_t)(intptr_t)lv_obj_get_user_data(obj);
                            if (index < app->_history_files.size()) {
                                app->requestDeleteConfirm(app->_history_files[index]);
                            }
                        }
                    }, LV_EVENT_LONG_PRESSED, this);
                }
            }
        }
        _ui_mode = UiMode::HistoryList;
        applyUiMode();
    }
    mclog::tagInfo(getAppInfo().name, "history list, {} files", (unsigned)_history_files.size());
}

void AppMeeting::exitHistoryList() {
    {
        LvglLockGuard lock;
        _ui_mode = UiMode::Main;
        applyUiMode();
    }
}

void AppMeeting::showConfirmDialog(const std::string& path) {
    LvglLockGuard lock;
    if (_confirm_label != nullptr) {
        const size_t slash = path.rfind('/');
        const std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
        lv_label_set_text_fmt(_confirm_label, "删除 %s？", name.c_str());
    }
    if (_confirm_panel != nullptr) {
        lv_obj_clear_flag(_confirm_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(_confirm_panel);
    }
}

void AppMeeting::hideConfirmDialog() {
    LvglLockGuard lock;
    if (_confirm_panel != nullptr) {
        lv_obj_add_flag(_confirm_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

void AppMeeting::doDeleteFile(const std::string& path) {
    if (path.empty()) {
        return;
    }
    // 若正在回放该文件，先停止回放
    if (_playback_path == path) {
        stopPlayback();
    }
    bool removed = false;
    {
        SdCardAccessGuard sd_guard;
        removed = remove(path.c_str()) == 0;
    }
    mclog::tagInfo(getAppInfo().name, "delete {}: {}", path.c_str(), removed ? "ok" : "failed");
    if (removed) {
        // 从历史列表移除并重建（若当前在历史列表界面）
        if (_ui_mode == UiMode::HistoryList) {
            enterHistoryList();
        }
    }
}

// ---------------- WAV 回放 ----------------

void AppMeeting::startPlayback(const std::string& path) {
    stopPlayback();
    FILE* f = nullptr;
    long data_pos = 0;
    long data_end = 0;
    uint32_t sample_rate = 16000;
    {
        SdCardAccessGuard sd_guard;
        f = std::fopen(path.c_str(), "rb");
        if (f == nullptr) {
            mclog::tagError(getAppInfo().name, "open playback failed: {}", path.c_str());
            return;
        }
        // 解析 WAV：找 fmt 的采样率 与 data chunk 的位置/大小
        uint8_t hdr[12];
        if (std::fread(hdr, 1, 12, f) != 12) {
            std::fclose(f);
            return;
        }
        bool found = false;
        uint8_t chunk[8];
        while (std::fread(chunk, 1, 8, f) == 8) {
            const uint32_t size = (uint32_t)chunk[4] | ((uint32_t)chunk[5] << 8) |
                                  ((uint32_t)chunk[6] << 16) | ((uint32_t)chunk[7] << 24);
            if (chunk[0] == 'f' && chunk[1] == 'm' && chunk[2] == 't' && chunk[3] == ' ') {
                // fmt chunk: 采样率在偏移 8 处（audio_format 2 + channels 2 + sample_rate 4）
                uint8_t fmt[16];
                long here = ftell(f);
                if (std::fread(fmt, 1, 16, f) == 16) {
                    sample_rate = (uint32_t)fmt[8] | ((uint32_t)fmt[9] << 8) |
                                  ((uint32_t)fmt[10] << 16) | ((uint32_t)fmt[11] << 24);
                }
                std::fseek(f, here + size + (size & 1), SEEK_SET);
                continue;
            }
            if (chunk[0] == 'd' && chunk[1] == 'a' && chunk[2] == 't' && chunk[3] == 'a') {
                data_pos = ftell(f);
                data_end = data_pos + size;
                found = true;
                break;
            }
            // 跳过其它 chunk（偶数字节对齐）
            std::fseek(f, size + (size & 1), SEEK_CUR);
        }
        if (!found) {
            std::fclose(f);
            mclog::tagError(getAppInfo().name, "no data chunk in {}", path.c_str());
            return;
        }
        std::fseek(f, data_pos, SEEK_SET);
    }
    _playback_file = f;
    _playback_path = path;
    _playback_data_pos = data_pos;
    _playback_data_pos_start = data_pos;
    _playback_data_end = data_end;
    _playback_sample_rate = sample_rate > 0 ? sample_rate : 16000;
    _playback_total_ms = (uint32_t)((uint64_t)(data_end - data_pos) * 1000 /
                                    (_playback_sample_rate * 2));  // 16bit mono
    _playback_paused = false;
    _playback_audio_buf.resize(640);  // 20ms @16k

    // 播放期间暂停唤醒词 + 打开扬声器
    mibao::StandbyWakeWord::Suspend();
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    if (codec != nullptr) {
        // 确保 I2S 通道就绪（esp_codec_dev 需要 codec 已 Start）
        codec->Start();
        if (codec->input_enabled()) {
            codec->EnableInput(false);
        }
        if (!codec->output_enabled()) {
            codec->EnableOutput(true);
        }
        Settings settings("audio", false);
        uint8_t volume = 70;
        volume = settings.GetInt("output_volume", volume);
        if (volume <= 0) {
            volume = 10;
        }
        codec->SetOutputVolume(volume);
    }

    {
        LvglLockGuard lock;
        _ui_mode = UiMode::Playback;
        applyUiMode();
        updatePlaybackToggleButton();
        updatePlaybackProgress();
    }
    mclog::tagInfo(getAppInfo().name, "playback start: {}", path.c_str());
}

void AppMeeting::stopPlayback() {
    if (_playback_file != nullptr) {
        {
            SdCardAccessGuard sd_guard;
            std::fclose(_playback_file);
        }
        _playback_file = nullptr;
        mclog::tagInfo(getAppInfo().name, "playback stopped: {}", _playback_path.c_str());
    }
    _playback_path.clear();
    _playback_audio_buf.clear();
    _playback_data_pos_start = 0;
    _playback_data_end = 0;
    _playback_paused = false;
    // 关闭扬声器 + 恢复麦克风与唤醒词
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    if (codec != nullptr) {
        if (codec->output_enabled()) {
            codec->EnableOutput(false);
        }
        if (!codec->input_enabled()) {
            codec->EnableInput(true);
        }
    }
    mibao::StandbyWakeWord::Resume();
    if (_ui_mode == UiMode::Playback) {
        LvglLockGuard lock;
        _ui_mode = UiMode::HistoryList;
        applyUiMode();
    }
}

void AppMeeting::playbackTick() {
    // 播放期间主循环被音频输出阻塞，按钮事件必须在播放循环内部处理，
    // 否则 onRunning 永远轮不到（返回/暂停/删除）。
    // 滑动召唤期间丢弃误触的按钮点击。
    const bool swipe_active = view::swipe_recently_detected(400);

    if (!swipe_active && _playback_toggle_requested.exchange(false)) {
        _playback_paused = !_playback_paused;
        {
            LvglLockGuard lock;
            updatePlaybackToggleButton();
        }
    } else {
        _playback_toggle_requested = false;
    }
    if (!swipe_active && _back_requested.exchange(false)) {
        stopPlayback();
        return;
    } else {
        _back_requested = false;
    }
    if (_delete_confirmed.exchange(false)) {
        const std::string path = _confirm_path;
        hideConfirmDialog();
        doDeleteFile(path);
        return;
    }
    if (_delete_cancelled.exchange(false)) {
        hideConfirmDialog();
    }
    if (_confirm_visible.exchange(false)) {
        showConfirmDialog(_confirm_path);
    }

    // 处理拖拽 seek（主循环串行）
    if (_playback_seek_requested.exchange(false)) {
        const long total = _playback_data_end - _playback_data_pos_start;
        if (total > 0) {
            long pos = (long)((uint64_t)_playback_seek_value * (uint64_t)total / 1000);
            _playback_data_pos = _playback_data_pos_start + pos;
            if (_playback_file != nullptr) {
                SdCardAccessGuard sd_guard;
                std::fseek(_playback_file, _playback_data_pos, SEEK_SET);
            }
        }
        {
            LvglLockGuard lock;
            updatePlaybackProgress();
        }
    }

    if (_playback_file == nullptr) {
        return;
    }
    if (_playback_paused) {
        return;
    }
    if (_playback_data_pos >= _playback_data_end) {
        // 播完：停在末尾，按钮变「播放」，用户可再点从头播放
        _playback_paused = true;
        if (_playback_file != nullptr) {
            SdCardAccessGuard sd_guard;
            std::fseek(_playback_file, _playback_data_pos_start, SEEK_SET);
        }
        _playback_data_pos = _playback_data_pos_start;
        {
            LvglLockGuard lock;
            updatePlaybackToggleButton();
            updatePlaybackProgress();
        }
        return;
    }
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    if (codec == nullptr || !codec->output_enabled()) {
        return;
    }
    // 每轮播放 40ms 音频（640 样本 @16k）：OutputData 阻塞到 I2S 缓冲
    // 有空位 = 自然限速；单声道直接写（esp_codec_dev 内部处理声道）。
    _playback_audio_buf.resize(640);
    long remaining = _playback_data_end - _playback_data_pos;
    size_t want = 640 * 2;
    if ((long)want > remaining) {
        want = (size_t)remaining;
    }
    size_t samples = want / 2;
    if (samples > 0) {
        size_t n = 0;
        {
            SdCardAccessGuard sd_guard;
            n = std::fread(_playback_audio_buf.data(), 2, samples, _playback_file);
        }
        if (n > 0) {
            _playback_audio_buf.resize(n);
            codec->OutputData(_playback_audio_buf);
            _playback_data_pos += (long)(n * 2);
        }
    }
    {
        LvglLockGuard lock;
        updatePlaybackProgress();
    }
    if (_playback_data_pos >= _playback_data_end) {
        // 播完：停在末尾，按钮变「播放」
        _playback_paused = true;
        if (_playback_file != nullptr) {
            SdCardAccessGuard sd_guard;
            std::fseek(_playback_file, _playback_data_pos_start, SEEK_SET);
        }
        _playback_data_pos = _playback_data_pos_start;
        {
            LvglLockGuard lock;
            updatePlaybackToggleButton();
            updatePlaybackProgress();
        }
    }
}

void AppMeeting::updatePlaybackToggleButton() {
    // 调用方需持有 LVGL 锁
    if (_playback_toggle_btn == nullptr) {
        return;
    }
    auto* label = lv_obj_get_child(_playback_toggle_btn, 0);
    if (label != nullptr) {
        lv_label_set_text(static_cast<lv_obj_t*>(label), _playback_paused ? "播放" : "暂停");
    }
    // 播放/暂停都用绿色系，避免与红色「删除」混淆
    lv_obj_set_style_bg_color(_playback_toggle_btn,
                              lv_color_hex(_playback_paused ? kAccentColor : kAccentDarkColor), 0);
}

void AppMeeting::updatePlaybackProgress() {
    // 调用方需持有 LVGL 锁
    if (_playback_progress != nullptr && _playback_data_end > _playback_data_pos) {
        int value = (int)((uint64_t)(_playback_data_pos - _playback_data_pos_start) * 1000 /
                          (_playback_data_end - _playback_data_pos_start));
        if (value < 0) value = 0;
        if (value > 1000) value = 1000;
        lv_slider_set_value(_playback_progress, value, LV_ANIM_OFF);
    }
    if (_playback_time_label != nullptr) {
        // 时间标签每 500ms 更新一次，避免频繁 set_text 造成闪烁
        const uint32_t now = GetHAL().millis();
        if (now - _playback_last_time_update_ms >= 500 || _playback_paused) {
            _playback_last_time_update_ms = now;
            const uint32_t elapsed_ms =
                (uint32_t)((uint64_t)(_playback_data_pos - _playback_data_pos_start) * 1000 /
                           (_playback_sample_rate * 2));
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%02lu:%02lu / %02lu:%02lu",
                          (unsigned long)(elapsed_ms / 60000), (unsigned long)((elapsed_ms / 1000) % 60),
                          (unsigned long)(_playback_total_ms / 60000),
                          (unsigned long)((_playback_total_ms / 1000) % 60));
            lv_label_set_text(_playback_time_label, buf);
        }
    }
}
