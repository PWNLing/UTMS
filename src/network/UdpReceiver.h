#pragma once

#include <optional>

#include <QElapsedTimer>
#include <QObject>

#include "core/RadarFrameStore.h"

class QTimer;
class QUdpSocket;

namespace utms {

enum class UdpStatus {
    kStopped,
    kListeningNoData,
    kReceiving
};

class UdpReceiver : public QObject
{
    Q_OBJECT

public:
    explicit UdpReceiver(QObject *parent = nullptr);

public slots:
    void startListening(quint16 port);
    void stopListening();
    void shutdown();

signals:
    void frameReceived(const utms::RadarFrame &frame);
    void statusChanged(utms::UdpStatus status, const QString &detail);
    void stopped();

private slots:
    void readPendingDatagrams();
    void updateReceiveStatus();

private:
    bool acceptsSequence(const RadarFrame &frame);
    void setStatus(UdpStatus status, const QString &detail);

    QUdpSocket *socket_ = nullptr;
    QTimer *status_timer_ = nullptr;
    RadarFrameStore frame_store_;
    QElapsedTimer last_accepted_timer_;
    std::optional<qint64> last_sequence_;
    std::optional<double> last_sender_timestamp_seconds_;
    UdpStatus status_ = UdpStatus::kStopped;
    QString status_detail_;
};

}  // namespace utms

Q_DECLARE_METATYPE(utms::UdpStatus)
