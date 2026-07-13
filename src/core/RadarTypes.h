#pragma once

#include <array>
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

inline constexpr std::array<TargetType, 5> kTargetTypes{TargetType::kCar, TargetType::kTruck, TargetType::kPedestrian,
                                                        TargetType::kBicycle, TargetType::kUnknown};

QString targetTypeDisplayName(TargetType type);
QString targetTypeColorName(TargetType type);

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

struct TargetStatistics {
    int car_count = 0;
    int truck_count = 0;
    int pedestrian_count = 0;
    int bicycle_count = 0;
    int unknown_count = 0;

    int count(TargetType type) const;
    int totalCount() const;
    int vehicleGroupCount() const;
};

TargetStatistics calculateTargetStatistics(const QVector<TrackData> &tracks);

struct RadarFrame {
    QDateTime received_at;
    std::optional<double> sender_timestamp_seconds;
    std::optional<qint64> sequence;
    std::optional<GeoPosition> ego_position;
    QVector<TrackData> tracks;
    TargetStatistics statistics;
};

} // namespace utms

Q_DECLARE_METATYPE(utms::RadarFrame)
Q_DECLARE_METATYPE(utms::TargetType)
