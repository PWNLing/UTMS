#include "history/HistoryController.h"

#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QTimer>

namespace utms {
namespace {

constexpr int kDatabaseRetryIntervalMs = 5'000;
constexpr int kMaintenanceIntervalMs = 24 * 60 * 60 * 1'000;
constexpr int kWriteBatchIntervalMs = 100;
constexpr int kMaximumWriteBatchSize = 32;

} // namespace

HistoryController::HistoryController(QObject *parent)
    : QObject(parent), retry_timer_(new QTimer(this)), maintenance_timer_(new QTimer(this)),
      sampling_timer_(new QTimer(this)), write_batch_timer_(new QTimer(this)) {
    qRegisterMetaType<HistoryConfiguration>();
    qRegisterMetaType<HistoryQuery>();
    qRegisterMetaType<HistoryQueryResult>();
    qRegisterMetaType<HistoryExportRequest>();
    qRegisterMetaType<QVector<HistorySession>>();
    retry_timer_->setInterval(kDatabaseRetryIntervalMs);
    connect(retry_timer_, &QTimer::timeout, this, &HistoryController::retryPendingOperations);
    maintenance_timer_->setInterval(kMaintenanceIntervalMs);
    connect(maintenance_timer_, &QTimer::timeout, this, &HistoryController::cleanupExpiredHistory);
    sampling_timer_->setTimerType(Qt::PreciseTimer);
    connect(sampling_timer_, &QTimer::timeout, this, &HistoryController::queueLatestSample);
    write_batch_timer_->setInterval(kWriteBatchIntervalMs);
    write_batch_timer_->setSingleShot(true);
    connect(write_batch_timer_, &QTimer::timeout, this, [this]() { flushPendingWrites(); });
}

void HistoryController::initialize(const QString &database_path) {
    if (initialized_ || shutting_down_) {
        return;
    }

    database_path_ = database_path;
    retryPendingOperations();
}

void HistoryController::startSession() {
    if (shutting_down_) {
        return;
    }

    if (session_active_ && !session_stop_pending_) {
        return;
    }

    session_start_pending_ = true;
    retryPendingOperations();
}

void HistoryController::stopSession() {
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

void HistoryController::recordAcceptedFrame(const RadarFrame &frame) {
    if (shutting_down_ || !session_active_ || !active_session_id_.has_value() || recording_failed_ ||
        store_ == nullptr) {
        return;
    }

    const std::optional<int> interval_ms = sampling_policy_.intervalMs(configuration_.sampling_rate);
    if (!interval_ms.has_value()) {
        return;
    }
    if (interval_ms.value() == 0) {
        queueFrameForWrite(frame);
        return;
    }
    latest_sample_frame_ = frame;
}

void HistoryController::saveConfiguration(const HistoryConfiguration &configuration) {
    if (shutting_down_) {
        return;
    }

    pending_configuration_ = configuration;
    retryPendingOperations();
}

void HistoryController::queryHistory(const HistoryQuery &query) {
    if (shutting_down_ || store_ == nullptr) {
        reportError(tr("历史数据库不可用，无法执行查询"));
        return;
    }

    if (!flushPendingWrites()) {
        return;
    }

    QString error;
    const std::optional<HistoryQueryResult> result = store_->queryHistory(query, &error);
    if (!result.has_value()) {
        reportError(error);
        return;
    }
    emit queryCompleted(result.value());
}

void HistoryController::exportHistoryCsv(const HistoryExportRequest &request) {
    if (shutting_down_ || store_ == nullptr) {
        reportError(tr("历史数据库不可用，无法导出 CSV"));
        return;
    }

    if (!flushPendingWrites()) {
        return;
    }

    QString error;
    const std::optional<int> record_count =
        store_->exportCsv(request.query, request.selected_track_id, request.output_path, &error);
    if (!record_count.has_value()) {
        reportError(error);
        return;
    }

    qInfo() << "HistoryController: exported history CSV" << request.output_path << record_count.value();
    emit exportCompleted(request.output_path, record_count.value());
}

void HistoryController::deleteSession(qint64 session_id) {
    if (shutting_down_ || store_ == nullptr) {
        reportError(tr("历史数据库不可用，无法删除会话"));
        return;
    }

    QString error;
    if (!store_->deleteSession(session_id, &error)) {
        reportError(error);
        return;
    }

    qInfo() << "HistoryController: deleted history session" << session_id;
    emit sessionDeleted(session_id);
    refreshHistoryInfo();
}

void HistoryController::deleteAllSessions() {
    if (shutting_down_ || store_ == nullptr) {
        reportError(tr("历史数据库不可用，无法删除全部会话"));
        return;
    }

    QString error;
    const std::optional<int> deleted_count = store_->deleteAllSessions(&error);
    if (!deleted_count.has_value()) {
        reportError(error);
        return;
    }

    qInfo() << "HistoryController: deleted all history sessions" << deleted_count.value();
    emit allSessionsDeleted(deleted_count.value());
    refreshHistoryInfo();
}

void HistoryController::refreshHistoryInfo() {
    if (shutting_down_ || store_ == nullptr) {
        return;
    }

    QString error;
    const std::optional<QVector<HistorySession>> sessions = store_->loadSessions(&error);
    if (!sessions.has_value()) {
        reportError(error);
        return;
    }
    emit sessionsLoaded(sessions.value());
    emit databaseSizeChanged(store_->databaseSizeBytes());
}

void HistoryController::cleanupExpiredHistory() {
    if (shutting_down_) {
        return;
    }
    cleanup_pending_ = true;
    retryPendingOperations();
}

void HistoryController::retryPendingOperations() {
    if (shutting_down_) {
        return;
    }

    if (!initialized_ && !initializeStore()) {
        updateRetryTimer();
        return;
    }

    retryInitializationSteps();
    if (recording_failed_) {
        tryRecoverRecording();
    }
    if (session_stop_pending_) {
        tryStopSession();
    }
    if (!abandoned_session_recovery_pending_ && !session_stop_pending_ && session_start_pending_) {
        tryStartSession();
    }
    if (pending_configuration_.has_value()) {
        trySaveConfiguration();
    }
    if (cleanup_pending_) {
        tryCleanupExpiredHistory();
    }
    updateRetryTimer();
}

void HistoryController::shutdown() {
    shutting_down_ = true;
    retry_timer_->stop();
    maintenance_timer_->stop();
    sampling_timer_->stop();
    write_batch_timer_->stop();
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

bool HistoryController::initializeStore() {
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
    if (!maintenance_timer_->isActive()) {
        maintenance_timer_->start();
    }
    if (database_recovery_log_pending_) {
        qInfo() << "HistoryController: database recovered after retry" << database_path_;
        database_recovery_log_pending_ = false;
    } else {
        qInfo() << "HistoryController: initialized database" << database_path_;
    }
    return true;
}

void HistoryController::retryInitializationSteps() {
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
            configuration_ = configuration.value();
            cleanup_pending_ = true;
            emit configurationLoaded(configuration_);
        }
    }

    if (!abandoned_session_recovery_pending_ && !configuration_load_pending_ && !availability_announced_) {
        availability_announced_ = true;
        last_error_message_.clear();
        emit availabilityChanged(true, tr("历史数据库已就绪"));
        refreshHistoryInfo();
    }
}

void HistoryController::tryStartSession() {
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
    active_session_id_ = session_id;
    latest_sample_frame_.reset();
    pending_write_frames_.clear();
    configureSamplingTimer();
    last_error_message_.clear();
    const QString detail = tr("历史会话 %1 记录中").arg(session_id.value());
    qInfo() << "HistoryController: started session" << session_id.value();
    emit sessionActiveChanged(true, detail);
    refreshHistoryInfo();
}

void HistoryController::tryStopSession() {
    if (!session_active_) {
        session_stop_pending_ = false;
        return;
    }
    if (!initialized_ || store_ == nullptr) {
        reportError(tr("历史数据库不可用，无法正常关闭活动会话"));
        return;
    }

    sampling_timer_->stop();
    if (!recording_failed_) {
        queueLatestSample();
        if (!flushPendingWrites()) {
            return;
        }
    }

    QString error;
    if (!store_->closeActiveSession(QDateTime::currentDateTimeUtc(), &error)) {
        reportError(error);
        return;
    }

    session_stop_pending_ = false;
    session_active_ = false;
    active_session_id_.reset();
    latest_sample_frame_.reset();
    pending_write_frames_.clear();
    write_batch_timer_->stop();
    last_error_message_.clear();
    qInfo() << "HistoryController: closed active session";
    emit sessionActiveChanged(false, tr("历史会话已关闭"));
    refreshHistoryInfo();
}

void HistoryController::trySaveConfiguration() {
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
    configuration_ = configuration;
    cleanup_pending_ = true;
    configureSamplingTimer();
    last_error_message_.clear();
    emit configurationLoaded(configuration);
    emit availabilityChanged(true, tr("历史配置已保存"));
    qInfo() << "HistoryController: saved sampling and retention configuration";
}

void HistoryController::tryRecoverRecording() {
    if (!recording_failed_ || store_ == nullptr) {
        return;
    }

    QString error;
    if (!store_->probeWriteAccess(&error)) {
        reportError(error);
        return;
    }

    recording_failed_ = false;
    configureSamplingTimer();
    last_error_message_.clear();
    qInfo() << "HistoryController: history recording recovered";
    emit availabilityChanged(true, tr("历史记录已恢复"));
}

void HistoryController::tryCleanupExpiredHistory() {
    if (!cleanup_pending_ || store_ == nullptr || configuration_load_pending_) {
        return;
    }

    QString error;
    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addDays(-configuration_.retention_days);
    const std::optional<int> deleted_count = store_->cleanupExpiredHistory(cutoff, &error);
    if (!deleted_count.has_value()) {
        reportError(error);
        return;
    }

    cleanup_pending_ = false;
    if (deleted_count.value() > 0) {
        qInfo() << "HistoryController: cleaned expired sessions" << deleted_count.value();
    }
    refreshHistoryInfo();
}

void HistoryController::configureSamplingTimer() {
    sampling_timer_->stop();
    latest_sample_frame_.reset();
    if (!session_active_ || recording_failed_) {
        return;
    }

    const std::optional<int> interval_ms = sampling_policy_.intervalMs(configuration_.sampling_rate);
    if (interval_ms.has_value() && interval_ms.value() > 0) {
        sampling_timer_->start(interval_ms.value());
    }
}

void HistoryController::queueLatestSample() {
    if (!latest_sample_frame_.has_value() || recording_failed_ || !session_active_) {
        return;
    }

    queueFrameForWrite(latest_sample_frame_.value());
    latest_sample_frame_.reset();
}

void HistoryController::queueFrameForWrite(const RadarFrame &frame) {
    pending_write_frames_.append(frame);
    if (pending_write_frames_.size() >= kMaximumWriteBatchSize) {
        if (!flushPendingWrites()) {
            return;
        }
        return;
    }
    if (!write_batch_timer_->isActive()) {
        write_batch_timer_->start();
    }
}

bool HistoryController::flushPendingWrites() {
    write_batch_timer_->stop();
    if (pending_write_frames_.isEmpty()) {
        return true;
    }
    if (store_ == nullptr || !active_session_id_.has_value() || recording_failed_) {
        pending_write_frames_.clear();
        return false;
    }

    QVector<RadarFrame> frames;
    frames.swap(pending_write_frames_);
    QString error;
    if (store_->appendFrames(active_session_id_.value(), frames, &error)) {
        return true;
    }

    enterRecordingFailure(error);
    return false;
}

void HistoryController::enterRecordingFailure(const QString &error) {
    recording_failed_ = true;
    sampling_timer_->stop();
    write_batch_timer_->stop();
    latest_sample_frame_.reset();
    pending_write_frames_.clear();
    // 故障期间的快照会被主动丢弃；恢复探测只开启后续采样，绝不补写旧帧。
    reportError(error);
    emit availabilityChanged(false, tr("记录失败：%1").arg(error));
    updateRetryTimer();
}

void HistoryController::updateRetryTimer() {
    if (shutting_down_ || !hasPendingOperations()) {
        retry_timer_->stop();
        return;
    }
    if (!retry_timer_->isActive()) {
        retry_timer_->start();
    }
}

bool HistoryController::hasPendingOperations() const {
    return (!initialized_ && !database_path_.isEmpty()) || abandoned_session_recovery_pending_ ||
           configuration_load_pending_ || session_start_pending_ || session_stop_pending_ ||
           pending_configuration_.has_value() || recording_failed_ || cleanup_pending_;
}

void HistoryController::reportError(const QString &message) {
    if (last_error_message_ == message) {
        return;
    }
    last_error_message_ = message;
    qWarning() << "HistoryController:" << message;
    emit errorOccurred(message);
}

} // namespace utms
