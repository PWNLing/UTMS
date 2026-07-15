#pragma once

#include <atomic>

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QString>

#include "media/VideoRecordingSession.h"

namespace utms {

class RtspRecorder;

class RtspDecoderWorker : public QObject {
    Q_OBJECT

public:
    explicit RtspDecoderWorker(QObject *parent = nullptr);

    // 在请求排入工作线程前复位中断标志，随后发生的人工断开不会被 decode() 覆盖。
    void prepareStart();

    // 该函数仅修改原子中断标志，可从 GUI 线程安全调用，以中断 FFmpeg 阻塞 I/O。
    void requestStop();

    // 录像启停通过固定容量意图状态交给 decode() 循环处理，不依赖忙碌线程的 queued
    // slot。
    void requestStartRecording(quint64 attempt_id, const QString &output_directory);
    void requestStopRecording(const QString &reason);

public slots:
    // 工作线程入口：按“打开 TCP RTSP → 配置视频解码器 →
    // 解码并转换帧”三个阶段执行； FFmpeg 资源由实现内 RAII 对象持有，错误通过带
    // attempt_id 的信号返回 GUI 侧控制器。
    void decode(quint64 attempt_id, const QString &stream_url);

signals:
    void playbackStarted(quint64 attempt_id);
    void frameDecoded(quint64 attempt_id, const QImage &frame);
    void connectionFailed(quint64 attempt_id, const QString &detail);
    void playbackInterrupted(quint64 attempt_id, const QString &detail);
    void decodingFinished(quint64 attempt_id);
    void recordingStateChanged(utms::VideoRecordingState state, const QString &detail, const QString &output_path);
    void recordingDurationChanged(qint64 duration_seconds);

private:
    void processRecordingIntent(quint64 attempt_id, RtspRecorder &recorder);
    void pollRecording(quint64 attempt_id, RtspRecorder &recorder);
    void clearRecordingIntent(quint64 attempt_id);
    static int interruptCallback(void *opaque);

    std::atomic_bool stop_requested_{false};
    QMutex recording_mutex_;
    quint64 recording_attempt_id_ = 0;
    qint64 recording_requested_at_ms_ = 0;
    QString recording_output_directory_;
    QString recording_stop_reason_;
    bool recording_start_requested_ = false;
    bool recording_stop_requested_ = false;
};

} // namespace utms
