/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../app_meeting/app_meeting.h"

/**
 * @brief 米宝一号 - 个人灵感
 *
 * 直接继承会议录音的完整录音链路（主动轮询采集 + WAV 占位头回填），
 * 仅注入自己的名称/标题/录音根目录/文件前缀/launcher 图标：
 * - 录音目录：/sdcard/personal
 * - 文件前缀：personal_（personal_YYYYMMDD_HHMMSS.wav）
 */
class AppMibaoPersonal : public AppMeeting {
public:
    AppMibaoPersonal();
};
