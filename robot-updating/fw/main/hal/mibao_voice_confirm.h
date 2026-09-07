#pragma once

// 米宝一号：会议 app 内的语音播报 + 命令词确认（全程语音操作）
//
// 在 mooncake 的会议录制 app 中使用：
//   1) speak(text)         —— esp_tts 中文合成，播报确认语
//   2) listenConfirm(timeoutMs)
//                          —— esp_mn 命令词识别，等待用户回答
//                             识别到「是/好/开始/对」-> kYes
//                             识别到「不/取消/停/别」-> kNo
//                             超时或模型失败 -> kTimeout
//
// 复用 ESP-SR 模型（与唤醒词同一模型资源），命令词按拼音注册，
// 与唤醒词监听互斥：本模块运行期间不占用唤醒词。

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// 前向声明具体类型（避免在头文件暴露 esp-sr 依赖）
struct model_iface_data_t;
class AudioCodec;

namespace mibao {

class VoiceConfirm {
public:
    enum class Result { kYes, kNo, kTimeout, kError };

    VoiceConfirm();
    ~VoiceConfirm();

    // 初始化模型与 TTS（懒加载，首次调用时执行）
    bool init(AudioCodec* codec);

    // 播报一段中文文本（阻塞直到播完或失败）
    bool speak(const std::string& text);

    // 监听命令词，最多等待 timeoutMs 毫秒。
    // 返回 kYes / kNo / kTimeout / kError。
    Result listenConfirm(uint32_t timeout_ms);

    // 释放资源
    void deinit();

private:
    AudioCodec* codec_ = nullptr;
    void* tts_handle_ = nullptr;      // esp_tts_handle_t
    void* tts_voice_ = nullptr;       // esp_tts_voice_t*（cpp 内强转使用）
    void* mn_handle_ = nullptr;       // esp_mn_iface_t*（cpp 内强转使用）
    model_iface_data_t* mn_model_data_ = nullptr;
    void* resampler_ = nullptr;       // esp_ae_rate_cvt_handle_t（24k -> 16k）
    bool initialized_ = false;
};

}  // namespace mibao
