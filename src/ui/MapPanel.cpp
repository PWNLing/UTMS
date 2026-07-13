#include "ui/MapPanel.h"

#include <algorithm>

#include <QStackedWidget>
#include <QVBoxLayout>

#include "map/OnlineMapState.h"
#include "ui/OfflineMapWidget.h"
#include "ui/OnlineMapWidget.h"

namespace utms
{

MapPanel::MapPanel(QWidget *parent)
    : QWidget(parent), map_stack_(new QStackedWidget(this)), online_map_(new OnlineMapWidget(map_stack_)),
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
    connect(online_map_, &OnlineMapWidget::viewChanged, this, &MapPanel::handleOnlineViewChanged);
    connect(offline_map_, &OfflineMapWidget::viewChanged, this, &MapPanel::handleOfflineViewChanged);
}

void MapPanel::setFrame(const RadarFrame &frame)
{
    const OnlineMapUpdate update = state_.replaceFrame(frame);
    online_map_->renderFrame(state_, update);
    offline_map_->renderState(state_);
    online_map_->setSelectedTrackId(state_.selectedTrackId());
}

void MapPanel::setMapMode(MapMode mode)
{
    map_mode_ = mode;
    synchronizeViews();
    map_stack_->setCurrentWidget(mode == MapMode::kOnline ? static_cast<QWidget *>(online_map_)
                                                          : static_cast<QWidget *>(offline_map_));
}

void MapPanel::setOnlineLayer(OnlineMapLayer layer)
{
    state_.setLayer(layer);
    online_map_->setLayer(layer);
}

bool MapPanel::setSelectedTrackId(std::optional<qint64> track_id)
{
    if (!state_.setSelectedTrackId(track_id))
    {
        return false;
    }
    online_map_->setSelectedTrackId(track_id);
    offline_map_->setSelectedTrackId(track_id);
    return true;
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
    synchronizeViews();
    return true;
}

bool MapPanel::locateRadar()
{
    if (!state_.locateRadar())
    {
        return false;
    }
    synchronizeViews();
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
    offline_map_->setView(state_.center(), state_.zoom());
}

void MapPanel::handleOfflineViewChanged(const GeoPosition &center, int zoom)
{
    if (map_mode_ != MapMode::kOffline)
    {
        return;
    }
    state_.setCenter(center);
    state_.setZoom(zoom);
    online_map_->setView(state_.center(), state_.zoom());
}

void MapPanel::synchronizeViews()
{
    online_map_->setView(state_.center(), state_.zoom());
    offline_map_->setView(state_.center(), state_.zoom());
}

} // namespace utms
