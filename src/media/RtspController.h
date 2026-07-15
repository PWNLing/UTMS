#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include "media/RtspStateMachine.h"
#include "media/VideoDetection.h"
#include "media/VideoInferenceController.h"
#include "media/VideoRecordingSession.h"

class QThread;
class QTimer;

namespace utms {

class RtspDecoderWorker;

class RtspController : public QObject {
    Q_OBJECT

public:
    explicit RtspController(QObject *parent = nullptr);
    ~RtspController() override;

    RtspConnectionState state() const;
    VideoRecordingState recordingState() const;

public slots:
    void connectToStream(const QString &stream_url);
    void disconnectFromStream();
    void setDetectionEnabled(bool enabled);
    void startRecording();
    void stopRecording();
    void openRecordingDirectory();
    void shutdown();

signals:
    void stateChanged(utms::RtspConnectionState state, const QString &detail);
    void frameReady(const QImage &frame, const QVector<utms::VideoDetection> &detections);
    void detectionStateChanged(utms::VideoDetectionState state, const QString &detail);
    void recordingStateChanged(utms::VideoRecordingState state, const QString &detail, const QString &output_path);
    void recordingDurationChanged(qint64 duration_seconds);
    void decodeRequested(quint64 attempt_id, const QString &stream_url);
    void stopped();

private slots:
    void startConnectionAttempt(const QString &stream_url);
    void scheduleReconnect(int delay_ms);
    void handlePlaybackStarted(quint64 attempt_id);
    void handleConnectionFailure(quint64 attempt_id, const QString &detail);
    void handlePlaybackInterrupted(quint64 attempt_id, const QString &detail);
    void handleDecodedFrame(quint64 attempt_id, const QImage &frame);
    void handleDecodingFinished(quint64 attempt_id);
    void handleRecordingStateChanged(utms::VideoRecordingState state, const QString &detail,
                                     const QString &output_path);
    void handleDecoderThreadStopped();
    void handleInferenceStopped();

private:
    bool isCurrentAttempt(quint64 attempt_id) const;
    QString recordingDirectory() const;
    void requestRecordingStop(const QString &reason);
    void stopDecoder();
    void completeShutdownIfReady();

    RtspStateMachine *state_machine_ = nullptr;
    QTimer *reconnect_timer_ = nullptr;
    QThread *decoder_thread_ = nullptr;
    RtspDecoderWorker *decoder_worker_ = nullptr;
    VideoInferenceController *inference_controller_ = nullptr;
    QString pending_stream_url_;
    quint64 pending_session_generation_ = 0;
    quint64 session_generation_ = 0;
    quint64 next_attempt_id_ = 0;
    quint64 running_attempt_id_ = 0;
    quint64 running_session_generation_ = 0;
    bool pending_attempt_valid_ = false;
    bool decoder_busy_ = false;
    bool shutdown_started_ = false;
    bool decoder_shutdown_complete_ = false;
    bool inference_shutdown_complete_ = false;
    bool stopped_emitted_ = false;
    VideoRecordingState recording_state_ = VideoRecordingState::kIdle;
};

} // namespace utms
