#include "media/VideoInferenceController.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QThread>

#include "media/LatestVideoFrameBuffer.h"
#include "media/YoloInferenceWorker.h"

namespace utms {

VideoInferenceController::VideoInferenceController(QObject *parent)
    : QObject(parent), frame_buffer_(std::make_shared<LatestVideoFrameBuffer>()), inference_thread_(new QThread(this)),
      inference_worker_(new YoloInferenceWorker(frame_buffer_))
{
    qRegisterMetaType<utms::VideoDetectionState>();
    qRegisterMetaType<utms::VideoDetection>();
    qRegisterMetaType<QVector<utms::VideoDetection>>();

    if (!inference_worker_->moveToThread(inference_thread_)) {
        qCritical() << "VideoInferenceController: failed to move inference worker to its dedicated thread";
        delete inference_worker_;
        inference_worker_ = nullptr;
        return;
    }

    connect(this, &VideoInferenceController::enableDetectionRequested, inference_worker_,
            &YoloInferenceWorker::enableDetection);
    connect(this, &VideoInferenceController::disableDetectionRequested, inference_worker_,
            &YoloInferenceWorker::disableDetection);
    connect(this, &VideoInferenceController::processLatestFramesRequested, inference_worker_,
            &YoloInferenceWorker::processLatestFrames);
    connect(this, &VideoInferenceController::releaseModelRequested, inference_worker_,
            &YoloInferenceWorker::releaseModel);
    connect(this, &VideoInferenceController::shutdownRequested, inference_worker_, &YoloInferenceWorker::shutdown);
    connect(inference_worker_, &YoloInferenceWorker::modelReady, this, &VideoInferenceController::handleModelReady);
    connect(inference_worker_, &YoloInferenceWorker::modelError, this, &VideoInferenceController::handleModelError);
    connect(inference_worker_, &YoloInferenceWorker::inferenceCompleted, this,
            &VideoInferenceController::handleInferenceCompleted);
    connect(inference_worker_, &YoloInferenceWorker::inferenceError, this,
            &VideoInferenceController::handleInferenceError);
    connect(inference_worker_, &YoloInferenceWorker::stopped, inference_thread_, &QThread::quit);
    connect(inference_thread_, &QThread::finished, inference_worker_, &QObject::deleteLater);
    connect(inference_thread_, &QThread::finished, this, &VideoInferenceController::stopped);
    inference_thread_->start();
}

VideoInferenceController::~VideoInferenceController()
{
    if (inference_thread_ != nullptr && inference_thread_->isRunning()) {
        qCritical() << "VideoInferenceController: destroyed before asynchronous inference shutdown completed";
        if (inference_worker_ != nullptr) {
            inference_worker_->requestProcessing(false);
            emit shutdownRequested();
        }
        inference_thread_->setParent(nullptr);
        connect(inference_thread_, &QThread::finished, inference_thread_, &QObject::deleteLater);
    }
}

void VideoInferenceController::submitFrame(const QImage &frame)
{
    if (frame.isNull()) {
        return;
    }
    latest_preview_frame_ = frame;
    if (!detection_requested_) {
        emit frameReady(frame, {});
        return;
    }

    frame_buffer_->replace(frame, frame_generation_);
    if (detection_active_) {
        scheduleLatestFrame();
    }
    emit frameReady(frame, detection_active_ ? latest_detections_ : QVector<VideoDetection>{});
}

void VideoInferenceController::setDetectionEnabled(bool enabled)
{
    if (shutdown_started_ || inference_worker_ == nullptr || enabled == detection_requested_) {
        return;
    }

    if (!enabled) {
        detection_requested_ = false;
        detection_active_ = false;
        ++detection_request_id_;
        ++frame_generation_;
        frame_buffer_->clear();
        latest_detections_.clear();
        inference_worker_->requestProcessing(false);
        emit disableDetectionRequested();
        if (!latest_preview_frame_.isNull()) {
            emit frameReady(latest_preview_frame_, {});
        }
        emit detectionStateChanged(VideoDetectionState::kDisabled, {});
        return;
    }

    detection_requested_ = true;
    detection_active_ = false;
    ++detection_request_id_;
    inference_worker_->requestProcessing(true);
    emit detectionStateChanged(VideoDetectionState::kLoading, tr("正在加载 YOLO26 模型"));
    emit enableDetectionRequested(detection_request_id_, modelDirectory());
}

void VideoInferenceController::clearPendingFrames()
{
    ++frame_generation_;
    frame_buffer_->clear();
    latest_preview_frame_ = {};
    latest_detections_.clear();
}

void VideoInferenceController::releaseModel()
{
    if (shutdown_started_) {
        return;
    }

    detection_requested_ = false;
    detection_active_ = false;
    ++detection_request_id_;
    ++frame_generation_;
    frame_buffer_->clear();
    latest_preview_frame_ = {};
    latest_detections_.clear();
    if (inference_worker_ == nullptr) {
        emit detectionStateChanged(VideoDetectionState::kDisabled, {});
        return;
    }

    inference_worker_->requestProcessing(false);
    emit disableDetectionRequested();
    emit releaseModelRequested();
    emit detectionStateChanged(VideoDetectionState::kDisabled, {});
}

void VideoInferenceController::shutdown()
{
    if (shutdown_started_) {
        return;
    }
    shutdown_started_ = true;
    detection_requested_ = false;
    detection_active_ = false;
    ++detection_request_id_;
    ++frame_generation_;
    frame_buffer_->clear();
    latest_preview_frame_ = {};
    latest_detections_.clear();
    if (inference_worker_ == nullptr || !inference_thread_->isRunning()) {
        emit stopped();
        return;
    }

    inference_worker_->requestProcessing(false);
    emit shutdownRequested();
}

void VideoInferenceController::handleModelReady(quint64 request_id)
{
    if (request_id != detection_request_id_ || !detection_requested_) {
        return;
    }
    detection_active_ = true;
    emit detectionStateChanged(VideoDetectionState::kEnabled, {});
    scheduleLatestFrame();
}

void VideoInferenceController::handleModelError(quint64 request_id, const QString &detail)
{
    if (request_id != detection_request_id_ || !detection_requested_) {
        return;
    }
    detection_requested_ = false;
    detection_active_ = false;
    ++frame_generation_;
    frame_buffer_->clear();
    latest_detections_.clear();
    if (inference_worker_ != nullptr) {
        inference_worker_->requestProcessing(false);
    }
    if (!latest_preview_frame_.isNull()) {
        emit frameReady(latest_preview_frame_, {});
    }
    emit detectionStateChanged(VideoDetectionState::kError, detail);
}

void VideoInferenceController::handleInferenceCompleted(const QVector<utms::VideoDetection> &detections,
                                                        quint64 generation)
{
    if (detection_requested_ && detection_active_ && generation == frame_generation_) {
        latest_detections_ = detections;
        if (!latest_preview_frame_.isNull()) {
            emit frameReady(latest_preview_frame_, latest_detections_);
        }
    }
}

void VideoInferenceController::handleInferenceError(const QString &detail)
{
    if (!detection_requested_) {
        return;
    }
    detection_requested_ = false;
    detection_active_ = false;
    ++frame_generation_;
    frame_buffer_->clear();
    latest_detections_.clear();
    if (!latest_preview_frame_.isNull()) {
        emit frameReady(latest_preview_frame_, {});
    }
    emit detectionStateChanged(VideoDetectionState::kError, detail);
}

void VideoInferenceController::scheduleLatestFrame()
{
    if (detection_active_ && inference_worker_ != nullptr && frame_buffer_->tryBeginProcessing()) {
        emit processLatestFramesRequested();
    }
}

QString VideoInferenceController::modelDirectory() const
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("models/yolo26"));
}

} // namespace utms
