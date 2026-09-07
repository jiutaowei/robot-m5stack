/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

#include "audio_source.h"

#include <algorithm>
#include <vector>
#include <board.h>
#include <audio/audio_codec.h>
#include <hal/hal.h>

namespace stackchan::meeting {

StackChanAudioSource::StackChanAudioSource(const char* owner) {
    owner_ = owner != nullptr ? owner : "meeting";
    acquired_ = GetHAL().acquireMicrophone(owner_);
}

StackChanAudioSource::~StackChanAudioSource() {
    if (acquired_) {
        GetHAL().releaseMicrophone(owner_);
    }
}

bool StackChanAudioSource::read(AudioBlock& block) {
    if (!acquired_) {
        return false;
    }

    auto& board = Board::GetInstance();
    auto audio_codec = board.GetAudioCodec();
    if (!audio_codec) {
        return false;
    }

    const std::size_t input_channels = std::max(audio_codec->input_channels(), 1);
    std::vector<std::int16_t> input(kAudioBlockSamples * input_channels);
    if (!audio_codec->input_enabled()) {
        audio_codec->EnableInput(true);
    }
    if (!audio_codec->InputData(input)) {
        return false;
    }

    block.sequence = next_sequence_++;
    block.timestamp_ms = static_cast<std::int64_t>(block.sequence) * 20;
    for (std::size_t index = 0; index < block.samples.size(); ++index) {
        block.samples[index] = input[index * input_channels];
    }
    return true;
}

bool StackChanAudioSource::isAcquired() const {
    return acquired_;
}

}  // namespace stackchan::meeting
