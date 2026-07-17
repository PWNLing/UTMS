#pragma once

#include <variant>

#include <QMetaType>
#include <QString>
#include <QVector>

#include "core/RadarTypes.h"

namespace utms {

enum class GeofenceShape { kCircle, kRectangle, kPolygon };

struct CircleGeofence {
    GeoPosition center;
    double radius_m = 0.0;
};

struct RectangleGeofence {
    GeoPosition southwest;
    GeoPosition northeast;
};

struct PolygonGeofence {
    QVector<GeoPosition> vertices;
};

using GeofenceGeometry = std::variant<CircleGeofence, RectangleGeofence, PolygonGeofence>;

struct Geofence {
    qint64 id = 0;
    QString name;
    bool enabled = true;
    bool visible = true;
    GeofenceGeometry geometry = CircleGeofence{};
};

QString geofenceShapeDisplayName(GeofenceShape shape);

} // namespace utms

Q_DECLARE_METATYPE(utms::GeofenceShape)
Q_DECLARE_METATYPE(utms::Geofence)
