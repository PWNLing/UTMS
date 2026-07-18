#pragma once

#include <optional>

#include <QMainWindow>
#include <QVector>

#include "alert/AlertTypes.h"
#include "core/GeofenceTypes.h"
#include "core/RadarTypes.h"

class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QThread;

namespace utms {
class MapPanel;
class AlertNotificationWidget;
class AlertRuleManagerWidget;
class AlertWorker;
class BottomStatusBar;
class StatisticsWidget;
class SystemMonitorWidget;
class TrackTableWidget;
class RtspController;
class VideoStreamWidget;
class UdpReceiver;
class HistoryController;
class HistoryPlaybackController;
class HistoryQueryWidget;
class GeofenceManagerWidget;
enum class UdpStatus;
struct HistoryConfiguration;
struct HistoryExportRequest;
struct HistoryQuery;
struct HistoryQueryResult;
struct HistorySession;
} // namespace utms

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

  signals:
    void startListeningRequested(quint16 port);
    void stopListeningRequested();
    void shutdownUdpWorkerRequested();
    void startHistorySessionRequested();
    void stopHistorySessionRequested();
    void saveHistoryConfigurationRequested(const utms::HistoryConfiguration &configuration);
    void queryHistoryRequested(const utms::HistoryQuery &query);
    void exportHistoryRequested(const utms::HistoryExportRequest &request);
    void deleteHistorySessionRequested(qint64 session_id);
    void deleteAllHistorySessionsRequested();
    void refreshHistoryInfoRequested();
    void createGeofenceRequested(const utms::Geofence &geofence);
    void updateGeofenceRequested(const utms::Geofence &geofence);
    void updateGeofenceGeometryRequested(const utms::Geofence &geofence);
    void setGeofenceEnabledRequested(qint64 geofence_id, bool enabled);
    void setGeofenceVisibleRequested(qint64 geofence_id, bool visible);
    void deleteGeofenceRequested(qint64 geofence_id);
    void createAlertRuleRequested(const utms::AlertRule &rule);
    void updateAlertRuleRequested(const utms::AlertRule &rule);
    void setAlertRuleEnabledRequested(qint64 rule_id, bool enabled);
    void deleteAlertRuleRequested(qint64 rule_id);
    void clearAlertStateRequested();
    void shutdownHistoryWorkerRequested();
    void shutdownAlertWorkerRequested();

  protected:
    void closeEvent(QCloseEvent *event) override;

  private slots:
    void handleUdpStatusChanged(utms::UdpStatus status, const QString &detail);
    void handleUdpWorkerStopped();
    void handleVideoWorkerStopped();
    void handleSystemMonitorStopped();
    void handleHistoryConfigurationLoaded(const utms::HistoryConfiguration &configuration);
    void handleHistoryAvailabilityChanged(bool available, const QString &detail);
    void handleHistorySessionActiveChanged(bool active, const QString &detail);
    void handleHistoryError(const QString &message);
    void handleHistorySessionsLoaded(const QVector<utms::HistorySession> &sessions);
    void handleHistoryQueryCompleted(const utms::HistoryQueryResult &result);
    void handleHistoryExportCompleted(const QString &output_path, int record_count);
    void handleHistoryDatabaseSizeChanged(qint64 size_bytes);
    void handleGeofencesLoaded(const QVector<utms::Geofence> &geofences);
    void handleGeofenceError(const QString &message);
    void handleAlertRulesLoaded(const QVector<utms::AlertRule> &rules);
    void handleAlertRuleError(const QString &message);
    void handleTargetAlert(const utms::TargetAlert &alert);
    void handleAlertWorkerStopped();
    void handleReplayModeChanged(bool replay_mode);
    void handlePlaybackStateChanged(bool playing);
    void handlePlaybackFrameChanged(const utms::RadarFrame &frame, int frame_index, int frame_count,
                                    const QDateTime &frame_time);
    void updateCurrentFrame(const utms::RadarFrame &frame);

  private:
    void setupUi();
    void setupPlaybackController();
    void setupHistoryController();
    void setupAlertWorker();
    void setupUdpWorker();
    void setupVideoController();
    void requestHistoryShutdown();
    void requestAlertShutdown();
    void updateHistoryStatusLabel(const QString &detail, const QString &color);
    void updateNonMapDisplays(const utms::RadarFrame &frame);
    void selectReplayTrack(qint64 track_id);
    void updateSevereAlertNotification();
    void completeShutdownIfReady();

    QSpinBox *port_spin_box_ = nullptr;
    QPushButton *start_button_ = nullptr;
    QPushButton *stop_button_ = nullptr;
    QLabel *config_status_label_ = nullptr;
    QComboBox *history_sampling_combo_box_ = nullptr;
    QSpinBox *history_retention_spin_box_ = nullptr;
    QLabel *history_status_label_ = nullptr;
    utms::HistoryQueryWidget *history_query_widget_ = nullptr;
    utms::GeofenceManagerWidget *geofence_manager_widget_ = nullptr;
    utms::AlertRuleManagerWidget *alert_rule_manager_widget_ = nullptr;
    utms::AlertNotificationWidget *alert_notification_widget_ = nullptr;
    utms::TrackTableWidget *track_table_ = nullptr;
    utms::MapPanel *map_panel_ = nullptr;
    QComboBox *map_mode_combo_box_ = nullptr;
    QComboBox *map_layer_combo_box_ = nullptr;
    QDoubleSpinBox *longitude_spin_box_ = nullptr;
    QDoubleSpinBox *latitude_spin_box_ = nullptr;
    QPushButton *locate_radar_button_ = nullptr;
    utms::StatisticsWidget *statistics_widget_ = nullptr;
    utms::SystemMonitorWidget *system_monitor_widget_ = nullptr;
    utms::BottomStatusBar *bottom_status_bar_ = nullptr;
    utms::VideoStreamWidget *video_stream_widget_ = nullptr;
    utms::RtspController *rtsp_controller_ = nullptr;
    QThread *udp_thread_ = nullptr;
    utms::UdpReceiver *udp_receiver_ = nullptr;
    QThread *history_thread_ = nullptr;
    utms::HistoryController *history_controller_ = nullptr;
    QThread *alert_thread_ = nullptr;
    utms::AlertWorker *alert_worker_ = nullptr;
    utms::HistoryPlaybackController *playback_controller_ = nullptr;
    QVector<utms::TargetAlert> active_severe_alerts_;
    int active_severe_alert_count_ = 0;
    std::optional<utms::RadarFrame> latest_live_frame_;
    bool udp_listening_ = false;
    bool replay_mode_ = false;
    bool shutdown_started_ = false;
    bool udp_shutdown_complete_ = false;
    bool history_shutdown_requested_ = false;
    bool history_shutdown_complete_ = false;
    bool alert_shutdown_requested_ = false;
    bool alert_shutdown_complete_ = false;
    bool video_shutdown_complete_ = false;
    bool monitor_shutdown_complete_ = false;
};
