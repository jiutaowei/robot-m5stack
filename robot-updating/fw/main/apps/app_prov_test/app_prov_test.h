/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <lvgl.h>
#include <mooncake.h>
#include <cstdint>

/**
 * @brief 配网最小验证 app（纯 STA 直连）
 *
 * 上电进入后，直接以纯 STA 模式连接硬编码的测试 WiFi（jinhetech），
 * 屏幕实时显示连接状态与 IP。用于隔离"配网页/APSTA 模式"干扰，
 * 单独验证"ESP32-S3 纯 STA 能否连上目标路由器"。
 *
 * 结果解读：
 *  - 显示 Connected + IP  →  硬件 + 2.4G + 纯 STA 全 OK，问题只在配网页流程
 *  - 显示 FAILED          →  ESP32 连该路由器本身有问题（硬件/天线/频段/路由器）
 */
class AppProvisionTest : public mooncake::AppAbility {
public:
    AppProvisionTest();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    lv_obj_t* _root = nullptr;
    lv_obj_t* _status_label = nullptr;

    bool _started = false;
    bool _close_requested = false;
    std::uint32_t _start_ms = 0;

    void createUi();
    void destroyUi();
};