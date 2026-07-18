#include "ui/MapPanel.h"

#include <algorithm>
#include <cmath>

#include <QDateTime>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "core/GeofenceGeometry.h"
#include "map/OnlineMapState.h"
#include "ui/OfflineMapWidget.h"
#include "ui/OnlineMapWidget.h"

namespace utms
{
namespace
{

constexpr int kTrajectoryRefreshIntervalMs = 500;
constexpr double kCoordinateComparisonTolerance = 1e-10;
constexpr double kRadiusComparisonToleranceM = 1e-6;

bool positionsEqual(const GeoPosition &left, const GeoPosition &right)
{
    return std::abs(left.latitude - right.latitude) <= kCoordinateComparisonTolerance &&
           std::abs(left.longitude - right.longitude) <= kCoordinateComparisonTolerance;
}

bool geometriesEqual(const GeofenceGeometry &left, const GeofenceGeometry &right)
{
    if (left.index() != right.index())
    {
        return false;
    }
    if (const auto *left_circle = std::get_if<CircleGeofence>(&left); left_circle != nullptr)
    {
        const auto &right_circle = std::get<CircleGeofence>(right);
        return positionsEqual(left_circle->center, right_circle.center) &&
               std::abs(left_circle->radius_m - right_circle.radius_m) <= kRadiusComparisonToleranceM;
    }
    if (const auto *left_rectangle = std::get_if<RectangleGeofence>(&left); left_rectangle != nullptr)
    {
        const auto &right_rectangle = std::get<RectangleGeofence>(right);
        return positionsEqual(left_rectangle->southwest, right_rectangle.southwest) &&
               positionsEqual(left_rectangle->northeast, right_rectangle.northeast);
    }

    const auto &left_vertices = std::get<PolygonGeofence>(left).vertices;
    const auto &right_vertices = std::get<PolygonGeofence>(right).vertices;
    if (left_vertices.size() != right_vertices.size())
    {
        return false;
    }
    for (qsizetype vertex_index = 0; vertex_index < left_vertices.size(); ++vertex_index)
    {
        if (!positionsEqual(left_vertices.at(vertex_index), right_vertices.at(vertex_index)))
        {
            return false;
        }
    }
    return true;
}

bool geofenceCollectionsEqual(const QVector<Geofence> &left, const QVector<Geofence> &right)
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (qsizetype index = 0; index < left.size(); ++index)
    {
        const Geofence &left_geofence = left.at(index);
        const Geofence &right_geofence = right.at(index);
        if (left_geofence.id != right_geofence.id || left_geofence.name != right_geofence.name ||
            left_geofence.enabled != right_geofence.enabled || left_geofence.visible != right_geofence.visible ||
            !geometriesEqual(left_geofence.geometry, right_geofence.geometry))
        {
            return false;
        }
    }
    return true;
}

} // namespace

MapPanel::MapPanel(QWidget *parent)
    : QWidget(parent), map_stack_(new QStackedWidget(this)), trajectory_refresh_timer_(new QTimer(this)),
      alert_highlight_timer_(new QTimer(this)), online_map_(new OnlineMapWidget(map_stack_)),
      offline_map_(new OfflineMapWidget(map_stack_))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(map_stack_);
    map_stack_->addWidget(online_map_);
    map_stack_->addWidget(offline_map_);
    online_map_->renderFrame(state_, {});
    offline_map_->renderState(state_);
    connect(online_map_, &OnlineMapWidget::targetClicked, this, &MapPanel::handleTargetClicked);
    connect(offline_map_, &OfflineMapWidget::targetClicked, this, &MapPanel::handleTargetClicked);
    connect(online_map_, &OnlineMapWidget::geofenceEdited, this, &MapPanel::handleGeofenceEdited);
    connect(offline_map_, &OfflineMapWidget::geofenceEdited, this, &MapPanel::handleGeofenceEdited);
    connect(online_map_, &OnlineMapWidget::geofenceEditError, this, &MapPanel::geofenceEditError);
    connect(offline_map_, &OfflineMapWidget::geofenceEditError, this, &MapPanel::geofenceEditError);
    connect(online_map_, &OnlineMapWidget::viewChanged, this, &MapPanel::handleOnlineViewChanged);
    connect(offline_map_, &OfflineMapWidget::viewChanged, this, &MapPanel::handleOfflineViewChanged);
    trajectory_refresh_timer_->setInterval(kTrajectoryRefreshIntervalMs);
    connect(trajectory_refresh_timer_, &QTimer::timeout, this,
            [this]() { renderTrajectories(QDateTime::currentDateTime()); });
    trajectory_refresh_timer_->start();
    alert_highlight_timer_->setSingleShot(true);
    connect(alert_highlight_timer_, &QTimer::timeout, this,
            [this]()
            {
                alert_track_ids_.clear();
                online_map_->setAlertTrackIds({});
                offline_map_->setAlertTrackIds({});
            });
}

void MapPanel::setFrame(const RadarFrame &frame)
{
    trajectory_model_.replaceFrame(frame);
    latest_live_frame_ = frame;
    if (replay_mode_)
    {
        return;
    }
    renderDisplayFrame(frame);
}

void MapPanel::setReplayMode(bool replay_mode)
{
    if (replay_mode_ == replay_mode)
    {
        return;
    }

    replay_mode_ = replay_mode;
    replay_trajectory_.reset();
    if (replay_mode_)
    {
        renderTrajectories(QDateTime::currentDateTime());
        return;
    }

    state_.setSelectedTrackId(std::nullopt);
    trajectory_model_.setSelectedTrackId(std::nullopt);
    if (latest_live_frame_.has_value())
    {
        renderDisplayFrame(latest_live_frame_.value());
    }
    else
    {
        renderTrajectories(QDateTime::currentDateTime());
    }
}

void MapPanel::setReplayFrame(const RadarFrame &frame)
{
    if (!replay_mode_)
    {
        return;
    }
    renderDisplayFrame(frame);
}

void MapPanel::setReplayTrajectory(const HistoryReplayTrajectory &trajectory)
{
    if (!replay_mode_)
    {
        return;
    }
    replay_trajectory_ = trajectory;
    renderTrajectories(QDateTime::currentDateTime());
}

void MapPanel::clearReplayTrajectory()
{
    replay_trajectory_.reset();
    renderTrajectories(QDateTime::currentDateTime());
}

void MapPanel::renderDisplayFrame(const RadarFrame &frame)
{
    const std::optional<qint64> previous_selection = state_.selectedTrackId();
    const OnlineMapUpdate update = state_.replaceFrame(frame);
    const QDateTime render_time = frame.received_at.isValid() ? frame.received_at : QDateTime::currentDateTime();
    if (map_mode_ == MapMode::kOnline)
    {
        online_map_->renderFrame(state_, update);
        if (previous_selection != state_.selectedTrackId())
        {
            online_map_->setSelectedTrackId(state_.selectedTrackId());
        }
        renderTrajectories(render_time);
        return;
    }

    offline_map_->renderState(state_);
    if (update.automatic_center.has_value())
    {
        offline_map_->setView(state_.center(), state_.zoom());
    }
    renderTrajectories(render_time);
}

void MapPanel::setMapMode(MapMode mode)
{
    online_map_->setEditableGeofenceId(std::nullopt);
    offline_map_->setEditableGeofenceId(std::nullopt);
    map_mode_ = mode;
    synchronizeActiveMap();
    if (editable_geofence_id_.has_value())
    {
        if (map_mode_ == MapMode::kOnline)
        {
            online_map_->setEditableGeofenceId(editable_geofence_id_);
        }
        else
        {
            offline_map_->setEditableGeofenceId(editable_geofence_id_);
        }
    }
    map_stack_->setCurrentWidget(mode == MapMode::kOnline ? static_cast<QWidget *>(online_map_)
                                                          : static_cast<QWidget *>(offline_map_));
}

void MapPanel::setOnlineLayer(OnlineMapLayer layer)
{
    state_.setLayer(layer);
    online_map_->setLayer(layer);
}

void MapPanel::setCenter(const GeoPosition &center)
{
    state_.setCenter(center);
    applyViewToActiveMap();
}

void MapPanel::setTrajectoryDuration(RealtimeTrajectoryDuration duration)
{
    trajectory_model_.setDuration(duration);
    renderTrajectories(QDateTime::currentDateTime());
}

void MapPanel::setShowAllTrajectories(bool show_all_trajectories)
{
    trajectory_model_.setShowAllTargets(show_all_trajectories);
    renderTrajectories(QDateTime::currentDateTime());
}

void MapPanel::setGeofences(const QVector<Geofence> &geofences)
{
    const QVector<Geofence> previous_geofences = geofences_;
    confirmed_geofences_ = geofences;
    geofences_ = geofences;
    for (auto pending = pending_geofence_edits_.begin(); pending != pending_geofence_edits_.end();)
    {
        const qint64 pending_geofence_id = pending.key();
        const auto incoming =
            std::find_if(geofences_.begin(), geofences_.end(), [pending_geofence_id](const Geofence &geofence)
                         { return geofence.id == pending_geofence_id; });
        if (incoming == geofences_.end())
        {
            pending = pending_geofence_edits_.erase(pending);
            continue;
        }
        if (geometriesEqual(incoming->geometry, pending->geometry))
        {
            pending = pending_geofence_edits_.erase(pending);
            continue;
        }
        incoming->geometry = pending->geometry;
        ++pending;
    }

    if (editable_geofence_id_.has_value())
    {
        const auto editable =
            std::find_if(geofences_.cbegin(), geofences_.cend(), [this](const Geofence &geofence)
                         { return geofence.id == editable_geofence_id_.value() && geofence.visible; });
        if (editable == geofences_.cend())
        {
            setEditableGeofenceId(std::nullopt);
        }
    }

    const bool visible_state_changed = !geofenceCollectionsEqual(previous_geofences, geofences_);
    const bool suppress_online_refresh =
        map_mode_ == MapMode::kOnline && editable_geofence_id_.has_value() && !visible_state_changed;
    const bool suppress_offline_refresh =
        map_mode_ == MapMode::kOffline && editable_geofence_id_.has_value() && !visible_state_changed;
    if (!suppress_online_refresh)
    {
        online_map_->setGeofences(geofences_);
    }
    if (!suppress_offline_refresh)
    {
        offline_map_->setGeofences(geofences_);
    }
}

bool MapPanel::setEditableGeofenceId(std::optional<qint64> geofence_id)
{
    if (geofence_id.has_value())
    {
        const auto geofence =
            std::find_if(geofences_.cbegin(), geofences_.cend(), [geofence_id](const Geofence &candidate)
                         { return candidate.id == geofence_id.value() && candidate.visible; });
        if (geofence == geofences_.cend())
        {
            return false;
        }
    }

    editable_geofence_id_ = geofence_id;
    online_map_->setEditableGeofenceId(map_mode_ == MapMode::kOnline ? geofence_id : std::nullopt);
    offline_map_->setEditableGeofenceId(map_mode_ == MapMode::kOffline ? geofence_id : std::nullopt);
    if (!geofence_id.has_value())
    {
        online_map_->setGeofences(geofences_);
        offline_map_->setGeofences(geofences_);
    }
    emit geofenceEditingChanged(editable_geofence_id_);
    return true;
}

void MapPanel::discardPendingGeofenceEdits()
{
    pending_geofence_edits_.clear();
    editable_geofence_id_.reset();
    online_map_->cancelPendingGeofenceEdit();
    offline_map_->setEditableGeofenceId(std::nullopt);
    geofences_ = confirmed_geofences_;
    online_map_->setGeofences(geofences_);
    offline_map_->setGeofences(geofences_);
    emit geofenceEditingChanged(std::nullopt);
}

bool MapPanel::setSelectedTrackId(std::optional<qint64> track_id)
{
    if (!state_.setSelectedTrackId(track_id))
    {
        return false;
    }
    if (!replay_mode_)
    {
        trajectory_model_.setSelectedTrackId(track_id);
    }
    applySelectionToActiveMap(track_id);
    renderTrajectories(QDateTime::currentDateTime());
    return true;
}

void MapPanel::clearSelectionForMissingTarget()
{
    state_.setSelectedTrackId(std::nullopt);
    trajectory_model_.clearSelectionRetainingFocusedTrajectory(QDateTime::currentDateTime());
    applySelectionToActiveMap(std::nullopt);
    renderTrajectories(QDateTime::currentDateTime());
}

void MapPanel::applySelectionToActiveMap(std::optional<qint64> track_id)
{
    if (map_mode_ == MapMode::kOnline)
    {
        online_map_->setSelectedTrackId(track_id);
    }
    else
    {
        offline_map_->setSelectedTrackId(track_id);
    }
}

bool MapPanel::selectTarget(qint64 track_id, bool center_on_target)
{
    if (!setSelectedTrackId(track_id))
    {
        return false;
    }
    if (!center_on_target)
    {
        return true;
    }

    const auto target = std::find_if(state_.currentFrame().tracks.cbegin(), state_.currentFrame().tracks.cend(),
                                     [track_id](const TrackData &track) { return track.track_id == track_id; });
    if (target == state_.currentFrame().tracks.cend())
    {
        return false;
    }
    state_.setCenter(target->position);
    applyViewToActiveMap();
    return true;
}

bool MapPanel::locateRadar()
{
    if (!state_.locateRadar())
    {
        return false;
    }
    applyViewToActiveMap();
    return true;
}

bool MapPanel::locateGeofence(qint64 geofence_id)
{
    const auto geofence = std::find_if(geofences_.cbegin(), geofences_.cend(), [geofence_id](const Geofence &candidate)
                                       { return candidate.id == geofence_id; });
    if (geofence == geofences_.cend())
    {
        return false;
    }
    state_.setCenter(geofenceCenter(*geofence));
    applyViewToActiveMap();
    return true;
}

bool MapPanel::flashAlertTarget(qint64 track_id, int duration_ms)
{
    return flashAlertTargets({track_id}, duration_ms);
}

bool MapPanel::flashAlertTargets(const QVector<qint64> &track_ids, int duration_ms)
{
    if (duration_ms <= 0)
    {
        return false;
    }
    QSet<qint64> current_track_ids;
    for (const TrackData &track : state_.currentFrame().tracks)
    {
        current_track_ids.insert(track.track_id);
    }
    bool target_found = false;
    for (qint64 track_id : track_ids)
    {
        if (current_track_ids.contains(track_id))
        {
            alert_track_ids_.insert(track_id);
            target_found = true;
        }
    }
    if (!target_found)
    {
        return false;
    }
    online_map_->setAlertTrackIds(alert_track_ids_);
    offline_map_->setAlertTrackIds(alert_track_ids_);
    alert_highlight_timer_->start(duration_ms);
    return true;
}

MapMode MapPanel::mapMode() const { return map_mode_; }

GeoPosition MapPanel::center() const { return state_.center(); }

int MapPanel::zoom() const { return state_.zoom(); }

std::optional<qint64> MapPanel::selectedTrackId() const { return state_.selectedTrackId(); }

QSet<qint64> MapPanel::alertTrackIds() const { return alert_track_ids_; }

const RadarFrame &MapPanel::displayedFrame() const { return state_.currentFrame(); }

bool MapPanel::isReplayMode() const { return replay_mode_; }

std::optional<HistoryReplayTrajectory> MapPanel::replayTrajectory() const { return replay_trajectory_; }

QVector<RealtimeTrajectory> MapPanel::realtimeTrajectories(const QDateTime &now) const
{
    return trajectory_model_.visibleTrajectories(now);
}

const QVector<Geofence> &MapPanel::geofences() const { return geofences_; }

void MapPanel::handleTargetClicked(qint64 track_id)
{
    if (setSelectedTrackId(track_id))
    {
        emit targetClicked(track_id);
    }
}

void MapPanel::handleGeofenceEdited(const Geofence &geofence)
{
    const auto current = std::find_if(geofences_.begin(), geofences_.end(),
                                      [&geofence](const Geofence &candidate) { return candidate.id == geofence.id; });
    if (current == geofences_.end())
    {
        return;
    }
    current->geometry = geofence.geometry;
    pending_geofence_edits_.insert(geofence.id, *current);
    if (sender() == online_map_)
    {
        offline_map_->setGeofences(geofences_);
    }
    else
    {
        online_map_->setGeofences(geofences_);
    }
    emit geofenceEdited(geofence);
}

void MapPanel::handleOnlineViewChanged(const GeoPosition &center, int zoom)
{
    if (map_mode_ != MapMode::kOnline)
    {
        return;
    }
    state_.setCenter(center);
    state_.setZoom(zoom);
}

void MapPanel::handleOfflineViewChanged(const GeoPosition &center, int zoom)
{
    if (map_mode_ != MapMode::kOffline)
    {
        return;
    }
    state_.setCenter(center);
    state_.setZoom(zoom);
}

void MapPanel::applyViewToActiveMap()
{
    if (map_mode_ == MapMode::kOnline)
    {
        online_map_->setView(state_.center(), state_.zoom());
    }
    else
    {
        offline_map_->setView(state_.center(), state_.zoom());
    }
}

void MapPanel::renderTrajectories(const QDateTime &now)
{
    const QVector<RealtimeTrajectory> trajectories =
        replay_mode_ ? replayTrajectories() : trajectory_model_.visibleTrajectories(now);
    if (map_mode_ == MapMode::kOnline)
    {
        online_map_->setTrajectories(trajectories);
    }
    else
    {
        offline_map_->setTrajectories(trajectories);
    }
}

QVector<RealtimeTrajectory> MapPanel::replayTrajectories() const
{
    if (!replay_trajectory_.has_value())
    {
        return {};
    }

    RealtimeTrajectory trajectory;
    trajectory.track_id = replay_trajectory_->track_id;
    trajectory.type = replay_trajectory_->type;
    trajectory.selected = true;
    for (const QVector<GeoPosition> &segment : replay_trajectory_->segments)
    {
        trajectory.segments.append({segment, 1.0});
    }
    return {trajectory};
}

void MapPanel::synchronizeActiveMap()
{
    if (map_mode_ == MapMode::kOnline)
    {
        online_map_->synchronizeState(state_);
        online_map_->setLayer(state_.layer());
        online_map_->setSelectedTrackId(state_.selectedTrackId());
        online_map_->setAlertTrackIds(alert_track_ids_);
    }
    else
    {
        offline_map_->renderState(state_);
        offline_map_->setAlertTrackIds(alert_track_ids_);
    }
    applyViewToActiveMap();
    renderTrajectories(QDateTime::currentDateTime());
}

} // namespace utms
