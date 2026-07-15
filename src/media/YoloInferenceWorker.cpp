#include "media/YoloInferenceWorker.h"

#include <string>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QDebug>
#include <QPainter>

#include <onnxruntime_cxx_api.h>

#include "media/YoloModelConfig.h"
#include "media/YoloPostProcessor.h"

namespace utms {

struct YoloInferenceWorker::ModelSession {
    YoloModelConfig config;
    std::unique_ptr<Ort::Env> environment;
    std::unique_ptr<Ort::Session> session;
    std::string input_name;
    std::string output_name;
};

namespace {

QImage prepareInputImage(const QImage &frame, const YoloModelConfig &config)
{
    const QImage source = frame.convertToFormat(QImage::Format_RGB888);
    if (config.letterbox) {
        QImage result(config.input_size, QImage::Format_RGB888);
        result.fill(QColor(114, 114, 114));
        const QSize scaled_size = source.size().scaled(config.input_size, Qt::KeepAspectRatio);
        const QPoint offset((config.input_size.width() - scaled_size.width()) / 2,
                            (config.input_size.height() - scaled_size.height()) / 2);
        QPainter painter(&result);
        painter.drawImage(offset, source.scaled(scaled_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        return result;
    }
    return source.scaled(config.input_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

std::vector<float> makeInputTensor(const QImage &frame, const YoloModelConfig &config)
{
    const QImage input_image = prepareInputImage(frame, config);
    const int width = input_image.width();
    const int height = input_image.height();
    const size_t plane_size = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<float> tensor(plane_size * 3U);
    const float scale = config.normalize ? config.normalize_scale : 1.0F;

    for (int y = 0; y < height; ++y) {
        const auto *row = input_image.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            const size_t pixel_index = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            const uchar red = row[x * 3];
            const uchar green = row[x * 3 + 1];
            const uchar blue = row[x * 3 + 2];
            tensor[pixel_index] = static_cast<float>(config.swap_rb ? blue : red) * scale;
            tensor[plane_size + pixel_index] = static_cast<float>(green) * scale;
            tensor[plane_size * 2U + pixel_index] = static_cast<float>(config.swap_rb ? red : blue) * scale;
        }
    }
    return tensor;
}

} // namespace

YoloInferenceWorker::YoloInferenceWorker(std::shared_ptr<LatestVideoFrameBuffer> frame_buffer, QObject *parent)
    : QObject(parent), frame_buffer_(std::move(frame_buffer))
{
}

YoloInferenceWorker::~YoloInferenceWorker() = default;

void YoloInferenceWorker::requestProcessing(bool enabled)
{
    processing_requested_.store(enabled, std::memory_order_release);
}

void YoloInferenceWorker::enableDetection(quint64 request_id, const QString &model_directory)
{
    if (shutdown_started_) {
        return;
    }
    if (model_session_ == nullptr) {
        QString error;
        InitializationFailureStage failure_stage = InitializationFailureStage::kModelResource;
        if (!loadModel(model_directory, error, failure_stage)) {
            if (failure_stage == InitializationFailureStage::kInferenceRuntime) {
                qWarning() << "YoloInferenceWorker: failed to initialize YOLO26 inference from" << model_directory
                           << error;
            } else {
                qWarning() << "YoloInferenceWorker: failed to load YOLO26 model resources from" << model_directory
                           << error;
            }
            processing_requested_.store(false, std::memory_order_release);
            emit modelError(request_id, error);
            return;
        }
    }
    if (!processing_requested_.load(std::memory_order_acquire)) {
        return;
    }

    detection_enabled_ = true;
    qInfo() << "YoloInferenceWorker: YOLO26 model is ready";
    emit modelReady(request_id);
}

void YoloInferenceWorker::disableDetection()
{
    detection_enabled_ = false;
}

void YoloInferenceWorker::processLatestFrames()
{
    while (detection_enabled_ && processing_requested_.load(std::memory_order_acquire)) {
        const auto pending_frame = frame_buffer_->takeLatest();
        if (!pending_frame.has_value()) {
            return;
        }

        QString error;
        const QVector<VideoDetection> detections = runInference(pending_frame->frame, error);
        if (!processing_requested_.load(std::memory_order_acquire) || !detection_enabled_) {
            continue;
        }
        if (!error.isEmpty()) {
            qWarning() << "YoloInferenceWorker: inference failed:" << error;
            processing_requested_.store(false, std::memory_order_release);
            detection_enabled_ = false;
            frame_buffer_->finishProcessing();
            emit inferenceError(error);
            return;
        }
        emit inferenceCompleted(detections, pending_frame->generation);
    }

    frame_buffer_->finishProcessing();
}

void YoloInferenceWorker::releaseModel()
{
    detection_enabled_ = false;
    model_session_.reset();
}

void YoloInferenceWorker::shutdown()
{
    if (shutdown_started_) {
        return;
    }
    shutdown_started_ = true;
    processing_requested_.store(false, std::memory_order_release);
    releaseModel();
    emit stopped();
}

bool YoloInferenceWorker::loadModel(const QString &model_directory, QString &error,
                                    InitializationFailureStage &failure_stage)
{
    failure_stage = InitializationFailureStage::kModelResource;
    const auto config = YoloModelConfig::read(model_directory, &error);
    if (!config.has_value()) {
        return false;
    }

    failure_stage = InitializationFailureStage::kInferenceRuntime;
    try {
        auto session = std::make_unique<ModelSession>();
        session->config = *config;
        session->environment = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "UTMS-YOLO26");
        Ort::SessionOptions session_options;
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
#ifdef Q_OS_WIN
        const std::wstring model_path = session->config.model_path.toStdWString();
        session->session = std::make_unique<Ort::Session>(*session->environment, model_path.c_str(), session_options);
#else
        const QByteArray model_path = session->config.model_path.toUtf8();
        session->session =
            std::make_unique<Ort::Session>(*session->environment, model_path.constData(), session_options);
#endif
        if (session->session->GetInputCount() != 1 || session->session->GetOutputCount() < 1) {
            error = tr("YOLO26 模型必须包含一个输入和至少一个输出");
            return false;
        }

        Ort::AllocatorWithDefaultOptions allocator;
        const auto input_name = session->session->GetInputNameAllocated(0, allocator);
        const auto output_name = session->session->GetOutputNameAllocated(0, allocator);
        session->input_name = input_name.get();
        session->output_name = output_name.get();

        const auto input_shape = session->session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (input_shape.size() != 4 || input_shape.at(0) != 1 || input_shape.at(1) != 3 ||
            (input_shape.at(2) > 0 && input_shape.at(2) != session->config.input_size.height()) ||
            (input_shape.at(3) > 0 && input_shape.at(3) != session->config.input_size.width())) {
            error = tr("YOLO26 模型输入尺寸与 model.json 不一致");
            return false;
        }
        model_session_ = std::move(session);
        return true;
    } catch (const Ort::Exception &exception) {
        error = tr("无法初始化 YOLO26 ONNX 推理会话: %1").arg(QString::fromUtf8(exception.what()));
        return false;
    }
}

QVector<VideoDetection> YoloInferenceWorker::runInference(const QImage &frame, QString &error) const
{
    if (model_session_ == nullptr) {
        error = tr("YOLO26 模型尚未加载");
        return {};
    }
    if (frame.isNull()) {
        error = tr("无法对空视频帧执行检测");
        return {};
    }

    try {
        std::vector<float> input_values = makeInputTensor(frame, model_session_->config);
        const std::vector<int64_t> input_shape = {1, 3, model_session_->config.input_size.height(),
                                                  model_session_->config.input_size.width()};
        const Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info, input_values.data(), input_values.size(), input_shape.data(), input_shape.size());
        const char *input_names[] = {model_session_->input_name.c_str()};
        const char *output_names[] = {model_session_->output_name.c_str()};
        auto output_tensors = model_session_->session->Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1,
                                                           output_names, 1);
        if (output_tensors.empty() || !output_tensors.front().IsTensor()) {
            error = tr("YOLO26 模型未返回检测张量");
            return {};
        }

        const auto tensor_info = output_tensors.front().GetTensorTypeAndShapeInfo();
        if (tensor_info.GetElementType() != ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            error = tr("YOLO26 检测张量必须使用 float 类型");
            return {};
        }
        const size_t element_count = tensor_info.GetElementCount();
        const float *output_data = output_tensors.front().GetTensorData<float>();
        const std::vector<float> output_values(output_data, output_data + element_count);
        return YoloPostProcessor::process(output_values, tensor_info.GetShape(), model_session_->config, frame.size());
    } catch (const Ort::Exception &exception) {
        error = tr("YOLO26 推理失败: %1").arg(QString::fromUtf8(exception.what()));
        return {};
    }
}

} // namespace utms
