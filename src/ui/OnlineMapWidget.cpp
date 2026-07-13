#include "ui/OnlineMapWidget.h"

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
    connect(bridge_, &MapWebBridge::viewChanged, this, &OnlineMapWidget::handleViewChanged);
    connect(bridge_, &MapWebBridge::targetClicked, this, &OnlineMapWidget::targetClicked);
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
            {
                handleRenderProcessTermination(static_cast<int>(status), exit_code);
            });

    stacked_layout_->setCurrentWidget(web_view_);
    web_view_->load(QUrl(QStringLiteral("qrc:/map/online_map.html")));
}

void OnlineMapWidget::setFrame(const RadarFrame &frame)
{
    const OnlineMapUpdate update = state_.replaceFrame(frame);
    if (map_ready_)
    {
        emit bridge_->mapUpdateAvailable(createUpdateObject(update));
    }
}

void OnlineMapWidget::setLayer(OnlineMapLayer layer)
{
    state_.setLayer(layer);
    if (map_ready_)
    {
        emit bridge_->layerUpdated(layer == OnlineMapLayer::kStreet ? QStringLiteral("street")
                                                                    : QStringLiteral("satellite"));
    }
}

bool OnlineMapWidget::locateRadar()
{
    if (!state_.locateRadar())
    {
        return false;
    }
    const GeoPosition center = state_.center();
    if (map_ready_)
    {
        emit bridge_->centerUpdated(center.longitude, center.latitude);
    }
    return true;
}

void OnlineMapWidget::handlePageReady()
{
    map_ready_ = true;
    stacked_layout_->setCurrentWidget(web_view_);
    emit bridge_->initialStateAvailable(createInitialState());

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

void OnlineMapWidget::handleViewChanged(double longitude, double latitude, int zoom)
{
    state_.setCenter({latitude, longitude});
    state_.setZoom(zoom);
}

QJsonObject OnlineMapWidget::createInitialState() const
{
    QJsonArray targets;
    for (const OnlineMapTarget &target : state_.currentTargets())
    {
        targets.append(targetObject(target));
    }

    QJsonObject state{{QStringLiteral("center"), positionObject(state_.center())},
                      {QStringLiteral("zoom"), state_.zoom()},
                      {QStringLiteral("layer"), state_.layer() == OnlineMapLayer::kStreet
                                                    ? QStringLiteral("street")
                                                    : QStringLiteral("satellite")},
                      {QStringLiteral("targets"), targets}};
    if (state_.radarPosition().has_value())
    {
        state.insert(QStringLiteral("radar"), positionObject(state_.radarPosition().value()));
    }
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

void OnlineMapWidget::showError(const QString &message)
{
    map_ready_ = false;
    error_label_->setText(message);
    stacked_layout_->setCurrentWidget(error_label_);
}

} // namespace utms
