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
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>
#include <QtConcurrentRun>

#include "core/GeofenceGeometry.h"
#include "map/WebMercator.h"

namespace utms
{
namespace
{

constexpr int kTrackIdDataRole = 1;
constexpr int kGeofenceIdDataRole = 2;
constexpr int kGeofenceHandleDataRole = 3;
constexpr int kWholeGeofenceHandle = -1;
constexpr double kInitialResolutionMpp = 156'543.03392;

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

void OfflineMapWidget::setGeofences(const QVector<Geofence> &geofences)
{
    geofences_ = geofences;
    updateGeofenceItems();
}

void OfflineMapWidget::setEditableGeofenceId(std::optional<qint64> geofence_id)
{
    editable_geofence_id_ = geofence_id;
    updateGeofenceItems();
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
    updateGeofenceItems();
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
        const QVariant geofence_id = clicked_item->data(kGeofenceIdDataRole);
        if (geofence_id.isValid() && editable_geofence_id_ == geofence_id.toLongLong())
        {
            active_geofence_edit_item_ = clicked_item;
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void OfflineMapWidget::mouseReleaseEvent(QMouseEvent *event)
{
    QGraphicsView::mouseReleaseEvent(event);
    if (active_geofence_edit_item_ != nullptr)
    {
        QGraphicsItem *edited_item = active_geofence_edit_item_;
        active_geofence_edit_item_ = nullptr;
        const qint64 geofence_id = edited_item->data(kGeofenceIdDataRole).toLongLong();
        const int handle_index = edited_item->data(kGeofenceHandleDataRole).toInt();
        const auto current =
            std::find_if(geofences_.begin(), geofences_.end(),
                         [geofence_id](const Geofence &candidate) { return candidate.id == geofence_id; });
        if (current != geofences_.end())
        {
            Geofence edited = *current;
            if (handle_index == kWholeGeofenceHandle)
            {
                const QPointF offset_px = edited_item->pos();
                const auto translated = [this, offset_px](const GeoPosition &position)
                {
                    return WebMercator::globalPixelToGeo(WebMercator::geoToGlobalPixel(position, zoom_) + offset_px,
                                                         zoom_);
                };
                if (auto *circle = std::get_if<CircleGeofence>(&edited.geometry); circle != nullptr)
                {
                    circle->center = translated(circle->center);
                }
                else if (auto *rectangle = std::get_if<RectangleGeofence>(&edited.geometry); rectangle != nullptr)
                {
                    rectangle->southwest = translated(rectangle->southwest);
                    rectangle->northeast = translated(rectangle->northeast);
                }
                else
                {
                    auto &vertices = std::get<PolygonGeofence>(edited.geometry).vertices;
                    for (GeoPosition &vertex : vertices)
                    {
                        vertex = translated(vertex);
                    }
                }
            }
            else
            {
                const GeoPosition position = WebMercator::globalPixelToGeo(edited_item->scenePos(), zoom_);
                if (auto *circle = std::get_if<CircleGeofence>(&edited.geometry); circle != nullptr)
                {
                    if (handle_index == 0)
                    {
                        circle->center = position;
                    }
                    else
                    {
                        const QPointF center_px = WebMercator::geoToGlobalPixel(circle->center, zoom_);
                        const double radius_px = QLineF(center_px, edited_item->scenePos()).length();
                        const double latitude_radians = circle->center.latitude * 3.14159265358979323846 / 180.0;
                        const double meters_per_pixel =
                            kInitialResolutionMpp * std::cos(latitude_radians) /
                            static_cast<double>(quint64{1} << zoom_);
                        circle->radius_m = radius_px * meters_per_pixel;
                    }
                }
                else if (auto *rectangle = std::get_if<RectangleGeofence>(&edited.geometry); rectangle != nullptr)
                {
                    double south = rectangle->southwest.latitude;
                    double west = rectangle->southwest.longitude;
                    double north = rectangle->northeast.latitude;
                    double east = rectangle->northeast.longitude;
                    if (handle_index == 0 || handle_index == 1)
                    {
                        south = position.latitude;
                    }
                    else
                    {
                        north = position.latitude;
                    }
                    if (handle_index == 0 || handle_index == 3)
                    {
                        west = position.longitude;
                    }
                    else
                    {
                        east = position.longitude;
                    }
                    rectangle->southwest = {std::min(south, north), std::min(west, east)};
                    rectangle->northeast = {std::max(south, north), std::max(west, east)};
                }
                else
                {
                    auto &vertices = std::get<PolygonGeofence>(edited.geometry).vertices;
                    if (handle_index >= 0 && handle_index < vertices.size())
                    {
                        vertices[handle_index] = position;
                    }
                }
            }

            const QString validation_error = validateGeofence(edited);
            if (validation_error.isEmpty())
            {
                *current = edited;
                emit geofenceEdited(edited);
            }
            else
            {
                qWarning() << "OfflineMapWidget: rejected invalid geofence edit" << geofence_id << validation_error;
                emit geofenceEditError(validation_error);
            }
            updateGeofenceItems();
        }
    }
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

void OfflineMapWidget::updateGeofenceItems()
{
    active_geofence_edit_item_ = nullptr;
    geofence_handle_items_.clear();
    for (QGraphicsPathItem *item : std::as_const(geofence_items_))
    {
        map_scene_->removeItem(item);
        delete item;
    }
    geofence_items_.clear();

    for (const Geofence &geofence : std::as_const(geofences_))
    {
        if (!geofence.visible)
        {
            continue;
        }

        QPainterPath path;
        if (const auto *circle = std::get_if<CircleGeofence>(&geofence.geometry); circle != nullptr)
        {
            const QPointF center_px = WebMercator::geoToGlobalPixel(circle->center, zoom_);
            const double latitude_radians = circle->center.latitude * 3.14159265358979323846 / 180.0;
            const double meters_per_pixel =
                kInitialResolutionMpp * std::cos(latitude_radians) / static_cast<double>(quint64{1} << zoom_);
            const double radius_px = circle->radius_m / meters_per_pixel;
            path.addEllipse(center_px, radius_px, radius_px);
        }
        else
        {
            QVector<GeoPosition> vertices;
            if (const auto *rectangle = std::get_if<RectangleGeofence>(&geofence.geometry); rectangle != nullptr)
            {
                vertices = {{rectangle->southwest.latitude, rectangle->southwest.longitude},
                            {rectangle->southwest.latitude, rectangle->northeast.longitude},
                            {rectangle->northeast.latitude, rectangle->northeast.longitude},
                            {rectangle->northeast.latitude, rectangle->southwest.longitude}};
            }
            else
            {
                vertices = std::get<PolygonGeofence>(geofence.geometry).vertices;
            }
            if (!vertices.isEmpty())
            {
                path.moveTo(WebMercator::geoToGlobalPixel(vertices.constFirst(), zoom_));
                for (qsizetype vertex_index = 1; vertex_index < vertices.size(); ++vertex_index)
                {
                    path.lineTo(WebMercator::geoToGlobalPixel(vertices.at(vertex_index), zoom_));
                }
                path.closeSubpath();
            }
        }

        QColor stroke_color(geofence.enabled ? QStringLiteral("#1677ff") : QStringLiteral("#7f8c8d"));
        QColor fill_color = stroke_color;
        fill_color.setAlpha(32);
        QPen pen(stroke_color, 2.0, geofence.enabled ? Qt::SolidLine : Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
        pen.setCosmetic(true);
        QGraphicsPathItem *item = map_scene_->addPath(path, pen, QBrush(fill_color));
        item->setToolTip(geofence.name);
        item->setZValue(5.0);
        item->setData(kGeofenceIdDataRole, geofence.id);
        item->setData(kGeofenceHandleDataRole, kWholeGeofenceHandle);
        const bool editable = editable_geofence_id_ == geofence.id;
        item->setFlag(QGraphicsItem::ItemIsMovable, editable);
        geofence_items_.append(item);

        if (!editable)
        {
            continue;
        }

        QVector<QPointF> handle_positions;
        if (const auto *circle = std::get_if<CircleGeofence>(&geofence.geometry); circle != nullptr)
        {
            const QPointF center_px = WebMercator::geoToGlobalPixel(circle->center, zoom_);
            const double latitude_radians = circle->center.latitude * 3.14159265358979323846 / 180.0;
            const double meters_per_pixel =
                kInitialResolutionMpp * std::cos(latitude_radians) / static_cast<double>(quint64{1} << zoom_);
            handle_positions = {center_px, center_px + QPointF(circle->radius_m / meters_per_pixel, 0.0)};
        }
        else if (const auto *rectangle = std::get_if<RectangleGeofence>(&geofence.geometry); rectangle != nullptr)
        {
            handle_positions = {
                WebMercator::geoToGlobalPixel(rectangle->southwest, zoom_),
                WebMercator::geoToGlobalPixel(
                    {rectangle->southwest.latitude, rectangle->northeast.longitude}, zoom_),
                WebMercator::geoToGlobalPixel(rectangle->northeast, zoom_),
                WebMercator::geoToGlobalPixel(
                    {rectangle->northeast.latitude, rectangle->southwest.longitude}, zoom_)};
        }
        else
        {
            for (const GeoPosition &vertex : std::get<PolygonGeofence>(geofence.geometry).vertices)
            {
                handle_positions.append(WebMercator::geoToGlobalPixel(vertex, zoom_));
            }
        }

        for (qsizetype handle_index = 0; handle_index < handle_positions.size(); ++handle_index)
        {
            auto *handle = new QGraphicsEllipseItem(-5.0, -5.0, 10.0, 10.0, item);
            handle->setPos(handle_positions.at(handle_index));
            handle->setPen(QPen(Qt::white, 2.0));
            handle->setBrush(QColor(QStringLiteral("#1677ff")));
            handle->setZValue(1.0);
            handle->setFlag(QGraphicsItem::ItemIsMovable, true);
            handle->setData(kGeofenceIdDataRole, geofence.id);
            handle->setData(kGeofenceHandleDataRole, handle_index);
            geofence_handle_items_.append(handle);
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
