/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

#pragma once

#include "audio_source.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace stackchan::meeting {

inline constexpr std::uint8_t kScmtVersion = 1U;
inline constexpr std::size_t kScmtHeaderSize = 24U;

enum class ScmtFrameFlags : std::uint8_t {
    None = 0x00,
    Audio = 0x01,
    FinalSegment = 0x02,
};

struct ScmtFrameHeader {
    std::uint8_t version{kScmtVersion};
    std::uint8_t flags{0};
    std::uint32_t sequence{0};
    std::int64_t timestamp_ms{0};
    std::uint32_t payload_length{0};
};

std::vector<std::uint8_t> EncodeScmtPcmFrame(const AudioBlock& block, std::size_t sample_count,
                                             ScmtFrameFlags flags = ScmtFrameFlags::None);
bool DecodeScmtHeader(const std::vector<std::uint8_t>& frame, ScmtFrameHeader& header);

}  // namespace stackchan::meeting
