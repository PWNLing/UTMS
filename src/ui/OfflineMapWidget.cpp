#include "ui/OfflineMapWidget.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QCoreApplication>
#include <QDir>
#include <QFutureWatcher>
#include <QGraphicsEllipseItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>
#include <QtConcurrentRun>

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

const QPixmap &missingTilePixmap()
{
    static const QPixmap pixmap = []()
    {
        QPixmap placeholder(WebMercator::kTileSizePx, WebMercator::kTileSizePx);
        placeholder.fill(QColor(QStringLiteral("#eceff1")));
        QPainter painter(&placeholder);
        painter.setPen(QPen(QColor(QStringLiteral("#c5c9cc")), 2));
        painter.drawRect(placeholder.rect().adjusted(1, 1, -1, -1));
        painter.drawLine(0, 0, placeholder.width(), placeholder.height());
        painter.drawLine(placeholder.width(), 0, 0, placeholder.height());
        painter.setPen(QColor(QStringLiteral("#7f8c8d")));
        painter.drawText(placeholder.rect(), Qt::AlignCenter, QObject::tr("暂无离线地图"));
        return placeholder;
    }();
    return pixmap;
}

} // namespace

OfflineMapWidget::OfflineMapWidget(QWidget *parent)
    : QGraphicsView(parent), map_scene_(new QGraphicsScene(this)), missing_label_(new QLabel(viewport())),
      tile_update_timer_(new QTimer(this)), missing_log_timer_(new QTimer(this)),
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

    tile_update_timer_->setSingleShot(true);
    tile_update_timer_->setInterval(16);
    connect(tile_update_timer_, &QTimer::timeout, this, &OfflineMapWidget::updateTiles);
    const auto schedule_tile_update = [this]()
    {
        if (!applying_view_ && !tile_update_timer_->isActive())
        {
            tile_update_timer_->start();
        }
    };
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this,
            [schedule_tile_update](int) { schedule_tile_update(); });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
            [schedule_tile_update](int) { schedule_tile_update(); });

    missing_log_timer_->setSingleShot(true);
    missing_log_timer_->setInterval(100);
    connect(missing_log_timer_, &QTimer::timeout, this,
            [this]()
            {
                if (pending_missing_tile_count_ > 0)
                {
                    qWarning() << "OfflineMapWidget:" << pending_missing_tile_count_
                               << "offline tiles missing; first path:" << first_pending_missing_tile_path_;
                }
                pending_missing_tile_count_ = 0;
                first_pending_missing_tile_path_.clear();
            });
    setView(center_, zoom_);
}

void OfflineMapWidget::renderState(const OnlineMapState &state)
{
    render_state_ = state;
    updateMarkers();
}

void OfflineMapWidget::setTrajectories(const QVector<RealtimeTrajectory> &trajectories)
{
    trajectories_ = trajectories;
    updateTrajectoryItems();
}

void OfflineMapWidget::setView(const GeoPosition &center, int zoom)
{
    center_ = center;
    zoom_ = std::clamp(zoom, 15, 19);
    const qreal world_size_px = WebMercator::worldSizePx(zoom_);
    map_scene_->setSceneRect(0.0, 0.0, world_size_px, world_size_px);
    applying_view_ = true;
    centerOn(WebMercator::geoToGlobalPixel(center_, zoom_));
    applying_view_ = false;
    updateMarkers();
    updateTrajectoryItems();
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

void OfflineMapWidget::updateTrajectoryItems()
{
    for (QGraphicsPathItem *item : std::as_const(trajectory_items_))
    {
        map_scene_->removeItem(item);
        delete item;
    }
    trajectory_items_.clear();

    for (const RealtimeTrajectory &trajectory : std::as_const(trajectories_))
    {
        for (const RealtimeTrajectorySegment &segment : trajectory.segments)
        {
            if (segment.points.size() < 2)
            {
                continue;
            }

            QPainterPath path(WebMercator::geoToGlobalPixel(segment.points.constFirst(), zoom_));
            for (qsizetype point_index = 1; point_index < segment.points.size(); ++point_index)
            {
                path.lineTo(WebMercator::geoToGlobalPixel(segment.points.at(point_index), zoom_));
            }

            QColor color(targetTypeColorName(trajectory.type));
            color.setAlphaF(segment.opacity);
            QPen pen(color, trajectory.selected ? 3.0 : 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            pen.setCosmetic(true);
            QGraphicsPathItem *item = map_scene_->addPath(path, pen);
            item->setZValue(10.0);
            trajectory_items_.append(item);
        }
    }
}

void OfflineMapWidget::updateTiles()
{
    // 只在 GUI 线程计算视野与协调 scene；PNG 读取和解码由线程池完成，避免拖动时阻塞界面。
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

    QSet<QString> next_required_keys;
    QSet<QString> next_visible_keys;
    for (int tile_y = top; tile_y <= bottom; ++tile_y)
    {
        for (int tile_x = left; tile_x <= right; ++tile_x)
        {
            const bool is_visible =
                tile_x >= visible_left && tile_x <= visible_right && tile_y >= visible_top && tile_y <= visible_bottom;
            const QString key = QStringLiteral("%1/%2/%3").arg(zoom_).arg(tile_x).arg(tile_y);
            next_required_keys.insert(key);
            if (is_visible)
            {
                next_visible_keys.insert(key);
            }
        }
    }

    required_tile_keys_ = next_required_keys;
    visible_tile_keys_ = next_visible_keys;

    for (auto iterator = tile_items_.begin(); iterator != tile_items_.end();)
    {
        if (!required_tile_keys_.contains(iterator.key()))
        {
            missing_tile_keys_.remove(iterator.key());
            map_scene_->removeItem(iterator.value());
            delete iterator.value();
            iterator = tile_items_.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }

    for (int tile_y = top; tile_y <= bottom; ++tile_y)
    {
        for (int tile_x = left; tile_x <= right; ++tile_x)
        {
            const QString key = QStringLiteral("%1/%2/%3").arg(zoom_).arg(tile_x).arg(tile_y);
            if (!tile_items_.contains(key) && !loading_tile_keys_.contains(key))
            {
                requestTile(key, tilePath(tile_x, tile_y),
                            QPointF(tile_x * WebMercator::kTileSizePx, tile_y * WebMercator::kTileSizePx));
            }
        }
    }
    updateMissingLabel();
}

void OfflineMapWidget::requestTile(const QString &key, const QString &path, const QPointF &position_px)
{
    loading_tile_keys_.insert(key);
    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this,
            [this, watcher, key, path, position_px]()
            {
                const QImage image = watcher->result();
                watcher->deleteLater();
                loading_tile_keys_.remove(key);
                if (!required_tile_keys_.contains(key) || tile_items_.contains(key))
                {
                    updateMissingLabel();
                    return;
                }

                const bool is_missing = image.isNull();
                const QPixmap pixmap = is_missing ? missingTilePixmap() : QPixmap::fromImage(image);
                if (is_missing)
                {
                    missing_tile_keys_.insert(key);
                    recordMissingTile(path);
                }
                else
                {
                    missing_tile_keys_.remove(key);
                }

                QGraphicsPixmapItem *tile_item = map_scene_->addPixmap(pixmap);
                tile_item->setPos(position_px);
                tile_item->setZValue(-100.0);
                tile_items_.insert(key, tile_item);
                updateMissingLabel();
            });
    watcher->setFuture(QtConcurrent::run(
        [path]()
        {
            QImage image;
            image.load(path);
            return image;
        }));
}

void OfflineMapWidget::updateMissingLabel()
{
    bool has_pending_visible_tile = false;
    bool has_available_visible_tile = false;
    for (const QString &key : std::as_const(visible_tile_keys_))
    {
        has_pending_visible_tile = has_pending_visible_tile || loading_tile_keys_.contains(key);
        has_available_visible_tile =
            has_available_visible_tile || (tile_items_.contains(key) && !missing_tile_keys_.contains(key));
    }
    const bool all_missing = !visible_tile_keys_.isEmpty() && !has_pending_visible_tile && !has_available_visible_tile;
    missing_label_->setVisible(all_missing);
    if (all_missing)
    {
        missing_label_->raise();
    }
}

void OfflineMapWidget::recordMissingTile(const QString &path)
{
    if (logged_missing_tiles_.contains(path))
    {
        return;
    }
    logged_missing_tiles_.insert(path);
    ++pending_missing_tile_count_;
    if (first_pending_missing_tile_path_.isEmpty())
    {
        first_pending_missing_tile_path_ = path;
    }
    if (!missing_log_timer_->isActive())
    {
        missing_log_timer_->start();
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
