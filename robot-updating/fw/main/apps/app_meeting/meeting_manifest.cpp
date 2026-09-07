/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

#include "meeting_manifest.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace stackchan::meeting {
namespace {

std::string escape_json(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (char ch : value) {
        if (ch == '"' || ch == '\\') {
            result.push_back('\\');
        }
        result.push_back(ch);
    }
    return result;
}

bool extract_string(const std::string& json, const char* key, std::string& value) {
    const std::string marker = std::string("\"") + key + "\":\"";
    std::size_t position = json.find(marker);
    if (position == std::string::npos) return false;
    position += marker.size();
    value.clear();
    bool escaped = false;
    for (; position < json.size(); ++position) {
        const char ch = json[position];
        if (escaped) {
            value.push_back(ch);
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else if (ch == '"') {
            return true;
        } else {
            value.push_back(ch);
        }
    }
    return false;
}

bool extract_uint64(const std::string& json, const char* key, std::uint64_t& value) {
    const std::string marker = std::string("\"") + key + "\":";
    std::size_t position = json.find(marker);
    if (position == std::string::npos) return false;
    position += marker.size();
    std::size_t end = position;
    while (end < json.size() && json[end] >= '0' && json[end] <= '9') ++end;
    if (end == position) return false;
    try {
        value = std::stoull(json.substr(position, end - position));
    } catch (...) {
        return false;
    }
    return true;
}

bool extract_int64(const std::string& json, const char* key, std::int64_t& value) {
    const std::string marker = std::string("\"") + key + "\":";
    std::size_t position = json.find(marker);
    if (position == std::string::npos) return false;
    position += marker.size();
    std::size_t end = position;
    if (end < json.size() && json[end] == '-') ++end;
    while (end < json.size() && json[end] >= '0' && json[end] <= '9') ++end;
    if (end == position) return false;
    try {
        value = std::stoll(json.substr(position, end - position));
    } catch (...) {
        return false;
    }
    return true;
}

bool extract_bool(const std::string& json, const char* key, bool& value) {
    const std::string marker = std::string("\"") + key + "\":";
    const std::size_t position = json.find(marker);
    if (position == std::string::npos) return false;
    const std::size_t start = position + marker.size();
    if (json.compare(start, 4U, "true") == 0) {
        value = true;
        return true;
    }
    if (json.compare(start, 5U, "false") == 0) {
        value = false;
        return true;
    }
    return false;
}

bool has_key(const std::string& json, const char* key) {
    const std::string marker = std::string("\"") + key + "\":";
    return json.find(marker) != std::string::npos;
}

bool sync_file(std::FILE* file) {
    if (std::fflush(file) != 0) return false;
#ifdef _WIN32
    return _commit(_fileno(file)) == 0;
#else
    return fsync(fileno(file)) == 0;
#endif
}

}  // namespace

std::string SerializeManifest(const MeetingManifest& manifest) {
    std::ostringstream output;
    output << "{\"meetingId\":\"" << escape_json(manifest.meeting_id)
           << "\",\"sampleRate\":" << manifest.sample_rate
           << ",\"createdAt\":" << manifest.created_at_ms
           << ",\"hasLastAckSequence\":" << (manifest.has_last_ack_sequence ? "true" : "false")
           << ",\"lastAckSequence\":" << manifest.last_ack_sequence << ",\"segments\":[";
    for (std::size_t index = 0; index < manifest.segments.size(); ++index) {
        if (index != 0U) output << ',';
        const SegmentEntry& segment = manifest.segments[index];
        output << "{\"index\":" << segment.index << ",\"startSample\":" << segment.start_sample
               << ",\"sampleCount\":" << segment.sample_count << ",\"path\":\""
               << escape_json(segment.path) << "\",\"sha256\":\"" << segment.sha256
               << "\",\"uploaded\":" << (segment.uploaded ? "true" : "false") << '}';
    }
    output << "]}";
    return output.str();
}

bool ParseManifest(const std::string& json, MeetingManifest& manifest) {
    MeetingManifest parsed;
    std::uint64_t sample_rate = 0;
    std::uint64_t ack = 0;
    if (!extract_string(json, "meetingId", parsed.meeting_id) ||
        !extract_uint64(json, "sampleRate", sample_rate) || sample_rate > UINT32_MAX ||
        !extract_int64(json, "createdAt", parsed.created_at_ms) ||
        !extract_uint64(json, "lastAckSequence", ack) || ack > UINT32_MAX) {
        return false;
    }
    parsed.sample_rate = static_cast<std::uint32_t>(sample_rate);
    if (has_key(json, "hasLastAckSequence")) {
        if (!extract_bool(json, "hasLastAckSequence", parsed.has_last_ack_sequence)) {
            return false;
        }
    } else {
        parsed.has_last_ack_sequence = ack > 0U;
    }
    parsed.last_ack_sequence = static_cast<std::uint32_t>(ack);

    const std::string marker = "\"segments\":[";
    std::size_t position = json.find(marker);
    if (position == std::string::npos) return false;
    position += marker.size();
    while (position < json.size() && json[position] != ']') {
        const std::size_t begin = json.find('{', position);
        const std::size_t end = json.find('}', begin);
        if (begin == std::string::npos || end == std::string::npos) return false;
        const std::string object = json.substr(begin, end - begin + 1U);
        SegmentEntry segment;
        std::uint64_t index = 0;
        if (!extract_uint64(object, "index", index) || index > UINT32_MAX ||
            !extract_uint64(object, "startSample", segment.start_sample) ||
            !extract_uint64(object, "sampleCount", segment.sample_count) ||
            !extract_string(object, "path", segment.path) ||
            !extract_string(object, "sha256", segment.sha256) ||
            !extract_bool(object, "uploaded", segment.uploaded)) {
            return false;
        }
        segment.index = static_cast<std::uint32_t>(index);
        parsed.segments.push_back(std::move(segment));
        position = end + 1U;
        if (position < json.size() && json[position] == ',') ++position;
    }
    if (position >= json.size() || json[position] != ']') return false;
    manifest = std::move(parsed);
    return true;
}

bool SaveManifest(const std::string& path, const MeetingManifest& manifest) {
    const std::filesystem::path target(path);
    std::error_code error;
    if (!target.parent_path().empty()) {
        std::filesystem::create_directories(target.parent_path(), error);
        if (error) return false;
    }
    const std::string temporary = path + ".part";
    std::FILE* file = std::fopen(temporary.c_str(), "wb");
    if (file == nullptr) return false;
    const std::string json = SerializeManifest(manifest);
    const bool written = std::fwrite(json.data(), 1U, json.size(), file) == json.size();
    const bool synced = written && sync_file(file);
    const bool closed = std::fclose(file) == 0;
    if (!synced || !closed) return false;
    std::filesystem::rename(temporary, target, error);
    return !error;
}

bool LoadManifest(const std::string& path, MeetingManifest& manifest) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    const std::string json((std::istreambuf_iterator<char>(input)), {});
    return ParseManifest(json, manifest);
}

std::size_t FirstUnfinishedSegment(const MeetingManifest& manifest) {
    for (std::size_t index = 0; index < manifest.segments.size(); ++index) {
        if (!manifest.segments[index].uploaded) return index;
    }
    return manifest.segments.size();
}

}  // namespace stackchan::meeting
