/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace stackchan::meeting {

struct SegmentEntry {
    std::uint32_t index{0};
    std::uint64_t start_sample{0};
    std::uint64_t sample_count{0};
    std::string path;
    std::string sha256;
    bool uploaded{false};
};

struct MeetingManifest {
    std::string meeting_id;
    std::uint32_t sample_rate{16000U};
    std::int64_t created_at_ms{0};
    bool has_last_ack_sequence{false};
    std::uint32_t last_ack_sequence{0};
    std::vector<SegmentEntry> segments;
};

std::string SerializeManifest(const MeetingManifest& manifest);
bool ParseManifest(const std::string& json, MeetingManifest& manifest);
bool SaveManifest(const std::string& path, const MeetingManifest& manifest);
bool LoadManifest(const std::string& path, MeetingManifest& manifest);
std::size_t FirstUnfinishedSegment(const MeetingManifest& manifest);

}  // namespace stackchan::meeting
