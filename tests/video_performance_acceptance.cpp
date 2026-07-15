#include <memory>
#include <optional>

#include <QColor>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QTextStream>

#include "media/LatestVideoFrameBuffer.h"
#include "media/YoloInferenceWorker.h"
#include "media/YoloModelConfig.h"

namespace {

constexpr int kDefaultWarmupFrames = 5;
constexpr int kDefaultMeasuredFrames = 100;
constexpr double kMinimumThroughputFps = 10.0;

QString translatedText(const char *source_text)
{
    return QCoreApplication::translate("VideoPerformanceAcceptance", source_text);
}

std::optional<int> positiveOptionValue(const QCommandLineParser &parser, const QCommandLineOption &option)
{
    bool parsed = false;
    const int value = parser.value(option).toInt(&parsed);
    return parsed && value > 0 ? std::optional<int>(value) : std::nullopt;
}

bool processFrame(utms::YoloInferenceWorker &worker, const std::shared_ptr<utms::LatestVideoFrameBuffer> &frame_buffer,
                  const QImage &frame, quint64 generation, int &completed_count, QString &error)
{
    const int previous_count = completed_count;
    frame_buffer->replace(frame, generation);
    if (!frame_buffer->tryBeginProcessing()) {
        error = translatedText("推理帧缓冲区未进入处理状态");
        return false;
    }
    worker.processLatestFrames();
    if (!error.isEmpty()) {
        return false;
    }
    if (completed_count != previous_count + 1) {
        error = translatedText("推理工作线程未返回预期结果");
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(translatedText("UTMS YOLO26 CPU 性能验收"));

    QCommandLineParser parser;
    parser.setApplicationDescription(translatedText("测量 640×640 YOLO26n CPU 预处理、推理与后处理吞吐"));
    parser.addHelpOption();
    const QCommandLineOption warmup_option(QStringList{QStringLiteral("w"), QStringLiteral("warmup")},
                                           translatedText("预热帧数"), translatedText("count"),
                                           QString::number(kDefaultWarmupFrames));
    const QCommandLineOption iterations_option(QStringList{QStringLiteral("n"), QStringLiteral("iterations")},
                                               translatedText("计时帧数"), translatedText("count"),
                                               QString::number(kDefaultMeasuredFrames));
    parser.addOptions({warmup_option, iterations_option});
    parser.process(application);

    const auto warmup_frames = positiveOptionValue(parser, warmup_option);
    const auto measured_frames = positiveOptionValue(parser, iterations_option);
    if (!warmup_frames.has_value() || !measured_frames.has_value()) {
        QTextStream(stderr) << translatedText("warmup 和 iterations 必须是正整数") << '\n';
        return 2;
    }

    const QString model_directory =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("models/yolo26"));
    QString error;
    const auto model_config = utms::YoloModelConfig::read(model_directory, &error);
    if (!model_config.has_value()) {
        QTextStream(stderr) << translatedText("无法读取 YOLO26 验收模型：") << error << '\n';
        return 2;
    }
    if (QFileInfo(model_config->model_path).fileName() != QStringLiteral("yolo26n.onnx") ||
        model_config->input_size != QSize(640, 640)) {
        QTextStream(stderr) << translatedText("性能验收只允许使用 640×640 yolo26n.onnx") << '\n';
        return 2;
    }

    auto frame_buffer = std::make_shared<utms::LatestVideoFrameBuffer>();
    utms::YoloInferenceWorker worker(frame_buffer);
    bool model_ready = false;
    int completed_count = 0;
    QObject::connect(&worker, &utms::YoloInferenceWorker::modelReady, &application,
                     [&model_ready](quint64) { model_ready = true; });
    QObject::connect(&worker, &utms::YoloInferenceWorker::modelError, &application,
                     [&error](quint64, const QString &detail) { error = detail; });
    QObject::connect(&worker, &utms::YoloInferenceWorker::inferenceError, &application,
                     [&error](const QString &detail) { error = detail; });
    QObject::connect(&worker, &utms::YoloInferenceWorker::inferenceCompleted, &application,
                     [&completed_count](const QVector<utms::VideoDetection> &, quint64) { ++completed_count; });

    worker.requestProcessing(true);
    worker.enableDetection(1, model_directory);
    if (!model_ready || !error.isEmpty()) {
        QTextStream(stderr) << translatedText("YOLO26 初始化失败：") << error << '\n';
        return 2;
    }

    QImage frame(QSize(640, 640), QImage::Format_RGB888);
    frame.fill(QColor(32, 64, 96));
    quint64 generation = 0;
    for (int i = 0; i < *warmup_frames; ++i) {
        if (!processFrame(worker, frame_buffer, frame, ++generation, completed_count, error)) {
            QTextStream(stderr) << translatedText("YOLO26 预热失败：") << error << '\n';
            return 2;
        }
    }

    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < *measured_frames; ++i) {
        if (!processFrame(worker, frame_buffer, frame, ++generation, completed_count, error)) {
            QTextStream(stderr) << translatedText("YOLO26 性能测量失败：") << error << '\n';
            return 2;
        }
    }
    const qint64 elapsed_ns = timer.nsecsElapsed();
    worker.releaseModel();

    const double elapsed_seconds = static_cast<double>(elapsed_ns) / 1'000'000'000.0;
    const double throughput_fps = static_cast<double>(*measured_frames) / elapsed_seconds;
    const double average_latency_ms = static_cast<double>(elapsed_ns) / 1'000'000.0 / *measured_frames;
    const bool passed = throughput_fps >= kMinimumThroughputFps;

    QTextStream output(stdout);
    output.setRealNumberNotation(QTextStream::FixedNotation);
    output.setRealNumberPrecision(2);
    output << translatedText("模型目录：") << QDir::cleanPath(model_directory) << '\n'
           << translatedText("预热帧数：") << *warmup_frames << '\n'
           << translatedText("计时帧数：") << *measured_frames << '\n'
           << translatedText("CPU 检测吞吐：") << throughput_fps << translatedText(" FPS") << '\n'
           << translatedText("平均处理时延：") << average_latency_ms << translatedText(" ms") << '\n'
           << translatedText("验收阈值：>= ") << kMinimumThroughputFps << translatedText(" FPS") << '\n'
           << translatedText("结论：") << (passed ? translatedText("通过") : translatedText("不通过")) << '\n';
    return passed ? 0 : 3;
}
