/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "app_launcher/app_launcher.h"
#include "app_setup/app_setup.h"
#include "app_meeting/app_meeting.h"
// 米宝一号 (Mibao One) 自有 app
#include "app_chat/app_chat.h"
#include "app_mibao_personal/app_mibao_personal.h"
#include "app_mibao_iot/app_mibao_iot.h"
#include "app_mibao_files/app_mibao_files.h"
#include "app_mibao_video/app_mibao_video.h"
// 临时配网最小验证 app 已移除，改用正规 Hotspot Setup（app_setup → 设置 → Wi-Fi）
// #include "app_prov_test/app_prov_test.h"

// 注释掉的 app（保留源，不显示在 launcher）
// #include "app_ai_agent/app_ai_agent.h"        // 不用：AI 在 xiaozhi 内置
// #include "app_avatar/app_avatar.h"            // 不用：米宝不做虚拟形象
// #include "app_espnow_ctrl/app_espnow_ctrl.h"  // 保留源：ESP-NOW 协议由 app_meeting 接管
// #include "app_app_center/app_app_center.h"    // 不用
// #include "app_ezdata/app_ezdata.h"            // 不用
// #include "app_dance/app_dance.h"              // 不用：米宝不做 dance 移动
