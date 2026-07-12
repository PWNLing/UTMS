#pragma once

#include <optional>

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVector>

namespace utms {

enum class TargetType {
    kCar,
    kTruck,
    kPedestrian,
    kBicycle,
    kUnknown
};

QString targetTypeDisplayName(TargetType type);

struct GeoPosition {
    double latitude = 0.0;
    double longitude = 0.0;
};

struct TrackData {
    qint64 track_id = 0;
    TargetType type = TargetType::kUnknown;
    GeoPosition position;
    std::optional<double> velocity_mps;
    std::optional<double> distance_m;
    QDateTime first_seen_at;
};

struct RadarFrame {
    QDateTime received_at;
    std::optional<double> sender_timestamp_seconds;
    std::optional<qint64> sequence;
    std::optional<GeoPosition> ego_position;
    QVector<TrackData> tracks;
};

}  // namespace utms

Q_DECLARE_METATYPE(utms::RadarFrame)
