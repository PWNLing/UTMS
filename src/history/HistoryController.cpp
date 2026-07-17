#include "history/HistoryController.h"

#include <QDateTime>
#include <QDebug>
#include <QThread>

namespace utms {

HistoryController::HistoryController(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<HistoryConfiguration>();
}

void HistoryController::initialize(const QString &database_path)
{
    if (initialized_) {
        return;
    }

    auto store = std::make_unique<HistoryStore>(database_path);
    QString error;
    if (!store->initialize(&error)) {
        reportError(error);
        emit configurationLoaded(HistoryConfiguration{});
        emit availabilityChanged(false, error);
        return;
    }

    initialized_ = true;
    store_ = std::move(store);

    bool degraded = false;
    if (!store_->recoverAbandonedSessions(QDateTime::currentDateTimeUtc(), &error)) {
        reportError(error);
        degraded = true;
    }

    const std::optional<HistoryConfiguration> configuration = store_->loadConfiguration(&error);
    if (!configuration.has_value()) {
        reportError(error);
        emit configurationLoaded(HistoryConfiguration{});
        emit availabilityChanged(false, error);
        return;
    }

    emit configurationLoaded(configuration.value());
    if (!degraded) {
        emit availabilityChanged(true, tr("历史数据库已就绪"));
    }
    qInfo() << "HistoryController: initialized database" << database_path;
}

void HistoryController::startSession()
{
    if (!initialized_ || store_ == nullptr) {
        reportError(tr("历史数据库不可用，未创建本次监听会话"));
        return;
    }
    if (session_active_) {
        return;
    }

    QString error;
    const std::optional<qint64> session_id = store_->startSession(QDateTime::currentDateTimeUtc(), &error);
    if (!session_id.has_value()) {
        reportError(error);
        return;
    }

    session_active_ = true;
    const QString detail = tr("历史会话 %1 记录中").arg(session_id.value());
    qInfo() << "HistoryController: started session" << session_id.value();
    emit sessionActiveChanged(true, detail);
}

void HistoryController::stopSession()
{
    if (!session_active_) {
        return;
    }
    if (!initialized_ || store_ == nullptr) {
        session_active_ = false;
        reportError(tr("历史数据库不可用，无法正常关闭活动会话"));
        return;
    }

    QString error;
    if (!store_->closeActiveSession(QDateTime::currentDateTimeUtc(), &error)) {
        reportError(error);
        return;
    }

    session_active_ = false;
    qInfo() << "HistoryController: closed active session";
    emit sessionActiveChanged(false, tr("历史会话已关闭"));
}

void HistoryController::saveConfiguration(const HistoryConfiguration &configuration)
{
    if (!initialized_ || store_ == nullptr) {
        reportError(tr("历史数据库不可用，配置未保存"));
        return;
    }

    QString error;
    if (!store_->saveConfiguration(configuration, &error)) {
        reportError(error);
        return;
    }

    emit configurationLoaded(configuration);
    emit availabilityChanged(true, tr("历史配置已保存"));
    qInfo() << "HistoryController: saved sampling and retention configuration";
}

void HistoryController::shutdown()
{
    stopSession();
    store_.reset();
    initialized_ = false;
    emit stopped();
    QThread::currentThread()->quit();
}

void HistoryController::reportError(const QString &message)
{
    qWarning() << "HistoryController:" << message;
    emit errorOccurred(message);
}

} // namespace utms
