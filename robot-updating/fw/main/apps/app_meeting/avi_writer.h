#pragma once

// 米宝一号：会议录像的 AVI/MJPEG 文件写入器
//
// 把摄像头采集的 JPEG 帧（可选：麦克风 PCM 音频）写入 SD 卡，
// 封装为 AVI 容器（MJPEG 视频 + PCM 音频双流）。
// - 使用 RIFF AVI 格式，含 hdrl（视频 strl + 音频 strl）+ idx1 索引
// - 逐帧/逐块追加写入，结束时回填索引（与录音 WAV 头回填同思路）
// - 视频帧率由调用方控制（推荐 10fps），音频按实际采样率写入

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace stackchan::meeting {

class AviWriter {
public:
    AviWriter();
    ~AviWriter();

    // 打开文件并写入 AVI 头（hdrl + strl，帧数据占位）。
    // width/height 为视频尺寸，fps 为目标帧率。
    bool open(const std::string& path, uint16_t width, uint16_t height, uint8_t fps);

    // 打开并附加一路 PCM 音频流（16 位有符号，单声道）。
    bool openWithAudio(const std::string& path, uint16_t width, uint16_t height, uint8_t fps,
                       uint32_t audio_sample_rate);

    // 追加一帧 JPEG 数据（帧间写 '00dc' chunk + 索引项）。
    bool writeFrame(const uint8_t* jpeg_data, size_t jpeg_len);

    // 追加一块 PCM 音频样本（'01wb' chunk + 索引项）。
    bool writeAudio(const int16_t* samples, size_t sample_count);

    // 回填 AVI 头中的帧数/数据大小，写入 idx1 索引，关闭文件。
    bool close();

    // 已写入的视频帧数
    uint32_t frameCount() const { return frame_count_; }

private:
    FILE* file_ = nullptr;
    std::string path_;
    uint32_t riff_size_offset_ = 0;
    uint32_t movi_offset_ = 0;
    uint32_t avih_frames_offset_ = 0;  // avih 中 total frames 字段的文件偏移
    uint32_t video_strh_length_offset_ = 0;  // 视频 strh 中 length 字段的文件偏移
    uint32_t audio_strh_length_offset_ = 0;  // 音频 strh 中 length 字段的文件偏移
    uint32_t frame_count_ = 0;
    uint64_t movi_data_size_ = 0;
    uint16_t width_ = 0;
    uint16_t height_ = 0;
    uint8_t fps_ = 10;
    bool audio_enabled_ = false;
    uint32_t audio_sample_rate_ = 16000;
    uint32_t audio_sample_count_ = 0;
    bool header_written_ = false;

    // 每个 chunk 在 movi 数据区中的偏移、长度与 fourcc（供 idx1 索引回填）
    struct ChunkEntry {
        uint32_t fourcc;
        uint32_t offset;
        uint32_t size;
    };
    std::vector<ChunkEntry> chunk_entries_;

    bool writeHeader();
    void writeIndex();
};

}  // namespace stackchan::meeting
