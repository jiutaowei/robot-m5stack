/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

#include "meeting_protocol.h"

#include <algorithm>

namespace stackchan::meeting {
namespace {

void append_be32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void append_be64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> static_cast<unsigned>(shift)) & 0xffU));
    }
}

std::uint32_t read_be32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3U]);
}

std::uint64_t read_be64(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8U; ++index) {
        value = (value << 8U) | bytes[offset + index];
    }
    return value;
}

}  // namespace

std::vector<std::uint8_t> EncodeScmtPcmFrame(const AudioBlock& block, std::size_t sample_count,
                                             ScmtFrameFlags flags) {
    const std::size_t clamped_samples = std::min(sample_count, block.samples.size());
    std::vector<std::uint8_t> frame;
    frame.reserve(kScmtHeaderSize + clamped_samples * sizeof(std::int16_t));
    frame.insert(frame.end(), {'S', 'C', 'M', 'T'});
    frame.push_back(kScmtVersion);
    frame.push_back(static_cast<std::uint8_t>(ScmtFrameFlags::Audio) |
                    static_cast<std::uint8_t>(flags));
    frame.push_back(0x00);
    frame.push_back(0x00);
    append_be32(frame, block.sequence);
    append_be64(frame, static_cast<std::uint64_t>(block.timestamp_ms));
    append_be32(frame, static_cast<std::uint32_t>(clamped_samples * sizeof(std::int16_t)));
    for (std::size_t index = 0; index < clamped_samples; ++index) {
        const std::uint16_t sample = static_cast<std::uint16_t>(block.samples[index]);
        frame.push_back(static_cast<std::uint8_t>(sample & 0xffU));
        frame.push_back(static_cast<std::uint8_t>((sample >> 8U) & 0xffU));
    }
    return frame;
}

bool DecodeScmtHeader(const std::vector<std::uint8_t>& frame, ScmtFrameHeader& header) {
    if (frame.size() < kScmtHeaderSize || frame[0] != 'S' || frame[1] != 'C' ||
        frame[2] != 'M' || frame[3] != 'T' || frame[4] != kScmtVersion) {
        return false;
    }
    ScmtFrameHeader decoded;
    decoded.version = frame[4];
    decoded.flags = frame[5];
    decoded.sequence = read_be32(frame, 8U);
    decoded.timestamp_ms = static_cast<std::int64_t>(read_be64(frame, 12U));
    decoded.payload_length = read_be32(frame, 20U);
    if (frame.size() < kScmtHeaderSize + decoded.payload_length) {
        return false;
    }
    header = decoded;
    return true;
}

}  // namespace stackchan::meeting
