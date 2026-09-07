#pragma once

// 米宝一号：待机语音唤醒监听（免点击对话）
//
// 在 mooncake 界面运行期间监听唤醒词「米宝米宝」。
// 检测到唤醒词后通过回调通知上层（main.cpp）触发 requestXiaozhiStart()，
// 实现"开机即可喊话进对话、说退出回到界面"的智能音箱体验。
//
// 实现：复用 xiaozhi-esp32 的 CustomWakeWord（ESP-SR MultiNet 命令词识别），
// 通过 Board::GetInstance().GetAudioCodec() 获取麦克风，独立于 xiaozhi 的
// AudioService 运行。进入 xiaozhi 前必须 stop()，避免与 xiaozhi 自身的
// 唤醒词检测争用麦克风。

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class AudioCodec;
class CustomWakeWord;

namespace mibao {

class StandbyWakeWord {
public:
    using WakeCallback = std::function<void(const std::string& wake_word)>;

    StandbyWakeWord();
    ~StandbyWakeWord();

    // 启动监听。初始化 codec + 唤醒词模型 + 麦克风采集任务。
    // 返回 false 表示初始化失败（如模型缺失），调用方应忽略。
    bool start(WakeCallback on_wake);

    // 停止监听，释放任务与资源（进入 xiaozhi 前必须调用）。
    void stop();

    // 是否正在监听
    bool isRunning() const { return running_.load(); }

    // 全局暂停/恢复（视频回放等需要独占扬声器/麦克风场景使用）。
    // 暂停期间 inputTask 不再读取麦克风。
    static void Suspend();
    static void Resume();
    static bool IsSuspended();

private:
    static void inputTask(void* param);

    std::unique_ptr<CustomWakeWord> wake_word_;
    AudioCodec* codec_ = nullptr;
    WakeCallback on_wake_;
    std::atomic<bool> running_{false};
    std::atomic<bool> task_stop_{false};
    TaskHandle_t task_handle_ = nullptr;
    // 采样率转换（codec 24kHz -> 唤醒词 16kHz），与 xiaozhi ReadAudioData 一致
    void* resampler_ = nullptr;
    // 任务退出信号量：stop() 等待 inputTask 真正退出后再释放资源，避免 use-after-free
    void* exit_sem_ = nullptr;
};

}  // namespace mibao
