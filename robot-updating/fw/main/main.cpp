/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <mooncake_log.h>
#include <mooncake.h>
#include <apps/apps.h>
#include <hal/hal.h>
#include <hal/mibao_wake_word.h>
#include <esp_system.h>

using namespace mooncake;
using namespace smooth_ui_toolkit;

extern "C" void app_main(void)
{
    const auto reset_reason = esp_reset_reason();

    // Setup logger
    mclog::set_level(mclog::level_info);
    mclog::set_time_format(mclog::time_format_unix_milliseconds);
    mclog::tagInfo("Boot", "reset reason={}", static_cast<int>(reset_reason));

    // HAL init
    GetHAL().init();

    // Setup ui hal
    ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    ui_hal::on_get_tick([]() { return GetHAL().millis(); });

    // 启动策略：默认进 mooncake launcher（米宝界面）。
    // 无已保存 SSID 时，launcher 的 onLauncherOpen 会自动创建 HotspotSetupWorker
    // 进入浏览器 SoftAP 配网（app_launcher.cpp），保证"门控只优化体验、绝不切断
    // 最后的配网入口"。
    // 仅当设置里显式要求开机直进 AI（startAiAgentOnBoot）时才跳过 mooncake。
    const bool skip_mooncake =
        GetHAL().getXiaozhiConfig().startAiAgentOnBoot && GetHAL().getWarmRebootTarget() < 0;

    if (!skip_mooncake) {
        // 完整功能模式：加载全部米宝 app（有 SSID 时进入，正常使用）。
        GetMooncake().installApp(std::make_unique<AppLauncher>());
        GetMooncake().installApp(std::make_unique<AppSetup>());
        GetMooncake().installApp(std::make_unique<AppMeeting>());
        GetMooncake().installApp(std::make_unique<AppMibaoVideo>());
        GetMooncake().installApp(std::make_unique<AppChat>());
        GetMooncake().installApp(std::make_unique<AppMibaoPersonal>());
        GetMooncake().installApp(std::make_unique<AppMibaoIot>());
        GetMooncake().installApp(std::make_unique<AppMibaoFiles>());

        // 待机语音唤醒：mooncake 界面常驻监听「米宝米宝」，
        // 检测到即触发 xiaozhi 启动（免点击直接对话）。
        // 若唤醒词初始化失败（如模型缺失）则静默降级为纯触摸操作。
        mibao::StandbyWakeWord standby_wake;
        standby_wake.start([](const std::string& wake_word) {
            mclog::tagInfo("Boot", "wake word '{}' -> request xiaozhi start", wake_word);
            GetHAL().requestXiaozhiStart();
        });

        // Main loop
        while (1) {
            GetHAL().feedTheDog();
            GetHAL().updateHeapStatusLog();

            GetMooncake().update();

            if (GetHAL().isXiaozhiStartRequested()) {
                break;
            }
        }

        // 进入 xiaozhi 前必须停止待机唤醒，避免与 xiaozhi 自身的
        // 唤醒词检测争用麦克风。
        standby_wake.stop();

        // Uninstall all apps and destroy mooncake
        GetMooncake().uninstallAllApps();
        DestroyMooncake();
    }

    // Start xiaozhi, never returns
    GetHAL().startXiaozhi();
}
