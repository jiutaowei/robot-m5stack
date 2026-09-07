/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

#pragma once

#include "audio_source.h"

#include <cstddef>
#include <vector>

namespace stackchan::meeting {

class RecorderSink {
public:
    virtual ~RecorderSink() = default;
    virtual bool write(const AudioBlock& block) = 0;
};

struct ResumeGap {
    std::uint32_t sequence{0};
    std::int64_t timestamp_ms{0};
};

struct RecorderStats {
    std::size_t frames_captured{0};
    std::size_t frames_written_to_sd{0};
    std::size_t frames_queued_for_upload{0};
    std::size_t resume_gap_count{0};
};

class BoundedUploadQueue {
public:
    explicit BoundedUploadQueue(std::size_t capacity);

    bool tryPush(const AudioBlock& block);
    bool pop(AudioBlock& block);
    std::size_t size() const;
    std::size_t capacity() const;

private:
    std::size_t capacity_{0};
    std::vector<AudioBlock> blocks_;
};

class MeetingRecorder {
public:
    MeetingRecorder(AudioSource& source, RecorderSink& sd_sink, BoundedUploadQueue& upload_queue);

    RecorderStats captureUntilDry();
    const std::vector<ResumeGap>& resumeGaps() const;

private:
    AudioSource& source_;
    RecorderSink& sd_sink_;
    BoundedUploadQueue& upload_queue_;
    std::vector<ResumeGap> resume_gaps_;
};

}  // namespace stackchan::meeting
