#include "mibao_wake_word.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>
#include <esp_ae_rate_cvt.h>
#include <cJSON.h>
#include <model_path.h>
#include <cmath>
#include <vector>

#include "assets.h"
#include "audio_codec.h"
#include "boards/common/board.h"
#include "wake_words/custom_wake_word.h"
#include "hal/hal.h"

#define TAG "MibaoWakeWord"

namespace mibao {

// 全局暂停标志：视频回放等独占音频场景置位后，监听任务静默
static std::atomic<bool> g_suspended{false};

void StandbyWakeWord::Suspend() {
    g_suspended = true;
}

void StandbyWakeWord::Resume() {
    g_suspended = false;
}

bool StandbyWakeWord::IsSuspended() {
    return g_suspended.load();
}

StandbyWakeWord::StandbyWakeWord() = default;

StandbyWakeWord::~StandbyWakeWord()
{
    stop();
}

bool StandbyWakeWord::start(WakeCallback on_wake)
{
    if (running_.load()) {
        return true;
    }
    on_wake_ = std::move(on_wake);

    // 获取音频 codec（单例，与 xiaozhi 共享同一硬件）
    codec_ = Board::GetInstance().GetAudioCodec();
    if (codec_ == nullptr) {
        ESP_LOGE(TAG, "no audio codec available");
        return false;
    }

    // 初始化唤醒词识别。
    // 注意：ESP-SR 的 esp_srmodel_init("model") 需要名为 "model" 的 flash 分区，
    // 但本工程模型打包在 "assets" 分区里。
    // 正确做法（与 xiaozhi 框架 assets.cc 一致）：
    //   1) 从 index.json 读 srmodels 文件名，GetAssetData 取出
    //   2) srmodel_load 解析成 srmodel_list_t，传给 CustomWakeWord（非 nullptr 分支，
    //      命令词仍来自 Kconfig CONFIG_CUSTOM_WAKE_WORD = mi bao mi bao）
    // 注意：这里【不能】调用 Assets::Apply()——Apply 内部 LoadSrmodelsFromIndex
    // 会把模型存到 Assets::models_list_，xiaozhi 启动时二次 Apply 会 esp_srmodel_deinit
    // 释放它导致 srmodel_mmap_deinit 崩溃。单例构造时已 InitializePartition，
    // GetAssetData 直接可用，无需 Apply。
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
    srmodel_list_t* models = srmodel_load(static_cast<uint8_t*>(srmodels_ptr));
    if (models == nullptr) {
        ESP_LOGE(TAG, "failed to load srmodels.bin");
        return false;
    }

    wake_word_ = std::make_unique<CustomWakeWord>();
    if (!wake_word_->Initialize(codec_, models)) {
        ESP_LOGE(TAG, "failed to init wake word model");
        wake_word_.reset();
        return false;
    }
    // 降低检测阈值提高唤醒灵敏度（默认 0.2 偏高，实际识别概率常在 0.2~0.3 抖动）
    wake_word_->SetDetectionThreshold(0.12f);

    // 注意：不要在 Initialize 之后再 AddWakeWordCommand 追加"你好米宝/米宝同学"！
    // CustomWakeWord::Initialize -> ParseWakenetModelConfig 已经统一追加过
    // （custom_wake_word.cc），这里再追加会造成命令词重复注册，ESP-MN 重建 FST 时
    // 命令 id 错位（id 2/3 被重复项覆盖成 4/5），detect 返回的 command_id 与
    // commands_ 索引映射错位，导致检测到唤醒词却不触发回调（"喊话没反应"）。

    wake_word_->OnWakeWordDetected([this](const std::string& wake_word) {
        ESP_LOGI(TAG, "standby wake word detected: %s", wake_word.c_str());
        if (on_wake_) {
            on_wake_(wake_word);
        }
    });

    // 启动音频输入
    codec_->EnableInput(true);
    codec_->Start();

    // 创建采样率转换器（codec 输入采样率 -> 16kHz，唤醒词模型要求）
    // CoreS3 麦克风实际为 24kHz，与 xiaozhi 框架 ReadAudioData 的做法一致。
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
            ESP_LOGW(TAG, "failed to create resampler (%d), wake word may not work", rc);
            resampler_ = nullptr;
        } else {
            ESP_LOGI(TAG, "resampler %d -> 16000 created", codec_->input_sample_rate());
        }
    }

    running_.store(true);
    task_stop_.store(false);

    // 任务退出信号量（供 stop() 等待 inputTask 完全退出）
    exit_sem_ = xSemaphoreCreateBinary();
    if (exit_sem_ == nullptr) {
        ESP_LOGE(TAG, "failed to create exit semaphore");
        return false;
    }

    // 创建麦克风采集任务，喂给唤醒词识别
    BaseType_t ret = xTaskCreatePinnedToCore(
        &StandbyWakeWord::inputTask, "mibao_wake", 4096, this, 1, &task_handle_, 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "failed to create input task");
        running_.store(false);
        vSemaphoreDelete(exit_sem_);
        exit_sem_ = nullptr;
        wake_word_.reset();
        return false;
    }

    wake_word_->Start();
    ESP_LOGI(TAG, "standby wake word listening started");
    return true;
}

void StandbyWakeWord::stop()
{
    if (!running_.load()) {
        return;
    }
    task_stop_.store(true);
    running_.store(false);

    // 等待 inputTask 完全退出（它退出时会 give 信号量），
    // 确保不再访问 wake_word_/codec_ 后再释放，避免 use-after-free 崩溃。
    if (exit_sem_ != nullptr) {
        xSemaphoreTake(static_cast<QueueHandle_t>(exit_sem_), pdMS_TO_TICKS(1000));
        vSemaphoreDelete(static_cast<QueueHandle_t>(exit_sem_));
        exit_sem_ = nullptr;
    }
    if (task_handle_ != nullptr) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }

    if (wake_word_) {
        wake_word_->Stop();
        wake_word_.reset();
    }
    if (resampler_ != nullptr) {
        esp_ae_rate_cvt_close(resampler_);
        resampler_ = nullptr;
    }
    if (codec_ != nullptr) {
        codec_->EnableInput(false);
    }
    codec_ = nullptr;
    ESP_LOGI(TAG, "standby wake word stopped");
}

void StandbyWakeWord::inputTask(void* param)
{
    auto* self = static_cast<StandbyWakeWord*>(param);
    std::vector<int16_t> data;
    std::vector<int16_t> resampled;

    while (!self->task_stop_.load()) {
        // 全局暂停（视频回放等独占音频场景）
        if (g_suspended.load()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        // 麦克风被其他应用（会议录音/录像带音频）占用时暂停唤醒监听，
        // 避免与它们争抢 I2S 音频数据导致录音断断续续。
        if (GetHAL().isMicrophoneOwnedBy("meeting") || GetHAL().isMicrophoneOwnedBy("video")) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        // 10ms 一帧，与 CustomWakeWord 期望的 chunksize 对齐。
        // InputData 按 vector 大小读取，必须先 resize 再读（与 xiaozhi ReadAudioData 一致）。
        data.clear();
        data.resize(160 * self->codec_->input_sample_rate() / 16000 * self->codec_->input_channels());
        if (!self->codec_->InputData(data)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (!self->running_.load()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 重采样到 16kHz（若 codec 采样率不是 16k）
        if (self->resampler_ != nullptr && self->codec_->input_sample_rate() != 16000) {
            uint32_t in_sample_num = data.size() / self->codec_->input_channels();
            uint32_t out_samples = 0;
            esp_ae_rate_cvt_get_max_out_sample_num(self->resampler_, in_sample_num, &out_samples);
            resampled.clear();
            resampled.resize(out_samples * self->codec_->input_channels());
            uint32_t actual = out_samples;
            esp_ae_rate_cvt_process(self->resampler_, (esp_ae_sample_t)data.data(), in_sample_num,
                                    (esp_ae_sample_t)resampled.data(), &actual);
            resampled.resize(actual * self->codec_->input_channels());
            self->wake_word_->Feed(resampled);
        } else {
            self->wake_word_->Feed(data);
        }

        // 临时调试：每 50 帧打印一次输入能量与喂入采样数，确认麦克风数据流
        static uint32_t dbg_cnt = 0;
        if ((++dbg_cnt % 50) == 0) {
            int64_t sum = 0;
            size_t n = data.size() > 320 ? 320 : data.size();
            for (size_t i = 0; i < n; i++) {
                int32_t s = data[i];
                sum += s * s;
            }
            float rms = (n > 0) ? sqrtf((float)(sum / (int64_t)n)) : 0.0f;
            ESP_LOGI(TAG, "[dbg] read %d samples, feed %d samples, rms=%.1f",
                     (int)data.size(), (int)(self->resampler_ ? resampled.size() : data.size()), rms);
        }
    }

    // 通知 stop() 本任务已完全退出，之后资源可安全释放
    if (self->exit_sem_ != nullptr) {
        xSemaphoreGive(static_cast<QueueHandle_t>(self->exit_sem_));
    }
    vTaskDelete(nullptr);
}

}  // namespace mibao
