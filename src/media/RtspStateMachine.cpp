#include "media/RtspStateMachine.h"

#include <QUrl>

namespace utms {

RtspStateMachine::RtspStateMachine(QObject *parent) : QObject(parent) {}

RtspConnectionState RtspStateMachine::state() const { return state_; }

bool RtspStateMachine::isConnectionDesired() const { return connection_desired_; }

QString RtspStateMachine::streamUrl() const { return stream_url_; }

bool RtspStateMachine::requestConnect(const QString &stream_url)
{
    const QString normalized_url = stream_url.trimmed();
    const QUrl parsed_url(normalized_url, QUrl::StrictMode);
    const bool valid_rtsp_url = parsed_url.isValid() &&
                                parsed_url.scheme().compare(QStringLiteral("rtsp"), Qt::CaseInsensitive) == 0 &&
                                !parsed_url.host().isEmpty();
    if (connection_desired_ || !valid_rtsp_url) {
        if (!valid_rtsp_url) {
            setState(RtspConnectionState::kDisconnected, tr("请输入有效的 rtsp:// 视频地址"));
        }
        return false;
    }

    stream_url_ = normalized_url;
    connection_desired_ = true;
    setState(RtspConnectionState::kConnecting, tr("正在连接视频流"));
    emit connectionAttemptRequested(stream_url_);
    return true;
}

void RtspStateMachine::requestDisconnect()
{
    connection_desired_ = false;
    setState(RtspConnectionState::kDisconnected, tr("已断开"));
    emit decoderStopRequested();
}

bool RtspStateMachine::requestReconnect()
{
    if (!connection_desired_ || state_ != RtspConnectionState::kReconnecting) {
        return false;
    }

    setState(RtspConnectionState::kConnecting, tr("正在重新连接视频流"));
    emit connectionAttemptRequested(stream_url_);
    return true;
}

void RtspStateMachine::reportPlaybackStarted()
{
    if (!connection_desired_) {
        return;
    }
    setState(RtspConnectionState::kPlaying, tr("播放中"));
}

void RtspStateMachine::reportConnectionFailure(const QString &detail) { reportFailure(detail); }

void RtspStateMachine::reportPlaybackInterrupted(const QString &detail) { reportFailure(detail); }

void RtspStateMachine::setState(RtspConnectionState state, const QString &detail)
{
    state_ = state;
    emit stateChanged(state_, detail);
}

void RtspStateMachine::reportFailure(const QString &detail)
{
    if (!connection_desired_) {
        return;
    }

    const QString failure_detail = detail.isEmpty() ? tr("视频流不可用") : detail;
    setState(RtspConnectionState::kReconnecting, failure_detail);
    emit reconnectScheduled(kReconnectIntervalMs);
}

} // namespace utms
