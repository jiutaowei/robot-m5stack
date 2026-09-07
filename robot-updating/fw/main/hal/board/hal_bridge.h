/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "stackchan_camera.h"
#include <cstdint>
#include <lvgl.h>
#include <driver/i2c_master.h>
#include <string_view>

namespace hal_bridge {

struct TouchPoint_t {
    int num = 0;
    int x   = -1;
    int y   = -1;
};

struct Data_t {
    TouchPoint_t touchPoint;
    bool isXiaozhiMode              = false;
    bool isXiaozhiModeToggleEnabled = false;
};

struct XiaozhiConfig_t {
    uint32_t idleShutdownTimeSeconds = 600;
    bool allowShutdownWhenCharging   = false;
    uint8_t idleRandomMovementLevel  = 2;
    bool startAiAgentOnBoot          = false;
};

void lock();
void unlock();
Data_t& get_data();

void set_touch_point(int num, int x, int y);
TouchPoint_t get_touch_point();

bool is_xiaozhi_mode();
void set_xiaozhi_mode(bool mode);
void toggle_xiaozhi_chat_state();

void disply_lvgl_lock();
void disply_lvgl_unlock();
lv_disp_t* display_get_lvgl_display();

void xiaozhi_board_init();
void start_xiaozhi_app();
bool is_xiaozhi_ready();
bool is_xiaozhi_idle();
XiaozhiConfig_t get_xiaozhi_config();
void set_xiaozhi_config(const XiaozhiConfig_t& config);

i2c_master_bus_handle_t board_get_i2c_bus();
StackChanCamera* board_get_camera();
int board_get_battery_level();
bool board_is_battery_charging();
void board_prepare_sdcard_bus();
bool board_sdcard_is_mounted();
// Re-run the SD mount if the boot-time attempt failed (bounded retries,
// serialized with LVGL).  Returns true when the card is mounted.
bool board_sdcard_try_remount();
void board_begin_sdcard_access();
void board_end_sdcard_access();
void board_set_backlight_brightness(uint8_t brightness, bool permanent = false);
uint8_t board_get_backlight_brightness();
void board_set_speaker_volume(uint8_t volume, bool permanent = false);
uint8_t board_get_speaker_volume();

void app_play_sound(const std::string_view& sound);

/**
 * @brief 上传本地文件（录音 .wav / 视频 .avi）到服务器（multipart/form-data）。
 *
 * 供会议录音/文件管理批量转纪要共用。上传成功后服务器返回 JSON
 * （含 notes 字段），本函数解析并写入同目录 .txt（与文件名同名，改后缀）。
 *
 * @param url        服务器接口地址（如 http://ip:8003/mibao/meeting/transcribe）
 * @param path       本地文件完整路径
 * @param file_name  上传文件名（不含目录）
 * @param type       业务类型（meeting / personal）
 * @return           true 表示上传成功且纪要已写入本地 .txt
 */
bool upload_recording_for_notes(const std::string& url, const std::string& path,
                                const std::string& file_name, const std::string& type);

// 待播放视频路径（文件管理点击 .avi 时设置，视频播放器 onOpen 时取出播放）
void set_pending_video_playback(const std::string& path);
std::string take_pending_video_playback();

}  // namespace hal_bridge
