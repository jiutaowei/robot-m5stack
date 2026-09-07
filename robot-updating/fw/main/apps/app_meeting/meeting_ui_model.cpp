/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

#include "meeting_ui_model.h"

#include <iomanip>
#include <sstream>
#include <utility>

namespace stackchan::meeting {

MeetingState MeetingUiModel::state() const {
    return state_;
}

void MeetingUiModel::primaryAction() {
    switch (state_) {
        case MeetingState::Idle:
        case MeetingState::Error:
            transitionTo(MeetingState::Preparing);
            transitionTo(MeetingState::Recording);
            break;
        case MeetingState::Recording:
            transitionTo(MeetingState::Paused);
            break;
        case MeetingState::Paused:
            transitionTo(MeetingState::Recording);
            break;
        default:
            break;
    }
}

void MeetingUiModel::endMeeting() {
    if (state_ == MeetingState::Recording || state_ == MeetingState::Paused) {
        transitionTo(MeetingState::Finalizing);
    }
}

void MeetingUiModel::markUploading() {
    transitionTo(MeetingState::Uploading);
}

void MeetingUiModel::markProcessing() {
    transitionTo(MeetingState::Processing);
}

void MeetingUiModel::markCompleted() {
    transitionTo(MeetingState::Completed);
}

void MeetingUiModel::markError() {
    state_ = MeetingState::Error;
}

void MeetingUiModel::reset() {
    state_ = MeetingState::Idle;
    transcript_lines_.clear();
}

std::string MeetingUiModel::primaryActionLabel() const {
    switch (state_) {
        case MeetingState::Idle:
        case MeetingState::Error:
            return "开始";
        case MeetingState::Recording:
            return "暂停";
        case MeetingState::Paused:
            return "继续";
        default:
            return "请稍候";
    }
}

std::string MeetingUiModel::stateLabel() const {
    switch (state_) {
        case MeetingState::Idle:
            return "就绪";
        case MeetingState::Preparing:
            return "准备中";
        case MeetingState::Recording:
            return "录音中";
        case MeetingState::Paused:
            return "已暂停";
        case MeetingState::Finalizing:
            return "正在保存";
        case MeetingState::Uploading:
            return "上传中";
        case MeetingState::Processing:
            return "处理中";
        case MeetingState::Completed:
            return "已完成";
        case MeetingState::Error:
            return "可恢复错误";
    }
    return "就绪";
}

bool MeetingUiModel::recordingIndicatorVisible() const {
    return state_ == MeetingState::Recording;
}

void MeetingUiModel::addTranscript(std::string speaker, std::string text) {
    transcript_lines_.push_back(TranscriptLine{std::move(speaker), std::move(text)});
    while (transcript_lines_.size() > 4U) {
        transcript_lines_.erase(transcript_lines_.begin());
    }
}

std::vector<TranscriptLine> MeetingUiModel::recentTranscripts() const {
    return transcript_lines_;
}

std::string MeetingUiModel::formatDuration(std::uint32_t elapsed_ms) const {
    const std::uint32_t total_seconds = elapsed_ms / 1000U;
    const std::uint32_t minutes = total_seconds / 60U;
    const std::uint32_t seconds = total_seconds % 60U;
    std::ostringstream output;
    output << std::setfill('0') << std::setw(2) << minutes << ':' << std::setw(2) << seconds;
    return output.str();
}

void MeetingUiModel::transitionTo(MeetingState next) {
    if (CanTransition(state_, next)) {
        state_ = next;
    }
}

}  // namespace stackchan::meeting
