#include "map/OnlineMapState.h"

#include <algorithm>
#include <utility>

namespace utms
{
namespace
{

QString targetColor(TargetType type)
{
    switch (type)
    {
    case TargetType::kCar:
        return QStringLiteral("#3498db");
    case TargetType::kTruck:
        return QStringLiteral("#e67e22");
    case TargetType::kPedestrian:
        return QStringLiteral("#2ecc71");
    case TargetType::kBicycle:
        return QStringLiteral("#9b59b6");
    case TargetType::kUnknown:
        return QStringLiteral("#95a5a6");
    }
    return QStringLiteral("#95a5a6");
}

} // namespace

OnlineMapUpdate OnlineMapState::replaceFrame(const RadarFrame &frame)
{
    OnlineMapUpdate update;
    QHash<qint64, TrackData> next_targets;
    next_targets.reserve(frame.tracks.size());

    for (const TrackData &track : frame.tracks)
    {
        next_targets.insert(track.track_id, track);
        const auto previous = targets_by_id_.constFind(track.track_id);
        if (previous == targets_by_id_.cend() || !markerDataMatches(previous.value(), track))
        {
            const bool content_changed = previous == targets_by_id_.cend() || previous->type != track.type;
            update.upserted_targets.append(makeMapTarget(track, content_changed));
        }
    }

    for (auto iterator = targets_by_id_.cbegin(); iterator != targets_by_id_.cend(); ++iterator)
    {
        if (!next_targets.contains(iterator.key()))
        {
            update.removed_track_ids.append(iterator.key());
        }
    }
    std::sort(update.removed_track_ids.begin(), update.removed_track_ids.end());

    if (frame.ego_position.has_value())
    {
        const GeoPosition next_radar_position = frame.ego_position.value();
        if (!radar_position_.has_value() || radar_position_->latitude != next_radar_position.latitude ||
            radar_position_->longitude != next_radar_position.longitude)
        {
            update.radar_position = next_radar_position;
        }
        radar_position_ = next_radar_position;
        if (!automatically_centered_)
        {
            center_ = next_radar_position;
            update.automatic_center = next_radar_position;
            automatically_centered_ = true;
        }
    }

    current_frame_ = frame;
    targets_by_id_ = std::move(next_targets);
    if (selected_track_id_.has_value() && !targets_by_id_.contains(selected_track_id_.value()))
    {
        selected_track_id_.reset();
    }
    return update;
}

const RadarFrame &OnlineMapState::currentFrame() const
{
    return current_frame_;
}

QVector<OnlineMapTarget> OnlineMapState::currentTargets() const
{
    QVector<OnlineMapTarget> targets;
    targets.reserve(current_frame_.tracks.size());
    for (const TrackData &track : current_frame_.tracks)
    {
        targets.append(makeMapTarget(track, true));
    }
    return targets;
}

GeoPosition OnlineMapState::center() const
{
    return center_;
}

int OnlineMapState::zoom() const
{
    return zoom_;
}

OnlineMapLayer OnlineMapState::layer() const
{
    return layer_;
}

std::optional<GeoPosition> OnlineMapState::radarPosition() const
{
    return radar_position_;
}

std::optional<qint64> OnlineMapState::selectedTrackId() const
{
    return selected_track_id_;
}

void OnlineMapState::setCenter(const GeoPosition &center)
{
    center_ = center;
}

void OnlineMapState::setZoom(int zoom)
{
    zoom_ = std::clamp(zoom, 15, 19);
}

void OnlineMapState::setLayer(OnlineMapLayer layer)
{
    layer_ = layer;
}

bool OnlineMapState::setSelectedTrackId(std::optional<qint64> track_id)
{
    if (track_id.has_value() && !targets_by_id_.contains(track_id.value()))
    {
        return false;
    }
    selected_track_id_ = track_id;
    return true;
}

bool OnlineMapState::locateRadar()
{
    if (!radar_position_.has_value())
    {
        return false;
    }
    center_ = radar_position_.value();
    return true;
}

OnlineMapTarget OnlineMapState::makeMapTarget(const TrackData &track, bool content_changed)
{
    return {track.track_id,   track.type,          track.position,          track.velocity_mps,
            track.distance_m, track.first_seen_at, targetColor(track.type), content_changed};
}

bool OnlineMapState::markerDataMatches(const TrackData &left, const TrackData &right)
{
    return left.type == right.type && left.position.latitude == right.position.latitude &&
           left.position.longitude == right.position.longitude && left.velocity_mps == right.velocity_mps &&
           left.distance_m == right.distance_m && left.first_seen_at == right.first_seen_at;
}

} // namespace utms
