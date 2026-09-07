/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

#include "apps/app_meeting/meeting_types.h"
#include "apps/app_meeting/meeting_manifest.h"
#include "apps/app_meeting/audio_source.h"
#include "apps/app_meeting/meeting_protocol.h"
#include "apps/app_meeting/meeting_recorder.h"
#include "apps/app_meeting/meeting_uploader.h"
#include "apps/app_meeting/meeting_ui_model.h"
#include "apps/app_meeting/segment_store.h"
#include "apps/app_meeting/wav_writer.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::uint16_t read_le16(const stackchan::meeting::WavHeader& header, std::size_t offset) {
    return static_cast<std::uint16_t>(header[offset]) |
           static_cast<std::uint16_t>(header[offset + 1]) << 8U;
}

std::uint32_t read_le32(const stackchan::meeting::WavHeader& header, std::size_t offset) {
    return static_cast<std::uint32_t>(header[offset]) |
           static_cast<std::uint32_t>(header[offset + 1]) << 8U |
           static_cast<std::uint32_t>(header[offset + 2]) << 16U |
           static_cast<std::uint32_t>(header[offset + 3]) << 24U;
}

void test_state_transitions() {
    using stackchan::meeting::CanTransition;
    using stackchan::meeting::MeetingState;

    expect(CanTransition(MeetingState::Idle, MeetingState::Preparing), "idle -> preparing");
    expect(CanTransition(MeetingState::Preparing, MeetingState::Recording), "preparing -> recording");
    expect(CanTransition(MeetingState::Recording, MeetingState::Paused), "recording -> paused");
    expect(CanTransition(MeetingState::Paused, MeetingState::Recording), "paused -> recording");
    expect(CanTransition(MeetingState::Recording, MeetingState::Finalizing), "recording -> finalizing");
    expect(CanTransition(MeetingState::Finalizing, MeetingState::Uploading), "finalizing -> uploading");
    expect(CanTransition(MeetingState::Uploading, MeetingState::Processing), "uploading -> processing");
    expect(CanTransition(MeetingState::Processing, MeetingState::Completed), "processing -> completed");
    expect(!CanTransition(MeetingState::Completed, MeetingState::Recording), "completed must be terminal");
}

void test_pcm_wav_header() {
    using namespace stackchan::meeting;
    const WavHeader placeholder = WritePlaceholderHeader();
    expect(placeholder.size() == kWavHeaderSize, "WAV header is 44 bytes");
    expect(read_le32(placeholder, 0) == 0x46464952U, "RIFF marker");
    expect(read_le32(placeholder, 8) == 0x45564157U, "WAVE marker");
    expect(read_le32(placeholder, 12) == 0x20746d66U, "fmt marker");
    expect(read_le16(placeholder, 20) == 1U, "PCM format");
    expect(read_le16(placeholder, 22) == 1U, "mono channel count");
    expect(read_le32(placeholder, 24) == 16000U, "16 kHz sample rate");
    expect(read_le32(placeholder, 28) == 32000U, "byte rate");
    expect(read_le16(placeholder, 32) == 2U, "block alignment");
    expect(read_le16(placeholder, 34) == 16U, "16-bit samples");
    expect(read_le32(placeholder, 36) == 0x61746164U, "data marker");
    expect(read_le32(placeholder, 40) == 0U, "placeholder data size");

    const WavHeader finalized = FinalizeHeader(16000U);
    expect(read_le32(finalized, 40) == 32000U, "one second PCM data size");
    expect(read_le32(finalized, 4) == 32036U, "RIFF chunk size");
}

void test_recording_calculations() {
    using namespace stackchan::meeting;
    expect(DurationMilliseconds(16000U) == 1000U, "one second duration");
    expect(DurationMilliseconds(4800000U) == 300000U, "five minute duration");
    expect(PcmDataSizeBytes(4800000U) == 9600000U, "five minute PCM size");
    expect(WavFileSizeBytes(4800000U) == 9600044U, "five minute WAV size");
}

void test_manifest_round_trip_and_resume_index() {
    using namespace stackchan::meeting;
    MeetingManifest manifest;
    manifest.meeting_id = "meeting-42";
    manifest.sample_rate = 16000U;
    manifest.created_at_ms = 1234567;
    manifest.last_ack_sequence = 99U;
    manifest.segments = {
        SegmentEntry{0U, 0U, kSamplesPerSegment, "000000.wav", "abc", true},
        SegmentEntry{1U, kSamplesPerSegment, kSamplesPerSegment, "000001.wav", "def", false},
    };

    const std::string json = SerializeManifest(manifest);
    MeetingManifest decoded;
    expect(ParseManifest(json, decoded), "manifest JSON parses");
    expect(decoded.meeting_id == manifest.meeting_id, "manifest meeting ID round trip");
    expect(decoded.last_ack_sequence == 99U, "manifest ACK round trip");
    expect(decoded.segments.size() == 2U, "manifest segment count round trip");
    expect(FirstUnfinishedSegment(decoded) == 1U, "resume at first unfinished segment");

    const auto root = std::filesystem::temp_directory_path() / "stackchan-meeting-manifest-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto path = root / "manifest.json";
    expect(SaveManifest(path.string(), manifest), "manifest saved atomically");
    MeetingManifest loaded;
    expect(LoadManifest(path.string(), loaded), "manifest loaded from disk");
    expect(loaded.meeting_id == "meeting-42" && loaded.segments.size() == 2U,
           "persisted manifest round trip");
    std::filesystem::remove_all(root);
}

void test_segment_rotation_and_recovery() {
    using namespace stackchan::meeting;
    expect(ShouldRotateSegment(kSamplesPerSegment - 1U) == false, "do not rotate early");
    expect(ShouldRotateSegment(kSamplesPerSegment), "rotate at five minutes");

    const auto root = std::filesystem::temp_directory_path() / "stackchan-meeting-segment-test";
    std::filesystem::remove_all(root);
    SegmentStore store(root.string(), "meeting-42");
    const std::vector<std::int16_t> samples{1, -2, 3, -4};
    expect(store.WritePart(0U, samples), "write .part segment");
    expect(std::filesystem::exists(store.PartPath(0U)), ".part exists before recovery");

    SegmentEntry recovered;
    expect(store.RecoverPart(0U, 0U, "", recovered), "recover valid .part");
    expect(!std::filesystem::exists(store.PartPath(0U)), ".part renamed after recovery");
    expect(std::filesystem::exists(store.WavPath(0U)), "recovered WAV exists");
    expect(recovered.sample_count == samples.size(), "recovered sample count");
    expect(recovered.sha256.size() == 64U, "recovered SHA-256");

    std::ifstream wav(store.WavPath(0U), std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(wav)), {});
    wav.close();
    expect(bytes.size() == kWavHeaderSize + samples.size() * sizeof(std::int16_t), "WAV size after recovery");
    expect(read_le32(*reinterpret_cast<const WavHeader*>(bytes.data()), 40U) == samples.size() * 2U,
           "recovered WAV header finalized");
    std::filesystem::remove_all(root);
}

void test_recovery_rejects_checksum_mismatch() {
    using namespace stackchan::meeting;
    const auto root = std::filesystem::temp_directory_path() / "stackchan-meeting-checksum-test";
    std::filesystem::remove_all(root);
    SegmentStore store(root.string(), "meeting-43");
    expect(store.WritePart(0U, std::vector<std::int16_t>{10, 20}), "write checksum test part");
    SegmentEntry recovered;
    expect(!store.RecoverPart(0U, 0U, std::string(64U, '0'), recovered), "reject mismatched checksum");
    expect(std::filesystem::exists(store.PartPath(0U)), "mismatched .part remains recoverable");
    expect(!std::filesystem::exists(store.WavPath(0U)), "mismatched WAV not published");
    std::filesystem::remove_all(root);
}

void test_sha256_golden_vector() {
    using namespace stackchan::meeting;
    const std::vector<std::uint8_t> abc{'a', 'b', 'c'};
    expect(Sha256Hex(abc) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
           "SHA-256 golden vector");
}

class FakeAudioSource final : public stackchan::meeting::AudioSource {
public:
    explicit FakeAudioSource(std::uint32_t block_count) {
        blocks_.reserve(block_count);
        for (std::uint32_t sequence = 0; sequence < block_count; ++sequence) {
            stackchan::meeting::AudioBlock block{};
            block.sequence = sequence;
            block.timestamp_ms = static_cast<std::int64_t>(sequence) * 20;
            for (std::size_t i = 0; i < block.samples.size(); ++i) {
                block.samples[i] = static_cast<std::int16_t>(sequence * 1000U + i);
            }
            blocks_.push_back(block);
        }
    }

    bool read(stackchan::meeting::AudioBlock& block) override {
        if (next_ >= blocks_.size()) return false;
        block = blocks_[next_++];
        return true;
    }

private:
    std::vector<stackchan::meeting::AudioBlock> blocks_;
    std::size_t next_{0};
};

class CapturingSdSink final : public stackchan::meeting::RecorderSink {
public:
    bool write(const stackchan::meeting::AudioBlock& block) override {
        for (std::int16_t sample : block.samples) samples.push_back(sample);
        sequences.push_back(block.sequence);
        return true;
    }

    std::vector<std::int16_t> samples;
    std::vector<std::uint32_t> sequences;
};

class FakeMeetingTransport final : public stackchan::meeting::MeetingWebSocketTransport {
public:
    bool connect(const stackchan::meeting::MeetingUploadIdentity& identity) override {
        connected_identity = identity;
        connected = true;
        return true;
    }

    bool sendText(const std::string& text) override {
        texts.push_back(text);
        return true;
    }

    bool sendBinary(const std::vector<std::uint8_t>& bytes) override {
        binaries.push_back(bytes);
        return true;
    }

    bool connected{false};
    stackchan::meeting::MeetingUploadIdentity connected_identity;
    std::vector<std::string> texts;
    std::vector<std::vector<std::uint8_t>> binaries;
};

class FakeChunkClient final : public stackchan::meeting::MeetingChunkClient {
public:
    bool uploadSegment(const stackchan::meeting::SegmentEntry& segment) override {
        uploaded.push_back(segment);
        return true;
    }

    std::vector<stackchan::meeting::SegmentEntry> uploaded;
};

void test_recorder_keeps_sd_continuous_when_upload_stalls() {
    using namespace stackchan::meeting;
    FakeAudioSource source(4U);
    CapturingSdSink sd;
    BoundedUploadQueue upload(1U);
    MeetingRecorder recorder(source, sd, upload);

    const RecorderStats stats = recorder.captureUntilDry();
    expect(stats.frames_captured == 4U, "recorder captured all frames");
    expect(stats.frames_written_to_sd == 4U, "SD sink receives every frame");
    expect(stats.frames_queued_for_upload == 1U, "upload queue accepts only capacity");
    expect(stats.resume_gap_count == 3U, "full upload queue records resume gaps");
    expect(upload.size() == 1U, "upload queue remains bounded");

    expect(sd.sequences == std::vector<std::uint32_t>({0U, 1U, 2U, 3U}), "SD sequence continuity");
    expect(sd.samples.size() == 4U * kAudioBlockSamples, "SD sample count continuity");
    for (std::size_t i = 0; i < sd.samples.size(); ++i) {
        const std::uint32_t sequence = static_cast<std::uint32_t>(i / kAudioBlockSamples);
        const std::uint32_t offset = static_cast<std::uint32_t>(i % kAudioBlockSamples);
        expect(sd.samples[i] == static_cast<std::int16_t>(sequence * 1000U + offset),
               "SD samples remain in capture order");
    }
    expect(recorder.resumeGaps().size() == 3U, "resume gap metadata retained");
    expect(recorder.resumeGaps().front().sequence == 1U, "first dropped upload sequence recorded");
}

void test_scmt_binary_frame_golden_bytes() {
    using namespace stackchan::meeting;
    AudioBlock block{};
    block.sequence = 42U;
    block.timestamp_ms = 0x0102030405060708LL;
    block.samples[0] = static_cast<std::int16_t>(0x1234);
    block.samples[1] = static_cast<std::int16_t>(-2);

    const std::vector<std::uint8_t> frame = EncodeScmtPcmFrame(block, 2U, ScmtFrameFlags::FinalSegment);
    const std::vector<std::uint8_t> expected{
        'S', 'C', 'M', 'T', 0x01, 0x03, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x2a, 0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08, 0x00, 0x00, 0x00, 0x04,
        0x34, 0x12, 0xfe, 0xff,
    };
    expect(frame == expected, "SCMT PCM frame golden bytes");

    ScmtFrameHeader decoded{};
    expect(DecodeScmtHeader(frame, decoded), "SCMT header decodes");
    expect(decoded.sequence == 42U, "SCMT decoded sequence");
    expect(decoded.timestamp_ms == 0x0102030405060708LL, "SCMT decoded timestamp");
    expect(decoded.payload_length == 4U, "SCMT decoded payload length");

    const std::vector<std::uint8_t> default_frame = EncodeScmtPcmFrame(block, 1U);
    expect(default_frame[5] == 0x01, "default SCMT frame carries backend audio flag");
}

void test_ack_resume_and_transcript_events() {
    using namespace stackchan::meeting;
    MeetingUploader uploader;
    expect(uploader.nextSequence() == 0U, "uploader starts at sequence zero");
    expect(uploader.handleServerEvent("{\"type\":\"ack\",\"sequence\":42}"), "ACK event parses");
    expect(uploader.lastAckSequence() == 42U, "ACK sequence stored");
    expect(uploader.nextSequence() == 43U, "ACK 42 resumes at 43");

    expect(uploader.handleServerEvent(
               "{\"type\":\"transcript\",\"session\":0,\"sentenceId\":9,\"speakerId\":1,"
               "\"final\":false,\"startMs\":1200,\"endMs\":3300,\"text\":\"hello\"}"),
           "backend transcript event parses");
    const TranscriptEvent transcript = uploader.lastTranscript();
    expect(transcript.sentence_id == 9, "transcript sentence id");
    expect(transcript.speaker_id == 1, "transcript speaker id");
    expect(transcript.speaker == "Speaker 1", "transcript speaker label");
    expect(!transcript.final, "transcript final flag");
    expect(transcript.start_ms == 1200, "transcript start offset");
    expect(transcript.end_ms == 3300, "transcript end offset");
    expect(transcript.text == "hello", "transcript text");
}

void test_exponential_backoff_caps() {
    using namespace stackchan::meeting;
    ReconnectBackoff backoff(250U, 4000U);
    expect(backoff.nextDelayMs() == 250U, "first reconnect delay");
    expect(backoff.nextDelayMs() == 500U, "second reconnect delay");
    expect(backoff.nextDelayMs() == 1000U, "third reconnect delay");
    expect(backoff.nextDelayMs() == 2000U, "fourth reconnect delay");
    expect(backoff.nextDelayMs() == 4000U, "backoff reaches cap");
    expect(backoff.nextDelayMs() == 4000U, "backoff remains capped");
    backoff.reset();
    expect(backoff.nextDelayMs() == 250U, "backoff reset");
}

void test_manifest_ack_persist_and_reconnect_segment_replay() {
    using namespace stackchan::meeting;
    MeetingManifest manifest;
    manifest.meeting_id = "meeting-upload";
    manifest.sample_rate = 16000U;
    manifest.created_at_ms = 1;
    manifest.has_last_ack_sequence = true;
    manifest.last_ack_sequence = 41U;
    manifest.segments = {
        SegmentEntry{0U, 0U, 640U, "000000.wav", "aaa", true},
        SegmentEntry{1U, 640U, 640U, "000001.wav", "bbb", false},
        SegmentEntry{2U, 1280U, 640U, "000002.wav", "ccc", false},
    };

    MeetingUploader uploader;
    uploader.restoreFromManifest(manifest);
    expect(uploader.nextSequence() == 42U, "uploader resumes from persisted ACK");
    uploader.handleAck(42U);
    uploader.persistAck(manifest);
    expect(manifest.has_last_ack_sequence, "ACK presence persisted to manifest model");
    expect(manifest.last_ack_sequence == 42U, "ACK persisted to manifest model");

    const std::vector<SegmentEntry> missing = uploader.missingFinalizedSegments(manifest);
    expect(missing.size() == 2U, "reconnect replays missing finalized segments");
    expect(missing[0].index == 1U && missing[1].index == 2U, "missing segment order preserved");

    FakeChunkClient chunk_client;
    UploadStats stats;
    expect(uploader.replayMissingFinalizedSegments(manifest, chunk_client, stats),
           "chunk replay succeeds after reconnect");
    expect(stats.segments_replayed == 2U, "chunk replay stat");
    expect(chunk_client.uploaded.size() == 2U, "chunk client receives missing segments");
    expect(manifest.segments[1].uploaded && manifest.segments[2].uploaded,
           "chunk replay marks manifest segments uploaded");
}

void test_wss_transport_uses_device_identity_and_resumes_frames() {
    using namespace stackchan::meeting;
    MeetingManifest manifest;
    manifest.meeting_id = "meeting-wss";
    manifest.sample_rate = 16000U;
    manifest.created_at_ms = 1;
    manifest.has_last_ack_sequence = true;
    manifest.last_ack_sequence = 42U;

    MeetingUploader uploader;
    FakeMeetingTransport transport;
    MeetingUploadIdentity identity{
        "wss://meeting.example.invalid/upload",
        "Bearer existing-device-token",
        "mac-001",
        "client-abc",
    };
    expect(uploader.connect(transport, identity, manifest), "WSS uploader connects");
    expect(transport.connected, "transport connected");
    expect(transport.connected_identity.device_id == "mac-001", "device identity passed to transport");
    expect(transport.texts.empty(), "backend websocket receives binary frames only");

    AudioBlock old_block{};
    old_block.sequence = 41U;
    expect(uploader.uploadBlock(transport, old_block), "already acknowledged block skipped successfully");
    expect(transport.binaries.empty(), "old block is not re-sent");

    AudioBlock next_block{};
    next_block.sequence = 43U;
    next_block.timestamp_ms = 860;
    next_block.samples[0] = 7;
    expect(uploader.uploadBlock(transport, next_block), "next block sent");
    expect(transport.binaries.size() == 1U, "one binary frame sent");
    ScmtFrameHeader header{};
    expect(DecodeScmtHeader(transport.binaries[0], header), "sent binary frame is SCMT");
    expect(header.sequence == 43U, "sent frame resumes at sequence 43");
}

void test_manifest_distinguishes_no_ack_from_ack_zero() {
    using namespace stackchan::meeting;
    MeetingManifest fresh;
    fresh.meeting_id = "fresh";
    fresh.sample_rate = 16000U;
    fresh.created_at_ms = 1;

    MeetingUploader fresh_uploader;
    fresh_uploader.restoreFromManifest(fresh);
    expect(fresh_uploader.nextSequence() == 0U, "fresh manifest starts from sequence zero");

    fresh_uploader.handleAck(0U);
    fresh_uploader.persistAck(fresh);
    expect(fresh.has_last_ack_sequence, "ACK zero presence is persisted");
    expect(fresh.last_ack_sequence == 0U, "ACK zero value is persisted");

    MeetingUploader resumed;
    resumed.restoreFromManifest(fresh);
    expect(resumed.nextSequence() == 1U, "manifest with ACK zero resumes at sequence one");

    MeetingManifest parsed;
    expect(ParseManifest(SerializeManifest(fresh), parsed), "manifest round-trips ACK presence");
    expect(parsed.has_last_ack_sequence, "parsed manifest preserves ACK presence");
    expect(parsed.last_ack_sequence == 0U, "parsed manifest preserves ACK zero");
}

void test_meeting_ui_model_actions_and_recent_transcripts() {
    using namespace stackchan::meeting;
    MeetingUiModel model;
    expect(model.state() == MeetingState::Idle, "meeting UI starts idle");
    expect(model.primaryActionLabel() == "Start", "idle primary action");

    model.primaryAction();
    expect(model.state() == MeetingState::Recording, "primary action starts recording");
    expect(model.primaryActionLabel() == "Pause", "recording primary action");
    expect(model.recordingIndicatorVisible(), "recording indicator visible");

    model.primaryAction();
    expect(model.state() == MeetingState::Paused, "primary action pauses recording");
    expect(model.primaryActionLabel() == "Resume", "paused primary action");
    expect(!model.recordingIndicatorVisible(), "paused recording indicator hidden");

    model.primaryAction();
    expect(model.state() == MeetingState::Recording, "primary action resumes recording");
    model.endMeeting();
    expect(model.state() == MeetingState::Finalizing, "end meeting starts finalizing");
    model.markUploading();
    expect(model.state() == MeetingState::Uploading, "uploading state");
    model.markProcessing();
    expect(model.state() == MeetingState::Processing, "processing state");
    model.markCompleted();
    expect(model.state() == MeetingState::Completed, "completed state");

    model.addTranscript("A", "one");
    model.addTranscript("B", "two");
    model.addTranscript("C", "three");
    model.addTranscript("D", "four");
    model.addTranscript("E", "five");
    const std::vector<TranscriptLine> lines = model.recentTranscripts();
    expect(lines.size() == 4U, "meeting UI keeps four transcript lines");
    expect(lines.front().speaker == "B" && lines.front().text == "two", "oldest transcript line evicted");
    expect(model.formatDuration(125000U) == "02:05", "duration format");
}

}  // namespace

int main() {
    test_state_transitions();
    test_pcm_wav_header();
    test_recording_calculations();
    test_manifest_round_trip_and_resume_index();
    test_segment_rotation_and_recovery();
    test_recovery_rejects_checksum_mismatch();
    test_sha256_golden_vector();
    test_recorder_keeps_sd_continuous_when_upload_stalls();
    test_scmt_binary_frame_golden_bytes();
    test_ack_resume_and_transcript_events();
    test_exponential_backoff_caps();
    test_manifest_ack_persist_and_reconnect_segment_replay();
    test_wss_transport_uses_device_identity_and_resumes_frames();
    test_manifest_distinguishes_no_ack_from_ack_zero();
    test_meeting_ui_model_actions_and_recent_transcripts();
    std::cout << "meeting tests passed\n";
    return 0;
}
