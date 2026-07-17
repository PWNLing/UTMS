#include "ui/BottomStatusBar.h"

#include <array>

#include <QHBoxLayout>
#include <QLabel>
#include <QRandomGenerator>
#include <QTimer>

#include "core/RadarTypes.h"
#include "network/UdpReceiver.h"

namespace utms {

BottomStatusBar::BottomStatusBar(QWidget *parent) : QWidget(parent)
{
    setFixedHeight(38);
    setObjectName(QStringLiteral("bottomStatusBar"));
    setStyleSheet(QStringLiteral("#bottomStatusBar { border-top: 1px solid #d7d7d7; }"));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 3, 8, 3);
    layout->setSpacing(6);

    for (std::size_t index = 0; index < kTargetTypes.size(); ++index) {
        count_labels_[index] = new QLabel(this);
        count_labels_[index]->setStyleSheet(
            QStringLiteral("QLabel { color: %1; font-weight: 600; }").arg(targetTypeColorName(kTargetTypes[index])));
        layout->addWidget(count_labels_[index]);
        if (index + 1 < kTargetTypes.size()) {
            layout->addWidget(new QLabel(QStringLiteral("|"), this));
        }
    }

    layout->addStretch();
    display_mode_label_ = new QLabel(this);
    udp_status_label_ = new QLabel(this);
    frame_rate_label_ = new QLabel(this);
    layout->addWidget(display_mode_label_);
    layout->addWidget(new QLabel(QStringLiteral("|"), this));
    layout->addWidget(udp_status_label_);
    layout->addWidget(new QLabel(QStringLiteral("|"), this));
    layout->addWidget(frame_rate_label_);

    frame_rate_timer_ = new QTimer(this);
    frame_rate_timer_->setInterval(1'000);
    connect(frame_rate_timer_, &QTimer::timeout, this, [this]() { updateSimulatedFrameRate(); });
    frame_rate_timer_->start();
    updateStatistics(TargetStatistics{});
    setReplayState(false, false);
    setUdpStatus(UdpStatus::kStopped);
    updateSimulatedFrameRate();
}

void BottomStatusBar::updateStatistics(const TargetStatistics &statistics)
{
    const std::array<QString, 5> color_display_names{tr("蓝"), tr("橙"), tr("绿"), tr("紫"), tr("灰")};
    for (std::size_t index = 0; index < kTargetTypes.size(); ++index) {
        count_labels_[index]->setText(tr("%1(%2)：%3")
                                          .arg(targetTypeDisplayName(kTargetTypes[index]), color_display_names[index])
                                          .arg(statistics.count(kTargetTypes[index])));
    }
}

void BottomStatusBar::setReplayState(bool replay_mode, bool playing)
{
    if (!replay_mode)
    {
        display_mode_label_->setText(tr("实时模式"));
        display_mode_label_->setStyleSheet(QStringLiteral("QLabel { color: #208a4b; font-weight: 700; }"));
        return;
    }

    display_mode_label_->setText(playing ? tr("历史回放 · 播放") : tr("历史回放 · 暂停"));
    display_mode_label_->setStyleSheet(QStringLiteral("QLabel { color: #d35400; font-weight: 700; }"));
}

void BottomStatusBar::setUdpStatus(UdpStatus status)
{
    QString text;
    QString color;
    switch (status) {
    case UdpStatus::kStopped:
        text = tr("UDP 未启动");
        color = QStringLiteral("#c0392b");
        break;
    case UdpStatus::kListeningNoData:
        text = tr("UDP 等待数据");
        color = QStringLiteral("#d4a017");
        break;
    case UdpStatus::kReceiving:
        text = tr("UDP 正常");
        color = QStringLiteral("#208a4b");
        break;
    }
    udp_status_label_->setText(text);
    udp_status_label_->setStyleSheet(QStringLiteral("QLabel { color: %1; font-weight: 600; }").arg(color));
}

void BottomStatusBar::updateSimulatedFrameRate()
{
    const int frame_rate_tenths_fps = QRandomGenerator::global()->bounded(16) + 105;
    frame_rate_label_->setText(tr("帧率 %1 FPS").arg(frame_rate_tenths_fps / 10.0, 0, 'f', 1));
}

} // namespace utms
