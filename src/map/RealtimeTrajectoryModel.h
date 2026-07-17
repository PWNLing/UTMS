#pragma once

#include <optional>

#include <QHash>
#include <QSet>
#include <QVector>

#include "core/RadarTypes.h"

namespace utms
{

enum class RealtimeTrajectoryDuration
{
    kOff,
    kTenSeconds,
    kThirtySeconds,
    kOneMinute
};

struct RealtimeTrajectorySegment
{
    QVector<GeoPosition> points;
    qreal opacity = 1.0;
};

struct RealtimeTrajectory
{
    qint64 track_id = 0;
    TargetType type = TargetType::kUnknown;
    bool selected = false;
    QVector<RealtimeTrajectorySegment> segments;
};

class RealtimeTrajectoryModel
{
public:
    void replaceFrame(const RadarFrame &frame);
    void setDuration(RealtimeTrajectoryDuration duration);
    void setShowAllTargets(bool show_all_targets);
    void setSelectedTrackId(std::optional<qint64> track_id);
    void clearSelectionRetainingFocusedTrajectory(const QDateTime &cleared_at);

    QVector<RealtimeTrajectory> visibleTrajectories(const QDateTime &now) const;

private:
    struct TrajectorySample
    {
        GeoPosition position;
        QDateTime sampled_at;
    };

    struct TrackHistory
    {
        TargetType type = TargetType::kUnknown;
        QVector<QVector<TrajectorySample>> paths;
        std::optional<QDateTime> missing_since;
        bool present = false;
    };

    void pruneExpiredHistories(const QDateTime &now);
    void updateTrackHistory(const TrackData &track, const QDateTime &received_at);
    void markMissingTracks(const QSet<qint64> &current_track_ids, const QDateTime &received_at);
    static void appendVisibleSegments(RealtimeTrajectory &trajectory, const TrackHistory &history,
                                      const QDateTime &cutoff);
    static void applySegmentOpacities(RealtimeTrajectory &trajectory);

    QHash<qint64, TrackHistory> histories_;
    RealtimeTrajectoryDuration duration_ = RealtimeTrajectoryDuration::kThirtySeconds;
    bool show_all_targets_ = false;
    std::optional<qint64> focused_track_id_;
    std::optional<QDateTime> focus_expires_at_;
    std::optional<qint64> selected_track_id_;
};

} // namespace utms
