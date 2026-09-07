/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

#pragma once

#include "meeting_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace stackchan::meeting {

struct TranscriptLine {
    std::string speaker;
    std::string text;
};

class MeetingUiModel {
public:
    MeetingState state() const;
    void primaryAction();
    void endMeeting();
    void markUploading();
    void markProcessing();
    void markCompleted();
    void markError();
    void reset();

    std::string primaryActionLabel() const;
    std::string stateLabel() const;
    bool recordingIndicatorVisible() const;

    void addTranscript(std::string speaker, std::string text);
    std::vector<TranscriptLine> recentTranscripts() const;
    std::string formatDuration(std::uint32_t elapsed_ms) const;

private:
    MeetingState state_{MeetingState::Idle};
    std::vector<TranscriptLine> transcript_lines_;

    void transitionTo(MeetingState next);
};

}  // namespace stackchan::meeting
