/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <string>

/**
 * @brief 米宝自定义配置（NVS 命名空间 "mibao"）
 *
 * - upload_url    : 录音文件上传地址（默认空，空 = 不上传，仅本地保存）
 * - iot_url       : 硅基一号温室执行器控制服务基础地址
 *                   （默认 http://10.51.1.205:5000，实际请求拼 /actuator_control）
 * - ota_url       : OTA 升级服务器地址（默认空，空 = 未配置）
 * - ai_chat_enabled : 是否允许对话入口进入 xiaozhi（默认 false，纯占位页）
 * - auto_prov       : 是否允许开机 Wi-Fi 连接失败/未配网时自动进入热点配网
 *                   （默认 false：配网统一从 设置→Wi-Fi→Hotspot Setup 手动进入）
 */
namespace mibao {

std::string getUploadUrl();
void setUploadUrl(const std::string& url);

std::string getIotUrl();
void setIotUrl(const std::string& url);

std::string getOtaUrl();
void setOtaUrl(const std::string& url);

// 把 mibao/ota_url 同步写入 xiaozhi 框架使用的 NVS "wifi"/"ota_url" 键。
// xiaozhi 的 Ota::GetCheckVersionUrl()（fw/xiaozhi-esp32/main/ota.cc）只读
// "wifi" 命名空间的 ota_url，不读 mibao 命名空间；若不同步，设备端配置的
// ota_url 永远无法被 AI 对话链路的 OTA 激活使用。
void syncOtaUrlToXiaozhi();

bool isAiChatEnabled();
void setAiChatEnabled(bool enabled);

bool isAutoProvisioningEnabled();
void setAutoProvisioningEnabled(bool enabled);

// 从后台服务器 @ GET {upload_url origin}/api/device/config 拉取
// upload_url/iot_url/ota_url 并写入 NVS。返回是否成功（HTTP 200 且解析成功）。
bool pullDeviceConfigFromServer();

}  // namespace mibao
