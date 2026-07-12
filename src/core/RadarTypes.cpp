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

}  // namespace utms
