/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>

/**
 * @brief 米宝一号 - AI 对话入口 app（阶段 1.3）
 *
 * launcher 4 宫格中的"对话"入口：点击后直接进入 xiaozhi AI 对话
 * （产品决策 #5：唤醒/点击后直接进 AI 对话，不停留在中间页）。
 * 打开时显示极简过渡页，同时 requestXiaozhiStart()，
 * main.cpp 主循环检测到请求后退出 mooncake 并启动 xiaozhi。
 */
class AppChat : public mooncake::AppAbility {
public:
    AppChat();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    bool _close_requested = false;
};
