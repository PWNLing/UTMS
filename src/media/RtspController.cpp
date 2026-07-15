#include "media/RtspController.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include "media/RtspDecoderWorker.h"
#include "media/VideoInferenceController.h"

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
    : QObject(parent)
    , state_machine_(new RtspStateMachine(this))
    , reconnect_timer_(new QTimer(this))
    , decoder_thread_(new QThread(this))
    , decoder_worker_(new RtspDecoderWorker())
    , inference_controller_(new VideoInferenceController(this))
{
    qRegisterMetaType<utms::VideoRecordingState>();
    reconnect_timer_->setSingleShot(true);

    if (!decoder_worker_->moveToThread(decoder_thread_)) {
        qCritical() << "RtspController: failed to move decoder worker to its "
                       "dedicated thread";
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
    connect(decoder_worker_, &RtspDecoderWorker::recordingStateChanged, this,
            &RtspController::handleRecordingStateChanged);
    connect(decoder_worker_, &RtspDecoderWorker::recordingDurationChanged, this,
            &RtspController::recordingDurationChanged);
    connect(decoder_thread_, &QThread::finished, decoder_worker_, &QObject::deleteLater);
    connect(decoder_thread_, &QThread::finished, this, &RtspController::handleDecoderThreadStopped);
    connect(inference_controller_, &VideoInferenceController::frameReady, this, &RtspController::frameReady);
    connect(inference_controller_, &VideoInferenceController::detectionStateChanged, this,
            &RtspController::detectionStateChanged);
    connect(inference_controller_, &VideoInferenceController::stopped, this, &RtspController::handleInferenceStopped);

    decoder_thread_->start();
}

RtspController::~RtspController()
{
    if (decoder_thread_ != nullptr && decoder_thread_->isRunning()) {
        qCritical() << "RtspController: destroyed before asynchronous decoder "
                       "shutdown completed";
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

VideoRecordingState RtspController::recordingState() const { return recording_state_; }

void RtspController::connectToStream(const QString &stream_url)
{
    if (shutdown_started_ || decoder_worker_ == nullptr) {
        return;
    }
    if (!state_machine_->isConnectionDesired() && inference_controller_ != nullptr) {
        inference_controller_->releaseModel();
    }
    const quint64 previous_generation = session_generation_;
    ++session_generation_;
    if (!state_machine_->requestConnect(stream_url)) {
        session_generation_ = previous_generation;
    }
}

void RtspController::disconnectFromStream()
{
    requestRecordingStop(tr("用户主动断开 RTSP"));
    reconnect_timer_->stop();
    pending_stream_url_.clear();
    pending_attempt_valid_ = false;
    if (state_machine_->isConnectionDesired()) {
        qInfo() << "RtspController: operator disconnected video stream"
                << safeStreamDescription(state_machine_->streamUrl()) << "session" << session_generation_
                << "last attempt" << next_attempt_id_;
    }
    ++session_generation_;
    if (inference_controller_ != nullptr) {
        inference_controller_->releaseModel();
    }
    state_machine_->requestDisconnect();
}

void RtspController::setDetectionEnabled(bool enabled)
{
    if (!shutdown_started_ && inference_controller_ != nullptr) {
        inference_controller_->setDetectionEnabled(enabled);
    }
}

void RtspController::startRecording()
{
    if (shutdown_started_ || decoder_worker_ == nullptr || !decoder_busy_ ||
        state_machine_->state() != RtspConnectionState::kPlaying ||
        (recording_state_ != VideoRecordingState::kIdle && recording_state_ != VideoRecordingState::kError)) {
        return;
    }

    recording_state_ = VideoRecordingState::kStarting;
    qInfo() << "RtspController: operator requested recording for attempt" << running_attempt_id_;
    emit recordingStateChanged(recording_state_, tr("准备录制"), {});
    decoder_worker_->requestStartRecording(running_attempt_id_, recordingDirectory());
}

void RtspController::stopRecording()
{
    if (recording_state_ != VideoRecordingState::kStarting && recording_state_ != VideoRecordingState::kRecording) {
        return;
    }
    qInfo() << "RtspController: operator requested recording stop for attempt" << running_attempt_id_;
    requestRecordingStop(tr("用户停止录像"));
}

void RtspController::openRecordingDirectory()
{
    const QString directory_path = recordingDirectory();
    QDir directory(directory_path);
    if ((!directory.exists() && !directory.mkpath(QStringLiteral("."))) || !directory.exists()) {
        qWarning() << "RtspController: failed to create recording directory" << directory_path;
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(directory.absolutePath()))) {
        qWarning() << "RtspController: failed to open recording directory" << directory.absolutePath();
    }
}

void RtspController::shutdown()
{
    if (shutdown_started_) {
        return;
    }

    shutdown_started_ = true;
    requestRecordingStop(tr("应用退出"));
    reconnect_timer_->stop();
    pending_stream_url_.clear();
    pending_attempt_valid_ = false;
    ++session_generation_;
    if (inference_controller_ != nullptr) {
        inference_controller_->shutdown();
    } else {
        inference_shutdown_complete_ = true;
    }
    state_machine_->requestDisconnect();
    if (!decoder_thread_->isRunning()) {
        decoder_shutdown_complete_ = true;
    } else if (!decoder_busy_) {
        decoder_thread_->quit();
    }
    completeShutdownIfReady();
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
    if (inference_controller_ != nullptr) {
        inference_controller_->clearPendingFrames();
    }
    state_machine_->reportConnectionFailure(detail);
}

void RtspController::handlePlaybackInterrupted(quint64 attempt_id, const QString &detail)
{
    if (!isCurrentAttempt(attempt_id)) {
        return;
    }
    qWarning() << "RtspController: video playback interrupted" << safeStreamDescription(state_machine_->streamUrl())
               << "attempt" << attempt_id << detail;
    if (inference_controller_ != nullptr) {
        inference_controller_->clearPendingFrames();
    }
    state_machine_->reportPlaybackInterrupted(detail);
}

void RtspController::handleDecodedFrame(quint64 attempt_id, const QImage &frame)
{
    if (isCurrentAttempt(attempt_id) && state_machine_->state() == RtspConnectionState::kPlaying) {
        if (inference_controller_ != nullptr) {
            inference_controller_->submitFrame(frame);
        }
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

void RtspController::handleRecordingStateChanged(VideoRecordingState state, const QString &detail,
                                                 const QString &output_path)
{
    recording_state_ = state;
    emit recordingStateChanged(state, detail, output_path);
}

bool RtspController::isCurrentAttempt(quint64 attempt_id) const
{
    return decoder_busy_ && attempt_id == running_attempt_id_ && running_session_generation_ == session_generation_;
}

QString RtspController::recordingDirectory() const
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("recordings"));
}

void RtspController::requestRecordingStop(const QString &reason)
{
    if (recording_state_ != VideoRecordingState::kStarting && recording_state_ != VideoRecordingState::kRecording) {
        return;
    }
    recording_state_ = VideoRecordingState::kStopping;
    emit recordingStateChanged(recording_state_, tr("正在停止"), {});
    if (decoder_worker_ != nullptr) {
        decoder_worker_->requestStopRecording(reason);
    }
}

void RtspController::stopDecoder()
{
    reconnect_timer_->stop();
    requestRecordingStop(tr("RTSP 连接停止"));
    if (decoder_worker_ != nullptr) {
        decoder_worker_->requestStop();
    }
}

void RtspController::handleDecoderThreadStopped()
{
    decoder_shutdown_complete_ = true;
    completeShutdownIfReady();
}

void RtspController::handleInferenceStopped()
{
    inference_shutdown_complete_ = true;
    completeShutdownIfReady();
}

void RtspController::completeShutdownIfReady()
{
    if (shutdown_started_ && decoder_shutdown_complete_ && inference_shutdown_complete_ && !stopped_emitted_) {
        stopped_emitted_ = true;
        emit stopped();
    }
}

} // namespace utms
