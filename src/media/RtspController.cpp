#include "media/RtspController.h"

#include <QDebug>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include "media/RtspDecoderWorker.h"

namespace utms {
namespace {

QString safeStreamDescription(const QString &stream_url)
{
    const QUrl url(stream_url);
    if (!url.isValid()) {
        return QStringLiteral("invalid RTSP URL");
    }
    const int port = url.port(-1);
    return port > 0 ? QStringLiteral("%1://%2:%3").arg(url.scheme(), url.host()).arg(port)
                    : QStringLiteral("%1://%2").arg(url.scheme(), url.host());
}

} // namespace

RtspController::RtspController(QObject *parent)
    : QObject(parent), state_machine_(new RtspStateMachine(this)), reconnect_timer_(new QTimer(this)),
      decoder_thread_(new QThread(this)), decoder_worker_(new RtspDecoderWorker())
{
    reconnect_timer_->setSingleShot(true);

    if (!decoder_worker_->moveToThread(decoder_thread_)) {
        qCritical() << "RtspController: failed to move decoder worker to its dedicated thread";
        delete decoder_worker_;
        decoder_worker_ = nullptr;
        return;
    }

    connect(state_machine_, &RtspStateMachine::stateChanged, this, &RtspController::stateChanged);
    connect(state_machine_, &RtspStateMachine::connectionAttemptRequested, this,
            &RtspController::startConnectionAttempt);
    connect(state_machine_, &RtspStateMachine::reconnectScheduled, this, &RtspController::scheduleReconnect);
    connect(state_machine_, &RtspStateMachine::decoderStopRequested, this, &RtspController::stopDecoder);
    connect(reconnect_timer_, &QTimer::timeout, state_machine_, &RtspStateMachine::requestReconnect);
    connect(this, &RtspController::decodeRequested, decoder_worker_, &RtspDecoderWorker::decode);
    connect(decoder_worker_, &RtspDecoderWorker::playbackStarted, this, &RtspController::handlePlaybackStarted);
    connect(decoder_worker_, &RtspDecoderWorker::connectionFailed, this, &RtspController::handleConnectionFailure);
    connect(decoder_worker_, &RtspDecoderWorker::playbackInterrupted, this, &RtspController::handlePlaybackInterrupted);
    // 阻塞的是解码线程而非 GUI；每次只交付一帧，避免 GUI 事件队列积压旧画面。
    connect(decoder_worker_, &RtspDecoderWorker::frameDecoded, this, &RtspController::handleDecodedFrame,
            Qt::BlockingQueuedConnection);
    connect(decoder_worker_, &RtspDecoderWorker::decodingFinished, this, &RtspController::handleDecodingFinished);
    connect(decoder_thread_, &QThread::finished, decoder_worker_, &QObject::deleteLater);
    connect(decoder_thread_, &QThread::finished, this, &RtspController::stopped);

    decoder_thread_->start();
}

RtspController::~RtspController()
{
    if (decoder_thread_ != nullptr && decoder_thread_->isRunning()) {
        qCritical() << "RtspController: destroyed before asynchronous decoder shutdown completed";
        if (decoder_busy_) {
            connect(decoder_worker_, &RtspDecoderWorker::decodingFinished, decoder_thread_, &QThread::quit,
                    Qt::DirectConnection);
            decoder_worker_->requestStop();
        } else {
            decoder_thread_->quit();
        }
        decoder_thread_->setParent(nullptr);
        connect(decoder_thread_, &QThread::finished, decoder_thread_, &QObject::deleteLater);
    }
}

RtspConnectionState RtspController::state() const { return state_machine_->state(); }

void RtspController::connectToStream(const QString &stream_url)
{
    if (shutdown_started_ || decoder_worker_ == nullptr) {
        return;
    }
    const quint64 previous_generation = session_generation_;
    ++session_generation_;
    if (!state_machine_->requestConnect(stream_url)) {
        session_generation_ = previous_generation;
    }
}

void RtspController::disconnectFromStream()
{
    reconnect_timer_->stop();
    pending_stream_url_.clear();
    pending_attempt_valid_ = false;
    if (state_machine_->isConnectionDesired()) {
        qInfo() << "RtspController: operator disconnected video stream"
                << safeStreamDescription(state_machine_->streamUrl()) << "session" << session_generation_
                << "last attempt" << next_attempt_id_;
    }
    ++session_generation_;
    state_machine_->requestDisconnect();
}

void RtspController::shutdown()
{
    if (shutdown_started_) {
        return;
    }

    shutdown_started_ = true;
    reconnect_timer_->stop();
    pending_stream_url_.clear();
    pending_attempt_valid_ = false;
    ++session_generation_;
    state_machine_->requestDisconnect();
    if (!decoder_thread_->isRunning()) {
        emit stopped();
    } else if (!decoder_busy_) {
        decoder_thread_->quit();
    }
}

void RtspController::startConnectionAttempt(const QString &stream_url)
{
    reconnect_timer_->stop();
    if (shutdown_started_ || decoder_worker_ == nullptr) {
        return;
    }
    if (decoder_busy_) {
        pending_stream_url_ = stream_url;
        pending_session_generation_ = session_generation_;
        pending_attempt_valid_ = true;
        return;
    }

    const quint64 attempt_id = ++next_attempt_id_;
    running_attempt_id_ = attempt_id;
    running_session_generation_ = session_generation_;
    decoder_busy_ = true;
    decoder_worker_->prepareStart();
    qInfo() << "RtspController: connecting video stream" << safeStreamDescription(stream_url) << "attempt"
            << attempt_id;
    emit decodeRequested(attempt_id, stream_url);
}

void RtspController::scheduleReconnect(int delay_ms)
{
    if (shutdown_started_ || !state_machine_->isConnectionDesired()) {
        return;
    }
    qInfo() << "RtspController: scheduling video reconnect" << safeStreamDescription(state_machine_->streamUrl())
            << "after" << delay_ms << "ms; session" << session_generation_;
    reconnect_timer_->start(delay_ms);
}

void RtspController::handlePlaybackStarted(quint64 attempt_id)
{
    if (!isCurrentAttempt(attempt_id)) {
        return;
    }
    if (shutdown_started_ || !state_machine_->isConnectionDesired()) {
        stopDecoder();
        return;
    }
    qInfo() << "RtspController: video playback started" << safeStreamDescription(state_machine_->streamUrl())
            << "attempt" << attempt_id;
    state_machine_->reportPlaybackStarted();
}

void RtspController::handleConnectionFailure(quint64 attempt_id, const QString &detail)
{
    if (!isCurrentAttempt(attempt_id)) {
        return;
    }
    qWarning() << "RtspController: video connection failed" << safeStreamDescription(state_machine_->streamUrl())
               << "attempt" << attempt_id << detail;
    state_machine_->reportConnectionFailure(detail);
}

void RtspController::handlePlaybackInterrupted(quint64 attempt_id, const QString &detail)
{
    if (!isCurrentAttempt(attempt_id)) {
        return;
    }
    qWarning() << "RtspController: video playback interrupted" << safeStreamDescription(state_machine_->streamUrl())
               << "attempt" << attempt_id << detail;
    state_machine_->reportPlaybackInterrupted(detail);
}

void RtspController::handleDecodedFrame(quint64 attempt_id, const QImage &frame)
{
    if (isCurrentAttempt(attempt_id) && state_machine_->state() == RtspConnectionState::kPlaying) {
        emit frameReady(frame);
    }
}

void RtspController::handleDecodingFinished(quint64 attempt_id)
{
    if (attempt_id != running_attempt_id_) {
        return;
    }
    decoder_busy_ = false;
    running_attempt_id_ = 0;
    running_session_generation_ = 0;
    if (shutdown_started_) {
        decoder_thread_->quit();
        return;
    }

    if (pending_attempt_valid_ && pending_session_generation_ == session_generation_ &&
        state_machine_->state() == RtspConnectionState::kConnecting) {
        const QString stream_url = pending_stream_url_;
        pending_stream_url_.clear();
        pending_attempt_valid_ = false;
        startConnectionAttempt(stream_url);
    }
}

bool RtspController::isCurrentAttempt(quint64 attempt_id) const
{
    return decoder_busy_ && attempt_id == running_attempt_id_ && running_session_generation_ == session_generation_;
}

void RtspController::stopDecoder()
{
    reconnect_timer_->stop();
    if (decoder_worker_ != nullptr) {
        decoder_worker_->requestStop();
    }
}

} // namespace utms
