#include "core/GeofenceTypes.h"

namespace utms {

QString geofenceShapeDisplayName(GeofenceShape shape) {
    switch (shape) {
    case GeofenceShape::kCircle:
        return QStringLiteral("圆形");
    case GeofenceShape::kRectangle:
        return QStringLiteral("矩形");
    case GeofenceShape::kPolygon:
        return QStringLiteral("多边形");
    }

    return QStringLiteral("未知");
}

} // namespace utms
