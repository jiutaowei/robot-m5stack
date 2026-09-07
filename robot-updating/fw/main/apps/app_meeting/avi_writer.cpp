#include "avi_writer.h"

#include <cstring>

#include <esp_log.h>

#define TAG "AviWriter"

namespace stackchan::meeting {

// RIFF/AVI 小端常量
namespace {

constexpr uint32_t kFourcc(const char* s) {
    return (uint32_t)s[0] | ((uint32_t)s[1] << 8) | ((uint32_t)s[2] << 16) | ((uint32_t)s[3] << 24);
}

constexpr uint32_t FCC_RIFF = kFourcc("RIFF");
constexpr uint32_t FCC_AVI  = kFourcc("AVI ");
constexpr uint32_t FCC_LIST = kFourcc("LIST");
constexpr uint32_t FCC_HDRL = kFourcc("hdrl");
constexpr uint32_t FCC_AVIH = kFourcc("avih");
constexpr uint32_t FCC_STRL = kFourcc("strl");
constexpr uint32_t FCC_STRH = kFourcc("strh");
constexpr uint32_t FCC_STRF = kFourcc("strf");
constexpr uint32_t FCC_MOVI = kFourcc("movi");
constexpr uint32_t FCC_00DC = kFourcc("00dc");
constexpr uint32_t FCC_01WB = kFourcc("01wb");
constexpr uint32_t FCC_IDX1 = kFourcc("idx1");
constexpr uint32_t FCC_AUDS = kFourcc("auds");
constexpr uint32_t FCC_VIDS = kFourcc("vids");

void writeU32(FILE* f, uint32_t v) { fwrite(&v, 1, 4, f); }
void writeU16(FILE* f, uint16_t v) { fwrite(&v, 1, 2, f); }
void writeFcc(FILE* f, uint32_t fcc) { writeU32(f, fcc); }

}  // namespace

AviWriter::AviWriter() = default;

AviWriter::~AviWriter() {
    if (file_ != nullptr) {
        close();
    }
}

bool AviWriter::open(const std::string& path, uint16_t width, uint16_t height, uint8_t fps) {
    return openWithAudio(path, width, height, fps, 0);
}

bool AviWriter::openWithAudio(const std::string& path, uint16_t width, uint16_t height, uint8_t fps,
                              uint32_t audio_sample_rate) {
    path_ = path;
    width_ = width;
    height_ = height;
    fps_ = fps ? fps : 10;
    audio_enabled_ = audio_sample_rate > 0;
    audio_sample_rate_ = audio_enabled_ ? audio_sample_rate : 16000;
    audio_sample_count_ = 0;
    frame_count_ = 0;
    movi_data_size_ = 0;
    header_written_ = false;

    file_ = fopen(path_.c_str(), "wb");
    if (file_ == nullptr) {
        ESP_LOGE(TAG, "failed to open %s", path_.c_str());
        return false;
    }
    return writeHeader();
}

bool AviWriter::writeHeader() {
    // RIFF header（大小占位，close 时回填）
    writeFcc(file_, FCC_RIFF);
    riff_size_offset_ = ftell(file_);
    writeU32(file_, 0);  // riff size placeholder
    writeFcc(file_, FCC_AVI);

    // hdrl LIST
    writeFcc(file_, FCC_LIST);
    uint32_t hdrl_size_pos = ftell(file_);
    writeU32(file_, 0);  // hdrl size placeholder
    writeFcc(file_, FCC_HDRL);

    // avih
    writeFcc(file_, FCC_AVIH);
    writeU32(file_, 56);  // avih size
    writeU32(file_, 1000000 / fps_);  // microsec per frame
    writeU32(file_, 0);               // max bytes per sec (0 = unknown)
    writeU32(file_, 0);               // padding granularity
    writeU32(file_, 0x10);            // flags: HASINDEX
    avih_frames_offset_ = ftell(file_);
    writeU32(file_, 0);  // total frames (patched in close)
    writeU32(file_, 0);  // initial frames
    writeU32(file_, audio_enabled_ ? 2 : 1);  // streams
    writeU32(file_, 0);  // suggested buffer size
    writeU32(file_, width_);
    writeU32(file_, height_);
    writeU32(file_, 0);  // reserved
    writeU32(file_, 0);  // reserved
    writeU32(file_, 0);  // reserved

    // ---------- 视频 strl LIST ----------
    writeFcc(file_, FCC_LIST);
    uint32_t video_strl_size_pos = ftell(file_);
    writeU32(file_, 0);  // strl size placeholder
    writeFcc(file_, FCC_STRL);

    // strh
    writeFcc(file_, FCC_STRH);
    writeU32(file_, 56);  // strh size
    writeFcc(file_, FCC_VIDS);
    writeFcc(file_, kFourcc("MJPG"));
    writeU32(file_, 0);    // flags
    writeU16(file_, 0);    // priority
    writeU16(file_, 0);    // language
    writeU32(file_, 0);    // initial frames
    writeU32(file_, 1);    // scale
    writeU32(file_, fps_); // rate
    writeU32(file_, 0);    // start
    video_strh_length_offset_ = ftell(file_);
    writeU32(file_, 0);    // length (patched in close)
    writeU32(file_, 0);    // suggested buffer size
    writeU32(file_, 0);    // quality
    writeU32(file_, 0);    // sample size
    writeU16(file_, 0);    // rcFrame left
    writeU16(file_, 0);    // rcFrame top
    writeU16(file_, width_);   // rcFrame right
    writeU16(file_, height_);  // rcFrame bottom

    // strf (BITMAPINFOHEADER)
    writeFcc(file_, FCC_STRF);
    writeU32(file_, 40);  // BITMAPINFOHEADER size
    writeU32(file_, width_);
    writeU32(file_, height_);
    writeU16(file_, 1);   // planes
    writeU16(file_, 24);  // bit count (MJPEG 通常 24)
    writeFcc(file_, kFourcc("MJPG"));
    writeU32(file_, 0);  // size image
    writeU32(file_, 0);  // x pixels per meter
    writeU32(file_, 0);  // y pixels per meter
    writeU32(file_, 0);  // colors used
    writeU32(file_, 0);  // important colors

    // 回填视频 strl size
    const long video_strl_end = ftell(file_);
    const long video_strl_size = video_strl_end - video_strl_size_pos - 4;
    fseek(file_, video_strl_size_pos, SEEK_SET);
    writeU32(file_, (uint32_t)video_strl_size);
    fseek(file_, video_strl_end, SEEK_SET);

    // ---------- 音频 strl LIST ----------
    if (audio_enabled_) {
        writeFcc(file_, FCC_LIST);
        uint32_t audio_strl_size_pos = ftell(file_);
        writeU32(file_, 0);  // strl size placeholder
        writeFcc(file_, FCC_STRL);

        // strh (audio)
        writeFcc(file_, FCC_STRH);
        writeU32(file_, 56);  // strh size
        writeFcc(file_, FCC_AUDS);
        writeU32(file_, 0);   // fccHandler (PCM 无编码器)
        writeU32(file_, 0);   // flags
        writeU16(file_, 0);   // priority
        writeU16(file_, 0);   // language
        writeU32(file_, 0);   // initial frames
        writeU32(file_, 1);   // scale
        writeU32(file_, audio_sample_rate_);  // rate = 采样率
        writeU32(file_, 0);   // start
        audio_strh_length_offset_ = ftell(file_);
        writeU32(file_, 0);   // length = 总样本数 (patched in close)
        writeU32(file_, 0);   // suggested buffer size
        writeU32(file_, 0xFFFFFFFF);  // quality (PCM 用 -1)
        writeU32(file_, 2);   // sample size (16bit mono)
        writeU16(file_, 0);   // rcFrame left
        writeU16(file_, 0);   // rcFrame top
        writeU16(file_, 0);   // rcFrame right
        writeU16(file_, 0);   // rcFrame bottom

        // strf (WAVEFORMATEX)
        writeFcc(file_, FCC_STRF);
        writeU32(file_, 16);  // WAVEFORMATEX size
        writeU16(file_, 1);   // wFormatTag: PCM
        writeU16(file_, 1);   // nChannels: mono
        writeU32(file_, audio_sample_rate_);  // nSamplesPerSec
        writeU32(file_, audio_sample_rate_ * 2);  // nAvgBytesPerSec (16bit mono)
        writeU16(file_, 2);   // nBlockAlign
        writeU16(file_, 16);  // wBitsPerSample

        // 回填音频 strl size
        const long audio_strl_end = ftell(file_);
        const long audio_strl_size = audio_strl_end - audio_strl_size_pos - 4;
        fseek(file_, audio_strl_size_pos, SEEK_SET);
        writeU32(file_, (uint32_t)audio_strl_size);
        fseek(file_, audio_strl_end, SEEK_SET);
    }

    // 回填 hdrl size
    const long end_pos = ftell(file_);
    const long hdrl_size = end_pos - hdrl_size_pos - 4;
    fseek(file_, hdrl_size_pos, SEEK_SET);
    writeU32(file_, (uint32_t)hdrl_size);
    fseek(file_, end_pos, SEEK_SET);

    // movi LIST
    writeFcc(file_, FCC_LIST);
    uint32_t movi_size_pos = ftell(file_);
    writeU32(file_, 0);  // movi size placeholder
    writeFcc(file_, FCC_MOVI);
    movi_offset_ = ftell(file_);

    header_written_ = true;
    return true;
}

bool AviWriter::writeFrame(const uint8_t* jpeg_data, size_t jpeg_len) {
    if (file_ == nullptr || !header_written_ || jpeg_data == nullptr || jpeg_len == 0) {
        return false;
    }
    // 对齐到 2 字节边界
    const size_t pad = (jpeg_len & 1) ? 1 : 0;

    // 记录本帧在 movi 数据区的偏移（相对 movi_offset_），供 idx1 回填
    const uint32_t chunk_offset = static_cast<uint32_t>(ftell(file_) - movi_offset_);

    writeFcc(file_, FCC_00DC);
    writeU32(file_, (uint32_t)jpeg_len);
    fwrite(jpeg_data, 1, jpeg_len, file_);
    if (pad) {
        fputc(0, file_);
    }
    chunk_entries_.push_back({FCC_00DC, chunk_offset, static_cast<uint32_t>(jpeg_len)});
    movi_data_size_ += 8 + jpeg_len + pad;
    frame_count_++;
    return true;
}

bool AviWriter::writeAudio(const int16_t* samples, size_t sample_count) {
    if (file_ == nullptr || !header_written_ || !audio_enabled_ || samples == nullptr || sample_count == 0) {
        return false;
    }
    const size_t byte_len = sample_count * sizeof(int16_t);
    const size_t pad = (byte_len & 1) ? 1 : 0;

    const uint32_t chunk_offset = static_cast<uint32_t>(ftell(file_) - movi_offset_);

    writeFcc(file_, FCC_01WB);
    writeU32(file_, (uint32_t)byte_len);
    fwrite(samples, 1, byte_len, file_);
    if (pad) {
        fputc(0, file_);
    }
    chunk_entries_.push_back({FCC_01WB, chunk_offset, static_cast<uint32_t>(byte_len)});
    movi_data_size_ += 8 + byte_len + pad;
    audio_sample_count_ += (uint32_t)sample_count;
    return true;
}

void AviWriter::writeIndex() {
    // idx1：视频与音频每个 chunk 一个条目（标准索引，播放器可直接定位）
    writeFcc(file_, FCC_IDX1);
    writeU32(file_, (uint32_t)chunk_entries_.size() * 16);
    for (const auto& e : chunk_entries_) {
        writeFcc(file_, e.fourcc);
        writeU32(file_, e.fourcc == FCC_00DC ? 0x10 : 0x00);  // 视频帧标记关键帧
        writeU32(file_, e.offset);
        writeU32(file_, e.size);
    }
}

bool AviWriter::close() {
    if (file_ == nullptr) {
        return false;
    }

    // 回填 avih 的 total frames（视频帧数）
    if (avih_frames_offset_ != 0) {
        fseek(file_, avih_frames_offset_, SEEK_SET);
        writeU32(file_, frame_count_);
    }
    // 回填视频 strh length（帧数）
    if (video_strh_length_offset_ != 0) {
        fseek(file_, video_strh_length_offset_, SEEK_SET);
        writeU32(file_, frame_count_);
    }
    // 回填音频 strh length（总样本数）
    if (audio_enabled_ && audio_strh_length_offset_ != 0) {
        fseek(file_, audio_strh_length_offset_, SEEK_SET);
        writeU32(file_, audio_sample_count_);
    }

    // 索引与各 size 回填
    fseek(file_, 0, SEEK_END);
    writeIndex();

    // 回填 movi size：在 movi_offset_ 前 8 字节是 movi LIST 的 size 字段
    // movi LIST: LIST(4)+size(4)+movi(4)，movi_offset_ 指向 movi 之后
    const long end_pos = ftell(file_);
    const long movi_size_pos = (long)movi_offset_ - 8;
    if (movi_size_pos >= 0) {
        fseek(file_, movi_size_pos, SEEK_SET);
        writeU32(file_, (uint32_t)(end_pos - movi_size_pos - 4));
    }

    // 回填 RIFF size
    const long riff_end = ftell(file_);
    fseek(file_, riff_size_offset_, SEEK_SET);
    writeU32(file_, (uint32_t)(riff_end - riff_size_offset_ - 4));

    fclose(file_);
    file_ = nullptr;
    ESP_LOGI(TAG, "closed, frames=%u, audio_samples=%u, path=%s", frame_count_, audio_sample_count_, path_.c_str());
    return true;
}

}  // namespace stackchan::meeting
