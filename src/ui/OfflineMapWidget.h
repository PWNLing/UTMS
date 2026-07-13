#pragma once

#include <optional>

#include <QGraphicsView>
#include <QHash>
#include <QSet>

#include "map/OnlineMapState.h"

class QGraphicsEllipseItem;
class QGraphicsPixmapItem;
class QGraphicsSimpleTextItem;
class QLabel;
class QMouseEvent;
class QResizeEvent;
class QWheelEvent;

namespace utms
{

class OfflineMapWidget : public QGraphicsView
{
    Q_OBJECT

    public:
    explicit OfflineMapWidget(QWidget *parent = nullptr);

    void renderState(const OnlineMapState &state);
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
    void updateTiles();
    void updateMissingLabel(bool all_missing);
    QString tilePath(int tile_x, int tile_y) const;
    static QString targetTooltip(const OnlineMapTarget &target);

    QGraphicsScene *map_scene_ = nullptr;
    QLabel *missing_label_ = nullptr;
    QString tile_root_path_;
    OnlineMapState render_state_;
    GeoPosition center_{25.311724, 110.416819};
    int zoom_ = 17;
    QHash<QString, QGraphicsPixmapItem *> tile_items_;
    QHash<qint64, QGraphicsEllipseItem *> target_items_;
    QGraphicsEllipseItem *radar_item_ = nullptr;
    QGraphicsSimpleTextItem *selection_label_ = nullptr;
    QSet<QString> logged_missing_tiles_;
};

} // namespace utms
