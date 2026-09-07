/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

#pragma once

#include "meeting_manifest.h"

#include <cstdint>
#include <string>
#include <vector>

namespace stackchan::meeting {

inline constexpr std::uint64_t kSamplesPerSegment = 4800000U;

constexpr bool ShouldRotateSegment(std::uint64_t sample_count) {
    return sample_count >= kSamplesPerSegment;
}

class SegmentStore {
public:
    SegmentStore(std::string root, std::string meeting_id);

    std::string PartPath(std::uint32_t index) const;
    std::string WavPath(std::uint32_t index) const;
    bool WritePart(std::uint32_t index, const std::vector<std::int16_t>& samples) const;
    bool RecoverPart(std::uint32_t index, std::uint64_t start_sample,
                     const std::string& expected_sha256, SegmentEntry& recovered) const;

private:
    std::string root_;
    std::string meeting_id_;
};

std::string Sha256Hex(const std::vector<std::uint8_t>& data);

}  // namespace stackchan::meeting
