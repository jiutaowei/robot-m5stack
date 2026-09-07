/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal_bridge.h"
#include "stackchan_display.h"
#include <esp_log.h>
#include <esp_err.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <application.h>
#include <board.h>
#include <display.h>
#include <mutex>
#include <assets.h>
#include <settings.h>
#include <hal/hal.h>
#include <hal/mibao_config.h>
#include <cJSON.h>
#include <cstdio>
#include <vector>
#include <cstring>

static const char* _tag = "HAL_BRIDGE";

static constexpr std::string_view _xiaozhi_config_nvs_ns                           = "xiaozhi";
static constexpr std::string_view _xiaozhi_config_idle_shutdown_time_key           = "idle_sec";
static constexpr std::string_view _xiaozhi_config_allow_shutdown_when_charging_key = "ext_pwr";
static constexpr std::string_view _xiaozhi_config_idle_random_movement_key         = "idle_lv";
static constexpr std::string_view _xiaozhi_config_start_ai_agent_on_boot_key       = "boot_ai";

namespace hal_bridge {

/* -------------------------------------------------------------------------- */
/*                            State and touch point                           */
/* -------------------------------------------------------------------------- */

static std::mutex _mutex;
static Data_t _data;

void lock()
{
    _mutex.lock();
}

void unlock()
{
    _mutex.unlock();
}

Data_t& get_data()
{
    return _data;
}

void set_touch_point(int num, int x, int y)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _data.touchPoint.num = num;
    _data.touchPoint.x   = x;
    _data.touchPoint.y   = y;
}

TouchPoint_t get_touch_point()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _data.touchPoint;
}

bool is_xiaozhi_mode()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _data.isXiaozhiMode;
}

void set_xiaozhi_mode(bool mode)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _data.isXiaozhiMode = mode;
}

/* -------------------------------------------------------------------------- */
/*                                   Display                                  */
/* -------------------------------------------------------------------------- */
#define DISPLAY_TYPE StackChanAvatarDisplay

lv_disp_t* display_get_lvgl_display()
{
    auto display = static_cast<DISPLAY_TYPE*>(Board::GetInstance().GetDisplay());
    return display->GetLvglDisplay();
}

void disply_lvgl_lock()
{
    auto display = static_cast<DISPLAY_TYPE*>(Board::GetInstance().GetDisplay());
    display->LvglLock();
}

void disply_lvgl_unlock()
{
    auto display = static_cast<DISPLAY_TYPE*>(Board::GetInstance().GetDisplay());
    display->LvglUnlock();
}

/* -------------------------------------------------------------------------- */
/*                                 Application                                */
/* -------------------------------------------------------------------------- */

void xiaozhi_board_init()
{
    // Init board
    auto& board = Board::GetInstance();
}

/**
 * @brief 启动 xiaozhi 应用的任务入口函数
 */
static void start_xiaozhi_app_task(void* param)
{
    (void)param;
    // 检查是否有退出请求（用户点击了返回按钮）
    if (GetHAL().isXiaozhiExitRequested()) {
        ESP_LOGI("hal_bridge", "xiaozhi exit requested, skipping startup");
        GetHAL().resetXiaozhiExitRequest();
        vTaskDelete(NULL);
        return;
    }

    auto& app = Application::GetInstance();
    app.Initialize();

    // 在启动线程中运行（阻塞，直到 xiaozhi 退出）
    app.Run();

    esp_restart();
}

/**
 * @brief 启动 xiaozhi 应用（创建任务运行）
 *
 * 设置设备模式为 xiaozhi，启动 OTA 检查任务，并创建任务运行 xiaozhi 应用。
 * 此函数不阻塞，立即返回。
 */
void start_xiaozhi_app()
{
    ESP_LOGI("hal_bridge", "start xiaozhi app");
    if (xTaskCreate(start_xiaozhi_app_task, "xiaozhi_app", 8192, nullptr, 2, nullptr) != pdPASS) {
        ESP_LOGE("hal_bridge", "failed to start xiaozhi app task");
    }
}

XiaozhiConfig_t get_xiaozhi_config()
{
    XiaozhiConfig_t config;

    Settings settings(_xiaozhi_config_nvs_ns.data(), false);
    config.idleShutdownTimeSeconds = settings.GetInt(_xiaozhi_config_idle_shutdown_time_key.data(),
                                                     static_cast<int>(config.idleShutdownTimeSeconds));
    config.allowShutdownWhenCharging =
        settings.GetBool(_xiaozhi_config_allow_shutdown_when_charging_key.data(), config.allowShutdownWhenCharging);
    config.idleRandomMovementLevel =
        settings.GetInt(_xiaozhi_config_idle_random_movement_key.data(), config.idleRandomMovementLevel);
    config.startAiAgentOnBoot =
        settings.GetBool(_xiaozhi_config_start_ai_agent_on_boot_key.data(), config.startAiAgentOnBoot);

    return config;
}

void set_xiaozhi_config(const XiaozhiConfig_t& config)
{
    Settings settings(_xiaozhi_config_nvs_ns.data(), true);
    settings.SetInt(_xiaozhi_config_idle_shutdown_time_key.data(), config.idleShutdownTimeSeconds);
    settings.SetBool(_xiaozhi_config_allow_shutdown_when_charging_key.data(), config.allowShutdownWhenCharging);
    settings.SetInt(_xiaozhi_config_idle_random_movement_key.data(), config.idleRandomMovementLevel);
    settings.SetBool(_xiaozhi_config_start_ai_agent_on_boot_key.data(), config.startAiAgentOnBoot);
}

void app_play_sound(const std::string_view& sound)
{
    auto& app = Application::GetInstance();
    app.PlaySound(sound);
}

// 待播放视频路径（文件管理点击 .avi 时设置，视频播放器 onOpen 时取出播放）
static std::string _pending_video_playback;

// ---------- 录音/视频上传转纪要（会议录音、文件管理共用） ----------

namespace {
constexpr int kUploadTimeoutMs = 120000;
constexpr std::size_t kUploadChunkSize = 4096;

std::string uploadDeviceId() {
    std::uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char id[32] = {0};
    std::snprintf(id, sizeof(id), "mibao-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2],
                  mac[3], mac[4], mac[5]);
    return id;
}
}  // namespace

bool upload_recording_for_notes(const std::string& url, const std::string& path,
                                const std::string& file_name, const std::string& type) {
    auto network = Board::GetInstance().GetNetwork();
    auto http = network ? network->CreateHttp(0) : nullptr;
    if (!http) {
        ESP_LOGE(_tag, "upload: no network/http client");
        return false;
    }

    const std::string boundary = "----MibaoUpload9f8e7d6c";
    http->SetTimeout(kUploadTimeoutMs);
    http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

    // 视频(.avi) Content-Type 用 video/x-msvideo，其余按音频
    const bool is_avi = file_name.size() >= 4 &&
                        file_name.substr(file_name.size() - 4) == ".avi";
    const char* content_type = is_avi ? "video/x-msvideo" : "audio/wav";

    std::string head;
    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"type\"\r\n\r\n" + type + "\r\n";
    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"device_id\"\r\n\r\n" + uploadDeviceId() + "\r\n";
    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"file\"; filename=\"" + file_name + "\"\r\n";
    head += "Content-Type: " + std::string(content_type) + "\r\n\r\n";
    const std::string tail = "\r\n--" + boundary + "--\r\n";

    if (!http->Open("POST", url)) {
        ESP_LOGE(_tag, "upload: open failed: url=%s", url.c_str());
        return false;
    }
    if (http->Write(head.data(), head.size()) <= 0) {
        http->Close();
        return false;
    }

    std::FILE* file = nullptr;
    {
        board_begin_sdcard_access();
        file = std::fopen(path.c_str(), "rb");
        board_end_sdcard_access();
    }
    if (file == nullptr) {
        ESP_LOGE(_tag, "upload: open file failed: path=%s errno=%d", path.c_str(), errno);
        http->Close();
        return false;
    }

    std::vector<char> buffer(kUploadChunkSize);
    bool write_ok = true;
    for (;;) {
        std::size_t got = 0;
        {
            board_begin_sdcard_access();
            got = std::fread(buffer.data(), 1, buffer.size(), file);
            board_end_sdcard_access();
        }
        if (got == 0) {
            break;
        }
        if (http->Write(buffer.data(), got) <= 0) {
            write_ok = false;
            break;
        }
    }
    {
        board_begin_sdcard_access();
        std::fclose(file);
        board_end_sdcard_access();
    }
    if (!write_ok) {
        ESP_LOGE(_tag, "upload: send body failed: path=%s", path.c_str());
        http->Close();
        return false;
    }

    if (http->Write(tail.data(), tail.size()) <= 0) {
        http->Close();
        return false;
    }
    http->Write(buffer.data(), 0);  // chunked 结束块

    const int status = http->GetStatusCode();
    const std::string body = http->ReadAll();
    http->Close();

    const bool accepted = (status == 200) &&
                          (body.find("\"success\":true") != std::string::npos ||
                           body.find("\"success\": true") != std::string::npos);
    ESP_LOGI(_tag, "upload done: path=%s status=%d accepted=%d", path.c_str(), status,
             (int)accepted);

    // 解析响应 JSON 的 notes 字段，写入同目录 .txt
    if (accepted && !body.empty()) {
        cJSON* root = cJSON_Parse(body.c_str());
        if (root != nullptr) {
            cJSON* notes = cJSON_GetObjectItemCaseSensitive(root, "notes");
            if (notes != nullptr && cJSON_IsString(notes) && notes->valuestring != nullptr &&
                strlen(notes->valuestring) > 0) {
                std::string txt_path = path;
                const auto dot = txt_path.rfind('.');
                if (dot != std::string::npos) {
                    txt_path = txt_path.substr(0, dot);
                }
                txt_path += ".txt";
                {
                    board_begin_sdcard_access();
                    FILE* f = std::fopen(txt_path.c_str(), "wb");
                    if (f != nullptr) {
                        const bool ok = std::fwrite(notes->valuestring, 1U,
                                                    strlen(notes->valuestring), f) ==
                                        strlen(notes->valuestring);
                        std::fclose(f);
                        ESP_LOGI(_tag, "notes saved: %s ok=%d", txt_path.c_str(), (int)ok);
                    }
                    board_end_sdcard_access();
                }
            }
            cJSON_Delete(root);
        }
    }
    return accepted;
}

void set_pending_video_playback(const std::string& path)
{
    _pending_video_playback = path;
}

std::string take_pending_video_playback()
{
    std::string path = std::move(_pending_video_playback);
    _pending_video_playback.clear();
    return path;
}

}  // namespace hal_bridge
