#pragma once

#include <QMainWindow>

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
class TrackTableWidget;
class UdpReceiver;
enum class UdpStatus;
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

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void handleUdpStatusChanged(utms::UdpStatus status, const QString &detail);
    void handleUdpWorkerStopped();
    void updateCurrentFrame(const utms::RadarFrame &frame);

private:
    void setupUi();
    void setupUdpWorker();

    QSpinBox *port_spin_box_ = nullptr;
    QPushButton *start_button_ = nullptr;
    QPushButton *stop_button_ = nullptr;
    QLabel *config_status_label_ = nullptr;
    utms::TrackTableWidget *track_table_ = nullptr;
    utms::MapPanel *map_panel_ = nullptr;
    QComboBox *map_mode_combo_box_ = nullptr;
    QComboBox *map_layer_combo_box_ = nullptr;
    QDoubleSpinBox *longitude_spin_box_ = nullptr;
    QDoubleSpinBox *latitude_spin_box_ = nullptr;
    QPushButton *locate_radar_button_ = nullptr;
    utms::StatisticsWidget *statistics_widget_ = nullptr;
    utms::BottomStatusBar *bottom_status_bar_ = nullptr;
    QThread *udp_thread_ = nullptr;
    utms::UdpReceiver *udp_receiver_ = nullptr;
    bool shutdown_started_ = false;
    bool shutdown_complete_ = false;
};
