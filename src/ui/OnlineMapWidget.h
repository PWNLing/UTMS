#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QWidget>

#include "alert/AlertTypes.h"
#include "core/GeofenceTypes.h"
#include "map/OnlineMapState.h"
#include "map/RealtimeTrajectoryModel.h"

class QLabel;
class QStackedLayout;
class QWebEngineView;

namespace utms
{

class MapWebBridge;

class OnlineMapWidget : public QWidget
{
    Q_OBJECT

  public:
    explicit OnlineMapWidget(QWidget *parent = nullptr);

    void renderFrame(const OnlineMapState &state, const OnlineMapUpdate &update);
    void synchronizeState(const OnlineMapState &state);
    void setView(const GeoPosition &center, int zoom);
    void setLayer(OnlineMapLayer layer);
    void setSelectedTrackId(std::optional<qint64> track_id);
    void setAlertTrackIds(const QSet<qint64> &track_ids);
    void setAlertMarkers(const QVector<TargetAlert> &alerts);
    void setTrajectories(const QVector<RealtimeTrajectory> &trajectories);
    void setGeofences(const QVector<Geofence> &geofences);
    void setEditableGeofenceId(std::optional<qint64> geofence_id);
    void cancelPendingGeofenceEdit();

  signals:
    void targetClicked(qint64 track_id);
    void geofenceEdited(const Geofence &geofence);
    void geofenceEditError(const QString &message);
    void viewChanged(const GeoPosition &center, int zoom);
    void mapError(const QString &message);

  private slots:
    void handlePageReady();
    void handleMapError(const QString &message);
    void handleMapWarning(const QString &message);
    void handleGeofenceEdited(const QJsonObject &geofence_object);

  private:
    QJsonObject createInitialState() const;
    static QJsonObject createUpdateObject(const OnlineMapUpdate &update);
    static QJsonArray createTrajectoriesArray(const QVector<RealtimeTrajectory> &trajectories);
    static QJsonArray createGeofencesArray(const QVector<Geofence> &geofences);
    static QJsonArray createAlertMarkersArray(const QVector<TargetAlert> &alerts);
    void handleRenderProcessTermination(int status, int exit_code);
    void showError(const QString &message);

    OnlineMapState render_state_;
    QVector<RealtimeTrajectory> trajectories_;
    QVector<Geofence> geofences_;
    std::optional<qint64> editable_geofence_id_;
    QSet<qint64> alert_track_ids_;
    QVector<TargetAlert> alert_markers_;
    std::optional<qint64> recently_editable_geofence_id_;
    QStackedLayout *stacked_layout_ = nullptr;
    QWebEngineView *web_view_ = nullptr;
    QLabel *error_label_ = nullptr;
    MapWebBridge *bridge_ = nullptr;
    bool map_ready_ = false;
    int render_reload_attempts_ = 0;
};

} // namespace utms
