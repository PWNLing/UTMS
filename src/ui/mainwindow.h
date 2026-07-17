#pragma once

#include <QMainWindow>
#include <QVector>

class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QThread;

namespace utms {
class MapPanel;
class BottomStatusBar;
class StatisticsWidget;
class SystemMonitorWidget;
class TrackTableWidget;
class RtspController;
class VideoStreamWidget;
class UdpReceiver;
class HistoryController;
class HistoryQueryWidget;
enum class UdpStatus;
struct HistoryConfiguration;
struct HistoryExportRequest;
struct HistoryQuery;
struct HistoryQueryResult;
struct HistorySession;
struct RadarFrame;
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
    void refreshHistoryInfoRequested();
    void shutdownHistoryWorkerRequested();

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
    void updateCurrentFrame(const utms::RadarFrame &frame);

private:
    void setupUi();
    void setupHistoryController();
    void setupUdpWorker();
    void setupVideoController();
    void requestHistoryShutdown();
    void updateHistoryStatusLabel(const QString &detail, const QString &color);
    void completeShutdownIfReady();

    QSpinBox *port_spin_box_ = nullptr;
    QPushButton *start_button_ = nullptr;
    QPushButton *stop_button_ = nullptr;
    QLabel *config_status_label_ = nullptr;
    QComboBox *history_sampling_combo_box_ = nullptr;
    QSpinBox *history_retention_spin_box_ = nullptr;
    QLabel *history_status_label_ = nullptr;
    utms::HistoryQueryWidget *history_query_widget_ = nullptr;
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
    bool udp_listening_ = false;
    bool shutdown_started_ = false;
    bool udp_shutdown_complete_ = false;
    bool history_shutdown_requested_ = false;
    bool history_shutdown_complete_ = false;
    bool video_shutdown_complete_ = false;
    bool monitor_shutdown_complete_ = false;
};
