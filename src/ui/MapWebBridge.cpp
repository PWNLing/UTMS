#include "ui/MapWebBridge.h"

#include <QJsonObject>

namespace utms
{

MapWebBridge::MapWebBridge(const QString &api_key, const QString &security_code, QObject *parent)
    : QObject(parent), api_key_(api_key), security_code_(security_code)
{
}

QString MapWebBridge::apiKey() const
{
    return api_key_;
}

QString MapWebBridge::securityCode() const
{
    return security_code_;
}

void MapWebBridge::reportPageReady()
{
    emit pageReadyReported();
}

void MapWebBridge::reportMapError(const QString &message)
{
    emit mapErrorReported(message);
}

void MapWebBridge::reportMapWarning(const QString &message)
{
    emit mapWarningReported(message);
}

void MapWebBridge::reportViewChanged(double longitude, double latitude, int zoom)
{
    emit viewChanged(longitude, latitude, zoom);
}

void MapWebBridge::reportTargetClicked(const QString &track_id)
{
    bool converted = false;
    const qint64 numeric_track_id = track_id.toLongLong(&converted);
    if (converted)
    {
        emit targetClicked(numeric_track_id);
    }
}

void MapWebBridge::reportGeofenceEdited(const QJsonObject &geofence)
{
    emit geofenceEdited(geofence);
}

} // namespace utms
