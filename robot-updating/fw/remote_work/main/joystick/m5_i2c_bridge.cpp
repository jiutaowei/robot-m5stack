/*
 * SPDX-License-Identifier: MIT
 *
 * Use the I2C bus already initialized and owned by M5Unified. The previous
 * implementation created a legacy i2c_bus handle on the same controller,
 * which aborts on ESP-IDF 5.5 when M5Unified uses the new driver.
 */
#include "m5_i2c_bridge.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

namespace {
constexpr uint32_t kJoystickI2cFrequency = 100000;
i2c_master_bus_handle_t s_bus = nullptr;
i2c_master_dev_handle_t s_device = nullptr;
}

extern "C" bool m5_i2c_bridge_init(uint8_t address)
{
    if (s_device != nullptr) {
        return true;
    }

    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_0,
        .scl_io_num = GPIO_NUM_26,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {.enable_internal_pullup = true, .allow_pd = false},
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE("I2C Joystick", "Failed to create joystick bus: %s", esp_err_to_name(err));
        return false;
    }

    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = kJoystickI2cFrequency,
        .scl_wait_us = 0,
        .flags = {.disable_ack_check = false},
    };
    err = i2c_master_bus_add_device(s_bus, &device_config, &s_device);
    if (err != ESP_OK) {
        ESP_LOGE("I2C Joystick", "Failed to add joystick device: %s", esp_err_to_name(err));
        s_device = nullptr;
        return false;
    }

    err = i2c_master_probe(s_bus, address, 100);
    if (err == ESP_OK) {
        ESP_LOGI("I2C Joystick", "Found joystick at address 0x%02X on GPIO0/GPIO26", address);
    } else {
        ESP_LOGW("I2C Joystick", "Joystick probe failed at address 0x%02X: %s", address,
                 esp_err_to_name(err));
    }
    return true;
}

extern "C" bool m5_i2c_bridge_read(uint8_t address, uint8_t reg, uint8_t *data, size_t length)
{
    (void)address;
    if (s_device == nullptr) {
        return false;
    }
    return i2c_master_transmit_receive(s_device, &reg, 1, data, length, 100) == ESP_OK;
}
