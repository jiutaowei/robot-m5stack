#include "mibao_voice_confirm.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>
#include <esp_ae_rate_cvt.h>
#include <esp_mn_iface.h>
#include <esp_mn_models.h>
#include <esp_mn_speech_commands.h>
#include <esp_tts.h>
#include <esp_tts_voice_xiaole.h>
#include <model_path.h>
#include <cJSON.h>

#include <cstring>
#include <vector>

#include "assets.h"
#include "audio_codec.h"
#include "boards/common/board.h"

#define TAG "MibaoVoiceConfirm"

namespace mibao {

VoiceConfirm::VoiceConfirm() = default;

VoiceConfirm::~VoiceConfirm()
{
    deinit();
}

bool VoiceConfirm::init(AudioCodec* codec)
{
    if (initialized_) {
        return true;
    }
    codec_ = codec;
    if (codec_ == nullptr) {
        codec_ = Board::GetInstance().GetAudioCodec();
    }
    if (codec_ == nullptr) {
        ESP_LOGE(TAG, "no audio codec");
        return false;
    }

    // --- 初始化 TTS（小乐音色，内置数据）---
    tts_voice_ = esp_tts_voice_set_init(&esp_tts_voice_xiaole, nullptr);
    if (tts_voice_ == nullptr) {
        ESP_LOGE(TAG, "tts voice init failed");
        return false;
    }
    tts_handle_ = esp_tts_create(static_cast<esp_tts_voice_t*>(tts_voice_));
    if (tts_handle_ == nullptr) {
        ESP_LOGE(TAG, "tts create failed");
        esp_tts_voice_set_free(static_cast<esp_tts_voice_t*>(tts_voice_));
        tts_voice_ = nullptr;
        return false;
    }

    // --- 初始化命令词识别（multinet，复用唤醒词模型）---
    // 与 mibao_wake_word 同理：直接读 srmodels.bin 加载，
    // 【不能】调用 Assets::Apply()（避免污染 Assets::models_list_ 导致 xiaozhi 二次 Apply 崩溃）。
    void* srmodels_ptr = nullptr;
    size_t srmodels_size = 0;
    void* index_ptr = nullptr;
    size_t index_size = 0;
    if (!Assets::GetInstance().GetAssetData("index.json", index_ptr, index_size)) {
        ESP_LOGE(TAG, "failed to read index.json");
        return false;
    }
    cJSON* root = cJSON_ParseWithLength(static_cast<char*>(index_ptr), index_size);
    if (root == nullptr) {
        ESP_LOGE(TAG, "failed to parse index.json");
        return false;
    }
    cJSON* srmodels = cJSON_GetObjectItem(root, "srmodels");
    if (!cJSON_IsString(srmodels)) {
        ESP_LOGE(TAG, "index.json has no srmodels entry");
        cJSON_Delete(root);
        return false;
    }
    const std::string srmodels_file = srmodels->valuestring;
    cJSON_Delete(root);

    if (!Assets::GetInstance().GetAssetData(srmodels_file, srmodels_ptr, srmodels_size)) {
        ESP_LOGE(TAG, "failed to read %s", srmodels_file.c_str());
        return false;
    }
    auto* models = srmodel_load(static_cast<uint8_t*>(srmodels_ptr));
    if (models == nullptr || models->num == -1) {
        ESP_LOGE(TAG, "failed to load srmodels.bin");
        return false;
    }
    const char* mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, "cn");
    if (mn_name == nullptr) {
        mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, nullptr);
    }
    if (mn_name == nullptr) {
        ESP_LOGE(TAG, "no multinet model found");
        return false;
    }
    mn_handle_ = esp_mn_handle_from_name(const_cast<char*>(mn_name));
    if (mn_handle_ == nullptr) {
        ESP_LOGE(TAG, "multinet handle null");
        return false;
    }
    auto* mn_iface = static_cast<esp_mn_iface_t*>(mn_handle_);
    mn_model_data_ = mn_iface->create(mn_name, 3000);
    if (mn_model_data_ == nullptr) {
        ESP_LOGE(TAG, "multinet create failed");
        return false;
    }
    mn_iface->set_det_threshold(mn_model_data_, 0.15f);

    // 采样率转换（codec 24kHz -> 16kHz，命令词模型要求），与 xiaozhi 一致
    if (codec_->input_sample_rate() != 16000) {
        esp_ae_rate_cvt_cfg_t cfg = {
            .src_rate = (uint32_t)codec_->input_sample_rate(),
            .dest_rate = 16000,
            .channel = (uint8_t)codec_->input_channels(),
            .bits_per_sample = ESP_AE_BIT16,
            .complexity = 2,
        };
        esp_ae_err_t rc = esp_ae_rate_cvt_open(&cfg, &resampler_);
        if (rc != ESP_AE_ERR_OK || resampler_ == nullptr) {
            ESP_LOGW(TAG, "failed to create resampler (%d), confirm may not work", rc);
            resampler_ = nullptr;
        }
    }

    initialized_ = true;
    ESP_LOGI(TAG, "voice confirm initialized (tts + multinet)");
    return true;
}

bool VoiceConfirm::speak(const std::string& text)
{
    if (!initialized_ || tts_handle_ == nullptr || codec_ == nullptr) {
        ESP_LOGE(TAG, "not initialized");
        return false;
    }
    ESP_LOGI(TAG, "speak: %s", text.c_str());

    // 确保输出使能
    codec_->EnableOutput(true);
    codec_->Start();

    esp_tts_parse_chinese(tts_handle_, text.c_str());

    int len = 0;
    short* pcm = nullptr;
    int loops = 0;
    while ((pcm = esp_tts_stream_play(tts_handle_, &len, 2)) != nullptr) {
        if (len <= 0) {
            break;
        }
        // 输出到 codec（16k mono int16）
        std::vector<int16_t> out(pcm, pcm + len);
        codec_->OutputData(out);
        if (++loops > 10000) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    esp_tts_stream_reset(tts_handle_);
    return true;
}

VoiceConfirm::Result VoiceConfirm::listenConfirm(uint32_t timeout_ms)
{
    if (!initialized_ || mn_handle_ == nullptr || codec_ == nullptr) {
        ESP_LOGE(TAG, "not initialized");
        return Result::kError;
    }
    auto* mn_iface = static_cast<esp_mn_iface_t*>(mn_handle_);

    // 注册命令词（拼音）：1-4 确认，5-8 拒绝
    esp_mn_commands_clear();
    esp_mn_commands_add(1, "shi");        // 是
    esp_mn_commands_add(2, "hao");        // 好
    esp_mn_commands_add(3, "kai shi");    // 开始
    esp_mn_commands_add(4, "dui");        // 对
    esp_mn_commands_add(5, "bu");         // 不
    esp_mn_commands_add(6, "qu xiao");    // 取消
    esp_mn_commands_add(7, "ting");       // 停
    esp_mn_commands_update();

    // 启动麦克风输入
    codec_->EnableInput(true);
    codec_->Start();

    const int chunksize = mn_iface->get_samp_chunksize(mn_model_data_);
    std::vector<int16_t> buf;
    std::vector<int16_t> input;
    std::vector<int16_t> mono;
    std::vector<int16_t> resampled;

    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        mono.clear();
        // InputData 按 vector 大小读取，必须先 resize 再读（与 xiaozhi ReadAudioData 一致）
        mono.resize(160 * codec_->input_sample_rate() / 16000 * codec_->input_channels());
        if (!codec_->InputData(mono)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        // 双声道取左
        if (codec_->input_channels() == 2) {
            input.clear();
            input.reserve(mono.size() / 2);
            for (size_t i = 0; i + 1 < mono.size(); i += 2) {
                input.push_back(mono[i]);
            }
            mono.swap(input);
        }
        if (mono.empty()) {
            continue;
        }
        // 重采样到 16kHz
        if (resampler_ != nullptr && codec_->input_sample_rate() != 16000) {
            uint32_t in_sample_num = mono.size();
            uint32_t out_samples = 0;
            esp_ae_rate_cvt_get_max_out_sample_num(resampler_, in_sample_num, &out_samples);
            resampled.clear();
            resampled.resize(out_samples);
            uint32_t actual = out_samples;
            esp_ae_rate_cvt_process(resampler_, (esp_ae_sample_t)mono.data(), in_sample_num,
                                    (esp_ae_sample_t)resampled.data(), &actual);
            resampled.resize(actual);
            mono.swap(resampled);
        }
        if (mono.empty()) {
            continue;
        }
        // 累积到 chunksize 再喂给模型
        buf.insert(buf.end(), mono.begin(), mono.end());
        if (static_cast<int>(buf.size()) < chunksize) {
            continue;
        }

        esp_mn_state_t state = mn_iface->detect(mn_model_data_, buf.data());
        buf.erase(buf.begin(), buf.begin() + chunksize);
        if (state == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t* results = mn_iface->get_results(mn_model_data_);
            for (int i = 0; i < results->num; i++) {
                int cmd_id = results->command_id[i];
                ESP_LOGI(TAG, "command detected: id=%d string=%s prob=%f", cmd_id, results->string,
                         results->prob[i]);
                if (cmd_id >= 1 && cmd_id <= 4) {
                    mn_iface->clean(mn_model_data_);
                    return Result::kYes;
                }
                if (cmd_id >= 5 && cmd_id <= 7) {
                    mn_iface->clean(mn_model_data_);
                    return Result::kNo;
                }
            }
            mn_iface->clean(mn_model_data_);
        } else if (state == ESP_MN_STATE_TIMEOUT) {
            mn_iface->clean(mn_model_data_);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    ESP_LOGW(TAG, "confirm timeout");
    return Result::kTimeout;
}

void VoiceConfirm::deinit()
{
    if (tts_handle_ != nullptr) {
        esp_tts_destroy(tts_handle_);
        tts_handle_ = nullptr;
    }
    if (tts_voice_ != nullptr) {
        esp_tts_voice_set_free(static_cast<esp_tts_voice_t*>(tts_voice_));
        tts_voice_ = nullptr;
    }
    if (mn_handle_ != nullptr && mn_model_data_ != nullptr) {
        auto* mn_iface = static_cast<esp_mn_iface_t*>(mn_handle_);
        mn_iface->destroy(mn_model_data_);
        mn_model_data_ = nullptr;
        mn_handle_ = nullptr;
    }
    if (resampler_ != nullptr) {
        esp_ae_rate_cvt_close(resampler_);
        resampler_ = nullptr;
    }
    if (codec_ != nullptr) {
        codec_->EnableInput(false);
        codec_->EnableOutput(false);
    }
    codec_ = nullptr;
    initialized_ = false;
    ESP_LOGI(TAG, "voice confirm deinit");
}

}  // namespace mibao
