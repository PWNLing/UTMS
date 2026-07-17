#pragma once

#include <optional>

#include <QGraphicsView>
#include <QHash>
#include <QSet>

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
    void setView(const GeoPosition &center, int zoom);
    void setSelectedTrackId(std::optional<qint64> track_id);

    signals:
    void targetClicked(qint64 track_id);
    void viewChanged(const GeoPosition &center, int zoom);

    protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

    private:
    void updateMarkers();
    void updateTrajectoryItems();
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
    GeoPosition center_{25.311724, 110.416819};
    int zoom_ = 17;
    QHash<QString, QGraphicsPixmapItem *> tile_items_;
    QHash<qint64, QGraphicsEllipseItem *> target_items_;
    QVector<QGraphicsPathItem *> trajectory_items_;
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
};

} // namespace utms
