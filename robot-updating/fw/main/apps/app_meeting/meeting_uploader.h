/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

#pragma once

#include "audio_source.h"
#include "meeting_manifest.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace stackchan::meeting {

struct TranscriptEvent {
    std::uint32_t sequence{0};
    std::int64_t sentence_id{0};
    int speaker_id{0};
    bool final{false};
    std::int64_t start_ms{0};
    std::int64_t end_ms{0};
    std::string speaker;
    std::string text;
};

struct MeetingUploadIdentity {
    std::string url;
    std::string authorization;
    std::string device_id;
    std::string client_id;
};

struct UploadStats {
    std::size_t frames_sent{0};
    std::size_t frames_skipped{0};
    std::size_t segments_replayed{0};
};

class MeetingWebSocketTransport {
public:
    virtual ~MeetingWebSocketTransport() = default;
    virtual bool connect(const MeetingUploadIdentity& identity) = 0;
    virtual bool sendText(const std::string& text) = 0;
    virtual bool sendBinary(const std::vector<std::uint8_t>& bytes) = 0;
};

class MeetingChunkClient {
public:
    virtual ~MeetingChunkClient() = default;
    virtual bool uploadSegment(const SegmentEntry& segment) = 0;
};

class ReconnectBackoff {
public:
    ReconnectBackoff(std::uint32_t initial_delay_ms, std::uint32_t max_delay_ms);

    std::uint32_t nextDelayMs();
    void reset();

private:
    std::uint32_t initial_delay_ms_{0};
    std::uint32_t max_delay_ms_{0};
    std::uint32_t current_delay_ms_{0};
};

class MeetingUploader {
public:
    bool connect(MeetingWebSocketTransport& transport, const MeetingUploadIdentity& identity,
                 const MeetingManifest& manifest);
    bool uploadBlock(MeetingWebSocketTransport& transport, const AudioBlock& block);
    bool replayMissingFinalizedSegments(MeetingManifest& manifest, MeetingChunkClient& client,
                                        UploadStats& stats) const;

    bool handleServerEvent(const std::string& json);
    void handleAck(std::uint32_t sequence);
    void restoreFromManifest(const MeetingManifest& manifest);
    void persistAck(MeetingManifest& manifest) const;

    std::uint32_t lastAckSequence() const;
    std::uint32_t nextSequence() const;
    const TranscriptEvent& lastTranscript() const;
    std::vector<SegmentEntry> missingFinalizedSegments(const MeetingManifest& manifest) const;

private:
    std::uint32_t last_ack_sequence_{0};
    bool has_ack_{false};
    TranscriptEvent last_transcript_;
};

}  // namespace stackchan::meeting
