#pragma once

#include <memory>
#include <optional>

#include <QObject>

#include "history/HistoryStore.h"

class QTimer;

namespace utms {

class HistoryController : public QObject
{
    Q_OBJECT

public:
    explicit HistoryController(QObject *parent = nullptr);

public slots:
    void initialize(const QString &database_path);
    void startSession();
    void stopSession();
    void saveConfiguration(const utms::HistoryConfiguration &configuration);
    void retryPendingOperations();
    void shutdown();

signals:
    void configurationLoaded(const utms::HistoryConfiguration &configuration);
    void availabilityChanged(bool available, const QString &detail);
    void sessionActiveChanged(bool active, const QString &detail);
    void errorOccurred(const QString &message);
    void stopped();

private:
    bool initializeStore();
    void retryInitializationSteps();
    void tryStartSession();
    void tryStopSession();
    void trySaveConfiguration();
    void updateRetryTimer();
    bool hasPendingOperations() const;
    void reportError(const QString &message);

    QTimer *retry_timer_ = nullptr;
    std::unique_ptr<HistoryStore> store_;
    QString database_path_;
    std::optional<HistoryConfiguration> pending_configuration_;
    bool initialized_ = false;
    bool session_active_ = false;
    bool abandoned_session_recovery_pending_ = false;
    bool configuration_load_pending_ = false;
    bool session_start_pending_ = false;
    bool session_stop_pending_ = false;
    bool fallback_configuration_emitted_ = false;
    bool availability_announced_ = false;
    bool database_recovery_log_pending_ = false;
    bool shutting_down_ = false;
    QString last_error_message_;
};

} // namespace utms
