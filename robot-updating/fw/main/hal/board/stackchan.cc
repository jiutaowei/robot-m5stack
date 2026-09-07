#include "wifi_board.h"
#include "cores3_audio_codec.h"
#include "display/lcd_display.h"
#include "stackchan_display.h"
#include "application.h"
#include "config.h"
#include "power_save_timer.h"
#include "i2c_device.h"
#include "axp2101.h"
#include "settings.h"
#include <hal/mibao_config.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <driver/i2c_master.h>
#include <wifi_station.h>
#include <ssid_manager.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_ili9341.h>
#include <esp_timer.h>
#include <esp_rom_gpio.h>
#include <soc/spi_periph.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#if SDCARD_ENABLED
#include <driver/sdspi_host.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#endif
#include <algorithm>
#include <atomic>
#include "stackchan_camera.h"
#include "hal_bridge.h"

#define TAG "M5Stack-StackChan-Board"

namespace {
std::atomic_uint32_t g_sdcard_access_depth{0};
#if SDCARD_ENABLED
bool g_sdcard_mounted          = false;
int g_sdcard_mount_attempts    = 0;
esp_err_t g_sdcard_mount_error = ESP_OK;

// Maximum number of remount attempts after the boot-time attempt.  A failed
// boot mount (card slow to power up, transient SPI contention on the shared
// GPIO35/36/37 bus, ...) would otherwise leave the card "unmounted" forever.
constexpr int kMaxSdcardRemountAttempts = 3;
#endif

// CoreS3/StackChan physically shares GPIO35/36/37 between the LCD and the
// SD card: GPIO35 is LCD D/C or SD MISO, while GPIO36/37 are SCLK/MOSI for
// both peripherals.  The two SPI controllers cannot own the same GPIO-matrix
// output routes at the same time, so the routes must be switched as a group.
void board_prepare_lcd_bus()
{
    // LCD is write-only.  Route the shared clock/data outputs to SPI3.
    esp_rom_gpio_connect_out_signal(SDCARD_SPI_SCLK,
                                    spi_periph_signal[SPI3_HOST].spiclk_out,
                                    false, false);
    esp_rom_gpio_connect_out_signal(SDCARD_SPI_MOSI,
                                    spi_periph_signal[SPI3_HOST].spid_out,
                                    false, false);

    // GPIO35 is LCD D/C while the LCD owns the bus. Disconnect SD MISO from
    // the input matrix and return the pad to ordinary GPIO output control.
    esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT,
                                   spi_periph_signal[SDCARD_SPI_HOST].spiq_in,
                                   false);
    esp_rom_gpio_connect_out_signal(SDCARD_SPI_MISO, SIG_GPIO_OUT_IDX, false, false);
    gpio_set_direction(SDCARD_SPI_MISO, GPIO_MODE_OUTPUT);
    gpio_set_level(SDCARD_SPI_MISO, 1);
    gpio_set_pull_mode(SDCARD_SPI_MISO, GPIO_FLOATING);
    gpio_set_pull_mode(SDCARD_SPI_SCLK, GPIO_FLOATING);
    gpio_set_pull_mode(SDCARD_SPI_MOSI, GPIO_FLOATING);
}

#if SDCARD_ENABLED
void board_prepare_sdcard_bus_internal()
{
    // Restore all three shared bus routes to SPI2 before any SD transaction.
    esp_rom_gpio_connect_out_signal(SDCARD_SPI_SCLK,
                                    spi_periph_signal[SDCARD_SPI_HOST].spiclk_out,
                                    false, false);
    esp_rom_gpio_connect_out_signal(SDCARD_SPI_MOSI,
                                    spi_periph_signal[SDCARD_SPI_HOST].spid_out,
                                    false, false);
    esp_rom_gpio_connect_in_signal(SDCARD_SPI_MISO,
                                   spi_periph_signal[SDCARD_SPI_HOST].spiq_in,
                                   false);
    gpio_set_direction(SDCARD_SPI_MISO, GPIO_MODE_INPUT);
}
#endif
}

#define XPOWERS_AXP2101_ICC_CHG_SET (0x62)

class Pmic : public Axp2101 {
public:
    /**
     * @brief axp2101 charge currnet voltage parameters.
     */
    typedef enum __xpowers_axp2101_chg_curr {
        XPOWERS_AXP2101_CHG_CUR_0MA,
        XPOWERS_AXP2101_CHG_CUR_100MA = 4,
        XPOWERS_AXP2101_CHG_CUR_125MA,
        XPOWERS_AXP2101_CHG_CUR_150MA,
        XPOWERS_AXP2101_CHG_CUR_175MA,
        XPOWERS_AXP2101_CHG_CUR_200MA,
        XPOWERS_AXP2101_CHG_CUR_300MA,
        XPOWERS_AXP2101_CHG_CUR_400MA,
        XPOWERS_AXP2101_CHG_CUR_500MA,
        XPOWERS_AXP2101_CHG_CUR_600MA,
        XPOWERS_AXP2101_CHG_CUR_700MA,
        XPOWERS_AXP2101_CHG_CUR_800MA,
        XPOWERS_AXP2101_CHG_CUR_900MA,
        XPOWERS_AXP2101_CHG_CUR_1000MA,
    } xpowers_axp2101_chg_curr_t;

    // Power Init
    Pmic(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : Axp2101(i2c_bus, addr)
    {
        uint8_t data = ReadReg(0x90);
        data |= 0b10110100;
        WriteReg(0x90, data);
        // WriteReg(0x99, (0b11110 - 5));
        WriteReg(0x97, (0b11110 - 2));
        WriteReg(0x69, 0b00110101);
        WriteReg(0x30, 0b111111);
        WriteReg(0x90, 0xBF);
        WriteReg(0x94, 33 - 5);
        WriteReg(0x95, 33 - 5);
        WriteReg(0x27, 0x00);

        auto ret = setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_700MA);
        if (!ret) {
            ESP_LOGE(TAG, "Set charge current failed");
        } else {
            ESP_LOGI(TAG, "Set charge current success");
        }

        SetBrightness(0);
    }

    void SetBrightness(uint8_t brightness)
    {
        if (brightness == 0) {
            // DLDO1 off
            uint8_t val = ReadReg(0x90);
            WriteReg(0x90, val & 0x7F);
        } else {
            // 映射计算：将 1~100 映射到 寄存器值 20~28
            // 公式：MinReg + (input * (MaxReg - MinReg) / MaxInput)
            // 20 + (brightness * 8 / 100)
            if (brightness > 100) {
                brightness = 100;
            }
            uint8_t reg_val = 20 + ((uint16_t)brightness * 8 / 100);
            WriteReg(0x99, reg_val);

            // Make sure DLDO1 on
            uint8_t val = ReadReg(0x90);
            if (!(val & 0x80)) {
                WriteReg(0x90, val | 0x80);
            }
        }
    }

    /**
     * @brief Set charge current.
     * @param  opt: See xpowers_axp2101_chg_curr_t enum for details.
     * @retval
     */
    bool setChargerConstantCurr(uint8_t opt)
    {
        if (opt > XPOWERS_AXP2101_CHG_CUR_1000MA) {
            return false;
        }
        int val = ReadReg(XPOWERS_AXP2101_ICC_CHG_SET);
        if (val == -1) {
            return false;
        }
        val &= 0xE0;
        WriteReg(XPOWERS_AXP2101_ICC_CHG_SET, val | opt);
        return true;
    }

    bool IsExternalPowerConnected()
    {
        const uint8_t power_status      = ReadReg(0x01);
        const uint8_t current_direction = (power_status & 0b01100000) >> 5;
        const bool is_charging_done     = (power_status & 0b00000111) == 0b00000100;

        // Treat any non-discharging state as externally powered so a plugged-in cable
        // still counts even after the battery is full.
        return current_direction != 2 || is_charging_done;
    }
};

class CustomBacklight : public Backlight {
public:
    CustomBacklight(Pmic* pmic) : pmic_(pmic)
    {
    }

    void SetBrightnessImpl(uint8_t brightness) override
    {
        pmic_->SetBrightness(target_brightness_);
        brightness_ = target_brightness_;
    }

private:
    Pmic* pmic_;
};

class Aw9523 : public I2cDevice {
public:
    // Exanpd IO Init
    Aw9523(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr)
    {
        WriteReg(0x02, 0b00000111);  // P0
        WriteReg(0x03, 0b10001111);  // P1
        WriteReg(0x04, 0b00011000);  // CONFIG_P0
        WriteReg(0x05, 0b00001100);  // CONFIG_P1
        WriteReg(0x11, 0b00010000);  // GCR P0 port is Push-Pull mode.
        WriteReg(0x12, 0b11111111);  // LEDMODE_P0
        WriteReg(0x13, 0b11111111);  // LEDMODE_P1
    }

    void ResetAw88298()
    {
        ESP_LOGI(TAG, "Reset AW88298");
        WriteReg(0x02, 0b00000011);
        vTaskDelay(pdMS_TO_TICKS(10));
        WriteReg(0x02, 0b00000111);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    void ResetIli9342()
    {
        ESP_LOGI(TAG, "Reset IlI9342");
        WriteReg(0x03, 0b10000001);
        vTaskDelay(pdMS_TO_TICKS(20));
        WriteReg(0x03, 0b10000011);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
};

class Ft6336 : public I2cDevice {
public:
    struct TouchPoint_t {
        int num = 0;
        int x   = -1;
        int y   = -1;
    };

    Ft6336(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr)
    {
        uint8_t chip_id = ReadReg(0xA3);
        ESP_LOGI(TAG, "Get chip ID: 0x%02X", chip_id);
        read_buffer_ = new uint8_t[6];
    }

    ~Ft6336()
    {
        delete[] read_buffer_;
    }

    bool UpdateTouchPoint()
    {
        auto err = TryReadRegs(0x02, read_buffer_, 6);
        if (err != ESP_OK) {
            tp_.num = 0;
            tp_.x   = -1;
            tp_.y   = -1;

            consecutive_failures_++;
            int64_t now_us = esp_timer_get_time();
            if (last_error_log_us_ == 0 || (now_us - last_error_log_us_) >= 1000 * 1000) {
                ESP_LOGW(TAG, "FT6336 read failed (%s), skipped %lu sample(s)", esp_err_to_name(err),
                         static_cast<unsigned long>(consecutive_failures_));
                last_error_log_us_ = now_us;
            }
            return false;
        }

        consecutive_failures_ = 0;
        tp_.num               = read_buffer_[0] & 0x0F;
        tp_.x                 = ((read_buffer_[1] & 0x0F) << 8) | read_buffer_[2];
        tp_.y                 = ((read_buffer_[3] & 0x0F) << 8) | read_buffer_[4];
        return true;
    }

    inline const TouchPoint_t& GetTouchPoint()
    {
        return tp_;
    }

private:
    uint8_t* read_buffer_ = nullptr;
    TouchPoint_t tp_;
    int64_t last_error_log_us_     = 0;
    uint32_t consecutive_failures_ = 0;
};

class M5StackCoreS3Board : public WifiBoard {
private:
    static constexpr int kPowerSaveSleepDelaySeconds = 300;
    static constexpr int kPowerStatePollIntervalMs   = 1000;

    i2c_master_bus_handle_t i2c_bus_;
    Pmic* pmic_;
    Aw9523* aw9523_;
    Ft6336* ft6336_;
    LvglDisplay* display_;
    StackChanCamera* camera_;
    esp_timer_handle_t touchpad_timer_;
    PowerSaveTimer* power_save_timer_;
    hal_bridge::XiaozhiConfig_t xiaozhi_config_;
    bool last_power_save_enabled_      = false;
    int64_t last_power_state_check_ms_ = 0;

    bool ShouldEnablePowerSave(bool has_external_power, bool is_discharging) const
    {
        return is_discharging || (has_external_power && xiaozhi_config_.allowShutdownWhenCharging);
    }

    void UpdatePowerSaveEnabled(bool has_external_power, bool is_discharging)
    {
        const bool should_enable_power_save = ShouldEnablePowerSave(has_external_power, is_discharging);
        if (should_enable_power_save == last_power_save_enabled_) {
            return;
        }

        ESP_LOGI(TAG, "Power save timer %s: external_power=%d, discharging=%d, allowShutdownWhenCharging=%d",
                 should_enable_power_save ? "enabled" : "disabled", has_external_power, is_discharging,
                 xiaozhi_config_.allowShutdownWhenCharging);
        power_save_timer_->SetEnabled(should_enable_power_save);
        last_power_save_enabled_ = should_enable_power_save;
    }

    void PollPowerSaveState()
    {
        const int64_t now_ms = esp_timer_get_time() / 1000;
        if (last_power_state_check_ms_ != 0 && (now_ms - last_power_state_check_ms_) < kPowerStatePollIntervalMs) {
            return;
        }
        last_power_state_check_ms_ = now_ms;

        UpdatePowerSaveEnabled(pmic_->IsExternalPowerConnected(), pmic_->IsDischarging());
    }

    void InitializePowerSaveTimer()
    {
        xiaozhi_config_ = hal_bridge::get_xiaozhi_config();

        const int seconds_to_shutdown = xiaozhi_config_.idleShutdownTimeSeconds > 0
                                            ? static_cast<int>(xiaozhi_config_.idleShutdownTimeSeconds)
                                            : -1;
        const int seconds_to_sleep    = seconds_to_shutdown == -1
                                            ? kPowerSaveSleepDelaySeconds
                                            : std::min(kPowerSaveSleepDelaySeconds, seconds_to_shutdown);

        ESP_LOGI(TAG, "Init power save timer: sleep=%d s, shutdown=%d s, allow_shutdown_when_charging=%d",
                 seconds_to_sleep, seconds_to_shutdown, xiaozhi_config_.allowShutdownWhenCharging);

        power_save_timer_ = new PowerSaveTimer(-1, seconds_to_sleep, seconds_to_shutdown);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
            // GetBacklight()->SetBrightness(10);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness();
        });
        power_save_timer_->OnShutdownRequest([this]() { pmic_->PowerOff(); });
        UpdatePowerSaveEnabled(pmic_->IsExternalPowerConnected(), pmic_->IsDischarging());
    }

    void InitializeI2c()
    {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port          = (i2c_port_t)1,
            .sda_io_num        = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num        = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source        = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority     = 0,
            .trans_queue_depth = 0,
            .flags =
                {
                    .enable_internal_pullup = 1,
                },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void I2cDetect()
    {
        uint8_t address;
        printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
        for (int i = 0; i < 128; i += 16) {
            printf("%02x: ", i);
            for (int j = 0; j < 16; j++) {
                fflush(stdout);
                address       = i + j;
                esp_err_t ret = i2c_master_probe(i2c_bus_, address, pdMS_TO_TICKS(200));
                if (ret == ESP_OK) {
                    printf("%02x ", address);
                } else if (ret == ESP_ERR_TIMEOUT) {
                    printf("UU ");
                } else {
                    printf("-- ");
                }
            }
            printf("\r\n");
        }
    }

    void InitializeAxp2101()
    {
        ESP_LOGI(TAG, "Init AXP2101");
        pmic_ = new Pmic(i2c_bus_, 0x34);
    }

    void InitializeAw9523()
    {
        ESP_LOGI(TAG, "Init AW9523");
        aw9523_ = new Aw9523(i2c_bus_, 0x58);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    void PollTouchpad()
    {
        if (!ft6336_->UpdateTouchPoint()) {
            return;
        }
        auto& touch_point = ft6336_->GetTouchPoint();

        // Update hal touch point
        hal_bridge::set_touch_point(touch_point.num, touch_point.x, touch_point.y);
    }

    void InitializeFt6336TouchPad()
    {
        ESP_LOGI(TAG, "Init FT6336");
        ft6336_ = new Ft6336(i2c_bus_, 0x38);

        // 创建定时器，20ms 间隔
        esp_timer_create_args_t timer_args = {
            .callback =
                [](void* arg) {
                    M5StackCoreS3Board* board = (M5StackCoreS3Board*)arg;
                    board->PollTouchpad();
                    board->PollPowerSaveState();
                },
            .arg                   = this,
            .dispatch_method       = ESP_TIMER_TASK,
            .name                  = "touchpad_timer",
            .skip_unhandled_events = true,
        };

        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &touchpad_timer_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(touchpad_timer_, 20 * 1000));
    }

    void InitializeSpi()
    {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num      = GPIO_NUM_37;
        // The LCD is write-only.  Do not claim GPIO35 as the LCD bus MISO;
        // GPIO35 is also the LCD D/C signal and is used as SD MISO on SPI2.
        buscfg.miso_io_num      = GPIO_NUM_NC;
        buscfg.sclk_io_num      = GPIO_NUM_36;
        buscfg.quadwp_io_num    = GPIO_NUM_NC;
        buscfg.quadhd_io_num    = GPIO_NUM_NC;
        buscfg.max_transfer_sz  = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeSdCard()
    {
#if SDCARD_ENABLED
        ESP_LOGI(TAG, "Init SD card: host=%d mosi=%d miso=%d sclk=%d cs=%d", SDCARD_SPI_HOST,
                 SDCARD_SPI_MOSI, SDCARD_SPI_MISO, SDCARD_SPI_SCLK, SDCARD_SPI_CS);

        spi_bus_config_t bus_cfg = {};
        bus_cfg.mosi_io_num     = SDCARD_SPI_MOSI;
        bus_cfg.miso_io_num     = SDCARD_SPI_MISO;
        bus_cfg.sclk_io_num     = SDCARD_SPI_SCLK;
        bus_cfg.quadwp_io_num   = GPIO_NUM_NC;
        bus_cfg.quadhd_io_num   = GPIO_NUM_NC;
        bus_cfg.max_transfer_sz = 4096;

        esp_err_t ret = spi_bus_initialize((spi_host_device_t)SDCARD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
        // 诊断：SPI3(LCD) 已先认领 GPIO36/37，若 SPI2 复用同引脚会被 ESP-IDF 拒绝并返回
        // ESP_ERR_INVALID_STATE。这里必须打印真实返回值，否则无法判断 SD 超时是"引脚冲突"还是"卡不响应"。
        ESP_LOGI(TAG, "SD SPI bus init (SPI2) returned: %s", esp_err_to_name(ret));
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to initialize SD SPI bus: %s", esp_err_to_name(ret));
            g_sdcard_mount_error = ret;
            return;
        }

        TryMountSdCard();
#else
        ESP_LOGI(TAG, "SD card mount disabled");
#endif
    }

#if SDCARD_ENABLED
public:
    // Attempt to (re)mount the SD card.  Assumes the SPI2 bus has already
    // been initialized and that the caller holds exclusive access to the
    // shared SPI bus (see board_begin_sdcard_access for later, on-demand
    // calls; at boot the LCD is not running yet).
    static void TryMountSdCard()
    {
        g_sdcard_mount_attempts++;

        hal_bridge::board_prepare_sdcard_bus();
        // 给卡上电/总线稳态一点时间，降低上电瞬态导致的 TIMEOUT/NOT_FOUND。
        vTaskDelay(pdMS_TO_TICKS(50));

        // 先按常规 10MHz 挂载；失败则重新整理总线并降到 1MHz 再试一次，
        // 覆盖走线较长/接触不良时的时序问题。
        // 注：SPI 模式 SD 卡在 10MHz 下写入易 0x107 超时（录像高频写时
        // 尤其明显），改用 4MHz（SPI 卡可靠标准频率）显著降低写入超时。
        const int freq_khz_list[]  = {4000, 1000};
        esp_err_t ret              = ESP_FAIL;
        sdmmc_card_t* card         = nullptr;

        for (int i = 0; i < (int)(sizeof(freq_khz_list) / sizeof(freq_khz_list[0])); ++i) {
            sdmmc_host_t host = SDSPI_HOST_DEFAULT();
            host.slot         = SDCARD_SPI_HOST;
            host.max_freq_khz = freq_khz_list[i];

            sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
            slot_config.gpio_cs               = SDCARD_SPI_CS;
            slot_config.host_id               = (spi_host_device_t)SDCARD_SPI_HOST;

            esp_vfs_fat_sdmmc_mount_config_t mount_config = {
                .format_if_mount_failed   = false,
                .max_files                = 8,
                .allocation_unit_size     = 16 * 1024,
                .disk_status_check_enable = false,
            };

            card = nullptr;
            ret  = esp_vfs_fat_sdspi_mount(SDCARD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);
            if (ret == ESP_OK) {
                break;
            }
            ESP_LOGW(TAG, "SD mount freq %d kHz failed (attempt %d): %s", freq_khz_list[i],
                     g_sdcard_mount_attempts, esp_err_to_name(ret));
            // 下一档前重新整理总线并稍作等待
            if (i + 1 < (int)(sizeof(freq_khz_list) / sizeof(freq_khz_list[0]))) {
                hal_bridge::board_prepare_sdcard_bus();
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }

        g_sdcard_mount_error = ret;
        if (ret == ESP_OK) {
            g_sdcard_mounted = true;
            sdmmc_card_print_info(stdout, card);
            ESP_LOGI(TAG, "SD card mounted at %s (attempt %d)", SDCARD_MOUNT_POINT, g_sdcard_mount_attempts);
        } else {
            g_sdcard_mounted = false;
            // 分类错误码，便于从串口日志判断是卡检测/时序/格式问题
            const char* hint = "Check FAT32 format, card seating, and SPI pins.";
            if (ret == ESP_ERR_TIMEOUT || ret == ESP_ERR_INVALID_RESPONSE) {
                hint = "Bus timing/response issue: likely SPI contention on shared GPIO35/36/37 or slow card power-up.";
            } else if (ret == ESP_ERR_NOT_FOUND) {
                hint = "Card not detected: check card seating / CS pin / card power.";
            } else if (ret == ESP_ERR_INVALID_STATE) {
                hint = "SPI bus/SDSPI in invalid state: bus init or route switch problem.";
            }
            ESP_LOGE(TAG, "Failed to mount SD card at %s (attempt %d): %s. %s", SDCARD_MOUNT_POINT,
                     g_sdcard_mount_attempts, esp_err_to_name(ret), hint);
        }
    }

private:
#endif

    void InitializeIli9342Display()
    {
        ESP_LOGI(TAG, "Init IlI9342");

        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel       = nullptr;

        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num                   = GPIO_NUM_3;
        io_config.dc_gpio_num                   = GPIO_NUM_35;
        io_config.spi_mode                      = 2;
        io_config.pclk_hz                       = 40 * 1000 * 1000;
        io_config.trans_queue_depth             = 10;
        io_config.lcd_cmd_bits                  = 8;
        io_config.lcd_param_bits                = 8;
        // ILI9341 uses the conventional SPI LCD levels: D/C low for a
        // command and high for parameters/pixel data.  The ESP-IDF flag names
        // describe the inverted alternative, so keep them explicitly clear.
        io_config.flags.dc_high_on_cmd          = 0;
        io_config.flags.dc_low_on_data          = 0;
        io_config.flags.dc_low_on_param         = 0;
        // GPIO35 is shared with SD MISO.  Release the SD input-matrix route
        // before the first LCD command/data transaction.
        board_prepare_lcd_bus();
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num             = GPIO_NUM_NC;
        panel_config.rgb_ele_order              = LCD_RGB_ELEMENT_ORDER_BGR;
        panel_config.bits_per_pixel             = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        aw9523_->ResetIli9342();

        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));

        // Keep the SD MISO route disconnected while LVGL owns the LCD.
        board_prepare_lcd_bus();

        // display_ = new StackChanLcdDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X,
        //                                    DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        display_ = new StackChanAvatarDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X,
                                              DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeCamera()
    {
        ESP_LOGI(TAG, "Init Camera");

        static esp_cam_ctlr_dvp_pin_config_t dvp_pin_config = {
            .data_width = CAM_CTLR_DATA_WIDTH_8,
            .data_io =
                {
                    [0] = CAMERA_PIN_D0,
                    [1] = CAMERA_PIN_D1,
                    [2] = CAMERA_PIN_D2,
                    [3] = CAMERA_PIN_D3,
                    [4] = CAMERA_PIN_D4,
                    [5] = CAMERA_PIN_D5,
                    [6] = CAMERA_PIN_D6,
                    [7] = CAMERA_PIN_D7,
                },
            .vsync_io = CAMERA_PIN_VSYNC,
            .de_io    = CAMERA_PIN_HREF,
            .pclk_io  = CAMERA_PIN_PCLK,
            .xclk_io  = CAMERA_PIN_XCLK,
        };

        esp_video_init_sccb_config_t sccb_config = {
            .init_sccb  = false,
            .i2c_handle = i2c_bus_,
            .freq       = 100000,
        };

        esp_video_init_dvp_config_t dvp_config = {
            .sccb_config = sccb_config,
            .reset_pin   = CAMERA_PIN_RESET,
            .pwdn_pin    = CAMERA_PIN_PWDN,
            .dvp_pin     = dvp_pin_config,
            .xclk_freq   = XCLK_FREQ_HZ,
        };

        esp_video_init_config_t video_config = {
            .dvp = &dvp_config,
        };

        camera_ = new StackChanCamera(video_config);
        camera_->SetHMirror(false);
    }

public:
    M5StackCoreS3Board()
    {
        InitializeI2c();
        InitializeAxp2101();
        InitializePowerSaveTimer();
        InitializeAw9523();
        I2cDetect();
        InitializeSpi();
        InitializeSdCard();
        InitializeIli9342Display();
        InitializeCamera();
        InitializeFt6336TouchPad();
        GetBacklight()->RestoreBrightness();
    }

    // 米宝配网门控（任务 25 回归修复）：门控只优化体验，绝不切断入口。
    // 无任何已存 SSID（首次开机/未配过网）时无条件允许自动配网——否则
    // no_saved_ssid 直进 xiaozhi 后又无配网入口，设备死局（停在表情界面）。
    // 有凭证时按 NVS "mibao"/"auto_prov"（默认 false）决定是否在 Wi-Fi
    // 超时后自动弹热点，避免打扰；手动入口（设置→Hotspot Setup）不受影响。
    virtual bool AllowAutoWifiConfigMode() override
    {
        if (SsidManager::GetInstance().GetSsidList().empty()) {
            return true;
        }
        return mibao::isAutoProvisioningEnabled();
    }

    virtual AudioCodec* GetAudioCodec() override
    {
        static CoreS3AudioCodec audio_codec(i2c_bus_, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS,
                                            AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN, AUDIO_CODEC_AW88298_ADDR,
                                            AUDIO_CODEC_ES7210_ADDR, AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override
    {
        return display_;
    }

    virtual Camera* GetCamera() override
    {
        return camera_;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override
    {
        static bool last_discharging = false;
        charging                     = pmic_->IsCharging();
        discharging                  = pmic_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }

        level = pmic_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override
    {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }

    virtual Backlight* GetBacklight() override
    {
        static CustomBacklight backlight(pmic_);
        return &backlight;
    }

    i2c_master_bus_handle_t GetI2cBus()
    {
        return i2c_bus_;
    }
};

DECLARE_BOARD(M5StackCoreS3Board);

i2c_master_bus_handle_t hal_bridge::board_get_i2c_bus()
{
    auto& board = (M5StackCoreS3Board&)Board::GetInstance();
    return board.GetI2cBus();
}

StackChanCamera* hal_bridge::board_get_camera()
{
    auto& board = Board::GetInstance();
    auto camera = (StackChanCamera*)board.GetCamera();
    return camera;
}

int hal_bridge::board_get_battery_level()
{
    auto& board      = Board::GetInstance();
    int level        = 0;
    bool charging    = false;
    bool discharging = false;
    if (board.GetBatteryLevel(level, charging, discharging)) {
        return level;
    } else {
        return 100;
    }
}

bool hal_bridge::board_is_battery_charging()
{
    auto& board      = Board::GetInstance();
    int level        = 0;
    bool charging    = false;
    bool discharging = false;
    if (board.GetBatteryLevel(level, charging, discharging)) {
        return charging;
    } else {
        return false;
    }
}

void hal_bridge::board_prepare_sdcard_bus()
{
#if SDCARD_ENABLED
    // CoreS3 SD follows the official M5Stack SPI wiring: CS=4, SCK=36,
    // MISO=35, MOSI=37. Restore the GPIO-matrix MISO route before SD access.
    board_prepare_sdcard_bus_internal();
    gpio_set_pull_mode(SDCARD_SPI_MISO, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(SDCARD_SPI_MOSI, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(SDCARD_SPI_SCLK, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(SDCARD_SPI_CS, GPIO_PULLUP_ONLY);
#endif
}

bool hal_bridge::board_sdcard_is_mounted()
{
#if SDCARD_ENABLED
    return g_sdcard_mounted;
#else
    return false;
#endif
}

bool hal_bridge::board_sdcard_try_remount()
{
#if SDCARD_ENABLED
    if (g_sdcard_mounted) {
        return true;
    }
    // The boot-time mount is attempted very early, before the SD card power
    // rail and the shared SPI bus have always settled.  Give apps a bounded
    // way to recover without rebooting.  The access guard stops the LVGL
    // task so the shared GPIO35/36/37 routes can be switched safely.
    if (g_sdcard_mount_attempts >= 1 + kMaxSdcardRemountAttempts) {
        return false;
    }
    board_begin_sdcard_access();
    M5StackCoreS3Board::TryMountSdCard();
    board_end_sdcard_access();
    return g_sdcard_mounted;
#else
    return false;
#endif
}

void hal_bridge::board_begin_sdcard_access()
{
#if SDCARD_ENABLED
    if (g_sdcard_access_depth.fetch_add(1) == 0) {
        // Serialize SD access with the LVGL task.  Stopping the LVGL tick
        // alone does not wait for an already-running LCD DMA transaction.
        lvgl_port_lock(0);
        lvgl_port_stop();
    }
    board_prepare_sdcard_bus();
#endif
}

void hal_bridge::board_end_sdcard_access()
{
#if SDCARD_ENABLED
    const uint32_t previous_depth = g_sdcard_access_depth.load();
    if (previous_depth == 0) {
        return;
    }

    if (g_sdcard_access_depth.fetch_sub(1) == 1) {
        // StackChan/CoreS3 shares GPIO35 between SD MISO and the LCD D/C
        // signal. Restore the LCD-side GPIO-matrix state before resuming LVGL.
        board_prepare_lcd_bus();
        lvgl_port_resume();
        lvgl_port_unlock();
    }
#endif
}

void hal_bridge::board_set_backlight_brightness(uint8_t brightness, bool permanent)
{
    auto& board    = Board::GetInstance();
    auto backlight = board.GetBacklight();
    if (backlight) {
        backlight->SetBrightness(brightness, false);
        if (permanent) {
            Settings settings("display", true);
            settings.SetInt("brightness", brightness);
        }
    }
}

uint8_t hal_bridge::board_get_backlight_brightness()
{
    auto& board    = Board::GetInstance();
    auto backlight = board.GetBacklight();
    if (backlight) {
        return backlight->brightness();
    } else {
        return 0;
    }
}

void hal_bridge::board_set_speaker_volume(uint8_t volume, bool permanent)
{
    auto& board      = Board::GetInstance();
    auto audio_codec = board.GetAudioCodec();
    if (audio_codec) {
        Settings settings("audio", false);
        const int persisted_volume = settings.GetInt("output_volume", audio_codec->output_volume());
        audio_codec->SetOutputVolume(volume);
        if (!permanent) {
            Settings writable_settings("audio", true);
            writable_settings.SetInt("output_volume", persisted_volume);
            return;
        }
    }
}

uint8_t hal_bridge::board_get_speaker_volume()
{
    int volume = 90;  // 默认音量 90/100
    Settings settings("audio", false);
    volume = settings.GetInt("output_volume", volume);
    if (volume <= 0) {
        volume = 10;
    }
    return volume;
}

void hal_bridge::toggle_xiaozhi_chat_state()
{
    auto& app = Application::GetInstance();
    if (app.GetDeviceState() == kDeviceStateStarting) {
        // EnterWifiConfigMode();
        return;
    }
    app.ToggleChatState();
}
