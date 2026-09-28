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
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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

// ===== UDP 广播自动发现服务器 =====
// Mac（服务器）IP 由 DHCP 分配可能变化，设备里存的地址一旦失效就连不上，
// 而设备又无法通过 OTA 自我修复（鸡生蛋）。因此设备启动后向局域网广播探测包，
// 服务器端（xiaozhi-server/core/api/discovery_responder.py）回复其当前 IP。
//   设备 -> 服务器: "MIBAO_DISCOVER"
//   服务器 -> 设备: "MIBAO_SERVER|<ip>|<ws_port>|<http_port>"
constexpr int kDiscoveryPort           = 8004;
// WiFi 链路易有较大抖动（实测往返可达数百毫秒），超时给足，避免回复晚到被判失败
constexpr int kDiscoveryTimeoutMs      = 3000;
constexpr int kDiscoveryRetry          = 3;
constexpr const char* kDiscoveryProbe  = "MIBAO_DISCOVER";

// 广播探测一次，成功返回服务器 IP（失败返回空串）
std::string discoverServerIpOnce(int timeout_ms)
{
    const int sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return "";
    }

    int broadcast = 1;
    ::setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    struct timeval tv = {};
    tv.tv_sec         = timeout_ms / 1000;
    tv.tv_usec        = (timeout_ms % 1000) * 1000;
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dest = {};
    dest.sin_family         = AF_INET;
    dest.sin_port           = htons(kDiscoveryPort);
    dest.sin_addr.s_addr    = htonl(INADDR_BROADCAST);

    ::sendto(sock, kDiscoveryProbe, std::strlen(kDiscoveryProbe), 0,
             reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest));

    char buf[160] = {0};
    struct sockaddr_in from = {};
    socklen_t from_len      = sizeof(from);
    const int n             = ::recvfrom(sock, buf, sizeof(buf) - 1, 0,
                                         reinterpret_cast<struct sockaddr*>(&from), &from_len);
    ::close(sock);

    if (n <= 0) {
        mclog::tagWarn(_tag, "discovery: no reply (n={}, errno={})", n, errno);
        return "";
    }
    buf[n] = '\0';

    char ip[64] = {0};
    if (sscanf(buf, "MIBAO_SERVER|%63[^|]", ip) != 1 || ip[0] == '\0') {
        // 容错：只回纯 IP 的实现
        if (sscanf(buf, "%63s", ip) != 1) {
            return "";
        }
    }
    // 基本的 IPv4 校验（至少含一个点且不含空格）
    if (std::strchr(ip, '.') == nullptr) {
        return "";
    }
    return std::string(ip);
}

// 带重试的广播发现（WiFi 刚连上时首包可能丢）
std::string discoverServerIp()
{
    for (int i = 0; i < kDiscoveryRetry; ++i) {
        const std::string ip = discoverServerIpOnce(kDiscoveryTimeoutMs);
        if (!ip.empty()) {
            return ip;
        }
    }
    return "";
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

// 把发现到的服务器地址写入 NVS（仅在变化时写，减少 flash 擦写）。
// 同时更新 xiaozhi 的 wifi/ota_url，保证 AI 对话的 OTA 走新地址。
bool applyDiscoveredAddress(const std::string& ip)
{
    if (ip.empty()) {
        return false;
    }
    const std::string origin  = "http://" + ip + ":8003";
    const std::string ota_url = origin + "/xiaozhi/ota/";
    const std::string up_url  = origin + "/mibao/meeting/transcribe";

    Settings mibao_r(kNvsNamespace, false);
    const std::string cur_ota = mibao_r.GetString(kOtaUrlKey, "");
    const std::string cur_up  = mibao_r.GetString(kUploadUrlKey, "");
    if (extractOrigin(cur_ota) == origin && extractOrigin(cur_up) == origin) {
        return true;  // 地址未变化，无需写 NVS
    }

    Settings mibao_w(kNvsNamespace, true);
    mibao_w.SetString(kOtaUrlKey, ota_url);
    mibao_w.SetString(kUploadUrlKey, up_url);

    Settings wifi_w("wifi", true);
    wifi_w.SetString(kOtaUrlKey, ota_url);

    mclog::tagInfo(_tag, "server address updated by discovery: {}", origin);
    return true;
}

// 后台周期发现任务：设备长时间运行期间 Mac IP 变化也能自动跟随
void discoveryWatchTask(void* arg)
{
    (void)arg;
    // 首次稍等，让 WiFi 完成连接（失败会在后续周期重试）
    vTaskDelay(pdMS_TO_TICKS(15000));
    while (true) {
        const std::string ip = discoverServerIpOnce(3000);
        if (!ip.empty()) {
            applyDiscoveredAddress(ip);
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
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
    //
    // 注意：服务器 IP 变化由 UDP 广播发现处理（见 discoverServerIp /
    // pullDeviceConfigFromServer），发现成功后会覆盖此处的地址，
    // 因此这里不再做写死 IP 的迁移。
    Settings wifi_settings("wifi", false);
    std::string url = wifi_settings.GetString(kOtaUrlKey, "");
    if (!url.empty()) {
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
    std::string origin;

    // ① 优先 UDP 广播发现服务器：Mac IP 变化后仍能找到（最可靠）
    const std::string discovered_ip = discoverServerIp();
    if (!discovered_ip.empty()) {
        origin = "http://" + discovered_ip + ":8003";
        // 同步写入 NVS：后续 xiaozhi OTA（读 wifi/ota_url）、录音转纪要上传
        // （读 mibao/upload_url）都会自动使用新地址。
        applyDiscoveredAddress(discovered_ip);
        mclog::tagInfo(_tag, "discovered server at {} (UDP broadcast)", discovered_ip);
    } else {
        // ② 回退：按原有逻辑从配置的 upload_url / ota_url 推导
        mclog::tagWarn(_tag, "UDP discovery failed, fallback to configured server address");
        std::string base_url = getUploadUrl();
        if (base_url.empty()) {
            base_url = getOtaUrl();
        }
        if (base_url.empty()) {
            mclog::tagInfo(_tag, "upload_url and ota_url are both empty, skip pulling config");
            return false;
        }
        origin = extractOrigin(base_url);
        if (origin.empty()) {
            mclog::tagError(_tag, "invalid base_url, cannot extract origin: {}", base_url);
            return false;
        }
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
    // 默认开启：AI 对话是设备主功能，不应要求用户先手动打开开关
    Settings settings(kNvsNamespace, false);
    return settings.GetBool(kAiChatKey, true);
}

void setAiChatEnabled(bool enabled)
{
    Settings settings(kNvsNamespace, true);
    settings.SetBool(kAiChatKey, enabled);
}

bool isAutoProvisioningEnabled()
{
    // 默认开启：换到没有已知 Wi-Fi 的环境时，设备会自动开配网热点，
    // 用户用手机即可完成配网，无需在屏幕上找菜单（"能自动就不手动"）。
    Settings settings(kNvsNamespace, false);
    return settings.GetBool(kAutoProvKey, true);
}

void setAutoProvisioningEnabled(bool enabled)
{
    Settings settings(kNvsNamespace, true);
    settings.SetBool(kAutoProvKey, enabled);
}


bool refreshServerAddressFromDiscovery(int timeout_ms)
{
    const std::string ip = discoverServerIpOnce(timeout_ms > 0 ? timeout_ms : 1500);
    if (ip.empty()) {
        return false;
    }
    return applyDiscoveredAddress(ip);
}

void startServerDiscoveryWatch()
{
    static bool started = false;
    if (started) {
        return;
    }
    started = true;
    if (xTaskCreate(discoveryWatchTask, "mibao_disc", 4096, nullptr, 5, nullptr) != pdPASS) {
        started = false;
        mclog::tagWarn(_tag, "failed to create server discovery watch task");
    } else {
        mclog::tagInfo(_tag, "server discovery watch task started (every 30s)");
    }
}

}  // namespace mibao
