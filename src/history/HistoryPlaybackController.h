#pragma once

#include <optional>

#include <QObject>

#include "history/HistoryTypes.h"

class QTimer;

namespace utms {

class HistoryPlaybackController : public QObject {
    Q_OBJECT

public:
    explicit HistoryPlaybackController(QObject *parent = nullptr);

    bool beginReplay(const HistoryQueryResult &result);
    bool isReplayMode() const;
    bool isPlaying() const;
    int currentFrameIndex() const;
    std::optional<RadarFrame> currentFrame() const;
    std::optional<HistoryReplayTrajectory> selectedTrajectory() const;
    double playbackRate() const;
    bool setPlaybackRate(double playback_rate);

public slots:
    void play();
    void pause();
    void returnToLive();
    void previousFrame();
    void nextFrame();
    void seekTo(const QDateTime &selected_time);
    void setSelectedTrackId(qint64 track_id);
    void clearSelectedTrackId();

signals:
    void replayModeChanged(bool replay_mode);
    void playbackStateChanged(bool playing);
    void frameChanged(const utms::RadarFrame &frame, int frame_index, int frame_count, const QDateTime &frame_time);
    void dataGapSkipped(qint64 gap_ms);
    void selectedTrajectoryChanged(const utms::HistoryReplayTrajectory &trajectory);
    void selectedTrajectoryCleared();

private:
    void scheduleNextFrame();
    void advancePlayback();
    void moveToFrame(int frame_index);
    void emitDataGapsBetween(int first_frame_index, int second_frame_index);
    void emitSelectedTrajectory();
    static RadarFrame toRadarFrame(const HistoryFrameRecord &record);

    QTimer *playback_timer_ = nullptr;
    HistoryQueryResult result_;
    std::optional<qint64> selected_track_id_;
    int current_frame_index_ = -1;
    double playback_rate_ = 1.0;
    bool replay_mode_ = false;
    bool playing_ = false;
};

} // namespace utms
