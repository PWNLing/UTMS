#pragma once

#include <optional>

#include <QGraphicsView>
#include <QHash>
#include <QSet>

#include "alert/AlertTypes.h"
#include "core/GeofenceTypes.h"
#include "map/OnlineMapState.h"
#include "map/RealtimeTrajectoryModel.h"

class QGraphicsEllipseItem;
class QGraphicsPixmapItem;
class QGraphicsPathItem;
class QGraphicsSimpleTextItem;
class QImage;
class QLabel;
class QMouseEvent;
class QResizeEvent;
class QTimer;
class QWheelEvent;

namespace utms
{

class OfflineMapWidget : public QGraphicsView
{
    Q_OBJECT

  public:
    explicit OfflineMapWidget(QWidget *parent = nullptr);

    void renderState(const OnlineMapState &state);
    void setTrajectories(const QVector<RealtimeTrajectory> &trajectories);
    void setGeofences(const QVector<Geofence> &geofences);
    void setEditableGeofenceId(std::optional<qint64> geofence_id);
    void setView(const GeoPosition &center, int zoom);
    void setSelectedTrackId(std::optional<qint64> track_id);
    void setAlertTrackIds(const QSet<qint64> &track_ids);
    void setAlertMarkers(const QVector<TargetAlert> &alerts);

  signals:
    void targetClicked(qint64 track_id);
    void geofenceEdited(const Geofence &geofence);
    void geofenceEditError(const QString &message);
    void viewChanged(const GeoPosition &center, int zoom);

  protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

  private:
    void updateMarkers();
    void updateTrajectoryItems();
    void updateGeofenceItems();
    void updateAlertMarkerItems();
    void updateTiles();
    void requestTile(const QString &key, const QString &path, const QPointF &position_px);
    void updateMissingLabel();
    void recordMissingTile(const QString &path);
    QString tilePath(int tile_x, int tile_y) const;
    static QString targetTooltip(const OnlineMapTarget &target);

    QGraphicsScene *map_scene_ = nullptr;
    QLabel *missing_label_ = nullptr;
    QTimer *tile_update_timer_ = nullptr;
    QTimer *missing_log_timer_ = nullptr;
    QString tile_root_path_;
    OnlineMapState render_state_;
    QVector<RealtimeTrajectory> trajectories_;
    QVector<Geofence> geofences_;
    GeoPosition center_{25.311724, 110.416819};
    int zoom_ = 17;
    QHash<QString, QGraphicsPixmapItem *> tile_items_;
    QHash<qint64, QGraphicsEllipseItem *> target_items_;
    QVector<QGraphicsPathItem *> trajectory_items_;
    QVector<QGraphicsPathItem *> geofence_items_;
    QVector<QGraphicsEllipseItem *> geofence_handle_items_;
    QHash<qint64, QGraphicsEllipseItem *> alert_marker_items_;
    QGraphicsEllipseItem *radar_item_ = nullptr;
    QGraphicsSimpleTextItem *selection_label_ = nullptr;
    QSet<QString> required_tile_keys_;
    QSet<QString> visible_tile_keys_;
    QSet<QString> loading_tile_keys_;
    QSet<QString> missing_tile_keys_;
    QSet<QString> logged_missing_tiles_;
    QString first_pending_missing_tile_path_;
    int pending_missing_tile_count_ = 0;
    bool applying_view_ = false;
    std::optional<qint64> editable_geofence_id_;
    QSet<qint64> alert_track_ids_;
    QVector<TargetAlert> alert_markers_;
    QGraphicsItem *active_geofence_edit_item_ = nullptr;
};

} // namespace utms
