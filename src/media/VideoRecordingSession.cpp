#include "media/VideoRecordingSession.h"

#include <algorithm>
#include <chrono>

namespace utms {

qint64 videoRecordingMonotonicTimeMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

VideoRecordingState VideoRecordingSession::state() const { return state_; }

bool VideoRecordingSession::requestStart(qint64 now_ms)
{
    if (state_ != VideoRecordingState::kIdle && state_ != VideoRecordingState::kError) {
        return false;
    }

    state_ = VideoRecordingState::kStarting;
    start_requested_at_ms_ = now_ms;
    recording_started_at_ms_ = 0;
    return true;
}

VideoRecordingAction VideoRecordingSession::handleVideoPacket(bool is_keyframe, qint64 now_ms)
{
    if (state_ == VideoRecordingState::kStarting) {
        const VideoRecordingAction timeout_action = checkKeyframeTimeout(now_ms);
        if (timeout_action != VideoRecordingAction::kNone) {
            return timeout_action;
        }
        if (is_keyframe) {
            state_ = VideoRecordingState::kRecording;
            recording_started_at_ms_ = now_ms;
            return VideoRecordingAction::kBeginWriting;
        }
        return VideoRecordingAction::kNone;
    }

    return state_ == VideoRecordingState::kRecording ? VideoRecordingAction::kWritePacket : VideoRecordingAction::kNone;
}

VideoRecordingAction VideoRecordingSession::checkKeyframeTimeout(qint64 now_ms)
{
    if (state_ == VideoRecordingState::kStarting && now_ms - start_requested_at_ms_ >= kKeyframeTimeoutMs) {
        state_ = VideoRecordingState::kError;
        return VideoRecordingAction::kKeyframeTimeout;
    }
    return VideoRecordingAction::kNone;
}

VideoRecordingAction VideoRecordingSession::requestStop()
{
    if (state_ == VideoRecordingState::kStarting) {
        state_ = VideoRecordingState::kIdle;
        return VideoRecordingAction::kCancelPending;
    }
    if (state_ == VideoRecordingState::kRecording) {
        state_ = VideoRecordingState::kStopping;
        return VideoRecordingAction::kFinalize;
    }
    return VideoRecordingAction::kNone;
}

VideoRecordingAction VideoRecordingSession::requestInterruptionStop() { return requestStop(); }

void VideoRecordingSession::reportFinalized(bool succeeded)
{
    if (state_ == VideoRecordingState::kStopping) {
        state_ = succeeded ? VideoRecordingState::kIdle : VideoRecordingState::kError;
    }
}

void VideoRecordingSession::reportFailure() { state_ = VideoRecordingState::kError; }

qint64 VideoRecordingSession::durationSeconds(qint64 now_ms) const
{
    if (state_ != VideoRecordingState::kRecording && state_ != VideoRecordingState::kStopping) {
        return 0;
    }
    return std::max<qint64>(0, now_ms - recording_started_at_ms_) / 1'000;
}

} // namespace utms
