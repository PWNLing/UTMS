#pragma once

#include <QJsonObject>
#include <QWidget>

#include "map/OnlineMapState.h"

class QLabel;
class QStackedLayout;
class QWebEngineView;

namespace utms
{

class MapWebBridge;

class OnlineMapWidget : public QWidget
{
    Q_OBJECT

  public:
    explicit OnlineMapWidget(QWidget *parent = nullptr);

    void setFrame(const RadarFrame &frame);
    void setLayer(OnlineMapLayer layer);
    bool locateRadar();

  signals:
    void targetClicked(qint64 track_id);
    void mapError(const QString &message);

  private slots:
    void handlePageReady();
    void handleMapError(const QString &message);
    void handleMapWarning(const QString &message);
    void handleViewChanged(double longitude, double latitude, int zoom);

  private:
    QJsonObject createInitialState() const;
    static QJsonObject createUpdateObject(const OnlineMapUpdate &update);
    void handleRenderProcessTermination(int status, int exit_code);
    void showError(const QString &message);

    OnlineMapState state_;
    QStackedLayout *stacked_layout_ = nullptr;
    QWebEngineView *web_view_ = nullptr;
    QLabel *error_label_ = nullptr;
    MapWebBridge *bridge_ = nullptr;
    bool map_ready_ = false;
    int render_reload_attempts_ = 0;
};

} // namespace utms
