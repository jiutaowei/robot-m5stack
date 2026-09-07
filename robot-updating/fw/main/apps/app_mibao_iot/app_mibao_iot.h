/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>

/**
 * @brief 米宝一号 - 物联网控制 app
 *
 * 阶段 0：空壳，仅返回键 + 标题 + 占位提示
 * 阶段 5：补 IoT 控制（2×2 设备网格 + 异步 HTTP 控制）
 */
class AppMibaoIot : public mooncake::AppAbility {
public:
    AppMibaoIot();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    bool _close_requested = false;
};
