#include "ui/MapPanel.h"

#include <algorithm>

#include <QDateTime>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "map/OnlineMapState.h"
#include "ui/OfflineMapWidget.h"
#include "ui/OnlineMapWidget.h"

namespace utms
{

MapPanel::MapPanel(QWidget *parent)
    : QWidget(parent), map_stack_(new QStackedWidget(this)), trajectory_refresh_timer_(new QTimer(this)),
      online_map_(new OnlineMapWidget(map_stack_)), offline_map_(new OfflineMapWidget(map_stack_))
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
    connect(online_map_, &OnlineMapWidget::viewChanged, this, &MapPanel::handleOnlineViewChanged);
    connect(offline_map_, &OfflineMapWidget::viewChanged, this, &MapPanel::handleOfflineViewChanged);
    trajectory_refresh_timer_->setInterval(500);
    connect(trajectory_refresh_timer_, &QTimer::timeout, this,
            [this]() { renderTrajectories(QDateTime::currentDateTime()); });
    trajectory_refresh_timer_->start();
}

void MapPanel::setFrame(const RadarFrame &frame)
{
    trajectory_model_.replaceFrame(frame);
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
    map_mode_ = mode;
    synchronizeActiveMap();
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

bool MapPanel::setSelectedTrackId(std::optional<qint64> track_id)
{
    if (!state_.setSelectedTrackId(track_id))
    {
        return false;
    }
    trajectory_model_.setSelectedTrackId(track_id);
    if (map_mode_ == MapMode::kOnline)
    {
        online_map_->setSelectedTrackId(track_id);
    }
    else
    {
        offline_map_->setSelectedTrackId(track_id);
    }
    renderTrajectories(QDateTime::currentDateTime());
    return true;
}

void MapPanel::clearSelectionForMissingTarget()
{
    state_.setSelectedTrackId(std::nullopt);
    trajectory_model_.clearSelectionRetainingFocusedTrajectory(QDateTime::currentDateTime());
    if (map_mode_ == MapMode::kOnline)
    {
        online_map_->setSelectedTrackId(std::nullopt);
    }
    else
    {
        offline_map_->setSelectedTrackId(std::nullopt);
    }
    renderTrajectories(QDateTime::currentDateTime());
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

MapMode MapPanel::mapMode() const
{
    return map_mode_;
}

GeoPosition MapPanel::center() const
{
    return state_.center();
}

int MapPanel::zoom() const
{
    return state_.zoom();
}

std::optional<qint64> MapPanel::selectedTrackId() const
{
    return state_.selectedTrackId();
}

QVector<RealtimeTrajectory> MapPanel::realtimeTrajectories(const QDateTime &now) const
{
    return trajectory_model_.visibleTrajectories(now);
}

void MapPanel::handleTargetClicked(qint64 track_id)
{
    if (setSelectedTrackId(track_id))
    {
        emit targetClicked(track_id);
    }
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
    const QVector<RealtimeTrajectory> trajectories = trajectory_model_.visibleTrajectories(now);
    if (map_mode_ == MapMode::kOnline)
    {
        online_map_->setTrajectories(trajectories);
    }
    else
    {
        offline_map_->setTrajectories(trajectories);
    }
}

void MapPanel::synchronizeActiveMap()
{
    if (map_mode_ == MapMode::kOnline)
    {
        online_map_->synchronizeState(state_);
        online_map_->setLayer(state_.layer());
        online_map_->setSelectedTrackId(state_.selectedTrackId());
    }
    else
    {
        offline_map_->renderState(state_);
    }
    applyViewToActiveMap();
    renderTrajectories(QDateTime::currentDateTime());
}

} // namespace utms
