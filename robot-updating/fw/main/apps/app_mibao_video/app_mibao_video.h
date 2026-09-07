/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * 米宝一号「会议录像」app：
 * - 全屏显示摄像头实时画面（LVGL Image 定时刷新 JPEG）
 * - 开始/停止录像：摄像头帧 → AVI/MJPEG 写入 SD 卡
 * - 语音打开：MCP 工具 self.mibao.open_video 设置标记，打开后直接开始录像
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <lvgl.h>
#include <mooncake.h>

#include "../app_meeting/audio_source.h"
#include "../app_meeting/avi_writer.h"

class AppMibaoVideo : public mooncake::AppAbility {
public:
    AppMibaoVideo();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    // 界面模式：预览（含录像）/ 历史列表 / 回放
    enum class UiMode { Preview, HistoryList, Playback };
    UiMode _ui_mode = UiMode::Preview;

    lv_obj_t* _root = nullptr;
    lv_obj_t* _preview = nullptr;       // 摄像头预览 Image
    lv_obj_t* _record_btn = nullptr;    // 开始/停止
    lv_obj_t* _record_btn_label = nullptr;
    lv_obj_t* _history_btn = nullptr;   // 历史录像入口
    lv_obj_t* _playback_btn = nullptr;  // 回放中的返回按钮
    lv_obj_t* _status_label = nullptr;
    lv_obj_t* _duration_label = nullptr;
    lv_obj_t* _recording_dot = nullptr;

    // 历史列表面板（覆盖全屏）
    lv_obj_t* _history_panel = nullptr;
    lv_obj_t* _history_list = nullptr;

    // 回放控制（播放器式：底部 返回/播放暂停/删除 + 进度条 + 时间）
    enum class PlaybackState { Ready, Playing, Paused, Finished };
    PlaybackState _playback_state = PlaybackState::Ready;
    lv_obj_t* _playback_toggle_btn = nullptr;   // 播放/暂停切换按钮（底部中间）
    lv_obj_t* _playback_toggle_btn_label = nullptr;
    lv_obj_t* _playback_progress = nullptr;     // 进度条（slider，可拖拽）
    lv_obj_t* _playback_time_label = nullptr;   // 当前时间 / 总时长
    std::atomic<bool> _playback_seek_requested{false};
    int _playback_seek_value = 0;
    uint32_t _playback_total_frames = 0;  // 视频总帧数（来自 idx1）
    long _playback_movi_pos = -1;              // movi 数据区起点（重播用）
    std::atomic<bool> _toggle_playback_control{false};

    // 删除确认弹窗（自绘：覆盖层 + 面板 + 取消/删除按钮）
    lv_obj_t* _confirm_panel = nullptr;
    lv_obj_t* _confirm_label = nullptr;
    lv_obj_t* _confirm_delete_btn = nullptr;
    std::string _confirm_path;              // 待确认删除的文件
    std::atomic<bool> _confirm_visible{false};
    std::atomic<bool> _delete_confirmed{false};
    std::atomic<bool> _delete_cancelled{false};

    // 回放界面的删除按钮（右上角，仅回放模式显示）
    lv_obj_t* _playback_delete_btn = nullptr;

    // launcher 图标（复用米宝会议图，实例成员保证地址稳定）
    lv_image_dsc_t _icon_dsc = {};
    std::uint32_t _theme_color = 0x2DBE8D;

    std::unique_ptr<stackchan::meeting::AviWriter> _avi;
    std::string _video_path;
    uint32_t _recording_started_ms = 0;
    uint32_t _recorded_frames = 0;
    bool _recording = false;
    bool _close_requested = false;
    // 录音源（与会议录音一致：acquireMicrophone 协调麦克风占用）
    std::unique_ptr<stackchan::meeting::StackChanAudioSource> _audio_source;
    bool _audio_ok = false;
    // 视频节流：按墙钟每约 100ms 抓一帧（10fps）
    uint32_t _last_video_ms = 0;
    // 音频批写：攒够 8 块（约 106ms）随视频帧一起写 SD，降低 SPI 事务频率
    // （CoreS3 SD 卡 SPI 模式持续高频写入易 0x107 超时）
    std::vector<std::int16_t> _audio_pending;
    std::vector<std::int16_t> _audio_write_buf;
    // 按钮点击（LVGL 触摸线程）只置标记，真正的开始/停止在 onRunning
    // （主循环线程）里执行：避免停止时与正在写帧的循环抢同一个 AVI 文件。
    std::atomic<bool> _toggle_requested{false};

    // 历史与回放（同样：触摸线程只置标记，主循环执行）
    std::atomic<bool> _history_requested{false};
    std::atomic<bool> _back_requested{false};
    std::atomic<bool> _playback_requested{false};
    std::string _playback_request_path;  // 触摸线程写路径后置 flag，主循环消费

    std::vector<std::string> _history_files;  // 新文件在前
    // 长按标记：LVGL 长按松手后仍会发 CLICKED，用此标记抑制随之而来的播放
    std::atomic<bool> _long_pressed{false};

    FILE* _playback_file = nullptr;
    std::string _playback_path;
    std::vector<std::uint8_t> _playback_jpeg;
    std::vector<std::int16_t> _audio_playback_buf;  // 音频输出复用缓冲
    uint32_t _last_playback_ms = 0;
    uint32_t _playback_shown_frames = 0;
    // 音频播放任务：与视频解码解耦（视频回放卡顿根因是音频 OutputData
    // 阻塞主循环；独立任务播放 + 队列传递，主循环只做视频解码）
    TaskHandle_t _audio_task = nullptr;
    QueueHandle_t _audio_queue = nullptr;
    std::atomic<bool> _audio_task_stop{false};
    std::atomic<uint32_t> _audio_task_processed{0};
    // 视频帧时间节流：每帧至少间隔多少 ms（10fps = 100ms）
    uint32_t _playback_last_frame_ms = 0;

    // 摄像头预览刷新
    lv_timer_t* _preview_timer = nullptr;

    // 实时预览：YUYV -> RGB565 转换后的持久缓冲 + LVGL 图像描述符
    uint8_t* _preview_buf = nullptr;
    lv_image_dsc_t _preview_dsc = {};
    void* _color_cvt_handle = nullptr;  // esp_imgfx_color_convert_handle_t
    uint32_t _last_idle_capture_ms = 0;
    // 临时状态消息（showStatusMessage）
    std::string _status_message;
    uint32_t _status_message_until_ms = 0;

    // 内部辅助
    void createUi();
    void destroyUi();
    void refreshUi();
    void toggleRecording();
    void stopRecording();
    void captureAndWriteFrame();
    std::string makeVideoPath() const;
    // 在状态标签上临时显示消息（毫秒后自动恢复）
    void showStatusMessage(const char* text, uint32_t duration_ms);
    // 把最近一帧（YUYV）转成 RGB565 写入预览缓冲；返回是否成功
    bool updatePreview();
    // 分配/释放预览缓冲与颜色转换器
    bool setupPreview();
    void cleanupPreview();
    // 触摸线程调用：请求开始/停止（onRunning 中处理）
    void requestToggle() { _toggle_requested = true; }
    void requestHistory() { _history_requested = true; }
    void requestBack() { _back_requested = true; }
    void requestPlaybackByIndex(size_t index);
    // 请求删除确认弹窗（列表长按 / 回放删除按钮）
    void requestDeleteConfirm(const std::string& path);
    // 请求删除某个文件（弹窗确认后由主循环执行）
    void requestDeleteFile(const std::string& path);
    void requestDeleteCancel();
    void requestDeleteConfirmAction();
    // 播放控制（触摸线程只置标记，主循环处理）
    void requestTogglePlaybackControl();
    void requestPlaybackSeek(int value) {
        _playback_seek_value = value;
        _playback_seek_requested = true;
    }

    // 历史与回放（主循环线程执行）
    void enterHistoryList();
    void exitHistoryList();
    void startPlayback(const std::string& path);
    void stopPlayback();
    void playbackTick();
    // 删除确认弹窗（主循环线程执行）
    void showConfirmDialog(const std::string& path);
    void hideConfirmDialog();
    void doDeleteFile(const std::string& path);
    // 播放控制（主循环线程执行）
    void updatePlaybackToggleButton();  // 更新播放/暂停按钮文案与颜色
    void updatePlaybackProgress();      // 更新进度条与时间标签
    void onPlaybackControlTapped();     // 播放/暂停/重播切换
    void seekPlayback(int percent);     // 按百分比 seek（0-1000）
    void finishPlayback();              // 播完：停在末尾，按钮变「播放」
    // 音频输出：打开扬声器并播放 PCM
    void playbackAudioOpen();
    void playbackAudioClose();
    void playbackAudioWrite(const int16_t* samples, size_t count);
    // 音频播放任务（视频回放用，解耦解码与播放）
    void startAudioPlaybackTask();
    void stopAudioPlaybackTask();
    void audioTaskPush(const std::vector<int16_t>& samples);
    static void audioTaskEntry(void* arg);
    // 按 _ui_mode 切换各控件显示/隐藏（需持 LVGL 锁调用）
    void applyUiMode();

    // 语音打开标记处理（MCP open_video）
    void runAutoRecordIfRequested();
    void clearAutoRecordFlag();

    // 摄像头（通过 hal_bridge 获取专用 StackChanCamera）
    class StackChanCamera* camera();
    // codec 实际输入采样率（录像 AVI 音频头按此标注）
    std::uint32_t audioInputSampleRate() const;
};
