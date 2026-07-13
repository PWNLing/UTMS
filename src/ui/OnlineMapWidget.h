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

    void renderFrame(const OnlineMapState &state, const OnlineMapUpdate &update);
    void setView(const GeoPosition &center, int zoom);
    void setLayer(OnlineMapLayer layer);
    void setSelectedTrackId(std::optional<qint64> track_id);

    signals:
    void targetClicked(qint64 track_id);
    void viewChanged(const GeoPosition &center, int zoom);
    void mapError(const QString &message);

    private slots:
    void handlePageReady();
    void handleMapError(const QString &message);
    void handleMapWarning(const QString &message);

    private:
    QJsonObject createInitialState() const;
    static QJsonObject createUpdateObject(const OnlineMapUpdate &update);
    void handleRenderProcessTermination(int status, int exit_code);
    void showError(const QString &message);

    OnlineMapState render_state_;
    QStackedLayout *stacked_layout_ = nullptr;
    QWebEngineView *web_view_ = nullptr;
    QLabel *error_label_ = nullptr;
    MapWebBridge *bridge_ = nullptr;
    bool map_ready_ = false;
    int render_reload_attempts_ = 0;
};

} // namespace utms
