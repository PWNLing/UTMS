#pragma once

#include <optional>

#include <QHash>
#include <QString>
#include <QVector>

#include "core/RadarTypes.h"

namespace utms
{

enum class OnlineMapLayer
{
    kStreet,
    kSatellite
};

struct OnlineMapTarget
{
    qint64 track_id = 0;
    TargetType type = TargetType::kUnknown;
    GeoPosition position;
    std::optional<double> velocity_mps;
    std::optional<double> distance_m;
    QDateTime first_seen_at;
    QString color;
    bool content_changed = false;
};

struct OnlineMapUpdate
{
    QVector<OnlineMapTarget> upserted_targets;
    QVector<qint64> removed_track_ids;
    std::optional<GeoPosition> radar_position;
    std::optional<GeoPosition> automatic_center;
};

class OnlineMapState
{
  public:
    OnlineMapUpdate replaceFrame(const RadarFrame &frame);

    const RadarFrame &currentFrame() const;
    QVector<OnlineMapTarget> currentTargets() const;
    GeoPosition center() const;
    int zoom() const;
    OnlineMapLayer layer() const;
    std::optional<GeoPosition> radarPosition() const;

    void setCenter(const GeoPosition &center);
    void setZoom(int zoom);
    void setLayer(OnlineMapLayer layer);
    bool locateRadar();

  private:
    static OnlineMapTarget makeMapTarget(const TrackData &track, bool content_changed);
    static bool markerDataMatches(const TrackData &left, const TrackData &right);

    RadarFrame current_frame_;
    QHash<qint64, TrackData> targets_by_id_;
    GeoPosition center_{25.311724, 110.416819};
    std::optional<GeoPosition> radar_position_;
    int zoom_ = 17;
    OnlineMapLayer layer_ = OnlineMapLayer::kStreet;
    bool automatically_centered_ = false;
};

} // namespace utms
