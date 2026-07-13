#include "core/RadarTypes.h"

namespace utms {

QString targetTypeDisplayName(TargetType type)
{
    switch (type) {
    case TargetType::kCar:
        return QStringLiteral("汽车");
    case TargetType::kTruck:
        return QStringLiteral("卡车");
    case TargetType::kPedestrian:
        return QStringLiteral("行人");
    case TargetType::kBicycle:
        return QStringLiteral("自行车");
    case TargetType::kUnknown:
        return QStringLiteral("未知");
    }

    return QStringLiteral("未知");
}

QString targetTypeColorName(TargetType type)
{
    switch (type) {
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

int TargetStatistics::count(TargetType type) const
{
    switch (type) {
    case TargetType::kCar:
        return car_count;
    case TargetType::kTruck:
        return truck_count;
    case TargetType::kPedestrian:
        return pedestrian_count;
    case TargetType::kBicycle:
        return bicycle_count;
    case TargetType::kUnknown:
        return unknown_count;
    }

    return unknown_count;
}

int TargetStatistics::totalCount() const
{
    return car_count + truck_count + pedestrian_count + bicycle_count + unknown_count;
}

int TargetStatistics::vehicleGroupCount() const
{
    return car_count + truck_count + bicycle_count;
}

TargetStatistics calculateTargetStatistics(const QVector<TrackData> &tracks)
{
    TargetStatistics statistics;
    for (const TrackData &track : tracks) {
        switch (track.type) {
        case TargetType::kCar:
            ++statistics.car_count;
            break;
        case TargetType::kTruck:
            ++statistics.truck_count;
            break;
        case TargetType::kPedestrian:
            ++statistics.pedestrian_count;
            break;
        case TargetType::kBicycle:
            ++statistics.bicycle_count;
            break;
        case TargetType::kUnknown:
            ++statistics.unknown_count;
            break;
        }
    }
    return statistics;
}

} // namespace utms
