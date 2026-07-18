#pragma once

#include <QHash>
#include <QSet>
#include <QWidget>

#include "core/GeofenceTypes.h"
#include "history/HistoryTypes.h"
#include "map/OnlineMapState.h"
#include "map/RealtimeTrajectoryModel.h"

class QStackedWidget;
class QTimer;

namespace utms
{

class OnlineMapWidget;
class OfflineMapWidget;
struct RadarFrame;

enum class MapMode
{
    kOnline,
    kOffline
};

class MapPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit MapPanel(QWidget *parent = nullptr);

    void setFrame(const RadarFrame &frame);
    void setReplayMode(bool replay_mode);
    void setReplayFrame(const RadarFrame &frame);
    void setReplayTrajectory(const HistoryReplayTrajectory &trajectory);
    void clearReplayTrajectory();
    void setMapMode(MapMode mode);
    void setOnlineLayer(OnlineMapLayer layer);
    void setCenter(const GeoPosition &center);
    void setTrajectoryDuration(RealtimeTrajectoryDuration duration);
    void setShowAllTrajectories(bool show_all_trajectories);
    void setGeofences(const QVector<Geofence> &geofences);
    void setAlertMarkers(const QVector<TargetAlert> &alerts);
    void clearAlertMarkers();
    bool setEditableGeofenceId(std::optional<qint64> geofence_id);
    void discardPendingGeofenceEdits();
    bool setSelectedTrackId(std::optional<qint64> track_id);
    void clearSelectionForMissingTarget();
    bool selectTarget(qint64 track_id, bool center_on_target);
    bool locateRadar();
    bool locateGeofence(qint64 geofence_id);
    bool locateAlert(const TargetAlert &alert);
    bool flashAlertTarget(qint64 track_id, int duration_ms = 3'000);
    bool flashAlertTargets(const QVector<qint64> &track_ids, int duration_ms = 3'000);

    MapMode mapMode() const;
    GeoPosition center() const;
    int zoom() const;
    std::optional<qint64> selectedTrackId() const;
    QSet<qint64> alertTrackIds() const;
    const RadarFrame &displayedFrame() const;
    bool isReplayMode() const;
    std::optional<HistoryReplayTrajectory> replayTrajectory() const;
    QVector<RealtimeTrajectory> realtimeTrajectories(const QDateTime &now) const;
    const QVector<Geofence> &geofences() const;
    const QVector<TargetAlert> &alertMarkers() const;

  signals:
    void targetClicked(qint64 track_id);
    void geofenceEdited(const Geofence &geofence);
    void geofenceEditingChanged(std::optional<qint64> geofence_id);
    void geofenceEditError(const QString &message);

  private:
    void handleTargetClicked(qint64 track_id);
    void handleGeofenceEdited(const Geofence &geofence);
    void handleOnlineViewChanged(const GeoPosition &center, int zoom);
    void handleOfflineViewChanged(const GeoPosition &center, int zoom);
    void applySelectionToActiveMap(std::optional<qint64> track_id);
    void applyViewToActiveMap();
    void renderDisplayFrame(const RadarFrame &frame);
    void renderTrajectories(const QDateTime &now);
    void refreshAlertMarkers();
    QVector<RealtimeTrajectory> replayTrajectories() const;
    void synchronizeActiveMap();

    OnlineMapState state_;
    RealtimeTrajectoryModel trajectory_model_;
    QStackedWidget *map_stack_ = nullptr;
    QTimer *trajectory_refresh_timer_ = nullptr;
    QTimer *alert_highlight_timer_ = nullptr;
    OnlineMapWidget *online_map_ = nullptr;
    OfflineMapWidget *offline_map_ = nullptr;
    MapMode map_mode_ = MapMode::kOnline;
    std::optional<RadarFrame> latest_live_frame_;
    std::optional<HistoryReplayTrajectory> replay_trajectory_;
    QVector<Geofence> geofences_;
    QVector<Geofence> confirmed_geofences_;
    QVector<TargetAlert> alert_markers_;
    QVector<TargetAlert> replay_alert_markers_;
    std::optional<TargetAlert> located_alert_;
    QHash<qint64, Geofence> pending_geofence_edits_;
    std::optional<qint64> editable_geofence_id_;
    QSet<qint64> alert_track_ids_;
    bool replay_mode_ = false;
};

} // namespace utms
