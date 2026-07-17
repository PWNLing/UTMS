#include "map/RealtimeTrajectoryModel.h"

#include <algorithm>
#include <cmath>

namespace utms
{
namespace
{

constexpr qint64 kSampleIntervalMs = 500;
constexpr qint64 kMaximumContinuousGapMs = 3'000;
constexpr qint64 kDisappearedRetentionMs = 5'000;
constexpr qint64 kMaximumStoredDurationMs = 60'000;
constexpr double kMaximumContinuousJumpMeters = 200.0;
constexpr double kEarthRadiusMeters = 6'371'000.0;
constexpr double kPi = 3.14159265358979323846;

double toRadians(double degrees)
{
    return degrees * kPi / 180.0;
}

double distanceMeters(const GeoPosition &left, const GeoPosition &right)
{
    const double left_latitude_rad = toRadians(left.latitude);
    const double right_latitude_rad = toRadians(right.latitude);
    const double latitude_delta_rad = right_latitude_rad - left_latitude_rad;
    const double longitude_delta_rad = toRadians(right.longitude - left.longitude);
    const double latitude_sine = std::sin(latitude_delta_rad / 2.0);
    const double longitude_sine = std::sin(longitude_delta_rad / 2.0);
    const double haversine =
        std::clamp(latitude_sine * latitude_sine +
                       std::cos(left_latitude_rad) * std::cos(right_latitude_rad) * longitude_sine * longitude_sine,
                   0.0, 1.0);
    return 2.0 * kEarthRadiusMeters * std::atan2(std::sqrt(haversine), std::sqrt(1.0 - haversine));
}

qint64 retentionMs(RealtimeTrajectoryDuration duration)
{
    switch (duration)
    {
    case RealtimeTrajectoryDuration::kOff:
        return 0;
    case RealtimeTrajectoryDuration::kTenSeconds:
        return 10'000;
    case RealtimeTrajectoryDuration::kThirtySeconds:
        return 30'000;
    case RealtimeTrajectoryDuration::kOneMinute:
        return 60'000;
    }
    return 0;
}

} // namespace

void RealtimeTrajectoryModel::replaceFrame(const RadarFrame &frame)
{
    // 仅消费已接受帧并维护补充显示状态；调用方须在同一线程顺序调用，本模型不修改 RadarFrame。
    pruneExpiredHistories(frame.received_at);
    QSet<qint64> current_track_ids;
    for (const TrackData &track : frame.tracks)
    {
        current_track_ids.insert(track.track_id);
        updateTrackHistory(track, frame.received_at);
    }
    markMissingTracks(current_track_ids, frame.received_at);
}

void RealtimeTrajectoryModel::pruneExpiredHistories(const QDateTime &now)
{
    if (!now.isValid())
    {
        return;
    }

    const QDateTime storage_cutoff = now.addMSecs(-kMaximumStoredDurationMs);
    for (auto history = histories_.begin(); history != histories_.end();)
    {
        if (!history->present && history->missing_since.has_value() &&
            history->missing_since->msecsTo(now) > kDisappearedRetentionMs)
        {
            history = histories_.erase(history);
            continue;
        }

        // 始终按最大可选的一分钟窗口裁剪，关闭或缩短显示后仍可在本次运行内恢复已有短航迹。
        for (QVector<TrajectorySample> &path : history->paths)
        {
            qsizetype expired_count = 0;
            while (expired_count < path.size() && path.at(expired_count).sampled_at < storage_cutoff)
            {
                ++expired_count;
            }
            if (expired_count > 0)
            {
                path.remove(0, expired_count);
            }
        }
        history->paths.erase(std::remove_if(history->paths.begin(), history->paths.end(),
                                            [](const QVector<TrajectorySample> &path) { return path.isEmpty(); }),
                             history->paths.end());
        ++history;
    }
}

void RealtimeTrajectoryModel::updateTrackHistory(const TrackData &track, const QDateTime &received_at)
{
    TrackHistory &history = histories_[track.track_id];
    history.type = track.type;
    if (history.paths.isEmpty())
    {
        history.paths.append(QVector<TrajectorySample>{});
    }

    QVector<TrajectorySample> &current_path = history.paths.last();
    const bool should_sample =
        current_path.isEmpty() || current_path.constLast().sampled_at.msecsTo(received_at) >= kSampleIntervalMs;
    if (should_sample)
    {
        const bool disappeared_too_long =
            history.missing_since.has_value() && history.missing_since->msecsTo(received_at) > kDisappearedRetentionMs;
        const bool continuity_broken =
            !current_path.isEmpty() &&
            (current_path.constLast().sampled_at.msecsTo(received_at) > kMaximumContinuousGapMs ||
             distanceMeters(current_path.constLast().position, track.position) > kMaximumContinuousJumpMeters ||
             disappeared_too_long);
        if (continuity_broken)
        {
            history.paths.append(QVector<TrajectorySample>{});
        }
        history.paths.last().append({track.position, received_at});
    }
    history.present = true;
    history.missing_since.reset();
}

void RealtimeTrajectoryModel::markMissingTracks(const QSet<qint64> &current_track_ids, const QDateTime &received_at)
{
    for (auto history = histories_.begin(); history != histories_.end(); ++history)
    {
        if (history->present && !current_track_ids.contains(history.key()))
        {
            history->present = false;
            history->missing_since = received_at;
        }
    }
}

void RealtimeTrajectoryModel::setDuration(RealtimeTrajectoryDuration duration)
{
    duration_ = duration;
}

void RealtimeTrajectoryModel::setShowAllTargets(bool show_all_targets)
{
    show_all_targets_ = show_all_targets;
}

void RealtimeTrajectoryModel::setSelectedTrackId(std::optional<qint64> track_id)
{
    selected_track_id_ = track_id;
    focused_track_id_ = track_id;
    focus_expires_at_.reset();
}

void RealtimeTrajectoryModel::clearSelectionRetainingFocusedTrajectory(const QDateTime &cleared_at)
{
    selected_track_id_.reset();
    if (focused_track_id_.has_value())
    {
        focus_expires_at_ = cleared_at.addMSecs(kDisappearedRetentionMs);
    }
}

QVector<RealtimeTrajectory> RealtimeTrajectoryModel::visibleTrajectories(const QDateTime &now) const
{
    // 输出仅包含渲染所需折线；不暴露采样点所有权，也不改变当前帧目标集合。
    QVector<RealtimeTrajectory> trajectories;
    const qint64 retention_ms = retentionMs(duration_);
    if (retention_ms == 0 || (!show_all_targets_ && !focused_track_id_.has_value()))
    {
        return trajectories;
    }

    const QDateTime cutoff = now.addMSecs(-retention_ms);
    for (auto history = histories_.cbegin(); history != histories_.cend(); ++history)
    {
        const bool focused = focused_track_id_ == history.key();
        const bool selected = selected_track_id_ == history.key();
        if (!show_all_targets_ &&
            (!focused || (focus_expires_at_.has_value() && now > focus_expires_at_.value())))
        {
            continue;
        }
        if (!history->present && history->missing_since.has_value() &&
            history->missing_since->msecsTo(now) > kDisappearedRetentionMs)
        {
            continue;
        }

        RealtimeTrajectory trajectory;
        trajectory.track_id = history.key();
        trajectory.type = history->type;
        trajectory.selected = selected;
        appendVisibleSegments(trajectory, history.value(), cutoff);
        if (trajectory.segments.isEmpty())
        {
            continue;
        }

        applySegmentOpacities(trajectory);
        trajectories.append(trajectory);
    }
    std::sort(trajectories.begin(), trajectories.end(),
              [](const RealtimeTrajectory &left, const RealtimeTrajectory &right)
              { return left.track_id < right.track_id; });
    return trajectories;
}

void RealtimeTrajectoryModel::appendVisibleSegments(RealtimeTrajectory &trajectory, const TrackHistory &history,
                                                    const QDateTime &cutoff)
{
    for (const QVector<TrajectorySample> &path : history.paths)
    {
        QVector<GeoPosition> visible_points;
        for (const TrajectorySample &sample : path)
        {
            if (sample.sampled_at >= cutoff)
            {
                visible_points.append(sample.position);
            }
        }

        qsizetype start_index = 0;
        while (start_index < visible_points.size() - 1)
        {
            const qsizetype end_index = std::min(start_index + 9, visible_points.size() - 1);
            RealtimeTrajectorySegment segment;
            for (qsizetype point_index = start_index; point_index <= end_index; ++point_index)
            {
                segment.points.append(visible_points.at(point_index));
            }
            trajectory.segments.append(segment);
            // 相邻显示段复用边界点，避免渐隐分段在地图上出现可见裂缝。
            start_index = end_index;
        }
    }
}

void RealtimeTrajectoryModel::applySegmentOpacities(RealtimeTrajectory &trajectory)
{
    for (qsizetype segment_index = 0; segment_index < trajectory.segments.size(); ++segment_index)
    {
        trajectory.segments[segment_index].opacity =
            0.25 + 0.75 * static_cast<qreal>(segment_index + 1) / trajectory.segments.size();
    }
}

} // namespace utms
