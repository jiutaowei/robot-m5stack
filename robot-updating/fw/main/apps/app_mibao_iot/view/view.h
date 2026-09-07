/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <functional>

namespace MibaoIotView {

/**
 * @brief 构建物联网 app 的 UI
 * @param on_back 用户点击左上角"返回"按钮的回调
 */
void build(std::function<void()> on_back);

/**
 * @brief 每帧轮询：需在 LVGL 锁内调用（由 app onRunning 驱动）。
 *        负责回收后台 HTTP 任务的结果并刷新 UI（toast / 设备按键状态）。
 *        控制请求本身在独立大栈任务里执行，避免阻塞/压爆 LVGL 任务栈。
 */
void update();

/**
 * @brief 销毁 UI，释放资源
 */
void destroy();

}  // namespace MibaoIotView
