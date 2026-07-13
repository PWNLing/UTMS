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

}  // namespace utms
