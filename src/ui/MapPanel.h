#pragma once

#include <QWidget>

class QStackedWidget;

namespace utms
{

class OnlineMapWidget;
enum class OnlineMapLayer;
struct RadarFrame;

class MapPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit MapPanel(QWidget *parent = nullptr);

    void setFrame(const RadarFrame &frame);
    void setOnlineLayer(OnlineMapLayer layer);
    bool locateRadar();

  signals:
    void targetClicked(qint64 track_id);

  private:
    QStackedWidget *map_stack_ = nullptr;
    OnlineMapWidget *online_map_ = nullptr;
};

} // namespace utms
