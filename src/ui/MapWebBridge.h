#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

namespace utms
{

class MapWebBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString apiKey READ apiKey CONSTANT)
    Q_PROPERTY(QString securityCode READ securityCode CONSTANT)

  public:
    explicit MapWebBridge(const QString &api_key, const QString &security_code, QObject *parent = nullptr);

    QString apiKey() const;
    QString securityCode() const;

  public slots:
    void reportPageReady();
    void reportMapError(const QString &message);
    void reportMapWarning(const QString &message);
    void reportViewChanged(double longitude, double latitude, int zoom);
    void reportTargetClicked(const QString &track_id);
    void reportGeofenceEdited(const QJsonObject &geofence);

  signals:
    void pageReadyReported();
    void mapErrorReported(const QString &message);
    void mapWarningReported(const QString &message);
    void viewChanged(double longitude, double latitude, int zoom);
    void targetClicked(qint64 track_id);
    void initialStateAvailable(const QJsonObject &state);
    void mapUpdateAvailable(const QJsonObject &update);
    void viewUpdated(double longitude, double latitude, int zoom);
    void layerUpdated(const QString &layer);
    void selectionUpdated(const QString &track_id);
    void alertHighlightUpdated(const QJsonArray &track_ids);
    void trajectoriesUpdated(const QJsonArray &trajectories);
    void geofencesUpdated(const QJsonArray &geofences);
    void geofenceEditingUpdated(const QString &geofence_id);
    void geofenceEdited(const QJsonObject &geofence);

  private:
    QString api_key_;
    QString security_code_;
};

} // namespace utms
