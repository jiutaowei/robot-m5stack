/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

#pragma once

namespace stackchan::meeting {

enum class MeetingState {
    Idle,
    Preparing,
    Recording,
    Paused,
    Finalizing,
    Uploading,
    Processing,
    Completed,
    Error,
};

constexpr bool CanTransition(MeetingState from, MeetingState to) {
    switch (from) {
        case MeetingState::Idle:
            return to == MeetingState::Preparing;
        case MeetingState::Preparing:
            return to == MeetingState::Recording || to == MeetingState::Error;
        case MeetingState::Recording:
            return to == MeetingState::Paused || to == MeetingState::Finalizing ||
                   to == MeetingState::Error;
        case MeetingState::Paused:
            return to == MeetingState::Recording || to == MeetingState::Finalizing ||
                   to == MeetingState::Error;
        case MeetingState::Finalizing:
            return to == MeetingState::Uploading || to == MeetingState::Processing ||
                   to == MeetingState::Error;
        case MeetingState::Uploading:
            return to == MeetingState::Processing || to == MeetingState::Error;
        case MeetingState::Processing:
            return to == MeetingState::Completed || to == MeetingState::Error;
        case MeetingState::Error:
            return to == MeetingState::Preparing;
        case MeetingState::Completed:
            return false;
    }
    return false;
}

}  // namespace stackchan::meeting
