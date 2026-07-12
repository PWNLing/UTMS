#include "network/UdpReceiver.h"

#include <QDebug>
#include <QHostAddress>
#include <QThread>
#include <QTimer>
#include <QUdpSocket>

#include "core/RadarJsonParser.h"

namespace utms {
namespace {

constexpr int kDataFreshnessTimeoutMs = 3'000;
constexpr int kStatusCheckIntervalMs = 250;
constexpr qint64 kRestartSequenceUpperBound = 100;
constexpr qint64 kRestartMinimumSequenceDrop = 100;
constexpr double kRestartTimestampAdvanceSeconds = 0.5;

}  // namespace

UdpReceiver::UdpReceiver(QObject *parent)
    : QObject(parent)
    , status_timer_(new QTimer(this))
{
    status_timer_->setInterval(kStatusCheckIntervalMs);
    connect(status_timer_, &QTimer::timeout, this, &UdpReceiver::updateReceiveStatus);
}

void UdpReceiver::startListening(quint16 port)
{
    if (socket_ != nullptr) {
        return;
    }

    socket_ = new QUdpSocket(this);
    connect(socket_, &QUdpSocket::readyRead, this, &UdpReceiver::readPendingDatagrams);

    if (!socket_->bind(QHostAddress::AnyIPv4, port)) {
        const QString error = tr("端口 %1 绑定失败: %2").arg(port).arg(socket_->errorString());
        qWarning() << "UdpReceiver:" << error;
        socket_->deleteLater();
        socket_ = nullptr;
        setStatus(UdpStatus::kStopped, error);
        return;
    }

    frame_store_.clear();
    last_sequence_.reset();
    last_sender_timestamp_seconds_.reset();
    last_accepted_timer_.invalidate();
    status_timer_->start();
    setStatus(UdpStatus::kListeningNoData, tr("正在监听端口 %1，等待有效数据").arg(port));
    qInfo() << "UdpReceiver: listening on UDP port" << port;
}

void UdpReceiver::stopListening()
{
    status_timer_->stop();
    if (socket_ != nullptr) {
        socket_->close();
        socket_->deleteLater();
        socket_ = nullptr;
    }

    frame_store_.clear();
    last_sequence_.reset();
    last_sender_timestamp_seconds_.reset();
    last_accepted_timer_.invalidate();
    setStatus(UdpStatus::kStopped, tr("UDP 未启动"));
    qInfo() << "UdpReceiver: stopped";
    emit stopped();
}

void UdpReceiver::shutdown()
{
    stopListening();
    QThread::currentThread()->quit();
}

void UdpReceiver::readPendingDatagrams()
{
    while (socket_ != nullptr && socket_->hasPendingDatagrams()) {
        QByteArray payload;
        payload.resize(static_cast<qsizetype>(socket_->pendingDatagramSize()));
        const qint64 bytes_read = socket_->readDatagram(payload.data(), payload.size());
        if (bytes_read < 0) {
            qWarning() << "UdpReceiver: failed to read datagram:" << socket_->errorString();
            continue;
        }
        payload.resize(static_cast<qsizetype>(bytes_read));

        const RadarParseResult parse_result = RadarJsonParser::parse(payload);
        if (!parse_result.frame.has_value()) {
            qWarning() << "UdpReceiver:" << parse_result.error;
            continue;
        }
        for (const QString &warning : parse_result.warnings) {
            qWarning() << "UdpReceiver:" << warning;
        }

        if (!acceptsSequence(parse_result.frame.value())) {
            continue;
        }

        last_accepted_timer_.restart();
        const RadarFrame current_frame = frame_store_.replace(parse_result.frame.value());
        setStatus(UdpStatus::kReceiving, tr("近期已收到有效数据"));
        emit frameReceived(current_frame);
    }
}

void UdpReceiver::updateReceiveStatus()
{
    if (socket_ == nullptr) {
        return;
    }
    if (!last_accepted_timer_.isValid() || last_accepted_timer_.elapsed() > kDataFreshnessTimeoutMs) {
        setStatus(UdpStatus::kListeningNoData, tr("UDP 已启动，等待有效数据"));
    }
}

bool UdpReceiver::acceptsSequence(const RadarFrame &frame)
{
    if (last_accepted_timer_.isValid() && last_accepted_timer_.elapsed() > kDataFreshnessTimeoutMs) {
        qInfo() << "UdpReceiver: resetting sequence baseline after data timeout";
        last_sequence_.reset();
        last_sender_timestamp_seconds_.reset();
    }

    if (!frame.sequence.has_value()) {
        return true;
    }

    const qint64 sequence = frame.sequence.value();
    if (last_sequence_.has_value()) {
        if (sequence == last_sequence_.value()) {
            qWarning() << "UdpReceiver: discarded duplicate sequence" << sequence;
            return false;
        }
        if (sequence < last_sequence_.value()) {
            const bool sender_restarted = sequence <= kRestartSequenceUpperBound
                                          && last_sequence_.value() - sequence
                                                 >= kRestartMinimumSequenceDrop
                                          && frame.sender_timestamp_seconds.has_value()
                                          && last_sender_timestamp_seconds_.has_value()
                                          && frame.sender_timestamp_seconds.value()
                                                 > last_sender_timestamp_seconds_.value()
                                                       + kRestartTimestampAdvanceSeconds;
            if (sender_restarted) {
                qInfo() << "UdpReceiver: resetting sequence baseline after sender restart";
            } else {
                qWarning() << "UdpReceiver: discarded out-of-order sequence" << sequence;
                return false;
            }
        }
        if (sequence > last_sequence_.value() + 1) {
            qWarning() << "UdpReceiver: sequence jump from" << last_sequence_.value() << "to" << sequence;
        }
    }

    last_sequence_ = sequence;
    if (frame.sender_timestamp_seconds.has_value()) {
        last_sender_timestamp_seconds_ = frame.sender_timestamp_seconds;
    }
    return true;
}

void UdpReceiver::setStatus(UdpStatus status, const QString &detail)
{
    if (status_ == status && status_detail_ == detail) {
        return;
    }
    status_ = status;
    status_detail_ = detail;
    emit statusChanged(status, detail);
}

}  // namespace utms
