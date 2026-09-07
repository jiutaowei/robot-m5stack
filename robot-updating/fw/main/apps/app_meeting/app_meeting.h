/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

#pragma once

#include "audio_source.h"
#include "meeting_ui_model.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <lvgl.h>
#include <memory>
#include <mooncake.h>
#include <string>

enum class EspNowRemoteCommand : uint8_t;

// 子类（如 AppMibaoPersonal）可能需要切换到米宝全局中文字体
LV_FONT_DECLARE(mibao_zh_font);

namespace stackchan::meeting {

/**
 * @brief 后台上传会话（堆分配，由 app 与上传 task 共享持有）
 *
 * app 关闭后 task 仍可安全跑完（仅写本结构，不触碰已销毁的 app）；
 * 上传失败不影响本地文件。TODO: 本批不做失败重试队列。
 */
struct UploadSession {
    std::string url;
    std::string path;
    std::string file_name;
    std::string type;  // meeting / personal（由录音文件前缀决定）
    std::atomic<bool> finished{false};
    std::atomic<bool> success{false};
};

}  // namespace stackchan::meeting

/**
 * @brief 会议录音 app（可参数化复用）
 *
 * 构造参数默认值 = 会议录音现行为；子类（如 AppMibaoPersonal）可传入
 * 自己的名称/标题/录音根目录/文件前缀/launcher 图标实现同链路复用。
 * 录音链路（主动轮询采集 + WAV 占位头回填）对所有派生类完全一致。
 */
class AppMeeting : public mooncake::AppAbility {
public:
    explicit AppMeeting(std::string app_name = "会议录音", std::string title = "会议录音",
                        std::string root_dir = "/sdcard/meetings", std::string file_prefix = "meeting_",
                        std::string icon_bin = "mibao_meeting_150.bin");

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

protected:
    stackchan::meeting::MeetingUiModel _model;
    lv_obj_t* _root = nullptr;
    lv_obj_t* _title_label = nullptr;
    lv_obj_t* _state_label = nullptr;
    lv_obj_t* _duration_label = nullptr;
    lv_obj_t* _recording_dot = nullptr;
    lv_obj_t* _network_label = nullptr;
    lv_obj_t* _storage_label = nullptr;
    lv_obj_t* _info_panel = nullptr;
    lv_obj_t* _controls_panel = nullptr;
    lv_obj_t* _primary_button = nullptr;
    lv_obj_t* _primary_button_label = nullptr;
    lv_obj_t* _end_button = nullptr;
    lv_obj_t* _end_button_label = nullptr;
    // 历史录音入口（结束按钮右侧）
    lv_obj_t* _history_button = nullptr;
    lv_obj_t* _history_button_label = nullptr;
    // 历史界面（覆盖层，参考会议录像 App）
    enum class UiMode { Main, HistoryList, Playback };
    UiMode _ui_mode = UiMode::Main;
    lv_obj_t* _history_panel = nullptr;
    lv_obj_t* _history_list = nullptr;
    lv_obj_t* _history_back_btn = nullptr;
    lv_obj_t* _playback_btn = nullptr;
    lv_obj_t* _playback_delete_btn = nullptr;
    lv_obj_t* _playback_convert_btn = nullptr;  // 播放页"转纪要"（删除按钮右侧）
    lv_obj_t* _confirm_panel = nullptr;
    lv_obj_t* _confirm_label = nullptr;
    lv_obj_t* _confirm_delete_btn = nullptr;
    std::vector<std::string> _history_files;
    std::string _confirm_path;
    std::atomic<bool> _long_pressed{false};
    std::atomic<bool> _press_moved{false};  // 按下后发生滑动则抑制点击/长按
    int _press_start_x = 0;
    int _press_start_y = 0;
    std::atomic<bool> _history_requested{false};
    std::atomic<bool> _convert_requested{false};  // 转纪要请求（播放页按钮）
    std::atomic<bool> _convert_running{false};    // 转换进行中
    std::atomic<bool> _convert_done_refresh{false};  // 转换完成需刷新纪要列表
    std::atomic<bool> _back_requested{false};
    std::atomic<bool> _playback_requested{false};
    std::atomic<bool> _confirm_visible{false};
    std::atomic<bool> _delete_confirmed{false};
    std::atomic<bool> _delete_cancelled{false};
    std::string _playback_request_path;
    // WAV 回放状态
    std::FILE* _playback_file = nullptr;
    std::string _playback_path;
    long _playback_data_pos = 0;
    long _playback_data_end = 0;
    long _playback_data_pos_start = 0;
    uint32_t _playback_sample_rate = 16000;
    uint32_t _playback_total_ms = 0;
    std::vector<std::int16_t> _playback_audio_buf;
    std::vector<std::int16_t> _playback_stereo_buf;  // 单声道 -> 双声道转换缓冲
    uint32_t _playback_last_time_update_ms = 0;  // 时间标签刷新节流
    std::atomic<bool> _delete_requested{false};
    // 「最近录音」卡片：中下方独立卡片，避免文件名与顶部时长/状态重叠
    lv_obj_t* _recent_panel = nullptr;
    lv_obj_t* _recent_title_label = nullptr;
    lv_obj_t* _recent_file_label = nullptr;
    lv_obj_t* _recent_duration_label = nullptr;
    std::string _recent_record_name;
    std::string _recent_record_duration;
    std::string _recent_record_path;  // 最近录音完整路径（点击卡片进入播放）
    // 播放器控件（详情页）
    lv_obj_t* _playback_progress = nullptr;   // 进度条（slider）
    lv_obj_t* _playback_toggle_btn = nullptr;  // 播放/暂停切换按钮
    lv_obj_t* _playback_time_label = nullptr;  // 当前时间/总时长
    std::atomic<bool> _playback_toggle_requested{false};
    std::atomic<bool> _playback_seek_requested{false};
    int _playback_seek_value = 0;
    bool _playback_paused = false;
    std::uint32_t _recording_started_ms = 0;
    std::uint32_t _final_duration_ms = 0;
    std::uint64_t _recorded_sample_count = 0;
    std::unique_ptr<stackchan::meeting::StackChanAudioSource> _audio_source;
    std::FILE* _recording_file = nullptr;
    std::string _recording_path;
    std::string _storage_status = "存储：就绪";
    bool _recording_save_failed = false;
    bool _close_requested = false;

    // 后台上传：app 与 FreeRTOS task 通过 shared_ptr 共享会话，app 关闭后
    // task 仍可安全完成；onRunning 轮询 finished 标志并更新 UI 状态。
    std::shared_ptr<stackchan::meeting::UploadSession> _upload_session;
    bool _upload_consumed = false;  // 本轮上传结果是否已被 UI 消费

    // 参数化配置（构造函数注入）
    const std::string _app_title;
    const std::string _root_dir;
    const std::string _file_prefix;
    lv_image_dsc_t _icon_dsc = {};
    std::uint32_t _theme_color = 0x2DBE8D;
    // UI 中文字体：会议录音默认 meeting_zh_font；子类可换 mibao_zh_font
    // （如「个人灵感」标题中的字不在会议子集里）
    const lv_font_t* _ui_font = nullptr;
    // 历史列表显示模式：
    //  false = 录音文件（.wav，点击播放）——会议录音默认
    //  true  = 纪要文件（.txt，点击查看内容）——个人灵感子类使用
    bool _list_txt_notes = false;

    void createUi();
    void destroyUi();
    void refreshUi();
    void handlePrimaryAction();
    void handleEndAction();
    void handleRemoteCommand(EspNowRemoteCommand command);
    void resetFinishedMeeting();
    void finalizeMeeting();
    void stopMeetingTasks();
    bool beginRecording();
    void startUploadIfConfigured();
    void pollUploadResult();
    void captureRecordingFrame();
    bool writeAudioBlock(const stackchan::meeting::AudioBlock& block);
    bool finalizeRecordingFile();
    void abortRecordingFile();
    std::uint32_t elapsedRecordingMs() const;
    std::string wifiLabel() const;
    std::string makeRecordingPath() const;

    // 历史录音（列表 + 播放 + 删除；参考会议录像 App 交互）
    void enterHistoryList();
    void exitHistoryList();
    void showConfirmDialog(const std::string& path);
    void hideConfirmDialog();
    void doDeleteFile(const std::string& path);
    void requestPlaybackByIndex(size_t index);
    void showNotesDialog(const std::string& path);
    void startConvertAll();
    void convertAllTask();
    void requestHistory() { _history_requested = true; }
    void requestConvertAll() { _convert_requested = true; }
    void requestBack() { _back_requested = true; }
    void requestDeleteConfirm(const std::string& path) {
        if (!path.empty()) {
            _confirm_path = path;
            _confirm_visible = true;
        }
    }
    void requestDeleteCancel() { _delete_cancelled = true; }
    void requestDeleteConfirmAction() { _delete_confirmed = true; }
    void requestPlaybackToggle() { _playback_toggle_requested = true; }
    void applyUiMode();
    // WAV 回放
    void startPlayback(const std::string& path);
    void stopPlayback();
    void playbackTick();
    void updatePlaybackToggleButton();
    void updatePlaybackProgress();

    // 语音打开确认：由「米宝米宝」对话中的 MCP 工具触发（voice_meeting 标记），
    // app 打开后播报"已进入会议录制，是否开启？"并监听"是/否"命令词。
    void runVoiceConfirmIfRequested();
    void clearVoiceMeetingFlag();
};
