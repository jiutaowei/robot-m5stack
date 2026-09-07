/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <algorithm>
#include <mooncake_log.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_err.h>
#include <esp_system.h>
#include <esp_event.h>
#include <lwip/err.h>
#include <lwip/sys.h>
#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <espnow.h>
#include <esp_check.h>
#include <deque>
#include <mutex>

static const std::string_view _tag = "HAL-EspNow";

namespace {

constexpr uint8_t kStackChanReceiverId = 1;

std::mutex s_remote_command_mutex;
std::deque<EspNowRemoteCommandEvent> s_remote_command_queue;
bool s_have_remote_sequence = false;
uint8_t s_last_remote_sequence = 0;
bool s_espnow_started = false;

bool isRemoteCommand(uint8_t value)
{
    return value == static_cast<uint8_t>(EspNowRemoteCommand::MeetingPrimary) ||
           value == static_cast<uint8_t>(EspNowRemoteCommand::MeetingEnd);
}

}  // namespace

static EventGroupHandle_t s_wifi_event_group = NULL;
static const int WIFI_CONNECTED_BIT          = BIT0;
static const int WIFI_DISCONNECTED_BIT       = BIT1;
static const int WIFI_FAIL_BIT               = BIT2;
static const int WIFI_STARTED_BIT            = BIT3;

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    const char* TAG = "WiFi";

    // Wifi started
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_STARTED_BIT);
    }

    // Disconnected
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }

    // Connected
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void _wifi_init(int channel = 1)
{
    mclog::tagInfo(_tag, "wifi init");

    // ESP_ERROR_CHECK(nvs_flash_init());
    // ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    if (!s_wifi_event_group) {
        s_wifi_event_group = xEventGroupCreate();
    }

    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr));
    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr, nullptr));

    ESP_ERROR_CHECK(esp_wifi_start());

    channel = std::clamp(channel, 1, 13);

    mclog::tagInfo(_tag, "wifi channel set to {}", channel);

    // 建议先开启混杂模式再设信道，确保射频频率被强制锁定
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    ESP_ERROR_CHECK(esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(false));
}

// `espnow_set_config_for_data_type()` invokes this after the Espressif ESP-NOW
// component has validated and removed its 20-byte transport header.  The
// remote's nine-byte meeting payload therefore starts at data[0].
static esp_err_t handle_espnow_packet(uint8_t* src_addr, void* data, size_t size,
                                      wifi_pkt_rx_ctrl_t* rx_ctrl)
{
    const char* TAG = "EspNow";

    if (src_addr == nullptr || data == nullptr || rx_ctrl == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    const auto* payload = static_cast<const uint8_t*>(data);
    std::vector<uint8_t> received_data(payload, payload + size);

    // Extended StackChan remote packet:
    // [0] target id, [1-2] yaw, [3-4] pitch, [5-6] speed,
    // [7] command, [8] command sequence.
    // Only accept commands addressed to this receiver or broadcast.
    if (received_data.size() >= 9) {
        const uint8_t target_id = received_data[0];
        const uint8_t command   = received_data[7];
        const uint8_t sequence  = received_data[8];

        if ((target_id == 0 || target_id == kStackChanReceiverId) && isRemoteCommand(command)) {
            std::lock_guard<std::mutex> lock(s_remote_command_mutex);
            if (!s_have_remote_sequence || sequence != s_last_remote_sequence) {
                s_have_remote_sequence = true;
                s_last_remote_sequence = sequence;
                s_remote_command_queue.push_back({static_cast<EspNowRemoteCommand>(command), sequence});
                ESP_LOGI(TAG, "meeting command accepted: command=%u sequence=%u from " MACSTR " channel=%u rssi=%d",
                         command, sequence, MAC2STR(src_addr), rx_ctrl->channel, rx_ctrl->rssi);
            }
        }
    }

    GetHAL().onEspNowData.emit(received_data);
    return ESP_OK;
}

bool select_espnow_channel(int channel)
{
    channel = std::clamp(channel, 1, 13);

    uint8_t current_channel = 0;
    wifi_second_chan_t second_channel = WIFI_SECOND_CHAN_NONE;
    esp_err_t ret = esp_wifi_get_channel(&current_channel, &second_channel);
    if (ret != ESP_OK) {
        mclog::tagError(_tag, "cannot read Wi-Fi channel: {}", esp_err_to_name(ret));
        return false;
    }
    if (current_channel == channel) {
        mclog::tagInfo(_tag, "ESP-NOW channel confirmed: {}", channel);
        return true;
    }

    // ESP-NOW and station Wi-Fi share one radio.  This operation can be
    // rejected while the station is associated with an AP on another channel.
    ret = esp_wifi_set_promiscuous(true);
    if (ret == ESP_OK) {
        ret = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        const esp_err_t disable_ret = esp_wifi_set_promiscuous(false);
        if (ret == ESP_OK) {
            ret = disable_ret;
        }
    }

    esp_wifi_get_channel(&current_channel, &second_channel);
    if (ret != ESP_OK || current_channel != channel) {
        mclog::tagError(_tag,
                         "ESP-NOW needs channel {}, but radio remains on {} ({}). Set the remote to the actual "
                         "channel or disconnect Wi-Fi.",
                         channel, current_channel, esp_err_to_name(ret));
        return false;
    }
    mclog::tagInfo(_tag, "ESP-NOW channel switched to {}", channel);
    return true;
}

void Hal::startEspNow(int channel)
{
    mclog::tagInfo(_tag, "start EspNow on channel {}", channel);

    wifi_mode_t wifi_mode = WIFI_MODE_NULL;
    const esp_err_t wifi_status = esp_wifi_get_mode(&wifi_mode);
    if (wifi_status == ESP_ERR_WIFI_NOT_INIT) {
        _wifi_init(channel);
    } else if (wifi_status != ESP_OK) {
        mclog::tagError(_tag, "cannot initialise ESP-NOW: Wi-Fi state is {}", esp_err_to_name(wifi_status));
        return;
    } else if (!select_espnow_channel(channel)) {
        return;
    }

    if (s_espnow_started) {
        mclog::tagInfo(_tag, "EspNow already started; channel was rechecked");
        return;
    }

    // The actual remote firmware sends frames through Espressif's ESP-NOW
    // component.  Its receiver removes the 20-byte component header before
    // delivering the 9-byte meeting payload to handle_espnow_packet().
    espnow_config_t config = ESPNOW_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(espnow_init(&config));
    ESP_ERROR_CHECK(espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_DATA, true, handle_espnow_packet));

    s_espnow_started = true;

    mclog::tagInfo(_tag, "factory mac: {}", getFactoryMacString());
}

bool Hal::pollEspNowRemoteCommand(EspNowRemoteCommandEvent& event)
{
    std::lock_guard<std::mutex> lock(s_remote_command_mutex);
    if (s_remote_command_queue.empty()) {
        return false;
    }

    event = s_remote_command_queue.front();
    s_remote_command_queue.pop_front();
    return true;
}

bool Hal::espNowSend(const std::vector<uint8_t>& data, const uint8_t* destAddr)
{
    mclog::tagInfo(_tag, "send data with size: {}", data.size());

    const uint8_t broadcast[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    const uint8_t* destination = destAddr == nullptr ? broadcast : destAddr;
    const esp_err_t ret = espnow_send(ESPNOW_DATA_TYPE_DATA, destination, data.data(), data.size(), nullptr,
                                      portMAX_DELAY);

    if (ret != ESP_OK) {
        mclog::tagError(_tag, "send failed: {}", esp_err_to_name(ret));
        return false;
    }
    return true;
}

#include <driver/gpio.h>

void Hal::setLaserEnabled(bool enabled)
{
    static bool laser_enabled = false;
    static bool is_inited     = false;

    if (laser_enabled == enabled) {
        return;
    }

    const gpio_num_t laser_pin = GPIO_NUM_2;

    if (!is_inited) {
        gpio_reset_pin(laser_pin);
        gpio_set_direction(laser_pin, GPIO_MODE_OUTPUT);
        gpio_set_pull_mode(laser_pin, GPIO_PULLUP_ONLY);
        is_inited = true;
    }

    mclog::tagInfo(_tag, "set laser {}", enabled ? "enabled" : "disabled");

    if (enabled) {
        gpio_set_level(laser_pin, 1);
    } else {
        gpio_set_level(laser_pin, 0);
    }
    laser_enabled = enabled;
}
