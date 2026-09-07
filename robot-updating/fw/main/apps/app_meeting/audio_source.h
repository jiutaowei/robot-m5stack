/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace stackchan::meeting {

inline constexpr std::size_t kAudioBlockSamples = 320U;

struct AudioBlock {
    std::uint32_t sequence{0};
    std::int64_t timestamp_ms{0};
    std::array<std::int16_t, kAudioBlockSamples> samples{};
};

class AudioSource {
public:
    virtual ~AudioSource() = default;
    virtual bool read(AudioBlock& block) = 0;
};

class StackChanAudioSource final : public AudioSource {
public:
    // owner: 麦克风占用者标识（"meeting" 会议录音 / "video" 录像），
    //        默认 "meeting" 保持向后兼容
    explicit StackChanAudioSource(const char* owner = "meeting");
    ~StackChanAudioSource() override;
    bool read(AudioBlock& block) override;
    bool isAcquired() const;

private:
    std::uint32_t next_sequence_{0};
    bool acquired_{false};
    const char* owner_{"meeting"};
};

}  // namespace stackchan::meeting
