#pragma once

#include <QObject>
#include <QString>

namespace utms {

enum class RtspConnectionState { kDisconnected, kConnecting, kPlaying, kReconnecting };

class RtspStateMachine : public QObject {
    Q_OBJECT

public:
    static constexpr int kReconnectIntervalMs = 3'000;

    explicit RtspStateMachine(QObject *parent = nullptr);

    RtspConnectionState state() const;
    bool isConnectionDesired() const;
    QString streamUrl() const;

    bool requestConnect(const QString &stream_url);
    void requestDisconnect();
    bool requestReconnect();
    void reportPlaybackStarted();
    void reportConnectionFailure(const QString &detail);
    void reportPlaybackInterrupted(const QString &detail);

signals:
    void stateChanged(utms::RtspConnectionState state, const QString &detail);
    void connectionAttemptRequested(const QString &stream_url);
    void reconnectScheduled(int delay_ms);
    void decoderStopRequested();

private:
    void setState(RtspConnectionState state, const QString &detail);
    void reportFailure(const QString &detail);

    RtspConnectionState state_ = RtspConnectionState::kDisconnected;
    bool connection_desired_ = false;
    QString stream_url_;
};

} // namespace utms

Q_DECLARE_METATYPE(utms::RtspConnectionState)
