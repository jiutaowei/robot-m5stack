/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_mibao_personal.h"

// 16px 版米宝中文字体（与 meeting_zh_font 同尺寸），用于信息栏小字
LV_FONT_DECLARE(mibao_zh_font_16);

AppMibaoPersonal::AppMibaoPersonal()
    : AppMeeting("个人灵感", "个人灵感", "/sdcard/personal", "personal_", "mibao_personal_150.bin") {
    // 「灵感」不在 meeting_zh_font 子集里，界面切换到米宝中文字体。
    // 会议页信息栏是小面板（116px 宽等），必须用 16px 版，否则 26px 会过大重叠。
    _ui_font = &mibao_zh_font_16;
    // 与会议录音功能完全一致（录音/历史/播放/转纪要），仅存储目录不同：
    // 会议录音 → /sdcard/meetings，个人灵感 → /sdcard/personal
    // （文件管理里已按"会议/个人灵感"文件夹区分）
    _list_txt_notes = false;
}
