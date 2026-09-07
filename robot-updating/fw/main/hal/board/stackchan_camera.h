#pragma once
#include "sdkconfig.h"

#ifndef CONFIG_IDF_TARGET_ESP32
#include <lvgl.h>
#include <thread>
#include <memory>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "camera.h"
#include "jpg/image_to_jpeg.h"
#include "esp_video_init.h"

struct JpegChunk {
    uint8_t* data;
    size_t len;
};

class StackChanCamera : public Camera {
private:
    struct FrameBuffer {
        uint8_t* data         = nullptr;
        size_t len            = 0;
        uint16_t width        = 0;
        uint16_t height       = 0;
        v4l2_pix_fmt_t format = 0;
    } frame_;
    v4l2_pix_fmt_t sensor_format_ = 0;
#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    uint16_t sensor_width_  = 0;
    uint16_t sensor_height_ = 0;
#endif  // CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    int video_fd_      = -1;
    bool streaming_on_ = false;
    struct MmapBuffer {
        void* start   = nullptr;
        size_t length = 0;
    };
    std::vector<MmapBuffer> mmap_buffers_;
    std::string explain_url_;
    std::string explain_token_;
    std::thread encoder_thread_;
    // 拍照时播放快门音效；录像等静默抓帧场景可关闭（mooncake 模式无
    // xiaozhi 音频服务，PlaySound 会因 codec_ 为空崩溃）。
    bool play_shutter_sound_ = true;

public:
    StackChanCamera(const esp_video_init_config_t& config);
    ~StackChanCamera();

    virtual void SetExplainUrl(const std::string& url, const std::string& token);
    virtual bool Capture() override;
    bool StreamCaptures();
    // 轻量抓帧：仅 DQBUF 一次并复制到 PSRAM（无快门声、无丢帧、无预览转换）。
    // 供录像/实时预览逐帧调用，避免 Capture() 拍照式开销拖慢帧率。
    bool GrabFrame();

    // 暂停/恢复摄像头 DMA 抓帧（VIDIOC_STREAMOFF/ON）。
    // 录像写 SD 前暂停，写完恢复：CoreS3 摄像头 DMA 与 SD SPI2 并发
    // 会偶发 0x107 超时（会议录音无摄像头所以正常）。
    bool PauseStream();
    bool ResumeStream();

    // 控制 Capture() 是否播放快门音效（默认 true）。录像等逐帧抓取必须关闭。
    void SetPlayShutterSound(bool enabled) { play_shutter_sound_ = enabled; }

    // 翻转控制函数
    virtual bool SetHMirror(bool enabled) override;
    virtual bool SetVFlip(bool enabled) override;
    virtual std::string Explain(const std::string& question);

    const uint8_t* GetFrameData()
    {
        return frame_.data;
    }
    size_t GetFrameSize()
    {
        return frame_.len;
    }
    int GetFrameWidth()
    {
        return frame_.width;
    }
    int GetFrameHeight()
    {
        return frame_.height;
    }
    int GetFrameFormat()
    {
        return frame_.format;
    }
};

#endif  // ndef CONFIG_IDF_TARGET_ESP32
