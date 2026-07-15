#pragma once

#include <QMetaType>

namespace utms {

enum class VideoRecordingState { kIdle, kStarting, kRecording, kStopping, kError };

enum class VideoRecordingAction { kNone, kBeginWriting, kWritePacket, kCancelPending, kFinalize, kKeyframeTimeout };

qint64 videoRecordingMonotonicTimeMs();

class VideoRecordingSession {
public:
    static constexpr qint64 kKeyframeTimeoutMs = 10'000;

    VideoRecordingState state() const;
    bool requestStart(qint64 now_ms);
    VideoRecordingAction handleVideoPacket(bool is_keyframe, qint64 now_ms);
    VideoRecordingAction checkKeyframeTimeout(qint64 now_ms);
    VideoRecordingAction requestStop();
    VideoRecordingAction requestInterruptionStop();
    void reportFinalized(bool succeeded);
    void reportFailure();
    qint64 durationSeconds(qint64 now_ms) const;

private:
    VideoRecordingState state_ = VideoRecordingState::kIdle;
    qint64 start_requested_at_ms_ = 0;
    qint64 recording_started_at_ms_ = 0;
};

} // namespace utms

Q_DECLARE_METATYPE(utms::VideoRecordingState)
