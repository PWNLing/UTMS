#pragma once

#include <atomic>
#include <memory>

#include <QImage>
#include <QObject>
#include <QString>

#include "media/LatestVideoFrameBuffer.h"
#include "media/VideoDetection.h"

namespace utms {

// 模型与 ONNX Runtime 会话只属于推理线程；正常结果和错误均通过信号返回 GUI 控制器。
class YoloInferenceWorker : public QObject {
    Q_OBJECT

public:
    explicit YoloInferenceWorker(std::shared_ptr<LatestVideoFrameBuffer> frame_buffer, QObject *parent = nullptr);
    ~YoloInferenceWorker() override;

    // 控制器可从 GUI 线程修改该原子标志，令正在运行的工作线程在当前推理结束后停止取帧。
    void requestProcessing(bool enabled);

public slots:
    void enableDetection(quint64 request_id, const QString &model_directory);
    void disableDetection();
    void processLatestFrames();
    void releaseModel();
    void shutdown();

signals:
    void modelReady(quint64 request_id);
    void modelError(quint64 request_id, const QString &detail);
    void inferenceCompleted(const QVector<utms::VideoDetection> &detections, quint64 generation);
    void inferenceError(const QString &detail);
    void stopped();

private:
    struct ModelSession;

    bool loadModel(const QString &model_directory, QString &error);
    QVector<VideoDetection> runInference(const QImage &frame, QString &error) const;

    std::shared_ptr<LatestVideoFrameBuffer> frame_buffer_;
    std::unique_ptr<ModelSession> model_session_;
    std::atomic_bool processing_requested_{false};
    bool detection_enabled_ = false;
    bool shutdown_started_ = false;
};

} // namespace utms
