/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "app_mibao_video.h"

#include <apps/common/common.h>
#include <assets/assets.h>
#include <board.h>
#include <hal/board/hal_bridge.h>
#include <audio/audio_codec.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <settings.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <dirent.h>

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_imgfx_color_convert.h>

#include "hal/mibao_wake_word.h"
#include "image_to_jpeg.h"
#include "jpg/jpeg_to_image.h"
#include "../app_meeting/avi_writer.h"

using namespace mooncake;

LV_FONT_DECLARE(mibao_zh_font);
LV_FONT_DECLARE(mibao_zh_font_16);

// SD 卡访问保护（与会议录音一致）：防止与摄像头等其他 SPI 设备总线冲突
class SdCardAccessGuard {
public:
    SdCardAccessGuard() { hal_bridge::board_begin_sdcard_access(); }
    ~SdCardAccessGuard() { hal_bridge::board_end_sdcard_access(); }
};

// 与 app_meeting 一致的配色
namespace {
constexpr std::uint32_t kAccentColor = 0x2DBE8D;
constexpr std::uint32_t kAccentDarkColor = 0x155D4A;
constexpr std::uint32_t kBgColor = 0x101417;
constexpr std::uint32_t kDangerColor = 0xE64B4B;
constexpr uint8_t kPreviewFps = 10;
constexpr uint32_t kPreviewIntervalMs = 1000 / kPreviewFps;
}  // namespace

AppMibaoVideo::AppMibaoVideo() {
    setAppInfo().name = "会议录像";
    // launcher 图标复用米宝会议图（新资源名避免覆盖原生 icon）
    _icon_dsc = assets::get_image("mibao_meeting_150.bin");
    setAppInfo().icon = (void*)&_icon_dsc;
    setAppInfo().userData = (void*)&_theme_color;
}

void AppMibaoVideo::onCreate() {
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppMibaoVideo::onOpen() {
    mclog::tagInfo(getAppInfo().name, "on open");

    _recording = false;
    _avi.reset();
    _video_path.clear();
    _recording_started_ms = 0;
    _recorded_frames = 0;
    _close_requested = false;
    _toggle_requested = false;
    _history_requested = false;
    _back_requested = false;
    _playback_requested = false;
    _playback_request_path.clear();
    _history_files.clear();
    _confirm_visible = false;
    _delete_confirmed = false;
    _delete_cancelled = false;
    _confirm_path.clear();
    _long_pressed = false;
    _toggle_playback_control = false;
    _playback_seek_requested = false;
    _playback_seek_value = 0;
    _ui_mode = UiMode::Preview;

    // 惰性重挂 SD（与会议录音一致）
    if (!hal_bridge::board_sdcard_is_mounted()) {
        hal_bridge::board_sdcard_try_remount();
    }

    // 整个 App 存续期间静默抓帧（待机预览 + 录像都不播快门音），
    // 并开启水平镜像（自拍视角：人对着屏幕，画面方向与直觉一致）
    if (auto* cam = camera(); cam != nullptr) {
        cam->SetPlayShutterSound(false);
        cam->SetHMirror(true);
    }
    setupPreview();

    {
        LvglLockGuard lock;
        createUi();
        view::create_home_indicator([this]() { _close_requested = true; }, kAccentColor, kAccentDarkColor);
        view::create_status_bar(kAccentColor, kAccentDarkColor);
        // 边缘左/右滑 = 返回（播放页回历史 / 历史页回主界面）
        view::set_edge_back_callback([this]() { requestBack(); });
        refreshUi();
    }

    // 语音打开标记：直接开始录像
    runAutoRecordIfRequested();

    // 文件管理点击 .avi 跳转过来：直接播放该视频
    const std::string pending_video = hal_bridge::take_pending_video_playback();
    if (!pending_video.empty()) {
        _ui_mode = UiMode::HistoryList;  // 播放请求需在 HistoryList 模式下处理
        _playback_request_path = pending_video;
        _playback_requested    = true;
    }
}

void AppMibaoVideo::onRunning() {
    // 所有按钮动作（触摸线程）只置标记，统一在本线程（主循环）串行执行，
    // 杜绝并发访问 AVI 文件/摄像头产生的崩溃。
    // 滑动召唤（底部上滑 home / 顶部下滑状态栏）期间丢弃误触的按钮点击。
    const bool swipe_active = view::swipe_recently_detected(400);

    if (!swipe_active && _toggle_requested.exchange(false) && _ui_mode == UiMode::Preview) {
        toggleRecording();
    } else {
        _toggle_requested = false;  // 滑动期间即使误触也清掉标志
    }
    if (!swipe_active && _history_requested.exchange(false) && _ui_mode == UiMode::Preview && !_recording) {
        enterHistoryList();
    } else {
        _history_requested = false;
    }
    if (!swipe_active && _back_requested.exchange(false)) {
        if (_ui_mode == UiMode::Playback) {
            stopPlayback();  // 回放 -> 历史列表
        } else if (_ui_mode == UiMode::HistoryList) {
            exitHistoryList();  // 历史列表 -> 预览
        }
    } else {
        _back_requested = false;
    }
    if (!swipe_active && _playback_requested.exchange(false) && _ui_mode == UiMode::HistoryList &&
        !_playback_request_path.empty()) {
        startPlayback(_playback_request_path);
        _playback_request_path.clear();
    } else {
        _playback_requested = false;
        _playback_request_path.clear();
    }

    // 删除确认弹窗：请求显示 / 取消 / 确认 都由本线程串行处理
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

    // 播放控制：点击播放/暂停按钮切换；拖拽进度条 seek
    if (_toggle_playback_control.exchange(false)) {
        onPlaybackControlTapped();
    }
    if (_playback_seek_requested.exchange(false) && _ui_mode == UiMode::Playback) {
        seekPlayback(_playback_seek_value);
    }

    bool preview_refreshed = false;
    switch (_ui_mode) {
        case UiMode::Preview: {
            if (_recording) {
                captureAndWriteFrame();
                // 录像中每帧同步刷新预览
                preview_refreshed = updatePreview();
            } else if (GetHAL().millis() - _last_idle_capture_ms >= 150) {
                // 待机时中频抓帧刷新预览（约 7fps，兼顾流畅与 CPU 占用）
                _last_idle_capture_ms = GetHAL().millis();
                auto* cam = camera();
                if (cam != nullptr && cam->GrabFrame()) {
                    preview_refreshed = updatePreview();
                }
            }
            break;
        }
        case UiMode::HistoryList:
            break;
        case UiMode::Playback:
            playbackTick();
            break;
    }

    {
        LvglLockGuard lock;
        if (preview_refreshed && _preview != nullptr) {
            lv_obj_invalidate(_preview);
        }
        refreshUi();
        view::update_home_indicator();
        view::update_status_bar();
    }

    if (_close_requested) {
        _close_requested = false;
        close();
    }
}

void AppMibaoVideo::onClose() {
    mclog::tagInfo(getAppInfo().name, "on close");
    stopRecording();
    stopPlayback();
    hideConfirmDialog();

    {
        LvglLockGuard lock;
        view::clear_edge_back_callback();
        destroyUi();
        view::destroy_home_indicator();
        view::destroy_status_bar();
    }
    cleanupPreview();
    // 恢复摄像头默认状态（快门音效 + 非镜像，与板级初始化一致）
    if (auto* cam = camera(); cam != nullptr) {
        cam->SetPlayShutterSound(true);
        cam->SetHMirror(false);
    }
}

StackChanCamera* AppMibaoVideo::camera() {
    return hal_bridge::board_get_camera();
}

uint32_t AppMibaoVideo::audioInputSampleRate() const {
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    if (codec != nullptr && codec->input_sample_rate() > 0) {
        return static_cast<uint32_t>(codec->input_sample_rate());
    }
    return 16000;
}

void AppMibaoVideo::createUi() {
    _root = lv_obj_create(lv_screen_active());
    lv_obj_remove_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(_root, lv_color_hex(kBgColor), 0);
    lv_obj_set_size(_root, 320, 240);
    // 去掉容器默认内边距与边框（LVGL 容器默认带边框，录像界面不想要白框）
    lv_obj_set_style_pad_all(_root, 0, 0);
    lv_obj_set_style_border_width(_root, 0, 0);
    lv_obj_set_style_radius(_root, 0, 0);

    // 摄像头实时预览：全屏 320x240（摄像头原生分辨率，1:1 显示）
    // 状态文字/红点/按钮在其后创建，自然覆盖在画面上方
    _preview = lv_image_create(_root);
    lv_obj_set_size(_preview, 320, 240);
    lv_obj_align(_preview, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_preview, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_preview, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_preview, 0, 0);
    lv_obj_clear_flag(_preview, LV_OBJ_FLAG_CLICKABLE);
    if (_preview_buf != nullptr) {
        lv_image_set_src(_preview, &_preview_dsc);
    }

    // 状态文字（顶部系统状态栏为隐藏式，可占用顶部区域）
    _status_label = lv_label_create(_root);
    lv_label_set_text(_status_label, "就绪");
    lv_obj_set_style_text_font(_status_label, &mibao_zh_font_16, 0);
    lv_obj_align(_status_label, LV_ALIGN_TOP_LEFT, 8, 6);
    lv_obj_set_style_text_color(_status_label, lv_color_hex(0xFFFFFF), 0);

    _duration_label = lv_label_create(_root);
    lv_label_set_text(_duration_label, "00:00");
    lv_obj_set_style_text_font(_duration_label, &mibao_zh_font_16, 0);
    lv_obj_align(_duration_label, LV_ALIGN_TOP_RIGHT, -8, 6);
    lv_obj_set_style_text_color(_duration_label, lv_color_hex(0xFFFFFF), 0);

    // 录音红点（与状态文字同排，顶部）
    _recording_dot = lv_obj_create(_root);
    lv_obj_set_size(_recording_dot, 10, 10);
    lv_obj_set_style_bg_color(_recording_dot, lv_color_hex(kDangerColor), 0);
    lv_obj_set_style_radius(_recording_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(_recording_dot, LV_ALIGN_TOP_MID, -60, 9);
    lv_obj_add_flag(_recording_dot, LV_OBJ_FLAG_HIDDEN);

    // 底部按钮：与会议录音/文件管理统一（54×36、圆角 12、底部 -8）。
    // 放在 228 宽容器里两端均匀分布（与录音 controls_panel 一致）。
    lv_obj_t* btn_row = lv_obj_create(_root);
    // 按钮从左到右紧凑排列（与会议录音一致）：3 按钮 3×54+2×4=170，
    // 2 按钮模式 flex START 自动靠左、间隔 4。
    lv_obj_set_size(btn_row, 170, 36);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_style_pad_column(btn_row, 4, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    // 开始（录制/停止切换）
    _record_btn = lv_button_create(btn_row);
    lv_obj_set_size(_record_btn, 54, 36);
    lv_obj_add_flag(_record_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(_record_btn, lv_color_hex(kAccentColor), 0);
    lv_obj_set_style_radius(_record_btn, 12, 0);
    lv_obj_set_style_border_width(_record_btn, 0, 0);
    _record_btn_label = lv_label_create(_record_btn);
    lv_label_set_text(_record_btn_label, "开始");
    lv_obj_set_style_text_font(_record_btn_label, &mibao_zh_font_16, 0);
    lv_obj_set_style_text_color(_record_btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(_record_btn_label);
    lv_obj_add_event_cb(_record_btn, [](lv_event_t* e) {
        auto* self = static_cast<AppMibaoVideo*>(lv_event_get_user_data(e));
        if (self) {
            // 只置标记，实际切换在 onRunning（主循环线程）处理
            self->requestToggle();
        }
    }, LV_EVENT_CLICKED, this);

    // 历史录像入口（与开始按钮同排，两端分布）
    _history_btn = lv_button_create(btn_row);
    lv_obj_set_size(_history_btn, 54, 36);
    lv_obj_add_flag(_history_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(_history_btn, lv_color_hex(kAccentDarkColor), 0);
    lv_obj_set_style_radius(_history_btn, 12, 0);
    lv_obj_set_style_border_width(_history_btn, 0, 0);
    lv_obj_t* history_label = lv_label_create(_history_btn);
    lv_label_set_text(history_label, "历史");
    lv_obj_set_style_text_font(history_label, &mibao_zh_font_16, 0);
    lv_obj_set_style_text_color(history_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(history_label);
    lv_obj_add_event_cb(_history_btn, [](lv_event_t* e) {
        auto* self = static_cast<AppMibaoVideo*>(lv_event_get_user_data(e));
        if (self) {
            self->requestHistory();
        }
    }, LV_EVENT_CLICKED, this);

    // 回放中的返回按钮（默认隐藏；与主界面按钮同容器，两端分布）
    _playback_btn = lv_button_create(btn_row);
    lv_obj_set_size(_playback_btn, 54, 36);
    lv_obj_add_flag(_playback_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(_playback_btn, lv_color_hex(kAccentColor), 0);
    lv_obj_set_style_radius(_playback_btn, 12, 0);
    lv_obj_set_style_border_width(_playback_btn, 0, 0);
    lv_obj_t* playback_label = lv_label_create(_playback_btn);
    lv_label_set_text(playback_label, "返回");
    lv_obj_set_style_text_font(playback_label, &mibao_zh_font_16, 0);
    lv_obj_set_style_text_color(playback_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(playback_label);
    lv_obj_add_flag(_playback_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_playback_btn, [](lv_event_t* e) {
        auto* self = static_cast<AppMibaoVideo*>(lv_event_get_user_data(e));
        if (self) {
            self->requestBack();
        }
    }, LV_EVENT_CLICKED, this);

    // ---- 播放器控件（底部进度条 + 时间 + 三按钮）----
    // 进度条（slider，可拖拽）：底部按钮上方
    _playback_progress = lv_slider_create(_root);
    lv_obj_set_size(_playback_progress, 280, 12);
    lv_obj_align(_playback_progress, LV_ALIGN_BOTTOM_MID, 0, -52);
    lv_obj_set_style_bg_color(_playback_progress, lv_color_hex(0x3A4249), 0);
    lv_obj_set_style_bg_color(_playback_progress, lv_color_hex(kAccentColor), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_playback_progress, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_slider_set_range(_playback_progress, 0, 1000);
    lv_slider_set_value(_playback_progress, 0, LV_ANIM_OFF);
    lv_obj_add_flag(_playback_progress, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_playback_progress, [](lv_event_t* e) {
        auto* self = static_cast<AppMibaoVideo*>(lv_event_get_user_data(e));
        if (self && lv_event_get_code(e) == LV_EVENT_RELEASED) {
            self->requestPlaybackSeek((int)lv_slider_get_value(
                static_cast<lv_obj_t*>(lv_event_get_target(e))));
        }
    }, LV_EVENT_RELEASED, this);

    // 时间标签（进度条上方）
    _playback_time_label = lv_label_create(_root);
    lv_label_set_text(_playback_time_label, "00:00 / 00:00");
    lv_obj_set_style_text_font(_playback_time_label, &mibao_zh_font_16, 0);
    lv_obj_set_style_text_color(_playback_time_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_playback_time_label, LV_ALIGN_BOTTOM_MID, 0, -70);
    lv_obj_add_flag(_playback_time_label, LV_OBJ_FLAG_HIDDEN);

    // 播放/暂停切换按钮（底部中间，与返回/删除齐平）
    _playback_toggle_btn = lv_button_create(btn_row);
    lv_obj_set_size(_playback_toggle_btn, 54, 36);
    lv_obj_add_flag(_playback_toggle_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(_playback_toggle_btn, lv_color_hex(kAccentColor), 0);
    lv_obj_set_style_radius(_playback_toggle_btn, 12, 0);
    lv_obj_set_style_border_width(_playback_toggle_btn, 0, 0);
    _playback_toggle_btn_label = lv_label_create(_playback_toggle_btn);
    lv_label_set_text(_playback_toggle_btn_label, "播放");
    lv_obj_set_style_text_font(_playback_toggle_btn_label, &mibao_zh_font_16, 0);
    lv_obj_set_style_text_color(_playback_toggle_btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(_playback_toggle_btn_label);
    lv_obj_add_flag(_playback_toggle_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_playback_toggle_btn, [](lv_event_t* e) {
        auto* self = static_cast<AppMibaoVideo*>(lv_event_get_user_data(e));
        if (self) {
            self->requestTogglePlaybackControl();
        }
    }, LV_EVENT_CLICKED, this);

    // 历史列表面板（全屏覆盖，默认隐藏；进入时填充）
    _history_panel = lv_obj_create(_root);
    lv_obj_remove_flag(_history_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(_history_panel, 320, 240);
    lv_obj_align(_history_panel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_history_panel, lv_color_hex(kBgColor), 0);
    lv_obj_set_style_bg_opa(_history_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_history_panel, 0, 0);
    lv_obj_set_style_radius(_history_panel, 0, 0);
    // 去掉容器默认内边距，让底部返回按钮与右下角 home 键真正齐平
    lv_obj_set_style_pad_all(_history_panel, 0, 0);
    lv_obj_add_flag(_history_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* history_title = lv_label_create(_history_panel);
    lv_label_set_text(history_title, "历史录像");
    // 与会议录音历史界面字体一致（16px）
    lv_obj_set_style_text_font(history_title, &mibao_zh_font_16, 0);
    lv_obj_set_style_text_color(history_title, lv_color_hex(0xFFFFFF), 0);
    // 顶部状态栏为隐藏式，标题上移到顶部
    lv_obj_align(history_title, LV_ALIGN_TOP_LEFT, 12, 6);

    // 历史列表容器（LVGL 的 lv_list 控件被工程禁用，用可滚动容器 + 行按钮自绘）
    _history_list = lv_obj_create(_history_panel);
    lv_obj_set_size(_history_list, 296, 168);
    lv_obj_align(_history_list, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_color(_history_list, lv_color_hex(0x1A2024), 0);
    lv_obj_set_style_border_width(_history_list, 0, 0);
    lv_obj_set_style_radius(_history_list, 8, 0);
    lv_obj_set_style_pad_all(_history_list, 6, 0);
    // 纵向 flex：行按钮自上而下排列，不重叠
    lv_obj_set_flex_flow(_history_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_history_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    lv_obj_t* back_btn = lv_button_create(_history_panel);
    lv_obj_set_size(back_btn, 54, 36);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(kAccentColor), 0);
    lv_obj_set_style_radius(back_btn, 12, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "返回");
    lv_obj_set_style_text_font(back_label, &mibao_zh_font_16, 0);
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        auto* self = static_cast<AppMibaoVideo*>(lv_event_get_user_data(e));
        if (self) {
            self->requestBack();
        }
    }, LV_EVENT_CLICKED, this);

    // ---- 回放界面的删除按钮（右下角，与返回/播放齐平，仅回放模式显示） ----
    _playback_delete_btn = lv_button_create(btn_row);
    lv_obj_set_size(_playback_delete_btn, 54, 36);
    lv_obj_add_flag(_playback_delete_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(_playback_delete_btn, lv_color_hex(kDangerColor), 0);
    lv_obj_set_style_radius(_playback_delete_btn, 12, 0);
    lv_obj_set_style_border_width(_playback_delete_btn, 0, 0);
    lv_obj_t* del_label = lv_label_create(_playback_delete_btn);
    lv_label_set_text(del_label, "删除");
    lv_obj_set_style_text_font(del_label, &mibao_zh_font_16, 0);
    lv_obj_set_style_text_color(del_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(del_label);
    lv_obj_add_flag(_playback_delete_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_playback_delete_btn, [](lv_event_t* e) {
        auto* self = static_cast<AppMibaoVideo*>(lv_event_get_user_data(e));
        if (self) {
            self->requestDeleteConfirm(self->_playback_path);
        }
    }, LV_EVENT_CLICKED, this);

    // ---- 删除确认弹窗（覆盖层 + 面板 + 取消/删除） ----
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

    _confirm_label = lv_label_create(dialog);
    lv_label_set_text(_confirm_label, "删除这条录像？");
    lv_obj_set_style_text_font(_confirm_label, &mibao_zh_font_16, 0);
    lv_obj_set_style_text_color(_confirm_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_confirm_label, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t* cancel_btn = lv_button_create(dialog);
    lv_obj_set_size(cancel_btn, 100, 36);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 12, -12);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x3A4249), 0);
    lv_obj_set_style_radius(cancel_btn, 10, 0);
    lv_obj_set_style_border_width(cancel_btn, 0, 0);
    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "取消");
    lv_obj_set_style_text_font(cancel_label, &mibao_zh_font_16, 0);
    lv_obj_set_style_text_color(cancel_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
        auto* self = static_cast<AppMibaoVideo*>(lv_event_get_user_data(e));
        if (self) {
            self->requestDeleteCancel();
        }
    }, LV_EVENT_CLICKED, this);

    _confirm_delete_btn = lv_button_create(dialog);
    lv_obj_set_size(_confirm_delete_btn, 100, 36);
    lv_obj_align(_confirm_delete_btn, LV_ALIGN_BOTTOM_RIGHT, -12, -12);
    lv_obj_set_style_bg_color(_confirm_delete_btn, lv_color_hex(kDangerColor), 0);
    lv_obj_set_style_radius(_confirm_delete_btn, 10, 0);
    lv_obj_set_style_border_width(_confirm_delete_btn, 0, 0);
    lv_obj_t* confirm_label = lv_label_create(_confirm_delete_btn);
    lv_label_set_text(confirm_label, "删除");
    lv_obj_set_style_text_font(confirm_label, &mibao_zh_font_16, 0);
    lv_obj_set_style_text_color(confirm_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(confirm_label);
    lv_obj_add_event_cb(_confirm_delete_btn, [](lv_event_t* e) {
        auto* self = static_cast<AppMibaoVideo*>(lv_event_get_user_data(e));
        if (self) {
            self->requestDeleteConfirmAction();
        }
    }, LV_EVENT_CLICKED, this);
}

void AppMibaoVideo::destroyUi() {
    if (_preview_timer) {
        lv_timer_delete(_preview_timer);
        _preview_timer = nullptr;
    }
    if (_root) {
        lv_obj_delete(_root);
        _root = nullptr;
    }
    // 确认弹窗挂在 screen_active 上（非 _root），单独清理
    if (_confirm_panel) {
        lv_obj_delete(_confirm_panel);
        _confirm_panel = nullptr;
    }
    _preview = nullptr;
    _record_btn = nullptr;
    _record_btn_label = nullptr;
    _history_btn = nullptr;
    _playback_btn = nullptr;
    _playback_delete_btn = nullptr;
    _playback_toggle_btn = nullptr;
    _playback_toggle_btn_label = nullptr;
    _playback_progress = nullptr;
    _playback_time_label = nullptr;
    _confirm_label = nullptr;
    _confirm_delete_btn = nullptr;
    _status_label = nullptr;
    _duration_label = nullptr;
    _recording_dot = nullptr;
    _history_panel = nullptr;
    _history_list = nullptr;
}

void AppMibaoVideo::showStatusMessage(const char* text, uint32_t duration_ms) {
    _status_message = text;
    _status_message_until_ms = GetHAL().millis() + duration_ms;
}

void AppMibaoVideo::refreshUi() {
    if (_ui_mode == UiMode::Playback) {
        if (_status_label) {
            lv_label_set_text(_status_label, "回放中");
        }
        if (_record_btn_label) {
            lv_label_set_text(_record_btn_label, "开始");
        }
        return;
    }
    if (_ui_mode == UiMode::HistoryList) {
        return;
    }
    if (_status_label) {
        // 临时消息优先显示，超时后恢复
        if (GetHAL().millis() < _status_message_until_ms && !_status_message.empty()) {
            lv_label_set_text(_status_label, _status_message.c_str());
        } else {
            lv_label_set_text(_status_label, _recording ? "录像中" : "就绪");
            _status_message.clear();
        }
    }
    if (_recording_dot) {
        if (_recording) {
            lv_obj_clear_flag(_recording_dot, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_recording_dot, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_record_btn_label) {
        lv_label_set_text(_record_btn_label, _recording ? "停止" : "开始");
    }
    if (_record_btn) {
        lv_obj_set_style_bg_color(_record_btn,
                                  lv_color_hex(_recording ? kDangerColor : kAccentColor), 0);
    }
    // 录像中禁用「历史」入口，避免中途切换
    if (_history_btn) {
        if (_recording) {
            lv_obj_add_state(_history_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(_history_btn, LV_STATE_DISABLED);
        }
    }
    if (_duration_label) {
        if (_recording) {
            uint32_t sec = (GetHAL().millis() - _recording_started_ms) / 1000;
            char buf[16];
            snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
            lv_label_set_text(_duration_label, buf);
        } else {
            lv_label_set_text(_duration_label, "00:00");
        }
    }
}

std::string AppMibaoVideo::makeVideoPath() const {
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
    return "/sdcard/meetings/video_" + std::string(stamp) + ".avi";
}

void AppMibaoVideo::toggleRecording() {
    mclog::tagInfo(getAppInfo().name, "toggleRecording called, current recording={}", _recording);
    if (_recording) {
        stopRecording();
    } else {
        // 开始录像：SD 卡挂载检查 + 打开 AVI
        if (!hal_bridge::board_sdcard_is_mounted()) {
            hal_bridge::board_sdcard_try_remount();
        }
        if (!hal_bridge::board_sdcard_is_mounted()) {
            mclog::tagError(getAppInfo().name, "SD card not mounted");
            showStatusMessage("SD卡未就绪", 3000);
            return;
        }

        auto* cam = camera();
        if (cam == nullptr) {
            mclog::tagError(getAppInfo().name, "no camera");
            showStatusMessage("摄像头不可用", 3000);
            return;
        }

        // 确保录像目录存在（与会议录音一致：/sdcard/meetings）
        {
            SdCardAccessGuard sd_guard;
            struct stat st {};
            if (stat("/sdcard/meetings", &st) != 0) {
                mkdir("/sdcard/meetings", 0775);
            }
        }

        // 摄像头帧尺寸（来自 EspVideo 内部，用标准值；若 Capture 可用则开录）
        _video_path = makeVideoPath();
        _avi = std::make_unique<stackchan::meeting::AviWriter>();
        {
            SdCardAccessGuard sd_guard;
            // 尝试打开带音频的 AVI（PCM 流，采样率取 codec 实际输入率）
            const uint32_t sample_rate = audioInputSampleRate();
            if (!_avi->openWithAudio(_video_path, 320, 240, kPreviewFps, sample_rate)) {
                mclog::tagError(getAppInfo().name, "failed to open avi");
                _avi.reset();
                showStatusMessage("存储写入失败", 3000);
                return;
            }
        }

        // 音频采集（与会议录音同机制；owner="video" 供唤醒词让路）
        _audio_source = std::make_unique<stackchan::meeting::StackChanAudioSource>("video");
        _audio_ok = _audio_source->isAcquired();
        if (!_audio_ok) {
            _audio_source.reset();
            mclog::tagWarn(getAppInfo().name, "mic busy, recording video only");
        }

        _recording = true;
        _recording_started_ms = GetHAL().millis();
        _recorded_frames = 0;
        _last_video_ms = 0;
        mclog::tagInfo(getAppInfo().name, "recording started: {} (audio={})", _video_path.c_str(), (int)_audio_ok);
    }
}

void AppMibaoVideo::stopRecording() {
    if (!_recording && !_avi) {
        return;
    }
    _recording = false;
    // 释放麦克风（StackChanAudioSource 析构会 releaseMicrophone）
    _audio_source.reset();
    _audio_ok = false;
    _audio_pending.clear();
    _audio_write_buf.clear();
    if (_avi) {
        {
            SdCardAccessGuard sd_guard;
            _avi->close();
        }
        _avi.reset();
    }
    mclog::tagInfo(getAppInfo().name, "recording stopped, frames={}", _recorded_frames);
}

void AppMibaoVideo::captureAndWriteFrame() {
    auto* cam = camera();
    if (cam == nullptr || !_avi) {
        return;
    }

    // 1) 攒音频块：在视频节流窗口内尽可能多读（音频数据实时到达，
    //    视频每 100ms 才抓一帧，期间应攒约 8 块 13.3ms 音频）
    if (_audio_ok && _audio_source) {
        int read_ok = 0;
        for (int i = 0; i < 12; i++) {
            stackchan::meeting::AudioBlock block;
            if (!_audio_source->read(block)) {
                break;
            }
            read_ok++;
            _audio_pending.insert(_audio_pending.end(), block.samples.begin(), block.samples.end());
        }
        if (read_ok == 0) {
            mclog::tagWarn(getAppInfo().name, "audio read: no data this round");
        }
    }

    // 2) 视频节流：按墙钟每约 100ms 抓一帧（约 10fps）
    const uint32_t now_ms = GetHAL().millis();
    if (_last_video_ms != 0 && now_ms - _last_video_ms < 100) {
        return;
    }
    _last_video_ms = now_ms;

    // 3) 抓一帧视频（轻量抓帧，无快门声）
    if (!cam->GrabFrame()) {
        return;
    }
    const uint8_t* frame = cam->GetFrameData();
    size_t len = cam->GetFrameSize();
    if (frame == nullptr || len == 0) {
        return;
    }
    // 摄像头输出 YUYV 原始数据，AVI 声明 MJPG 流 → 先用软件编码器压成 JPEG
    uint8_t* jpeg = nullptr;
    size_t jpeg_len = 0;
    if (!image_to_jpeg(const_cast<uint8_t*>(frame), len, cam->GetFrameWidth(), cam->GetFrameHeight(),
                       static_cast<v4l2_pix_fmt_t>(cam->GetFrameFormat()), 60, &jpeg, &jpeg_len)) {
        mclog::tagError(getAppInfo().name, "jpeg encode failed");
        return;
    }

    // 4) 一次 SD 事务：先写攒下的音频块，再写视频帧（降低 SPI 频率）。
    //    注：不能在这里暂停摄像头流（STREAMOFF/ON 会让 DQBUF 永久阻塞，
    //    录像循环卡死）——CoreS3 上 SD 写入与摄像头并发偶发 0x107 超时，
    //    通过批写 + 降频缓解。
    {
        SdCardAccessGuard sd_guard;
        if (!_audio_pending.empty()) {
            _audio_write_buf.swap(_audio_pending);
            _avi->writeAudio(_audio_write_buf.data(), _audio_write_buf.size());
            _audio_pending.clear();
        }
        if (jpeg != nullptr && _avi->writeFrame(jpeg, jpeg_len)) {
            _recorded_frames++;
        }
    }
    free(jpeg);
}

bool AppMibaoVideo::updatePreview() {
    if (_preview_buf == nullptr) {
        return false;
    }
    auto* cam = camera();
    if (cam == nullptr) {
        return false;
    }
    const uint8_t* frame = cam->GetFrameData();
    const size_t len = cam->GetFrameSize();
    if (frame == nullptr || len == 0) {
        return false;
    }
    const uint32_t fmt = static_cast<uint32_t>(cam->GetFrameFormat());
    if (fmt == 0x56595559 /* V4L2_PIX_FMT_YUYV */ && _color_cvt_handle != nullptr) {
        // YUYV -> RGB565（BT.601），与摄像头内部预览转换同参数
        esp_imgfx_data_t in = {
            .data = const_cast<uint8_t*>(frame),
            .data_len = static_cast<uint32_t>(len),
        };
        esp_imgfx_data_t out = {
            .data = _preview_buf,
            .data_len = _preview_dsc.data_size,
        };
        if (esp_imgfx_color_convert_process(
                static_cast<esp_imgfx_color_convert_handle_t>(_color_cvt_handle), &in, &out) != ESP_IMGFX_ERR_OK) {
            return false;
        }
    } else if (fmt == 0x50424752 /* V4L2_PIX_FMT_RGB565 */) {
        const size_t n = len < _preview_dsc.data_size ? len : _preview_dsc.data_size;
        std::memcpy(_preview_buf, frame, n);
    } else {
        return false;
    }
    return true;
}

bool AppMibaoVideo::setupPreview() {
    auto* cam = camera();
    if (cam == nullptr) {
        return false;
    }
    int w = cam->GetFrameWidth();
    int h = cam->GetFrameHeight();
    if (w <= 0 || h <= 0) {
        w = 320;
        h = 240;
    }
    _preview_buf = static_cast<uint8_t*>(heap_caps_malloc(static_cast<size_t>(w) * h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (_preview_buf == nullptr) {
        mclog::tagError(getAppInfo().name, "preview buffer alloc failed");
        return false;
    }
    std::memset(_preview_buf, 0, static_cast<size_t>(w) * h * 2);

    _preview_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    _preview_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    _preview_dsc.header.w = static_cast<int32_t>(w);
    _preview_dsc.header.h = static_cast<int32_t>(h);
    _preview_dsc.header.stride = static_cast<uint32_t>(w) * 2;
    _preview_dsc.data = _preview_buf;
    _preview_dsc.data_size = static_cast<uint32_t>(w) * h * 2;

    // YUYV -> RGB565 转换器（只开一次，退出时关闭）
    esp_imgfx_color_convert_cfg_t cfg = {};
    cfg.in_res.width = static_cast<int16_t>(w);
    cfg.in_res.height = static_cast<int16_t>(h);
    cfg.in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_YUYV;
    cfg.out_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB565_LE;
    cfg.color_space_std = ESP_IMGFX_COLOR_SPACE_STD_BT601;
    esp_imgfx_color_convert_handle_t handle = nullptr;
    if (esp_imgfx_color_convert_open(&cfg, &handle) != ESP_IMGFX_ERR_OK || handle == nullptr) {
        mclog::tagError(getAppInfo().name, "color convert open failed");
        _color_cvt_handle = nullptr;
    } else {
        _color_cvt_handle = handle;
    }
    return true;
}

void AppMibaoVideo::cleanupPreview() {
    if (_color_cvt_handle != nullptr) {
        esp_imgfx_color_convert_close(static_cast<esp_imgfx_color_convert_handle_t>(_color_cvt_handle));
        _color_cvt_handle = nullptr;
    }
    if (_preview_buf != nullptr) {
        heap_caps_free(_preview_buf);
        _preview_buf = nullptr;
    }
    _preview_dsc = {};
}

void AppMibaoVideo::clearAutoRecordFlag() {
    Settings settings("mibao", true);
    settings.EraseKey("video_meeting");
}

void AppMibaoVideo::runAutoRecordIfRequested() {
    Settings settings("mibao", false);
    const bool requested = settings.GetInt("video_meeting", 0) == 1;
    if (!requested) {
        return;
    }
    clearAutoRecordFlag();
    mclog::tagInfo(getAppInfo().name, "voice-open requested, start recording directly");
    toggleRecording();
}

// ---------------- 历史列表与回放 ----------------

void AppMibaoVideo::requestPlaybackByIndex(size_t index) {
    if (index < _history_files.size()) {
        _playback_request_path = _history_files[index];
        _playback_requested = true;
    }
}

void AppMibaoVideo::requestDeleteConfirm(const std::string& path) {
    if (path.empty()) {
        return;
    }
    _confirm_path = path;
    _confirm_visible = true;
}

void AppMibaoVideo::requestDeleteFile(const std::string& path) {
    if (path.empty()) {
        return;
    }
    _confirm_path = path;
    _delete_confirmed = true;
}

void AppMibaoVideo::requestDeleteCancel() {
    _delete_cancelled = true;
}

void AppMibaoVideo::requestDeleteConfirmAction() {
    _delete_confirmed = true;
}

void AppMibaoVideo::requestTogglePlaybackControl() {
    _toggle_playback_control = true;
}

void AppMibaoVideo::applyUiMode() {
    // 调用方需持有 LVGL 锁
    if (_history_panel) {
        if (_ui_mode == UiMode::HistoryList) {
            lv_obj_clear_flag(_history_panel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_history_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }
    const bool is_playback = _ui_mode == UiMode::Playback;
    // 播放器控件：仅回放模式显示
    if (_playback_progress) {
        if (is_playback) {
            lv_obj_clear_flag(_playback_progress, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_playback_progress, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_playback_time_label) {
        if (is_playback) {
            lv_obj_clear_flag(_playback_time_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_playback_time_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_playback_toggle_btn) {
        if (is_playback) {
            lv_obj_clear_flag(_playback_toggle_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_playback_toggle_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_playback_btn) {
        if (is_playback) {
            lv_obj_clear_flag(_playback_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_playback_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_playback_delete_btn) {
        if (is_playback) {
            lv_obj_clear_flag(_playback_delete_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_playback_delete_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    const bool show_main_btns = _ui_mode == UiMode::Preview;
    if (_record_btn) {
        if (show_main_btns) {
            lv_obj_clear_flag(_record_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_record_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_history_btn) {
        if (show_main_btns) {
            lv_obj_clear_flag(_history_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_history_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void AppMibaoVideo::enterHistoryList() {
    // 1) 扫描 SD 卡并读取文件大小（一次性在 SD 访问保护内完成，
    //    避免 UI 构建时访问 SD 与摄像头 SPI 总线冲突）
    struct FileInfo {
        std::string path;
        std::string size_display;
    };
    std::vector<FileInfo> files;
    {
        SdCardAccessGuard sd_guard;
        DIR* dir = opendir("/sdcard/meetings");
        if (dir != nullptr) {
            while (struct dirent* ent = readdir(dir)) {
                const std::string name = ent->d_name;
                if (name.size() > 4 && name.compare(0, 6, "video_") == 0 &&
                    name.compare(name.size() - 4, 4, ".avi") == 0) {
                    FileInfo info;
                    info.path = "/sdcard/meetings/" + name;
                    struct stat st {};
                    if (stat(info.path.c_str(), &st) == 0 && st.st_size > 1024) {
                        char sizebuf[24] = {0};
                        std::snprintf(sizebuf, sizeof(sizebuf), "  %lu.%luMB",
                                      (unsigned long)(st.st_size / (1024 * 1024)),
                                      (unsigned long)((st.st_size % (1024 * 1024)) / (1024 * 102)));
                        info.size_display = sizebuf;
                    }
                    files.push_back(std::move(info));
                }
            }
            closedir(dir);
        }
    }
    // 文件名含时间戳，按路径倒序 = 新的在前
    std::sort(files.begin(), files.end(),
              [](const FileInfo& a, const FileInfo& b) { return a.path > b.path; });
    _history_files.clear();
    for (const auto& f : files) {
        _history_files.push_back(f.path);
    }

    {
        LvglLockGuard lock;
        // 2) 重建列表内容（滚动容器 + 每行一个按钮）
        if (_history_list) {
            lv_obj_clean(_history_list);
            if (files.empty()) {
                auto* item = lv_button_create(_history_list);
                lv_obj_set_size(item, 284, 36);
                lv_obj_add_state(item, LV_STATE_DISABLED);
                lv_obj_set_style_bg_color(item, lv_color_hex(0x14181B), 0);
                lv_obj_set_style_border_width(item, 0, 0);
                auto* label = lv_label_create(item);
                lv_label_set_text(label, "暂无录像");
                lv_obj_set_style_text_font(label, &mibao_zh_font_16, 0);
                lv_obj_set_style_text_color(label, lv_color_hex(0x8A9299), 0);
                lv_obj_center(label);
            } else {
                for (size_t i = 0; i < files.size(); i++) {
                    const FileInfo& info = files[i];
                    // 取文件名部分（完整路径形如 /sdcard/meetings/video_YYYYMMDD_HHMMSS.avi）
                    const size_t slash = info.path.rfind('/');
                    const std::string name =
                        (slash == std::string::npos) ? info.path : info.path.substr(slash + 1);
                    // video_YYYYMMDD_HHMMSS.avi -> "YY-MM-DD HH:MM"（含年份）
                    // 下标：0-5 "video_"，6-13 YYYYMMDD，14 "_"，15-20 HHMMSS
                    char display[48] = {0};
                    std::snprintf(display, sizeof(display), "%.2s-%.2s-%.2s %.2s:%.2s%s",
                                  name.c_str() + 8, name.c_str() + 10,
                                  name.c_str() + 12, name.c_str() + 15,
                                  name.c_str() + 17, info.size_display.c_str());
                    auto* item = lv_button_create(_history_list);
                    lv_obj_set_size(item, 284, 36);
                    lv_obj_set_style_bg_color(item, lv_color_hex(0x242B31), 0);
                    lv_obj_set_style_border_width(item, 0, 0);
                    lv_obj_set_style_radius(item, 6, 0);
                    lv_obj_set_user_data(item, (void*)(intptr_t)i);
                    auto* label = lv_label_create(item);
                    lv_label_set_text(label, display);
                    lv_obj_set_style_text_font(label, &mibao_zh_font_16, 0);
                    lv_obj_set_style_text_color(label, lv_color_hex(0xE6EDF3), 0);
                    lv_obj_center(label);
                    lv_obj_add_event_cb(item, [](lv_event_t* e) {
                        auto* self = static_cast<AppMibaoVideo*>(lv_event_get_user_data(e));
                        lv_obj_t* item_obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
                        if (self && item_obj) {
                            // 长按松手后也会发 CLICKED，此时已请求删除，忽略播放
                            if (self->_long_pressed.exchange(false)) {
                                return;
                            }
                            self->requestPlaybackByIndex((size_t)(intptr_t)lv_obj_get_user_data(item_obj));
                        }
                    }, LV_EVENT_CLICKED, this);
                    // 长按弹出删除确认
                    lv_obj_add_event_cb(item, [](lv_event_t* e) {
                        auto* self = static_cast<AppMibaoVideo*>(lv_event_get_user_data(e));
                        lv_obj_t* item_obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
                        if (self && item_obj) {
                            self->_long_pressed = true;
                            const size_t index = (size_t)(intptr_t)lv_obj_get_user_data(item_obj);
                            if (index < self->_history_files.size()) {
                                self->requestDeleteConfirm(self->_history_files[index]);
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

void AppMibaoVideo::exitHistoryList() {
    {
        LvglLockGuard lock;
        _ui_mode = UiMode::Preview;
        applyUiMode();
    }
}

void AppMibaoVideo::startPlayback(const std::string& path) {
    stopPlayback();
    FILE* f = nullptr;
    long movi_pos = -1;
    {
        SdCardAccessGuard sd_guard;
        f = std::fopen(path.c_str(), "rb");
        if (f == nullptr) {
            mclog::tagError(getAppInfo().name, "open playback failed: {}", path.c_str());
            return;
        }
        // 在文件头部找 'movi' 标记（本工程写入的 AVI 头是固定小头部）
        uint8_t buf[4];
        for (long pos = 0; pos + 4 <= 1024; pos++) {
            if (std::fseek(f, pos, SEEK_SET) != 0) {
                break;
            }
            if (std::fread(buf, 1, 4, f) != 4) {
                break;
            }
            if (buf[0] == 'm' && buf[1] == 'o' && buf[2] == 'v' && buf[3] == 'i') {
                movi_pos = pos + 4;
                break;
            }
        }
        if (movi_pos < 0) {
            std::fclose(f);
            mclog::tagError(getAppInfo().name, "no movi in {}", path.c_str());
            return;
        }
        // 统计视频总帧数（扫描 00dc chunk，直到 idx1）
        std::fseek(f, movi_pos, SEEK_SET);
        uint32_t total_frames = 0;
        uint8_t hdr[8];
        while (std::fread(hdr, 1, 8, f) == 8) {
            const uint32_t size = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) |
                                  ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
            if (std::memcmp(hdr, "00dc", 4) == 0) {
                total_frames++;
                std::fseek(f, size + (size & 1), SEEK_CUR);
            } else if (std::memcmp(hdr, "idx1", 4) == 0) {
                break;
            } else {
                std::fseek(f, size + (size & 1), SEEK_CUR);
            }
        }
        _playback_total_frames = total_frames;
        // 回到 movi 起点准备播放
        std::fseek(f, movi_pos, SEEK_SET);
    }
    _playback_file = f;
    _playback_path = path;
    _playback_movi_pos = movi_pos;
    _playback_shown_frames = 0;
    _last_playback_ms = 0;
    _playback_jpeg.clear();
    mclog::tagInfo(getAppInfo().name, "playback open: frames={}", _playback_total_frames);
    // 初始状态：自动播放
    _playback_state = PlaybackState::Playing;
    playbackAudioOpen();
    startAudioPlaybackTask();  // 音频播放独立任务，避免阻塞视频解码
    {
        LvglLockGuard lock;
        _ui_mode = UiMode::Playback;
        applyUiMode();
        updatePlaybackToggleButton();
        updatePlaybackProgress();
    }
    mclog::tagInfo(getAppInfo().name, "playback start: {}", path.c_str());
}

void AppMibaoVideo::stopPlayback() {
    if (_playback_file != nullptr) {
        {
            SdCardAccessGuard sd_guard;
            std::fclose(_playback_file);
        }
        _playback_file = nullptr;
        mclog::tagInfo(getAppInfo().name, "playback end, frames={}", _playback_shown_frames);
    }
    stopAudioPlaybackTask();
    playbackAudioClose();
    _playback_path.clear();
    _playback_jpeg.clear();
    _playback_movi_pos = -1;
    _playback_total_frames = 0;
    _playback_state = PlaybackState::Ready;
    if (_ui_mode == UiMode::Playback) {
        LvglLockGuard lock;
        // 播完/返回都回到历史列表
        _ui_mode = UiMode::HistoryList;
        applyUiMode();
    }
}

// ---------------- 音频输出（回放声音） ----------------

void AppMibaoVideo::playbackAudioOpen() {
    // 回放期间暂停待机唤醒词，避免扬声器声音被麦克风听到误唤醒
    mibao::StandbyWakeWord::Suspend();
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    if (codec == nullptr) {
        return;
    }
    // 确保 I2S 通道就绪（esp_codec_dev 需要 codec 已 Start）
    codec->Start();
    // 关闭麦克风输入通道：唤醒词虽已 Suspend，但其 I2S 输入通道仍占用
    // 全双工总线，导致输出播放被排队（I2S "Pending out channel"），声音卡顿。
    if (codec->input_enabled()) {
        codec->EnableInput(false);
    }
    if (!codec->output_enabled()) {
        codec->EnableOutput(true);
    }
    // 音量跟随系统设置
    uint8_t volume = 70;
    Settings settings("audio", false);
    volume = settings.GetInt("output_volume", volume);
    if (volume <= 0) {
        volume = 10;
    }
    codec->SetOutputVolume(volume);
    mclog::tagInfo(getAppInfo().name, "playback audio open, volume={}", (int)volume);
}

void AppMibaoVideo::playbackAudioClose() {
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    if (codec != nullptr && codec->output_enabled()) {
        codec->EnableOutput(false);
    }
    // 恢复麦克风输入（唤醒词 Suspend 检查通过后会重新读取）
    if (codec != nullptr && !codec->input_enabled()) {
        codec->EnableInput(true);
    }
    mibao::StandbyWakeWord::Resume();
}

void AppMibaoVideo::playbackAudioWrite(const int16_t* samples, size_t count) {
    if (samples == nullptr || count == 0) {
        return;
    }
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    if (codec == nullptr || !codec->output_enabled()) {
        return;
    }
    // 按 codec 单声道输出直接写（esp_codec_dev 内部处理声道）。
    // 注：曾尝试复制成双声道，反而引入噪音；保持单声道。
    // 每小块 OutputData 后让出 CPU（vTaskDelay），避免阻塞占满主循环
    // 导致视频帧解码/画面刷新卡顿。
    constexpr size_t kChunk = 160;  // 10ms @16k
    _audio_playback_buf.resize(kChunk);
    size_t offset = 0;
    while (offset < count) {
        size_t n = count - offset;
        if (n > kChunk) {
            n = kChunk;
        }
        std::memcpy(_audio_playback_buf.data(), samples + offset, n * sizeof(int16_t));
        _audio_playback_buf.resize(n);  // OutputData 按 vector 大小写 n 个样本
        codec->OutputData(_audio_playback_buf);
        offset += n;
        vTaskDelay(pdMS_TO_TICKS(2));  // 让出 CPU，主循环可解码/刷新
    }
    _audio_playback_buf.resize(kChunk);
}

// ---------------- 音频播放任务（视频回放解耦） ----------------

void AppMibaoVideo::audioTaskPush(const std::vector<int16_t>& samples) {
    if (_audio_queue == nullptr || samples.empty()) {
        return;
    }
    // 队列满则丢弃最旧的（保持实时，不阻塞主循环）
    auto* item = new std::vector<int16_t>(samples);
    if (xQueueSend(_audio_queue, &item, 0) != pdTRUE) {
        delete item;
    }
}

void AppMibaoVideo::audioTaskEntry(void* arg) {
    auto* self = static_cast<AppMibaoVideo*>(arg);
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    while (!self->_audio_task_stop.load()) {
        std::vector<int16_t>* item = nullptr;
        if (xQueueReceive(self->_audio_queue, &item, pdMS_TO_TICKS(50)) == pdTRUE && item != nullptr) {
            if (codec != nullptr && codec->output_enabled()) {
                // 分小块播放，每块后短延时（限速 = 自然 10ms 节奏）
                constexpr size_t kChunk = 160;
                size_t offset = 0;
                while (offset < item->size()) {
                    size_t n = item->size() - offset;
                    if (n > kChunk) {
                        n = kChunk;
                    }
                    std::vector<int16_t> sub(item->begin() + offset, item->begin() + offset + n);
                    codec->OutputData(sub);
                    offset += n;
                    vTaskDelay(pdMS_TO_TICKS(4));
                }
                self->_audio_task_processed.fetch_add(1);
            }
            delete item;
        }
    }
    vTaskDelete(nullptr);
}

void AppMibaoVideo::startAudioPlaybackTask() {
    if (_audio_task != nullptr) {
        return;
    }
    _audio_task_stop = false;
    _audio_queue = xQueueCreate(16, sizeof(std::vector<int16_t>*));
    xTaskCreatePinnedToCore(&AppMibaoVideo::audioTaskEntry, "mibao_audio_play", 4096, this, 2, &_audio_task, 1);
}

void AppMibaoVideo::stopAudioPlaybackTask() {
    _audio_task_stop = true;
    if (_audio_task != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(50));
        // 清空队列
        std::vector<int16_t>* item = nullptr;
        while (xQueueReceive(_audio_queue, &item, 0) == pdTRUE) {
            delete item;
        }
        _audio_task = nullptr;
    }
    if (_audio_queue != nullptr) {
        vQueueDelete(_audio_queue);
        _audio_queue = nullptr;
    }
}

void AppMibaoVideo::showConfirmDialog(const std::string& path) {
    LvglLockGuard lock;
    if (_confirm_label != nullptr) {
        // 文件名提示（取 basename）
        const size_t slash = path.rfind('/');
        const std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
        lv_label_set_text_fmt(_confirm_label, "删除 %s？", name.c_str());
    }
    if (_confirm_panel != nullptr) {
        lv_obj_clear_flag(_confirm_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(_confirm_panel);
    }
}

void AppMibaoVideo::hideConfirmDialog() {
    LvglLockGuard lock;
    if (_confirm_panel != nullptr) {
        lv_obj_add_flag(_confirm_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

void AppMibaoVideo::doDeleteFile(const std::string& path) {
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

// ---------------- 播放控制（看电影式） ----------------

void AppMibaoVideo::updatePlaybackToggleButton() {
    // 调用方需持有 LVGL 锁
    if (_playback_toggle_btn == nullptr) {
        return;
    }
    const char* text = "播放";
    std::uint32_t color = kAccentColor;
    if (_playback_state == PlaybackState::Playing) {
        text = "暂停";
        color = kAccentDarkColor;
    }
    if (_playback_toggle_btn_label != nullptr) {
        lv_label_set_text(_playback_toggle_btn_label, text);
    }
    lv_obj_set_style_bg_color(_playback_toggle_btn, lv_color_hex(color), 0);
}

void AppMibaoVideo::updatePlaybackProgress() {
    // 调用方需持有 LVGL 锁
    if (_playback_progress != nullptr && _playback_total_frames > 0) {
        uint32_t shown = _playback_shown_frames;
        if (shown > _playback_total_frames) {
            shown = _playback_total_frames;
        }
        int value = (int)((uint64_t)shown * 1000 / _playback_total_frames);
        lv_slider_set_value(_playback_progress, value, LV_ANIM_OFF);
    }
    if (_playback_time_label != nullptr) {
        const uint32_t elapsed_ms = (uint32_t)_playback_shown_frames * 100;  // 10fps
        const uint32_t total_ms = (uint32_t)_playback_total_frames * 100;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%02lu:%02lu / %02lu:%02lu",
                      (unsigned long)(elapsed_ms / 60000), (unsigned long)((elapsed_ms / 1000) % 60),
                      (unsigned long)(total_ms / 60000), (unsigned long)((total_ms / 1000) % 60));
        lv_label_set_text(_playback_time_label, buf);
    }
}

void AppMibaoVideo::onPlaybackControlTapped() {
    switch (_playback_state) {
        case PlaybackState::Ready:
        case PlaybackState::Finished:
            // 开始/重播：回到 movi 起点从头播放
            if (_playback_file != nullptr && _playback_movi_pos >= 0) {
                SdCardAccessGuard sd_guard;
                std::fseek(_playback_file, _playback_movi_pos, SEEK_SET);
            }
            _playback_shown_frames = 0;
            _last_playback_ms = 0;
            _playback_state = PlaybackState::Playing;
            {
                LvglLockGuard lock;
                updatePlaybackToggleButton();
                updatePlaybackProgress();
            }
            break;
        case PlaybackState::Playing:
            // 播放中点击 -> 暂停，按钮显示「播放」
            _playback_state = PlaybackState::Paused;
            {
                LvglLockGuard lock;
                updatePlaybackToggleButton();
            }
            break;
        case PlaybackState::Paused:
            // 暂停中点击 -> 继续
            _last_playback_ms = 0;
            _playback_state = PlaybackState::Playing;
            {
                LvglLockGuard lock;
                updatePlaybackToggleButton();
            }
            break;
    }
}

void AppMibaoVideo::seekPlayback(int percent) {
    // 按百分比 seek（0-1000）：跳转到对应视频帧，从那里继续播放
    if (_playback_file == nullptr || _playback_total_frames == 0) {
        return;
    }
    const uint32_t target_frame = (uint32_t)((uint64_t)percent * _playback_total_frames / 1000);
    if (_playback_movi_pos >= 0) {
        SdCardAccessGuard sd_guard;
        std::fseek(_playback_file, _playback_movi_pos, SEEK_SET);
        uint32_t frame_seen = 0;
        bool positioned = false;
        uint8_t hdr[8];
        while (std::fread(hdr, 1, 8, _playback_file) == 8) {
            const uint32_t size = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) |
                                  ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
            if (std::memcmp(hdr, "00dc", 4) == 0) {
                if (frame_seen >= target_frame) {
                    // 停在当前帧的 hdr 处，下轮 playbackTick 解码这一帧
                    std::fseek(_playback_file, -8, SEEK_CUR);
                    positioned = true;
                    break;
                }
                frame_seen++;
                std::fseek(_playback_file, size + (size & 1), SEEK_CUR);
            } else if (std::memcmp(hdr, "idx1", 4) == 0) {
                break;  // 到头
            } else {
                std::fseek(_playback_file, size + (size & 1), SEEK_CUR);
            }
        }
        if (!positioned) {
            // 未定位到（如 target 超范围），回到 movi 起点
            std::fseek(_playback_file, _playback_movi_pos, SEEK_SET);
        }
    }
    _playback_shown_frames = target_frame;
    _last_playback_ms = 0;
    _playback_last_frame_ms = 0;
    // 清空音频队列：不续播 seek 前的旧音频
    if (_audio_queue != nullptr) {
        std::vector<int16_t>* item = nullptr;
        while (xQueueReceive(_audio_queue, &item, 0) == pdTRUE) {
            delete item;
        }
    }
    // 从拖动位置继续播放
    _playback_state = PlaybackState::Playing;
    {
        LvglLockGuard lock;
        updatePlaybackToggleButton();
        updatePlaybackProgress();
    }
}

void AppMibaoVideo::finishPlayback() {
    // 播完：停在末尾，按钮变「播放」，用户可再点从头播放
    _playback_state = PlaybackState::Finished;
    if (_playback_file != nullptr && _playback_movi_pos >= 0) {
        SdCardAccessGuard sd_guard;
        std::fseek(_playback_file, _playback_movi_pos, SEEK_SET);
    }
    _playback_shown_frames = 0;
    _last_playback_ms = 0;
    {
        LvglLockGuard lock;
        updatePlaybackToggleButton();
        updatePlaybackProgress();
    }
}

void AppMibaoVideo::playbackTick() {
    if (_playback_file == nullptr) {
        return;
    }
    // 非播放状态（Ready/暂停/播完）不推进
    if (_playback_state != PlaybackState::Playing) {
        return;
    }

    uint8_t hdr[8];
    {
        SdCardAccessGuard sd_guard;
        if (std::fread(hdr, 1, 8, _playback_file) != 8) {
            // 读到末尾 -> 播完
            finishPlayback();
            return;
        }
    }
    const uint32_t size = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) | ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);

    if (std::memcmp(hdr, "00dc", 4) == 0) {
        // 视频帧：读出 JPEG；按 10fps 节拍解码显示（避免解码占满主循环）
        if (size == 0 || size > 512 * 1024) {
            finishPlayback();
            return;
        }
        _playback_jpeg.resize(size);
        {
            SdCardAccessGuard sd_guard;
            if (std::fread(_playback_jpeg.data(), 1, size, _playback_file) != size) {
                finishPlayback();
                return;
            }
            if (size & 1) {
                std::fseek(_playback_file, 1, SEEK_CUR);  // 2 字节对齐填充
            }
        }
        _playback_shown_frames++;
        // 视频节流：距上次解码 >= 100ms 才真正解码显示
        const uint32_t now = GetHAL().millis();
        if (now - _playback_last_frame_ms >= 100) {
            _playback_last_frame_ms = now;
            uint8_t* img = nullptr;
            size_t out_len = 0, w = 0, h = 0, stride = 0;
            if (jpeg_to_image(_playback_jpeg.data(), size, &img, &out_len, &w, &h, &stride) == ESP_OK && img != nullptr) {
                if (_preview_buf != nullptr && w > 0 && h > 0 && stride > 0) {
                    const size_t copy_w = (w < (size_t)_preview_dsc.header.w) ? w : (size_t)_preview_dsc.header.w;
                    const size_t max_h = (size_t)_preview_dsc.header.h;
                    for (size_t y = 0; y < h && y < max_h; y++) {
                        std::memcpy(_preview_buf + y * _preview_dsc.header.stride, img + y * stride, copy_w * 2);
                    }
                }
                heap_caps_free(img);
                {
                    LvglLockGuard lock;
                    if (_preview != nullptr) {
                        lv_obj_invalidate(_preview);
                    }
                    if (_duration_label != nullptr) {
                        char buf[20];
                        std::snprintf(buf, sizeof(buf), "%u 帧", (unsigned)_playback_shown_frames);
                        lv_label_set_text(_duration_label, buf);
                    }
                    updatePlaybackProgress();
                }
            }
        }
    } else if (std::memcmp(hdr, "01wb", 4) == 0) {
        // 音频块：读出 PCM 入队（独立任务播放，不阻塞视频解码）
        if (size == 0 || size > 512 * 1024) {
            return;
        }
        std::vector<int16_t> pcm(size / 2);
        {
            SdCardAccessGuard sd_guard;
            if (std::fread(pcm.data(), 1, size, _playback_file) != size) {
                return;
            }
            if (size & 1) {
                std::fseek(_playback_file, 1, SEEK_CUR);  // 2 字节对齐填充
            }
        }
        audioTaskPush(pcm);
    } else if (std::memcmp(hdr, "idx1", 4) == 0) {
        // 到索引区 = 播放结束
        finishPlayback();
    } else {
        // 其它块跳过
        SdCardAccessGuard sd_guard;
        std::fseek(_playback_file, size + (size & 1), SEEK_CUR);
    }
}
