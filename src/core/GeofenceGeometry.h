#pragma once

#include <QString>

#include "core/GeofenceTypes.h"

namespace utms {

GeofenceShape geofenceShape(const Geofence &geofence);
GeoPosition geofenceCenter(const Geofence &geofence);
QString validateGeofence(const Geofence &geofence);

} // namespace utms
