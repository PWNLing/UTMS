#pragma once

#include <array>

#include <QWidget>

class QLabel;
class QTimer;

namespace utms {

enum class UdpStatus;
struct TargetStatistics;

class BottomStatusBar : public QWidget {
public:
    explicit BottomStatusBar(QWidget *parent = nullptr);

    void updateStatistics(const TargetStatistics &statistics);
    void setUdpStatus(UdpStatus status);

private:
    void updateSimulatedFrameRate();

    std::array<QLabel *, 5> count_labels_{};
    QLabel *udp_status_label_ = nullptr;
    QLabel *frame_rate_label_ = nullptr;
    QTimer *frame_rate_timer_ = nullptr;
};

} // namespace utms
