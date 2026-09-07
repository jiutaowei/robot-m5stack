/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "mibao_config.h"
#include <settings.h>
#include <board.h>
#include <cJSON.h>
#include <mooncake_log.h>
#include <string_view>

namespace mibao {

namespace {

static const std::string_view _tag = "Mibao-Config";

// 配置拉取 HTTP 超时：不能阻塞太久（独立任务中执行，也需控制在合理范围）
constexpr int kPullConfigTimeoutMs = 5000;

// NVS 命名空间（<=15 字符）与键名（NVS 键 <=15 字符）
constexpr const char* kNvsNamespace    = "mibao";
constexpr const char* kUploadUrlKey    = "upload_url";
constexpr const char* kIotUrlKey       = "iot_url";
constexpr const char* kOtaUrlKey       = "ota_url";
constexpr const char* kAiChatKey       = "ai_chat";
constexpr const char* kAutoProvKey     = "auto_prov";
constexpr const char* kDefaultIotUrl   = "http://10.51.1.205:5000";

// 把 URL 中的旧 IP 替换为新 IP（服务器地址迁移用）
std::string replaceIp(const std::string& url, const std::string& old_ip, const std::string& new_ip)
{
    std::string result = url;
    size_t pos = 0;
    while ((pos = result.find(old_ip, pos)) != std::string::npos) {
        result.replace(pos, old_ip.size(), new_ip);
        pos += new_ip.size();
    }
    return result;
}

// 提取 URL 的 origin（scheme://host:port，去掉路径部分）。
// 例如 upload_url=http://192.168.1.5:8000/api/recording/upload -> http://192.168.1.5:8000
std::string extractOrigin(const std::string& url)
{
    const std::string scheme = "://";
    const size_t scheme_pos  = url.find(scheme);
    if (scheme_pos == std::string::npos) {
        // 不含协议的地址视为非法，返回空
        return "";
    }
    const size_t path_pos = url.find('/', scheme_pos + scheme.size());
    if (path_pos == std::string::npos) {
        // 没有路径部分，整个 URL 就是 origin
        return url;
    }
    return url.substr(0, path_pos);
}

}  // namespace

std::string getUploadUrl()
{
    Settings settings(kNvsNamespace, false);
    std::string url = settings.GetString(kUploadUrlKey, "");
    if (!url.empty()) {
        return url;
    }
    // 未显式配置 upload_url 时，从 ota_url（同一台服务器）自动推导：
    // ota_url 形如 http://ip:8003/xiaozhi/ota/，
    // 推导为 http://ip:8003/mibao/meeting/transcribe
    // 用 getOtaUrl()（而非直接读 mibao ns），它处理了 wifi/mibao 双
    // namespace 与旧 IP 自动迁移，保证拿到的是当前可用的服务器地址。
    const std::string ota_url = getOtaUrl();
    if (!ota_url.empty()) {
        const std::string origin = extractOrigin(ota_url);
        if (!origin.empty()) {
            return origin + "/mibao/meeting/transcribe";
        }
    }
    return "";
}

void setUploadUrl(const std::string& url)
{
    Settings settings(kNvsNamespace, true);
    settings.SetString(kUploadUrlKey, url);
}

std::string getIotUrl()
{
    Settings settings(kNvsNamespace, false);
    return settings.GetString(kIotUrlKey, kDefaultIotUrl);
}

void setIotUrl(const std::string& url)
{
    Settings settings(kNvsNamespace, true);
    settings.SetString(kIotUrlKey, url);
}

std::string getOtaUrl()
{
    // 优先读 wifi/ota_url：Web 配网（192.168.4.1 Advanced tab）写入的位置，
    // 与 xiaozhi OTA 读取（ota.cc::GetCheckVersionUrl）一致。
    // 修复"Web 配网保存 OTA URL 后设备屏幕看不到"的双 namespace 不同步问题。
    Settings wifi_settings("wifi", false);
    std::string url = wifi_settings.GetString(kOtaUrlKey, "");
    if (!url.empty()) {
        // 旧服务器地址自动迁移：192.168.1.144 已废弃（Mac 换 IP），
        // 若配置还是旧地址则改为当前服务器 192.168.1.224。
        if (url.find("192.168.1.144") != std::string::npos) {
            const std::string migrated = replaceIp(url, "192.168.1.144", "192.168.1.224");
            Settings wifi_w("wifi", true);
            wifi_w.SetString(kOtaUrlKey, migrated);
            Settings mibao_w(kNvsNamespace, true);
            mibao_w.SetString(kOtaUrlKey, migrated);
            mclog::tagInfo(_tag, "migrated ota_url: {} -> {}", url, migrated);
            return migrated;
        }
        return url;
    }
    // 回退到 mibao/ota_url：设备屏幕 MibaoUrlWorker 写入的位置，向后兼容旧数据。
    Settings mibao_settings(kNvsNamespace, false);
    return mibao_settings.GetString(kOtaUrlKey, "");
}

void setOtaUrl(const std::string& url)
{
    Settings settings(kNvsNamespace, true);
    settings.SetString(kOtaUrlKey, url);
}

// xiaozhi 框架的 OTA 激活只读 NVS "wifi"/"ota_url"（fw/xiaozhi-esp32/main/ota.cc），
// 与 mibao 的 ota_url 是两个命名空间。此处把 mibao/ota_url 同步过去，保证设备端
// 配置的 ota_url 能被 AI 对话链路的 OTA 激活真正使用。
void syncOtaUrlToXiaozhi()
{
    const std::string url = getOtaUrl();
    Settings settings("wifi", true);
    if (url.empty()) {
        // 空则清除，避免残留旧地址影响 xiaozhi 回退官方云。
        settings.EraseKey(kOtaUrlKey);
    } else {
        settings.SetString(kOtaUrlKey, url);
    }
}

bool pullDeviceConfigFromServer()
{
    // 优先使用 upload_url 作为服务器基础地址。
    // 如果 upload_url 未配置，则尝试从 ota_url 中提取 origin。
    std::string base_url = getUploadUrl();
    if (base_url.empty()) {
        base_url = getOtaUrl();
    }

    if (base_url.empty()) {
        mclog::tagInfo(_tag, "upload_url and ota_url are both empty, skip pulling config");
        return false;
    }

    const std::string origin = extractOrigin(base_url);
    if (origin.empty()) {
        mclog::tagError(_tag, "invalid base_url, cannot extract origin: {}", base_url);
        return false;
    }

    const std::string config_url = origin + "/api/device/config";

    auto network = Board::GetInstance().GetNetwork();
    auto http    = network ? network->CreateHttp(0) : nullptr;
    if (!http) {
        mclog::tagError(_tag, "failed to create http client for config pull");
        return false;
    }

    // 设置合理超时，避免阻塞任务太久
    http->SetTimeout(kPullConfigTimeoutMs);

    if (!http->Open("GET", config_url)) {
        mclog::tagError(_tag, "failed to open config request: {}", config_url);
        http->Close();
        return false;
    }

    const int status_code = http->GetStatusCode();
    if (status_code != 200) {
        mclog::tagError(_tag, "pull config failed, status code: {}", status_code);
        http->Close();
        return false;
    }

    const std::string response = http->ReadAll();
    http->Close();

    cJSON* root = cJSON_Parse(response.c_str());
    if (!root) {
        mclog::tagError(_tag, "failed to parse config json");
        return false;
    }

    // upload_url/iot_url/ota_url 非空才写入 NVS，避免覆盖本地已有配置
    cJSON* upload_url_json = cJSON_GetObjectItem(root, "upload_url");
    if (upload_url_json && cJSON_IsString(upload_url_json) && upload_url_json->valuestring != nullptr &&
        strlen(upload_url_json->valuestring) > 0) {
        setUploadUrl(upload_url_json->valuestring);
    }

    cJSON* iot_url_json = cJSON_GetObjectItem(root, "iot_url");
    if (iot_url_json && cJSON_IsString(iot_url_json) && iot_url_json->valuestring != nullptr &&
        strlen(iot_url_json->valuestring) > 0) {
        setIotUrl(iot_url_json->valuestring);
    }

    cJSON* ota_url_json = cJSON_GetObjectItem(root, "ota_url");
    if (ota_url_json && cJSON_IsString(ota_url_json) && ota_url_json->valuestring != nullptr &&
        strlen(ota_url_json->valuestring) > 0) {
        setOtaUrl(ota_url_json->valuestring);
        // 同步到 xiaozhi 的 NVS "wifi"/"ota_url"，供 AI 对话链路 OTA 激活使用。
        syncOtaUrlToXiaozhi();
    }

    mclog::tagInfo(_tag, "device config pulled from server: {}", config_url);

    cJSON_Delete(root);
    return true;
}

bool isAiChatEnabled()
{
    Settings settings(kNvsNamespace, false);
    return settings.GetBool(kAiChatKey, false);
}

void setAiChatEnabled(bool enabled)
{
    Settings settings(kNvsNamespace, true);
    settings.SetBool(kAiChatKey, enabled);
}

bool isAutoProvisioningEnabled()
{
    Settings settings(kNvsNamespace, false);
    return settings.GetBool(kAutoProvKey, false);
}

void setAutoProvisioningEnabled(bool enabled)
{
    Settings settings(kNvsNamespace, true);
    settings.SetBool(kAutoProvKey, enabled);
}

}  // namespace mibao
