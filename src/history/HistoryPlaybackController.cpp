#include "history/HistoryPlaybackController.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

#include <QTimer>

namespace utms {
namespace {

constexpr qint64 kLongDataGapMs = 5'000;
constexpr std::array<double, 4> kPlaybackRates{0.5, 1.0, 2.0, 4.0};

} // namespace

HistoryPlaybackController::HistoryPlaybackController(QObject *parent)
    : QObject(parent), playback_timer_(new QTimer(this)) {
    playback_timer_->setSingleShot(true);
    playback_timer_->setTimerType(Qt::PreciseTimer);
    connect(playback_timer_, &QTimer::timeout, this, &HistoryPlaybackController::advancePlayback);
}

bool HistoryPlaybackController::beginReplay(const HistoryQueryResult &result) {
    if (result.frames.isEmpty()) {
        return false;
    }

    pause();
    result_ = result;
    std::stable_sort(result_.frames.begin(), result_.frames.end(),
                     [](const HistoryFrameRecord &left, const HistoryFrameRecord &right) {
                         return left.frame_time < right.frame_time;
                     });
    current_frame_index_ = 0;
    selected_track_id_.reset();
    replay_mode_ = true;
    emit replayModeChanged(true);
    moveToFrame(current_frame_index_);
    return true;
}

bool HistoryPlaybackController::isReplayMode() const { return replay_mode_; }

bool HistoryPlaybackController::isPlaying() const { return playing_; }

int HistoryPlaybackController::currentFrameIndex() const { return current_frame_index_; }

std::optional<RadarFrame> HistoryPlaybackController::currentFrame() const {
    if (!replay_mode_ || current_frame_index_ < 0 || current_frame_index_ >= result_.frames.size()) {
        return std::nullopt;
    }
    return toRadarFrame(result_.frames.at(current_frame_index_));
}

std::optional<HistoryReplayTrajectory> HistoryPlaybackController::selectedTrajectory() const {
    if (!replay_mode_ || !selected_track_id_.has_value() || current_frame_index_ < 0) {
        return std::nullopt;
    }

    HistoryReplayTrajectory trajectory;
    trajectory.track_id = selected_track_id_.value();
    QVector<GeoPosition> current_segment;
    int previous_track_frame_index = -1;
    for (int frame_index = 0; frame_index <= current_frame_index_; ++frame_index) {
        const QVector<TrackData> &tracks = result_.frames.at(frame_index).tracks;
        const auto track = std::find_if(tracks.cbegin(), tracks.cend(), [this](const TrackData &candidate) {
            return candidate.track_id == selected_track_id_.value();
        });
        if (track == tracks.cend()) {
            if (!current_segment.isEmpty()) {
                trajectory.segments.append(current_segment);
                current_segment.clear();
            }
            previous_track_frame_index = -1;
            continue;
        }

        const qint64 track_gap_ms =
            previous_track_frame_index >= 0
                ? result_.frames.at(previous_track_frame_index)
                      .frame_time.msecsTo(result_.frames.at(frame_index).frame_time)
                : 0;
        if (track_gap_ms > kLongDataGapMs) {
            trajectory.segments.append(current_segment);
            current_segment.clear();
        }
        trajectory.type = track->type;
        current_segment.append(track->position);
        previous_track_frame_index = frame_index;
    }
    if (!current_segment.isEmpty()) {
        trajectory.segments.append(current_segment);
    }
    if (trajectory.segments.isEmpty()) {
        return std::nullopt;
    }
    return trajectory;
}

double HistoryPlaybackController::playbackRate() const { return playback_rate_; }

bool HistoryPlaybackController::setPlaybackRate(double playback_rate) {
    const bool supported = std::any_of(kPlaybackRates.cbegin(), kPlaybackRates.cend(), [playback_rate](double rate) {
        return std::abs(rate - playback_rate) < 0.001;
    });
    if (!supported) {
        return false;
    }

    playback_rate_ = playback_rate;
    if (playing_) {
        scheduleNextFrame();
    }
    return true;
}

void HistoryPlaybackController::play() {
    if (!replay_mode_ || playing_ || current_frame_index_ >= result_.frames.size() - 1) {
        return;
    }

    playing_ = true;
    emit playbackStateChanged(true);
    scheduleNextFrame();
}

void HistoryPlaybackController::pause() {
    playback_timer_->stop();
    if (!playing_) {
        return;
    }

    playing_ = false;
    emit playbackStateChanged(false);
}

void HistoryPlaybackController::returnToLive() {
    if (!replay_mode_) {
        return;
    }

    pause();
    replay_mode_ = false;
    current_frame_index_ = -1;
    selected_track_id_.reset();
    result_ = {};
    emit selectedTrajectoryCleared();
    emit replayModeChanged(false);
}

void HistoryPlaybackController::previousFrame() {
    if (!replay_mode_) {
        return;
    }
    pause();
    moveToFrame(current_frame_index_ - 1);
}

void HistoryPlaybackController::nextFrame() {
    if (!replay_mode_) {
        return;
    }
    pause();
    emitDataGapsBetween(current_frame_index_, current_frame_index_ + 1);
    moveToFrame(current_frame_index_ + 1);
}

void HistoryPlaybackController::seekTo(const QDateTime &selected_time) {
    if (!replay_mode_ || !selected_time.isValid()) {
        return;
    }

    pause();
    int nearest_index = 0;
    qint64 nearest_distance_ms = std::abs(result_.frames.constFirst().frame_time.msecsTo(selected_time));
    for (int index = 1; index < result_.frames.size(); ++index) {
        const qint64 distance_ms = std::abs(result_.frames.at(index).frame_time.msecsTo(selected_time));
        if (distance_ms < nearest_distance_ms) {
            nearest_distance_ms = distance_ms;
            nearest_index = index;
        }
    }
    emitDataGapsBetween(current_frame_index_, nearest_index);
    moveToFrame(nearest_index);
}

void HistoryPlaybackController::setSelectedTrackId(qint64 track_id) {
    if (!replay_mode_ || track_id <= 0) {
        return;
    }
    selected_track_id_ = track_id;
    emitSelectedTrajectory();
}

void HistoryPlaybackController::clearSelectedTrackId() {
    if (!selected_track_id_.has_value()) {
        return;
    }
    selected_track_id_.reset();
    emit selectedTrajectoryCleared();
}

void HistoryPlaybackController::scheduleNextFrame() {
    playback_timer_->stop();
    if (!playing_ || current_frame_index_ < 0 || current_frame_index_ >= result_.frames.size() - 1) {
        pause();
        return;
    }

    const QDateTime current_time = result_.frames.at(current_frame_index_).frame_time;
    const QDateTime next_time = result_.frames.at(current_frame_index_ + 1).frame_time;
    const qint64 gap_ms = qMax<qint64>(0, current_time.msecsTo(next_time));
    if (gap_ms > kLongDataGapMs) {
        emit dataGapSkipped(gap_ms);
        moveToFrame(current_frame_index_ + 1);
        if (current_frame_index_ >= result_.frames.size() - 1) {
            pause();
        } else {
            // 让事件循环先处理跳帧产生的界面更新，再按下一段的真实时间间隔继续播放。
            QTimer::singleShot(0, this, [this]() {
                scheduleNextFrame();
            });
        }
        return;
    }

    playback_timer_->start(static_cast<int>(std::llround(static_cast<double>(gap_ms) / playback_rate_)));
}

void HistoryPlaybackController::advancePlayback() {
    if (!playing_) {
        return;
    }

    moveToFrame(current_frame_index_ + 1);
    if (current_frame_index_ >= result_.frames.size() - 1) {
        pause();
        return;
    }
    scheduleNextFrame();
}

void HistoryPlaybackController::moveToFrame(int frame_index) {
    if (!replay_mode_ || frame_index < 0 || frame_index >= result_.frames.size()) {
        return;
    }

    current_frame_index_ = frame_index;
    const HistoryFrameRecord &record = result_.frames.at(frame_index);
    emit frameChanged(toRadarFrame(record), frame_index, result_.frames.size(), record.frame_time);
    emitSelectedTrajectory();
}

void HistoryPlaybackController::emitDataGapsBetween(int first_frame_index, int second_frame_index) {
    if (first_frame_index < 0 || second_frame_index < 0 || first_frame_index >= result_.frames.size() ||
        second_frame_index >= result_.frames.size() || first_frame_index == second_frame_index) {
        return;
    }

    const int range_start = qMin(first_frame_index, second_frame_index);
    const int range_end = qMax(first_frame_index, second_frame_index);
    for (int frame_index = range_start + 1; frame_index <= range_end; ++frame_index) {
        const qint64 gap_ms =
            result_.frames.at(frame_index - 1).frame_time.msecsTo(result_.frames.at(frame_index).frame_time);
        if (gap_ms > kLongDataGapMs) {
            emit dataGapSkipped(gap_ms);
        }
    }
}

void HistoryPlaybackController::emitSelectedTrajectory() {
    const std::optional<HistoryReplayTrajectory> trajectory = selectedTrajectory();
    if (trajectory.has_value()) {
        emit selectedTrajectoryChanged(trajectory.value());
    } else {
        emit selectedTrajectoryCleared();
    }
}

RadarFrame HistoryPlaybackController::toRadarFrame(const HistoryFrameRecord &record) {
    RadarFrame frame;
    frame.received_at = record.received_at;
    frame.sender_timestamp_seconds = record.sender_timestamp_seconds;
    frame.sequence = record.sequence;
    frame.ego_position = record.ego_position;
    frame.tracks = record.tracks;
    frame.statistics = calculateTargetStatistics(frame.tracks);
    return frame;
}

} // namespace utms
