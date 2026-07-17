#include "history/HistoryController.h"

#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QTimer>

namespace utms {
namespace {

constexpr int kDatabaseRetryIntervalMs = 5'000;

} // namespace

HistoryController::HistoryController(QObject *parent) : QObject(parent), retry_timer_(new QTimer(this))
{
    qRegisterMetaType<HistoryConfiguration>();
    retry_timer_->setInterval(kDatabaseRetryIntervalMs);
    connect(retry_timer_, &QTimer::timeout, this, &HistoryController::retryPendingOperations);
}

void HistoryController::initialize(const QString &database_path)
{
    if (initialized_ || shutting_down_) {
        return;
    }

    database_path_ = database_path;
    retryPendingOperations();
}

void HistoryController::startSession()
{
    if (shutting_down_) {
        return;
    }

    if (session_active_ && !session_stop_pending_) {
        return;
    }

    session_start_pending_ = true;
    retryPendingOperations();
}

void HistoryController::stopSession()
{
    if (shutting_down_) {
        return;
    }

    if (!session_active_ && session_start_pending_) {
        session_start_pending_ = false;
        updateRetryTimer();
        return;
    }
    if (!session_active_) {
        return;
    }

    session_stop_pending_ = true;
    retryPendingOperations();
}

void HistoryController::saveConfiguration(const HistoryConfiguration &configuration)
{
    if (shutting_down_) {
        return;
    }

    pending_configuration_ = configuration;
    retryPendingOperations();
}

void HistoryController::retryPendingOperations()
{
    if (shutting_down_) {
        return;
    }

    if (!initialized_ && !initializeStore()) {
        updateRetryTimer();
        return;
    }

    retryInitializationSteps();
    if (session_stop_pending_) {
        tryStopSession();
    }
    if (!abandoned_session_recovery_pending_ && !session_stop_pending_ && session_start_pending_) {
        tryStartSession();
    }
    if (pending_configuration_.has_value()) {
        trySaveConfiguration();
    }
    updateRetryTimer();
}

void HistoryController::shutdown()
{
    shutting_down_ = true;
    retry_timer_->stop();
    session_start_pending_ = false;
    if (session_active_) {
        session_stop_pending_ = true;
        tryStopSession();
    }
    if (pending_configuration_.has_value()) {
        trySaveConfiguration();
    }

    store_.reset();
    initialized_ = false;
    emit stopped();
    QThread::currentThread()->quit();
}

bool HistoryController::initializeStore()
{
    if (database_path_.isEmpty()) {
        return false;
    }

    auto store = std::make_unique<HistoryStore>(database_path_);
    QString error;
    if (!store->initialize(&error)) {
        database_recovery_log_pending_ = true;
        reportError(error);
        if (!fallback_configuration_emitted_) {
            fallback_configuration_emitted_ = true;
            emit configurationLoaded(HistoryConfiguration{});
        }
        emit availabilityChanged(false, error);
        return false;
    }

    store_ = std::move(store);
    initialized_ = true;
    abandoned_session_recovery_pending_ = true;
    configuration_load_pending_ = true;
    if (database_recovery_log_pending_) {
        qInfo() << "HistoryController: database recovered after retry" << database_path_;
        database_recovery_log_pending_ = false;
    } else {
        qInfo() << "HistoryController: initialized database" << database_path_;
    }
    return true;
}

void HistoryController::retryInitializationSteps()
{
    if (store_ == nullptr) {
        return;
    }

    QString error;
    if (abandoned_session_recovery_pending_) {
        const std::optional<int> recovered_count =
            store_->recoverAbandonedSessions(QDateTime::currentDateTimeUtc(), &error);
        if (!recovered_count.has_value()) {
            reportError(error);
        } else {
            abandoned_session_recovery_pending_ = false;
            if (recovered_count.value() > 0) {
                qInfo() << "HistoryController: marked abandoned sessions as abnormal" << recovered_count.value();
            }
        }
    }

    if (configuration_load_pending_) {
        const std::optional<HistoryConfiguration> configuration = store_->loadConfiguration(&error);
        if (!configuration.has_value()) {
            reportError(error);
            if (!fallback_configuration_emitted_) {
                fallback_configuration_emitted_ = true;
                emit configurationLoaded(HistoryConfiguration{});
            }
        } else {
            configuration_load_pending_ = false;
            emit configurationLoaded(configuration.value());
        }
    }

    if (!abandoned_session_recovery_pending_ && !configuration_load_pending_ && !availability_announced_) {
        availability_announced_ = true;
        last_error_message_.clear();
        emit availabilityChanged(true, tr("历史数据库已就绪"));
    }
}

void HistoryController::tryStartSession()
{
    if (!initialized_ || store_ == nullptr || session_active_) {
        return;
    }

    QString error;
    const std::optional<qint64> session_id = store_->startSession(QDateTime::currentDateTimeUtc(), &error);
    if (!session_id.has_value()) {
        reportError(error);
        return;
    }

    session_start_pending_ = false;
    session_active_ = true;
    last_error_message_.clear();
    const QString detail = tr("历史会话 %1 记录中").arg(session_id.value());
    qInfo() << "HistoryController: started session" << session_id.value();
    emit sessionActiveChanged(true, detail);
}

void HistoryController::tryStopSession()
{
    if (!session_active_) {
        session_stop_pending_ = false;
        return;
    }
    if (!initialized_ || store_ == nullptr) {
        reportError(tr("历史数据库不可用，无法正常关闭活动会话"));
        return;
    }

    QString error;
    if (!store_->closeActiveSession(QDateTime::currentDateTimeUtc(), &error)) {
        reportError(error);
        return;
    }

    session_stop_pending_ = false;
    session_active_ = false;
    last_error_message_.clear();
    qInfo() << "HistoryController: closed active session";
    emit sessionActiveChanged(false, tr("历史会话已关闭"));
}

void HistoryController::trySaveConfiguration()
{
    if (!initialized_ || store_ == nullptr || !pending_configuration_.has_value()) {
        return;
    }

    const HistoryConfiguration configuration = pending_configuration_.value();
    QString error;
    if (!store_->saveConfiguration(configuration, &error)) {
        reportError(error);
        return;
    }

    pending_configuration_.reset();
    last_error_message_.clear();
    emit configurationLoaded(configuration);
    emit availabilityChanged(true, tr("历史配置已保存"));
    qInfo() << "HistoryController: saved sampling and retention configuration";
}

void HistoryController::updateRetryTimer()
{
    if (shutting_down_ || !hasPendingOperations()) {
        retry_timer_->stop();
        return;
    }
    if (!retry_timer_->isActive()) {
        retry_timer_->start();
    }
}

bool HistoryController::hasPendingOperations() const
{
    return (!initialized_ && !database_path_.isEmpty()) || abandoned_session_recovery_pending_ ||
           configuration_load_pending_ || session_start_pending_ || session_stop_pending_ ||
           pending_configuration_.has_value();
}

void HistoryController::reportError(const QString &message)
{
    if (last_error_message_ == message) {
        return;
    }
    last_error_message_ = message;
    qWarning() << "HistoryController:" << message;
    emit errorOccurred(message);
}

} // namespace utms
