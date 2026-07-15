#pragma once

#include <memory>

#include <QImage>
#include <QMetaType>
#include <QObject>
#include <QString>

#include "media/VideoDetection.h"

class QThread;

namespace utms {

class LatestVideoFrameBuffer;
class YoloInferenceWorker;

enum class VideoDetectionState { kDisabled, kLoading, kEnabled, kError };

class VideoInferenceController : public QObject {
    Q_OBJECT

public:
    explicit VideoInferenceController(QObject *parent = nullptr);
    ~VideoInferenceController() override;

    void submitFrame(const QImage &frame);

public slots:
    void setDetectionEnabled(bool enabled);
    void clearPendingFrames();
    void releaseModel();
    void shutdown();

signals:
    void frameReady(const QImage &frame, const QVector<utms::VideoDetection> &detections);
    void detectionStateChanged(utms::VideoDetectionState state, const QString &detail);
    void stopped();
    void enableDetectionRequested(quint64 request_id, const QString &model_directory);
    void disableDetectionRequested();
    void processLatestFramesRequested();
    void releaseModelRequested();
    void shutdownRequested();

private slots:
    void handleModelReady(quint64 request_id);
    void handleModelError(quint64 request_id, const QString &detail);
    void handleInferenceCompleted(const QVector<utms::VideoDetection> &detections, quint64 generation);
    void handleInferenceError(const QString &detail);

private:
    void scheduleLatestFrame();
    QString modelDirectory() const;

    std::shared_ptr<LatestVideoFrameBuffer> frame_buffer_;
    QImage latest_preview_frame_;
    QVector<VideoDetection> latest_detections_;
    QThread *inference_thread_ = nullptr;
    YoloInferenceWorker *inference_worker_ = nullptr;
    quint64 frame_generation_ = 0;
    quint64 detection_request_id_ = 0;
    bool detection_requested_ = false;
    bool detection_active_ = false;
    bool shutdown_started_ = false;
};

} // namespace utms

Q_DECLARE_METATYPE(utms::VideoDetectionState)
