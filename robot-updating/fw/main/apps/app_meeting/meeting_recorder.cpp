/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

#include "meeting_recorder.h"

namespace stackchan::meeting {

BoundedUploadQueue::BoundedUploadQueue(std::size_t capacity) : capacity_(capacity) {
    blocks_.reserve(capacity);
}

bool BoundedUploadQueue::tryPush(const AudioBlock& block) {
    if (blocks_.size() >= capacity_) {
        return false;
    }
    blocks_.push_back(block);
    return true;
}

bool BoundedUploadQueue::pop(AudioBlock& block) {
    if (blocks_.empty()) {
        return false;
    }
    block = blocks_.front();
    blocks_.erase(blocks_.begin());
    return true;
}

std::size_t BoundedUploadQueue::size() const {
    return blocks_.size();
}

std::size_t BoundedUploadQueue::capacity() const {
    return capacity_;
}

MeetingRecorder::MeetingRecorder(AudioSource& source, RecorderSink& sd_sink,
                                 BoundedUploadQueue& upload_queue)
    : source_(source), sd_sink_(sd_sink), upload_queue_(upload_queue) {}

RecorderStats MeetingRecorder::captureUntilDry() {
    RecorderStats stats;
    AudioBlock block;
    while (source_.read(block)) {
        ++stats.frames_captured;
        if (!sd_sink_.write(block)) {
            break;
        }
        ++stats.frames_written_to_sd;
        if (upload_queue_.tryPush(block)) {
            ++stats.frames_queued_for_upload;
        } else {
            resume_gaps_.push_back(ResumeGap{block.sequence, block.timestamp_ms});
            ++stats.resume_gap_count;
        }
    }
    return stats;
}

const std::vector<ResumeGap>& MeetingRecorder::resumeGaps() const {
    return resume_gaps_;
}

}  // namespace stackchan::meeting
