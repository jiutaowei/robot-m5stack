/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

#include "meeting_uploader.h"

#include "meeting_protocol.h"

#include <limits>
#include <sstream>

namespace stackchan::meeting {
namespace {

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

bool extract_uint32(const std::string& json, const char* key, std::uint32_t& value) {
    const std::string marker = std::string("\"") + key + "\":";
    std::size_t position = json.find(marker);
    if (position == std::string::npos) return false;
    position += marker.size();
    std::size_t end = position;
    while (end < json.size() && json[end] >= '0' && json[end] <= '9') ++end;
    if (end == position) return false;
    try {
        const unsigned long parsed = std::stoul(json.substr(position, end - position));
        if (parsed > std::numeric_limits<std::uint32_t>::max()) return false;
        value = static_cast<std::uint32_t>(parsed);
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

}  // namespace

bool MeetingUploader::connect(MeetingWebSocketTransport& transport,
                              const MeetingUploadIdentity& identity,
                              const MeetingManifest& manifest) {
    restoreFromManifest(manifest);
    if (!transport.connect(identity)) return false;
    return true;
}

bool MeetingUploader::uploadBlock(MeetingWebSocketTransport& transport, const AudioBlock& block) {
    if (block.sequence < nextSequence()) return true;
    return transport.sendBinary(EncodeScmtPcmFrame(block, block.samples.size()));
}

bool MeetingUploader::replayMissingFinalizedSegments(MeetingManifest& manifest,
                                                     MeetingChunkClient& client,
                                                     UploadStats& stats) const {
    for (SegmentEntry& segment : manifest.segments) {
        if (segment.uploaded || segment.path.empty() || segment.sha256.empty()) continue;
        if (!client.uploadSegment(segment)) return false;
        segment.uploaded = true;
        ++stats.segments_replayed;
    }
    return true;
}

ReconnectBackoff::ReconnectBackoff(std::uint32_t initial_delay_ms, std::uint32_t max_delay_ms)
    : initial_delay_ms_(initial_delay_ms),
      max_delay_ms_(max_delay_ms),
      current_delay_ms_(initial_delay_ms) {}

std::uint32_t ReconnectBackoff::nextDelayMs() {
    const std::uint32_t delay = current_delay_ms_;
    if (current_delay_ms_ < max_delay_ms_) {
        const std::uint64_t doubled = static_cast<std::uint64_t>(current_delay_ms_) * 2U;
        current_delay_ms_ = doubled > max_delay_ms_ ? max_delay_ms_ : static_cast<std::uint32_t>(doubled);
    }
    return delay;
}

void ReconnectBackoff::reset() {
    current_delay_ms_ = initial_delay_ms_;
}

bool MeetingUploader::handleServerEvent(const std::string& json) {
    std::string type;
    if (!extract_string(json, "type", type)) return false;
    if (type == "ack") {
        std::uint32_t sequence = 0;
        if (!extract_uint32(json, "sequence", sequence)) return false;
        handleAck(sequence);
        return true;
    }
    if (type == "transcript") {
        TranscriptEvent event;
        std::uint32_t speaker_id = 0;
        if (!extract_int64(json, "sentenceId", event.sentence_id) ||
            !extract_uint32(json, "speakerId", speaker_id) ||
            !extract_bool(json, "final", event.final) ||
            !extract_int64(json, "startMs", event.start_ms) ||
            !extract_int64(json, "endMs", event.end_ms) ||
            !extract_string(json, "text", event.text)) {
            return false;
        }
        event.speaker_id = static_cast<int>(speaker_id);
        std::ostringstream speaker;
        speaker << "Speaker " << event.speaker_id;
        event.speaker = speaker.str();
        std::uint32_t sequence = 0;
        if (extract_uint32(json, "sequence", sequence)) {
            event.sequence = sequence;
        }
        last_transcript_ = event;
        return true;
    }
    return false;
}

void MeetingUploader::handleAck(std::uint32_t sequence) {
    if (!has_ack_ || sequence > last_ack_sequence_) {
        last_ack_sequence_ = sequence;
        has_ack_ = true;
    }
}

void MeetingUploader::restoreFromManifest(const MeetingManifest& manifest) {
    last_ack_sequence_ = manifest.last_ack_sequence;
    has_ack_ = manifest.has_last_ack_sequence;
}

void MeetingUploader::persistAck(MeetingManifest& manifest) const {
    manifest.has_last_ack_sequence = has_ack_;
    manifest.last_ack_sequence = last_ack_sequence_;
}

std::uint32_t MeetingUploader::lastAckSequence() const {
    return last_ack_sequence_;
}

std::uint32_t MeetingUploader::nextSequence() const {
    return has_ack_ ? last_ack_sequence_ + 1U : 0U;
}

const TranscriptEvent& MeetingUploader::lastTranscript() const {
    return last_transcript_;
}

std::vector<SegmentEntry> MeetingUploader::missingFinalizedSegments(const MeetingManifest& manifest) const {
    std::vector<SegmentEntry> missing;
    for (const SegmentEntry& segment : manifest.segments) {
        if (!segment.uploaded && !segment.path.empty() && !segment.sha256.empty()) {
            missing.push_back(segment);
        }
    }
    return missing;
}

}  // namespace stackchan::meeting
