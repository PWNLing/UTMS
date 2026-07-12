#pragma once

#include <QMainWindow>

class QCloseEvent;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QThread;

namespace utms {
class UdpReceiver;
enum class UdpStatus;
struct RadarFrame;
}  // namespace utms

class MainWindow : public QMainWindow
{
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
    void updateTrackTable(const utms::RadarFrame &frame);

private:
    void setupUi();
    void setupUdpWorker();

    QSpinBox *port_spin_box_ = nullptr;
    QPushButton *start_button_ = nullptr;
    QPushButton *stop_button_ = nullptr;
    QLabel *status_label_ = nullptr;
    QTableWidget *track_table_ = nullptr;
    QThread *udp_thread_ = nullptr;
    utms::UdpReceiver *udp_receiver_ = nullptr;
    bool shutdown_started_ = false;
    bool shutdown_complete_ = false;
};
