/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace stackchan::meeting {

inline constexpr std::uint32_t kMeetingSampleRate = 16000U;
inline constexpr std::uint16_t kMeetingChannelCount = 1U;
inline constexpr std::uint16_t kMeetingBitsPerSample = 16U;
inline constexpr std::size_t kWavHeaderSize = 44U;

using WavHeader = std::array<std::uint8_t, kWavHeaderSize>;

WavHeader WritePlaceholderHeader();
WavHeader FinalizeHeader(std::uint64_t sample_count);

constexpr std::uint64_t DurationMilliseconds(std::uint64_t sample_count) {
    return sample_count * 1000U / kMeetingSampleRate;
}

constexpr std::uint64_t PcmDataSizeBytes(std::uint64_t sample_count) {
    return sample_count * kMeetingChannelCount * kMeetingBitsPerSample / 8U;
}

constexpr std::uint64_t WavFileSizeBytes(std::uint64_t sample_count) {
    return kWavHeaderSize + PcmDataSizeBytes(sample_count);
}

}  // namespace stackchan::meeting
