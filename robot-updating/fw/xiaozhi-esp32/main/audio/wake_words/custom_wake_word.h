#ifndef CUSTOM_WAKE_WORD_H
#define CUSTOM_WAKE_WORD_H

#include <esp_attr.h>
#include <esp_mn_iface.h>
#include <esp_mn_models.h>
#include <esp_mn_speech_commands.h>
#include <model_path.h>

#include <deque>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "audio_codec.h"
#include "wake_word.h"

class CustomWakeWord : public WakeWord {
public:
    CustomWakeWord();
    ~CustomWakeWord();

    bool Initialize(AudioCodec* codec, srmodel_list_t* models_list);
    void Feed(const std::vector<int16_t>& data);
    void OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback);
    void Start();
    void Stop();
    size_t GetFeedSize();
    void EncodeWakeWordData();
    bool GetWakeWordOpus(std::vector<uint8_t>& opus);
    const std::string& GetLastDetectedWakeWord() const { return last_detected_wake_word_; }
    // 运行时调整检测阈值（0.0~1.0，越小越灵敏）。Initialize 之后调用。
    void SetDetectionThreshold(float threshold) {
        threshold_ = threshold;
        if (multinet_ != nullptr && multinet_model_data_ != nullptr) {
            multinet_->set_det_threshold(multinet_model_data_, threshold_);
        }
    }

    // 追加一个唤醒命令词（Initialize 之后调用，可多次）。
    // command 为拼音（如 "ni hao mi bao"），text 为显示名（如 "你好米宝"）。
    // 与 Kconfig / index.json 的命令词共存，全部参与唤醒检测。
    void AddWakeWordCommand(const std::string& command, const std::string& text,
                            const std::string& action = "wake") {
        commands_.push_back({command, text, action});
        if (multinet_ == nullptr || multinet_model_data_ == nullptr) {
            return;
        }
        // 重新注册全部命令词并重建 FST
        esp_mn_commands_clear();
        for (size_t i = 0; i < commands_.size(); i++) {
            esp_mn_commands_add(i + 1, commands_[i].command.c_str());
        }
        esp_mn_commands_update();
        multinet_->print_active_speech_commands(multinet_model_data_);
    }

private:
    struct Command {
        std::string command;
        std::string text;
        std::string action;
    };

    // multinet 相关成员变量
    esp_mn_iface_t* multinet_ = nullptr;
    model_iface_data_t* multinet_model_data_ = nullptr;
    srmodel_list_t *models_ = nullptr;
    char* mn_name_ = nullptr;
    std::string language_ = "cn";
    int duration_ = 3000;
    float threshold_ = 0.2;
    std::deque<Command> commands_;
 
    std::function<void(const std::string& wake_word)> wake_word_detected_callback_;
    AudioCodec* codec_ = nullptr;
    std::string last_detected_wake_word_;
    std::atomic<bool> running_ = false;
    std::vector<int16_t> input_buffer_;
    std::mutex input_buffer_mutex_;

    TaskHandle_t wake_word_encode_task_ = nullptr;
    StaticTask_t* wake_word_encode_task_buffer_ = nullptr;
    StackType_t* wake_word_encode_task_stack_ = nullptr;
    std::deque<std::vector<int16_t>> wake_word_pcm_;
    std::deque<std::vector<uint8_t>> wake_word_opus_;
    std::mutex wake_word_mutex_;
    std::condition_variable wake_word_cv_;

    void StoreWakeWordData(const std::vector<int16_t>& data);
    void ParseWakenetModelConfig();
};

#endif
