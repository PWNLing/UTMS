#pragma once

#include <atomic>

#include <QImage>
#include <QObject>
#include <QString>

namespace utms {

class RtspDecoderWorker : public QObject {
    Q_OBJECT

public:
    explicit RtspDecoderWorker(QObject *parent = nullptr);

    // 在请求排入工作线程前复位中断标志，随后发生的人工断开不会被 decode() 覆盖。
    void prepareStart();

    // 该函数仅修改原子中断标志，可从 GUI 线程安全调用，以中断 FFmpeg 阻塞 I/O。
    void requestStop();

public slots:
    // 工作线程入口：按“打开 TCP RTSP → 配置视频解码器 → 解码并转换帧”三个阶段执行；
    // FFmpeg 资源由实现内 RAII 对象持有，错误通过带 attempt_id 的信号返回 GUI 侧控制器。
    void decode(quint64 attempt_id, const QString &stream_url);

signals:
    void playbackStarted(quint64 attempt_id);
    void frameDecoded(quint64 attempt_id, const QImage &frame);
    void connectionFailed(quint64 attempt_id, const QString &detail);
    void playbackInterrupted(quint64 attempt_id, const QString &detail);
    void decodingFinished(quint64 attempt_id);

private:
    static int interruptCallback(void *opaque);

    std::atomic_bool stop_requested_{false};
};

} // namespace utms
