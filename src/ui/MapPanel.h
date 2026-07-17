#pragma once

#include <QWidget>

#include "map/OnlineMapState.h"
#include "map/RealtimeTrajectoryModel.h"

class QStackedWidget;
class QTimer;

namespace utms
{

class OnlineMapWidget;
class OfflineMapWidget;
struct RadarFrame;

enum class MapMode
{
    kOnline,
    kOffline
};

class MapPanel : public QWidget
{
    Q_OBJECT

public:
    explicit MapPanel(QWidget *parent = nullptr);

    void setFrame(const RadarFrame &frame);
    void setMapMode(MapMode mode);
    void setOnlineLayer(OnlineMapLayer layer);
    void setCenter(const GeoPosition &center);
    void setTrajectoryDuration(RealtimeTrajectoryDuration duration);
    void setShowAllTrajectories(bool show_all_trajectories);
    bool setSelectedTrackId(std::optional<qint64> track_id);
    void clearSelectionForMissingTarget();
    bool selectTarget(qint64 track_id, bool center_on_target);
    bool locateRadar();

    MapMode mapMode() const;
    GeoPosition center() const;
    int zoom() const;
    std::optional<qint64> selectedTrackId() const;
    QVector<RealtimeTrajectory> realtimeTrajectories(const QDateTime &now) const;

signals:
    void targetClicked(qint64 track_id);

private:
    void handleTargetClicked(qint64 track_id);
    void handleOnlineViewChanged(const GeoPosition &center, int zoom);
    void handleOfflineViewChanged(const GeoPosition &center, int zoom);
    void applyViewToActiveMap();
    void renderTrajectories(const QDateTime &now);
    void synchronizeActiveMap();

    OnlineMapState state_;
    RealtimeTrajectoryModel trajectory_model_;
    QStackedWidget *map_stack_ = nullptr;
    QTimer *trajectory_refresh_timer_ = nullptr;
    OnlineMapWidget *online_map_ = nullptr;
    OfflineMapWidget *offline_map_ = nullptr;
    MapMode map_mode_ = MapMode::kOnline;
};

} // namespace utms
