#include "ui/OfflineMapWidget.h"

#include <algorithm>
#include <cmath>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGraphicsEllipseItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>

#include "map/WebMercator.h"

namespace utms
{
namespace
{

constexpr int kTrackIdDataRole = 1;

QString optionalMeasurement(const std::optional<double> &measurement)
{
    return measurement.has_value() ? QString::number(measurement.value(), 'f', 2) : QStringLiteral("--");
}

QPixmap missingTilePixmap()
{
    QPixmap pixmap(WebMercator::kTileSizePx, WebMercator::kTileSizePx);
    pixmap.fill(QColor(QStringLiteral("#eceff1")));
    QPainter painter(&pixmap);
    painter.setPen(QPen(QColor(QStringLiteral("#c5c9cc")), 2));
    painter.drawRect(pixmap.rect().adjusted(1, 1, -1, -1));
    painter.drawLine(0, 0, pixmap.width(), pixmap.height());
    painter.drawLine(pixmap.width(), 0, 0, pixmap.height());
    painter.setPen(QColor(QStringLiteral("#7f8c8d")));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, QObject::tr("暂无离线地图"));
    return pixmap;
}

} // namespace

OfflineMapWidget::OfflineMapWidget(QWidget *parent)
    : QGraphicsView(parent), map_scene_(new QGraphicsScene(this)), missing_label_(new QLabel(viewport())),
      tile_root_path_(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data/map/amap")))
{
    setScene(map_scene_);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setFrameShape(QFrame::NoFrame);
    setRenderHint(QPainter::Antialiasing, true);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    missing_label_->setText(tr("当前区域无离线地图数据"));
    missing_label_->setAlignment(Qt::AlignCenter);
    missing_label_->setStyleSheet(QStringLiteral("QLabel { background: rgba(245,245,245,220); color: "
                                                 "#7f8c8d; padding: 10px; }"));
    missing_label_->hide();
    setView(center_, zoom_);
}

void OfflineMapWidget::renderState(const OnlineMapState &state)
{
    render_state_ = state;
    center_ = state.center();
    zoom_ = state.zoom();
    setView(center_, zoom_);
}

void OfflineMapWidget::setView(const GeoPosition &center, int zoom)
{
    center_ = center;
    zoom_ = std::clamp(zoom, 15, 19);
    const qreal world_size_px = WebMercator::worldSizePx(zoom_);
    map_scene_->setSceneRect(0.0, 0.0, world_size_px, world_size_px);
    centerOn(WebMercator::geoToGlobalPixel(center_, zoom_));
    updateMarkers();
    updateTiles();
}

void OfflineMapWidget::setSelectedTrackId(std::optional<qint64> track_id)
{
    if (!render_state_.setSelectedTrackId(track_id))
    {
        qWarning() << "OfflineMapWidget: ignored selection for unknown track" << track_id.value_or(0);
        return;
    }
    updateMarkers();
}

void OfflineMapWidget::mousePressEvent(QMouseEvent *event)
{
    if (QGraphicsItem *clicked_item = itemAt(event->position().toPoint()); clicked_item != nullptr)
    {
        const QVariant track_id = clicked_item->data(kTrackIdDataRole);
        if (track_id.isValid())
        {
            emit targetClicked(track_id.toLongLong());
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void OfflineMapWidget::mouseReleaseEvent(QMouseEvent *event)
{
    QGraphicsView::mouseReleaseEvent(event);
    center_ = WebMercator::globalPixelToGeo(mapToScene(viewport()->rect().center()), zoom_);
    updateTiles();
    emit viewChanged(center_, zoom_);
}

void OfflineMapWidget::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    const QSize label_size_px(std::min(360, std::max(0, viewport()->width() - 40)), 48);
    missing_label_->resize(label_size_px);
    missing_label_->move((viewport()->width() - label_size_px.width()) / 2,
                         (viewport()->height() - label_size_px.height()) / 2);
    updateTiles();
}

void OfflineMapWidget::wheelEvent(QWheelEvent *event)
{
    const int step = event->angleDelta().y() > 0 ? 1 : -1;
    const int next_zoom = std::clamp(zoom_ + step, 15, 19);
    if (next_zoom == zoom_)
    {
        event->accept();
        return;
    }

    center_ = WebMercator::globalPixelToGeo(mapToScene(viewport()->rect().center()), zoom_);
    setView(center_, next_zoom);
    emit viewChanged(center_, zoom_);
    event->accept();
}

void OfflineMapWidget::updateMarkers()
{
    // 航迹图元由 scene 拥有显示关系、由本类负责删除；按 ID 更新可避免整帧闪烁。
    if (selection_label_ != nullptr)
    {
        map_scene_->removeItem(selection_label_);
        delete selection_label_;
        selection_label_ = nullptr;
    }

    const std::optional<qint64> selected_track_id = render_state_.selectedTrackId();
    QSet<qint64> current_track_ids;
    for (const OnlineMapTarget &target : render_state_.currentTargets())
    {
        current_track_ids.insert(target.track_id);
        const QPointF position_px = WebMercator::geoToGlobalPixel(target.position, zoom_);
        const bool selected = selected_track_id == target.track_id;
        const qreal diameter_px = selected ? 14.0 : 10.0;
        QGraphicsEllipseItem *marker = target_items_.value(target.track_id);
        if (marker == nullptr)
        {
            marker = map_scene_->addEllipse(0.0, 0.0, diameter_px, diameter_px);
            marker->setZValue(20.0);
            marker->setData(kTrackIdDataRole, target.track_id);
            target_items_.insert(target.track_id, marker);
        }
        marker->setRect(position_px.x() - diameter_px / 2.0, position_px.y() - diameter_px / 2.0, diameter_px,
                        diameter_px);
        marker->setPen(QPen(selected ? QColor(QStringLiteral("#f1c40f")) : Qt::white, selected ? 3.0 : 1.0));
        marker->setBrush(QColor(target.color));
        marker->setToolTip(targetTooltip(target));

        if (selected)
        {
            selection_label_ = map_scene_->addSimpleText(QString::number(target.track_id));
            selection_label_->setBrush(Qt::black);
            selection_label_->setPos(position_px + QPointF(9.0, -18.0));
            selection_label_->setZValue(21.0);
        }
    }

    for (auto iterator = target_items_.begin(); iterator != target_items_.end();)
    {
        if (!current_track_ids.contains(iterator.key()))
        {
            map_scene_->removeItem(iterator.value());
            delete iterator.value();
            iterator = target_items_.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }

    if (render_state_.radarPosition().has_value())
    {
        const QPointF position_px = WebMercator::geoToGlobalPixel(render_state_.radarPosition().value(), zoom_);
        if (radar_item_ == nullptr)
        {
            radar_item_ = map_scene_->addEllipse(0.0, 0.0, 10.0, 10.0, QPen(Qt::white), QBrush(Qt::black));
            radar_item_->setZValue(22.0);
        }
        radar_item_->setRect(position_px.x() - 5.0, position_px.y() - 5.0, 10.0, 10.0);
    }
    else if (radar_item_ != nullptr)
    {
        map_scene_->removeItem(radar_item_);
        delete radar_item_;
        radar_item_ = nullptr;
    }
}

void OfflineMapWidget::updateTiles()
{
    // 仅保留可视区域及外围一圈。缺失瓦片使用共享占位图，并将一次刷新内的缺失合并为一条日志。
    if (viewport()->width() <= 0 || viewport()->height() <= 0)
    {
        return;
    }

    const QRectF visible_rect = mapToScene(viewport()->rect()).boundingRect();
    const int maximum_tile = (1 << zoom_) - 1;
    const int visible_left =
        std::clamp(static_cast<int>(std::floor(visible_rect.left() / WebMercator::kTileSizePx)), 0, maximum_tile);
    const int visible_right =
        std::clamp(static_cast<int>(std::floor(visible_rect.right() / WebMercator::kTileSizePx)), 0, maximum_tile);
    const int visible_top =
        std::clamp(static_cast<int>(std::floor(visible_rect.top() / WebMercator::kTileSizePx)), 0, maximum_tile);
    const int visible_bottom =
        std::clamp(static_cast<int>(std::floor(visible_rect.bottom() / WebMercator::kTileSizePx)), 0, maximum_tile);
    const int left = std::max(0, visible_left - 1);
    const int right = std::min(maximum_tile, visible_right + 1);
    const int top = std::max(0, visible_top - 1);
    const int bottom = std::min(maximum_tile, visible_bottom + 1);

    QSet<QString> required_keys;
    int available_visible_tile_count = 0;
    int newly_missing_tile_count = 0;
    QString first_missing_tile_path;
    const QPixmap placeholder = missingTilePixmap();
    for (int tile_y = top; tile_y <= bottom; ++tile_y)
    {
        for (int tile_x = left; tile_x <= right; ++tile_x)
        {
            const bool is_visible =
                tile_x >= visible_left && tile_x <= visible_right && tile_y >= visible_top && tile_y <= visible_bottom;
            const QString key = QStringLiteral("%1/%2/%3").arg(zoom_).arg(tile_x).arg(tile_y);
            required_keys.insert(key);
            if (tile_items_.contains(key))
            {
                if (is_visible && QFileInfo::exists(tilePath(tile_x, tile_y)))
                {
                    ++available_visible_tile_count;
                }
                continue;
            }

            const QString path = tilePath(tile_x, tile_y);
            QPixmap pixmap(path);
            if (pixmap.isNull())
            {
                pixmap = placeholder;
                if (!logged_missing_tiles_.contains(path))
                {
                    logged_missing_tiles_.insert(path);
                    ++newly_missing_tile_count;
                    if (first_missing_tile_path.isEmpty())
                    {
                        first_missing_tile_path = path;
                    }
                }
            }
            else
            {
                if (is_visible)
                {
                    ++available_visible_tile_count;
                }
            }
            QGraphicsPixmapItem *tile_item = map_scene_->addPixmap(pixmap);
            tile_item->setPos(tile_x * WebMercator::kTileSizePx, tile_y * WebMercator::kTileSizePx);
            tile_item->setZValue(-100.0);
            tile_items_.insert(key, tile_item);
        }
    }

    for (auto iterator = tile_items_.begin(); iterator != tile_items_.end();)
    {
        if (!required_keys.contains(iterator.key()))
        {
            map_scene_->removeItem(iterator.value());
            delete iterator.value();
            iterator = tile_items_.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }
    if (newly_missing_tile_count > 0)
    {
        qWarning() << "OfflineMapWidget:" << newly_missing_tile_count
                   << "offline tiles missing; first path:" << first_missing_tile_path;
    }
    updateMissingLabel(available_visible_tile_count == 0);
}

void OfflineMapWidget::updateMissingLabel(bool all_missing)
{
    missing_label_->setVisible(all_missing);
    if (all_missing)
    {
        missing_label_->raise();
    }
}

QString OfflineMapWidget::tilePath(int tile_x, int tile_y) const
{
    return QDir(tile_root_path_).filePath(QStringLiteral("%1/%2/%3.png").arg(zoom_).arg(tile_x).arg(tile_y));
}

QString OfflineMapWidget::targetTooltip(const OnlineMapTarget &target)
{
    return tr("航迹 ID：%1\n类别：%2\n经度：%3\n纬度：%4\n速度：%5\n距离：%6\n首次出现：%7")
        .arg(target.track_id)
        .arg(targetTypeDisplayName(target.type))
        .arg(target.position.longitude, 0, 'f', 7)
        .arg(target.position.latitude, 0, 'f', 7)
        .arg(optionalMeasurement(target.velocity_mps))
        .arg(optionalMeasurement(target.distance_m))
        .arg(target.first_seen_at.toString(QStringLiteral("HH:mm:ss")));
}

} // namespace utms
