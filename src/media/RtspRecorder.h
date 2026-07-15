#pragma once

#include <memory>

#include <QObject>
#include <QString>

#include "media/VideoRecordingSession.h"

struct AVPacket;
struct AVStream;

namespace utms {

class RtspRecorder : public QObject {
    Q_OBJECT

public:
    explicit RtspRecorder(QObject *parent = nullptr);
    ~RtspRecorder() override;

    VideoRecordingState state() const;
    void requestStart(const QString &output_directory, qint64 requested_at_ms);
    void requestStop();
    void interrupt(const QString &reason);
    void checkKeyframeTimeout();
    void processVideoPacket(const AVPacket &packet, const AVStream &input_stream);

signals:
    void stateChanged(utms::VideoRecordingState state, const QString &detail, const QString &output_path);
    void durationChanged(qint64 duration_seconds);

private:
    struct OutputContext;

    bool openOutput(const AVStream &input_stream, QString &failure_detail);
    bool writePacket(const AVPacket &packet, QString &failure_detail);
    void applyStopRequest(const QString &reason);
    void finalizeRecording();
    void failRecording(const QString &detail);
    void discardOutput();
    void emitDurationIfChanged();

    std::unique_ptr<OutputContext> output_;
    VideoRecordingSession session_;
    QString output_directory_;
    qint64 last_duration_seconds_ = -1;
};

} // namespace utms
