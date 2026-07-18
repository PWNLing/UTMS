#include "ui/OnlineMapWidget.h"

#include <utility>

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QStackedLayout>
#include <QTimer>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineView>

#include "core/GeofenceGeometry.h"
#include "map/AmapConfig.h"
#include "ui/MapWebBridge.h"

namespace utms
{
namespace
{

QJsonValue optionalMeasurement(const std::optional<double> &measurement)
{
    return measurement.has_value() ? QJsonValue(measurement.value()) : QJsonValue(QJsonValue::Null);
}

QJsonObject targetObject(const OnlineMapTarget &target)
{
    return {{QStringLiteral("trackId"), QString::number(target.track_id)},
            {QStringLiteral("longitude"), target.position.longitude},
            {QStringLiteral("latitude"), target.position.latitude},
            {QStringLiteral("type"), targetTypeDisplayName(target.type)},
            {QStringLiteral("color"), target.color},
            {QStringLiteral("velocity"), optionalMeasurement(target.velocity_mps)},
            {QStringLiteral("distance"), optionalMeasurement(target.distance_m)},
            {QStringLiteral("firstSeen"), target.first_seen_at.toString(QStringLiteral("HH:mm:ss"))},
            {QStringLiteral("contentChanged"), target.content_changed}};
}

QJsonObject positionObject(const GeoPosition &position)
{
    return {{QStringLiteral("longitude"), position.longitude}, {QStringLiteral("latitude"), position.latitude}};
}

std::optional<GeoPosition> positionFromObject(const QJsonValue &value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QJsonValue longitude = object.value(QStringLiteral("longitude"));
    const QJsonValue latitude = object.value(QStringLiteral("latitude"));
    if (!longitude.isDouble() || !latitude.isDouble())
    {
        return std::nullopt;
    }
    return GeoPosition{latitude.toDouble(), longitude.toDouble()};
}

QJsonObject trajectoryObject(const RealtimeTrajectory &trajectory)
{
    QJsonArray segments;
    for (const RealtimeTrajectorySegment &segment : trajectory.segments)
    {
        QJsonArray points;
        for (const GeoPosition &point : segment.points)
        {
            points.append(positionObject(point));
        }
        segments.append(QJsonObject{{QStringLiteral("points"), points}, {QStringLiteral("opacity"), segment.opacity}});
    }
    return {{QStringLiteral("trackId"), QString::number(trajectory.track_id)},
            {QStringLiteral("color"), targetTypeColorName(trajectory.type)},
            {QStringLiteral("selected"), trajectory.selected},
            {QStringLiteral("segments"), segments}};
}

QJsonObject geofenceObject(const Geofence &geofence)
{
    QJsonObject object{{QStringLiteral("id"), QString::number(geofence.id)},
                       {QStringLiteral("name"), geofence.name},
                       {QStringLiteral("enabled"), geofence.enabled}};
    if (const auto *circle = std::get_if<CircleGeofence>(&geofence.geometry); circle != nullptr)
    {
        object.insert(QStringLiteral("shape"), QStringLiteral("circle"));
        object.insert(QStringLiteral("center"), positionObject(circle->center));
        object.insert(QStringLiteral("radiusM"), circle->radius_m);
    }
    else if (const auto *rectangle = std::get_if<RectangleGeofence>(&geofence.geometry); rectangle != nullptr)
    {
        object.insert(QStringLiteral("shape"), QStringLiteral("rectangle"));
        object.insert(QStringLiteral("southwest"), positionObject(rectangle->southwest));
        object.insert(QStringLiteral("northeast"), positionObject(rectangle->northeast));
    }
    else
    {
        QJsonArray vertices;
        for (const GeoPosition &vertex : std::get<PolygonGeofence>(geofence.geometry).vertices)
        {
            vertices.append(positionObject(vertex));
        }
        object.insert(QStringLiteral("shape"), QStringLiteral("polygon"));
        object.insert(QStringLiteral("vertices"), vertices);
    }
    return object;
}

QJsonObject alertMarkerObject(const TargetAlert &alert)
{
    return {
        {QStringLiteral("id"), QString::number(alert.id)},
        {QStringLiteral("longitude"), alert.position.longitude},
        {QStringLiteral("latitude"), alert.position.latitude},
        {QStringLiteral("occurredAt"), alert.occurred_at.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))},
        {QStringLiteral("severity"), alertSeverityDisplayName(alert.severity)},
        {QStringLiteral("rule"), alert.rule_name},
        {QStringLiteral("geofence"), alert.geofence_name},
        {QStringLiteral("trackId"), QString::number(alert.track_id)},
        {QStringLiteral("targetType"), targetTypeDisplayName(alert.target_type)},
        {QStringLiteral("velocity"),
         alert.velocity_mps.has_value() ? QString::number(alert.velocity_mps.value(), 'f', 3) : QStringLiteral("--")},
        {QStringLiteral("distance"),
         alert.distance_m.has_value() ? QString::number(alert.distance_m.value(), 'f', 3) : QStringLiteral("--")},
        {QStringLiteral("description"), alert.description},
        {QStringLiteral("acknowledged"), alert.acknowledged},
        {QStringLiteral("acknowledgedAt"),
         alert.acknowledged_at.has_value()
             ? alert.acknowledged_at->toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
             : QStringLiteral("--")},
        {QStringLiteral("acknowledgedBy"),
         alert.acknowledged_by.isEmpty() ? QStringLiteral("--") : alert.acknowledged_by},
        {QStringLiteral("handlingNote"),
         alert.handling_note.isEmpty() ? QStringLiteral("--") : alert.handling_note}};
}

OnlineMapUpdate createFullUpdate(const OnlineMapState &previous_state, const OnlineMapState &next_state)
{
    OnlineMapUpdate update;
    QSet<qint64> next_track_ids;
    for (const OnlineMapTarget &target : next_state.currentTargets())
    {
        next_track_ids.insert(target.track_id);
        update.upserted_targets.append(target);
    }
    for (const TrackData &track : previous_state.currentFrame().tracks)
    {
        if (!next_track_ids.contains(track.track_id))
        {
            update.removed_track_ids.append(track.track_id);
        }
    }
    update.radar_position = next_state.radarPosition();
    return update;
}

} // namespace

OnlineMapWidget::OnlineMapWidget(QWidget *parent)
    : QWidget(parent), stacked_layout_(new QStackedLayout(this)), web_view_(new QWebEngineView(this)),
      error_label_(new QLabel(this))
{
    error_label_->setAlignment(Qt::AlignCenter);
    error_label_->setWordWrap(true);
    error_label_->setStyleSheet(QStringLiteral("QLabel { background: #f5f5f5; color: #b03a2e; padding: 24px; }"));
    stacked_layout_->addWidget(web_view_);
    stacked_layout_->addWidget(error_label_);

    const QString config_path =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("config/amap.json"));
    const AmapConfigResult config_result = loadAmapConfig(config_path);
    if (!config_result.config.has_value())
    {
        qWarning() << "OnlineMapWidget:" << config_result.error;
        showError(config_result.error);
        return;
    }

    bridge_ = new MapWebBridge(config_result.config->key, config_result.config->security_code, this);
    auto *channel = new QWebChannel(web_view_->page());
    channel->registerObject(QStringLiteral("mapBridge"), bridge_);
    web_view_->page()->setWebChannel(channel);

    connect(bridge_, &MapWebBridge::pageReadyReported, this, &OnlineMapWidget::handlePageReady);
    connect(bridge_, &MapWebBridge::mapErrorReported, this, &OnlineMapWidget::handleMapError);
    connect(bridge_, &MapWebBridge::mapWarningReported, this, &OnlineMapWidget::handleMapWarning);
    connect(bridge_, &MapWebBridge::viewChanged, this,
            [this](double longitude, double latitude, int zoom) { emit viewChanged({latitude, longitude}, zoom); });
    connect(bridge_, &MapWebBridge::targetClicked, this, &OnlineMapWidget::targetClicked);
    connect(bridge_, &MapWebBridge::geofenceEdited, this, &OnlineMapWidget::handleGeofenceEdited);
    connect(web_view_, &QWebEngineView::loadFinished, this,
            [this](bool success)
            {
                if (!success)
                {
                    handleMapError(tr("在线地图本地页面加载失败"));
                }
            });
    connect(web_view_->page(), &QWebEnginePage::renderProcessTerminated, this,
            [this](QWebEnginePage::RenderProcessTerminationStatus status, int exit_code)
            { handleRenderProcessTermination(static_cast<int>(status), exit_code); });

    stacked_layout_->setCurrentWidget(web_view_);
    web_view_->load(QUrl(QStringLiteral("qrc:/map/online_map.html")));
}

void OnlineMapWidget::renderFrame(const OnlineMapState &state, const OnlineMapUpdate &update)
{
    render_state_ = state;
    if (map_ready_)
    {
        emit bridge_->mapUpdateAvailable(createUpdateObject(update));
    }
}

void OnlineMapWidget::synchronizeState(const OnlineMapState &state)
{
    renderFrame(state, createFullUpdate(render_state_, state));
}

void OnlineMapWidget::setLayer(OnlineMapLayer layer)
{
    render_state_.setLayer(layer);
    if (map_ready_)
    {
        emit bridge_->layerUpdated(layer == OnlineMapLayer::kStreet ? QStringLiteral("street")
                                                                    : QStringLiteral("satellite"));
    }
}

void OnlineMapWidget::setView(const GeoPosition &center, int zoom)
{
    render_state_.setCenter(center);
    render_state_.setZoom(zoom);
    if (map_ready_)
    {
        emit bridge_->viewUpdated(center.longitude, center.latitude, render_state_.zoom());
    }
}

void OnlineMapWidget::setSelectedTrackId(std::optional<qint64> track_id)
{
    if (!render_state_.setSelectedTrackId(track_id))
    {
        qWarning() << "OnlineMapWidget: ignored selection for unknown track" << track_id.value_or(0);
        return;
    }
    if (map_ready_)
    {
        emit bridge_->selectionUpdated(track_id.has_value() ? QString::number(track_id.value()) : QString());
    }
}

void OnlineMapWidget::setAlertTrackIds(const QSet<qint64> &track_ids)
{
    alert_track_ids_ = track_ids;
    if (map_ready_)
    {
        QJsonArray track_id_array;
        for (qint64 track_id : std::as_const(alert_track_ids_))
        {
            track_id_array.append(QString::number(track_id));
        }
        emit bridge_->alertHighlightUpdated(track_id_array);
    }
}

void OnlineMapWidget::setAlertMarkers(const QVector<TargetAlert> &alerts)
{
    alert_markers_ = alerts;
    if (map_ready_)
    {
        emit bridge_->alertMarkersUpdated(createAlertMarkersArray(alert_markers_));
    }
}

void OnlineMapWidget::setTrajectories(const QVector<RealtimeTrajectory> &trajectories)
{
    trajectories_ = trajectories;
    if (map_ready_)
    {
        emit bridge_->trajectoriesUpdated(createTrajectoriesArray(trajectories_));
    }
}

void OnlineMapWidget::setGeofences(const QVector<Geofence> &geofences)
{
    geofences_ = geofences;
    if (map_ready_)
    {
        emit bridge_->geofencesUpdated(createGeofencesArray(geofences_));
    }
}

void OnlineMapWidget::setEditableGeofenceId(std::optional<qint64> geofence_id)
{
    if (editable_geofence_id_.has_value() && editable_geofence_id_ != geofence_id)
    {
        const qint64 previous_geofence_id = editable_geofence_id_.value();
        recently_editable_geofence_id_ = previous_geofence_id;
        QTimer::singleShot(2'000, this,
                           [this, previous_geofence_id]()
                           {
                               if (recently_editable_geofence_id_ == previous_geofence_id)
                               {
                                   recently_editable_geofence_id_.reset();
                               }
                           });
    }
    editable_geofence_id_ = geofence_id;
    if (map_ready_)
    {
        emit bridge_->geofenceEditingUpdated(geofence_id.has_value() ? QString::number(geofence_id.value())
                                                                     : QString());
    }
}

void OnlineMapWidget::cancelPendingGeofenceEdit()
{
    editable_geofence_id_.reset();
    recently_editable_geofence_id_.reset();
    if (map_ready_)
    {
        emit bridge_->geofenceEditingUpdated(QString());
    }
}

void OnlineMapWidget::handlePageReady()
{
    map_ready_ = true;
    stacked_layout_->setCurrentWidget(web_view_);
    emit bridge_->initialStateAvailable(createInitialState());
    emit bridge_->geofenceEditingUpdated(
        editable_geofence_id_.has_value() ? QString::number(editable_geofence_id_.value()) : QString());

    if (render_reload_attempts_ > 0)
    {
        QTimer::singleShot(30'000, this,
                           [this]()
                           {
                               if (map_ready_)
                               {
                                   render_reload_attempts_ = 0;
                               }
                           });
    }
}

void OnlineMapWidget::handleMapError(const QString &message)
{
    qWarning() << "OnlineMapWidget:" << message;
    showError(tr("在线地图加载失败：%1").arg(message));
    emit mapError(message);
}

void OnlineMapWidget::handleMapWarning(const QString &message)
{
    qWarning() << "OnlineMapWidget: runtime map warning:" << message;
}

void OnlineMapWidget::handleGeofenceEdited(const QJsonObject &geofence_object)
{
    bool converted = false;
    const qint64 geofence_id = geofence_object.value(QStringLiteral("id")).toString().toLongLong(&converted);
    const auto current = std::find_if(geofences_.begin(), geofences_.end(),
                                      [geofence_id](const Geofence &candidate) { return candidate.id == geofence_id; });
    const bool expected_edit = editable_geofence_id_ == geofence_id || recently_editable_geofence_id_ == geofence_id;
    if (!converted || current == geofences_.end() || !expected_edit)
    {
        return;
    }

    const auto restore_valid_geofences = [this, geofence_id](const QString &reason)
    {
        qWarning() << "OnlineMapWidget: rejected invalid geofence edit" << geofence_id << reason;
        emit geofenceEditError(reason);
        if (map_ready_)
        {
            emit bridge_->geofencesUpdated(createGeofencesArray(geofences_));
        }
    };

    Geofence edited = *current;
    const QString shape = geofence_object.value(QStringLiteral("shape")).toString();
    if (shape == QStringLiteral("circle"))
    {
        const std::optional<GeoPosition> center = positionFromObject(geofence_object.value(QStringLiteral("center")));
        const QJsonValue radius = geofence_object.value(QStringLiteral("radiusM"));
        if (!center.has_value() || !radius.isDouble())
        {
            restore_valid_geofences(tr("圆形围栏编辑数据不完整"));
            return;
        }
        edited.geometry = CircleGeofence{center.value(), radius.toDouble()};
    }
    else if (shape == QStringLiteral("rectangle"))
    {
        const std::optional<GeoPosition> southwest =
            positionFromObject(geofence_object.value(QStringLiteral("southwest")));
        const std::optional<GeoPosition> northeast =
            positionFromObject(geofence_object.value(QStringLiteral("northeast")));
        if (!southwest.has_value() || !northeast.has_value())
        {
            restore_valid_geofences(tr("矩形围栏编辑数据不完整"));
            return;
        }
        edited.geometry = RectangleGeofence{southwest.value(), northeast.value()};
    }
    else if (shape == QStringLiteral("polygon"))
    {
        const QJsonValue vertices_value = geofence_object.value(QStringLiteral("vertices"));
        if (!vertices_value.isArray())
        {
            restore_valid_geofences(tr("多边形围栏编辑数据不完整"));
            return;
        }
        QVector<GeoPosition> vertices;
        for (const QJsonValue &vertex_value : vertices_value.toArray())
        {
            const std::optional<GeoPosition> vertex = positionFromObject(vertex_value);
            if (!vertex.has_value())
            {
                restore_valid_geofences(tr("多边形围栏顶点数据无效"));
                return;
            }
            vertices.append(vertex.value());
        }
        edited.geometry = PolygonGeofence{vertices};
    }
    else
    {
        restore_valid_geofences(tr("围栏形状无效"));
        return;
    }

    const QString validation_error = validateGeofence(edited);
    if (!validation_error.isEmpty())
    {
        restore_valid_geofences(validation_error);
        return;
    }
    recently_editable_geofence_id_.reset();
    *current = edited;
    emit geofenceEdited(edited);
}

void OnlineMapWidget::handleRenderProcessTermination(int status, int exit_code)
{
    const QString detail = tr("在线地图渲染进程退出（状态 %1，代码 %2）").arg(status).arg(exit_code);
    qWarning() << "OnlineMapWidget:" << detail;
    map_ready_ = false;

    if (render_reload_attempts_ < 1)
    {
        ++render_reload_attempts_;
        web_view_->reload();
        return;
    }

    showError(detail);
    emit mapError(detail);
}

QJsonObject OnlineMapWidget::createInitialState() const
{
    QJsonArray targets;
    for (const OnlineMapTarget &target : render_state_.currentTargets())
    {
        targets.append(targetObject(target));
    }

    QJsonObject state{{QStringLiteral("center"), positionObject(render_state_.center())},
                      {QStringLiteral("zoom"), render_state_.zoom()},
                      {QStringLiteral("layer"), render_state_.layer() == OnlineMapLayer::kStreet
                                                    ? QStringLiteral("street")
                                                    : QStringLiteral("satellite")},
                      {QStringLiteral("targets"), targets},
                      {QStringLiteral("trajectories"), createTrajectoriesArray(trajectories_)},
                      {QStringLiteral("geofences"), createGeofencesArray(geofences_)},
                      {QStringLiteral("alertMarkers"), createAlertMarkersArray(alert_markers_)}};
    if (render_state_.radarPosition().has_value())
    {
        state.insert(QStringLiteral("radar"), positionObject(render_state_.radarPosition().value()));
    }
    if (render_state_.selectedTrackId().has_value())
    {
        state.insert(QStringLiteral("selectedTrackId"), QString::number(render_state_.selectedTrackId().value()));
    }
    QJsonArray alert_track_ids;
    for (qint64 track_id : alert_track_ids_)
    {
        alert_track_ids.append(QString::number(track_id));
    }
    state.insert(QStringLiteral("alertTrackIds"), alert_track_ids);
    return state;
}

QJsonObject OnlineMapWidget::createUpdateObject(const OnlineMapUpdate &update)
{
    QJsonArray upserted_targets;
    for (const OnlineMapTarget &target : update.upserted_targets)
    {
        upserted_targets.append(targetObject(target));
    }

    QJsonArray removed_track_ids;
    for (qint64 track_id : update.removed_track_ids)
    {
        removed_track_ids.append(QString::number(track_id));
    }

    QJsonObject object{{QStringLiteral("upsertedTargets"), upserted_targets},
                       {QStringLiteral("removedTrackIds"), removed_track_ids}};
    if (update.radar_position.has_value())
    {
        object.insert(QStringLiteral("radar"), positionObject(update.radar_position.value()));
    }
    if (update.automatic_center.has_value())
    {
        object.insert(QStringLiteral("automaticCenter"), positionObject(update.automatic_center.value()));
    }
    return object;
}

QJsonArray OnlineMapWidget::createTrajectoriesArray(const QVector<RealtimeTrajectory> &trajectories)
{
    QJsonArray objects;
    for (const RealtimeTrajectory &trajectory : trajectories)
    {
        objects.append(trajectoryObject(trajectory));
    }
    return objects;
}

QJsonArray OnlineMapWidget::createGeofencesArray(const QVector<Geofence> &geofences)
{
    QJsonArray objects;
    for (const Geofence &geofence : geofences)
    {
        if (geofence.visible)
        {
            objects.append(geofenceObject(geofence));
        }
    }
    return objects;
}

QJsonArray OnlineMapWidget::createAlertMarkersArray(const QVector<TargetAlert> &alerts)
{
    QJsonArray objects;
    for (const TargetAlert &alert : alerts)
    {
        objects.append(alertMarkerObject(alert));
    }
    return objects;
}

void OnlineMapWidget::showError(const QString &message)
{
    map_ready_ = false;
    error_label_->setText(message);
    stacked_layout_->setCurrentWidget(error_label_);
}

} // namespace utms
