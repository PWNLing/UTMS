#pragma once

#include <memory>
#include <optional>

#include <QObject>

#include "history/HistorySamplingPolicy.h"
#include "history/HistoryStore.h"

class QTimer;

namespace utms {

class HistoryController : public QObject {
    Q_OBJECT

  public:
    explicit HistoryController(QObject *parent = nullptr);

  public slots:
    void initialize(const QString &database_path);
    void startSession();
    void stopSession();
    void recordAcceptedFrame(const utms::RadarFrame &frame);
    void saveConfiguration(const utms::HistoryConfiguration &configuration);
    void queryHistory(const utms::HistoryQuery &query);
    void exportHistoryCsv(const utms::HistoryExportRequest &request);
    void deleteSession(qint64 session_id);
    void deleteAllSessions();
    void refreshGeofences();
    void createGeofence(const utms::Geofence &geofence);
    void updateGeofence(const utms::Geofence &geofence);
    void updateGeofenceGeometry(const utms::Geofence &geofence);
    void setGeofenceEnabled(qint64 geofence_id, bool enabled);
    void setGeofenceVisible(qint64 geofence_id, bool visible);
    void deleteGeofence(qint64 geofence_id);
    void refreshAlertRules();
    void createAlertRule(const utms::AlertRule &rule);
    void updateAlertRule(const utms::AlertRule &rule);
    void setAlertRuleEnabled(qint64 rule_id, bool enabled);
    void deleteAlertRule(qint64 rule_id);
    void persistTargetAlert(const utms::TargetAlert &alert);
    void refreshHistoryInfo();
    void cleanupExpiredHistory();
    void retryPendingOperations();
    void shutdown();

  signals:
    void configurationLoaded(const utms::HistoryConfiguration &configuration);
    void availabilityChanged(bool available, const QString &detail);
    void sessionActiveChanged(bool active, const QString &detail);
    void sessionsLoaded(const QVector<utms::HistorySession> &sessions);
    void queryCompleted(const utms::HistoryQueryResult &result);
    void exportCompleted(const QString &output_path, int record_count);
    void sessionDeleted(qint64 session_id);
    void allSessionsDeleted(int session_count);
    void geofencesLoaded(const QVector<utms::Geofence> &geofences);
    void geofenceErrorOccurred(const QString &message);
    void alertRulesLoaded(const QVector<utms::AlertRule> &rules);
    void alertRuleErrorOccurred(const QString &message);
    void targetAlertPersisted(qint64 alert_id);
    void targetAlertPersistenceFailed(const QString &message);
    void databaseSizeChanged(qint64 size_bytes);
    void errorOccurred(const QString &message);
    void stopped();

  private:
    bool initializeStore();
    void retryInitializationSteps();
    void tryStartSession();
    void tryStopSession();
    void trySaveConfiguration();
    void tryRecoverRecording();
    void tryCleanupExpiredHistory();
    void configureSamplingTimer();
    void queueLatestSample();
    void queueFrameForWrite(const RadarFrame &frame);
    bool flushPendingWrites();
    void enterRecordingFailure(const QString &error);
    void updateRetryTimer();
    bool hasPendingOperations() const;
    void reportError(const QString &message);

    QTimer *retry_timer_ = nullptr;
    QTimer *maintenance_timer_ = nullptr;
    QTimer *sampling_timer_ = nullptr;
    QTimer *write_batch_timer_ = nullptr;
    std::unique_ptr<HistoryStore> store_;
    QString database_path_;
    std::optional<HistoryConfiguration> pending_configuration_;
    HistoryConfiguration configuration_;
    HistorySamplingPolicy sampling_policy_;
    std::optional<qint64> active_session_id_;
    std::optional<RadarFrame> latest_sample_frame_;
    QVector<RadarFrame> pending_write_frames_;
    bool initialized_ = false;
    bool session_active_ = false;
    bool recording_failed_ = false;
    bool cleanup_pending_ = false;
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
