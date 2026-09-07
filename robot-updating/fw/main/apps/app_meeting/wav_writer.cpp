/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

#include "wav_writer.h"

#include <algorithm>
#include <limits>

namespace stackchan::meeting {
namespace {

void write_ascii(WavHeader& header, std::size_t offset, const char (&value)[5]) {
    std::copy_n(value, 4U, header.begin() + static_cast<std::ptrdiff_t>(offset));
}

void write_le16(WavHeader& header, std::size_t offset, std::uint16_t value) {
    header[offset] = static_cast<std::uint8_t>(value);
    header[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

void write_le32(WavHeader& header, std::size_t offset, std::uint32_t value) {
    header[offset] = static_cast<std::uint8_t>(value);
    header[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    header[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
    header[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

}  // namespace

WavHeader WritePlaceholderHeader() {
    WavHeader header{};
    write_ascii(header, 0U, "RIFF");
    write_le32(header, 4U, 36U);
    write_ascii(header, 8U, "WAVE");
    write_ascii(header, 12U, "fmt ");
    write_le32(header, 16U, 16U);
    write_le16(header, 20U, 1U);
    write_le16(header, 22U, kMeetingChannelCount);
    write_le32(header, 24U, kMeetingSampleRate);
    write_le32(header, 28U,
               kMeetingSampleRate * kMeetingChannelCount * kMeetingBitsPerSample / 8U);
    write_le16(header, 32U, kMeetingChannelCount * kMeetingBitsPerSample / 8U);
    write_le16(header, 34U, kMeetingBitsPerSample);
    write_ascii(header, 36U, "data");
    write_le32(header, 40U, 0U);
    return header;
}

WavHeader FinalizeHeader(std::uint64_t sample_count) {
    WavHeader header = WritePlaceholderHeader();
    const std::uint64_t calculated_size = PcmDataSizeBytes(sample_count);
    const auto data_size = static_cast<std::uint32_t>(
        calculated_size > std::numeric_limits<std::uint32_t>::max()
            ? std::numeric_limits<std::uint32_t>::max()
            : calculated_size);
    write_le32(header, 4U, data_size + 36U);
    write_le32(header, 40U, data_size);
    return header;
}

}  // namespace stackchan::meeting
