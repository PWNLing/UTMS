#include "ui/MapPanel.h"

#include <QStackedWidget>
#include <QVBoxLayout>

#include "map/OnlineMapState.h"
#include "ui/OnlineMapWidget.h"

namespace utms
{

MapPanel::MapPanel(QWidget *parent)
    : QWidget(parent), map_stack_(new QStackedWidget(this)), online_map_(new OnlineMapWidget(map_stack_))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(map_stack_);
    map_stack_->addWidget(online_map_);
    connect(online_map_, &OnlineMapWidget::targetClicked, this, &MapPanel::targetClicked);
}

void MapPanel::setFrame(const RadarFrame &frame)
{
    online_map_->setFrame(frame);
}

void MapPanel::setOnlineLayer(OnlineMapLayer layer)
{
    online_map_->setLayer(layer);
}

bool MapPanel::locateRadar()
{
    return online_map_->locateRadar();
}

} // namespace utms
