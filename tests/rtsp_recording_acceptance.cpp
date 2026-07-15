extern "C" {
#include <libavcodec/codec_id.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QTimer>

#include "media/RtspController.h"

namespace {

constexpr int kAcceptanceTimeoutMs = 30'000;
constexpr int kRecordingDurationMs = 4'000;
constexpr int kReconnectRecordingDurationMs = 15'000;

QString ffmpegError(int error_code)
{
    char error_buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    return av_strerror(error_code, error_buffer, sizeof(error_buffer)) < 0 ? QString::number(error_code)
                                                                           : QString::fromUtf8(error_buffer);
}

bool verifyPlayableVideoRecording(const QString &recording_path, QString &failure_detail)
{
    AVFormatContext *format_context = nullptr;
    const QByteArray encoded_path = QFile::encodeName(recording_path);
    const int open_result = avformat_open_input(&format_context, encoded_path.constData(), nullptr, nullptr);
    if (open_result < 0) {
        failure_detail = QStringLiteral("无法重新打开 MP4：%1").arg(ffmpegError(open_result));
        return false;
    }

    const auto close_input = [&]() { avformat_close_input(&format_context); };
    const int stream_info_result = avformat_find_stream_info(format_context, nullptr);
    if (stream_info_result < 0) {
        failure_detail = QStringLiteral("无法读取 MP4 流信息：%1").arg(ffmpegError(stream_info_result));
        close_input();
        return false;
    }

    const int video_stream_index = av_find_best_stream(format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_index < 0) {
        failure_detail = QStringLiteral("MP4 不包含可读视频轨：%1").arg(ffmpegError(video_stream_index));
        close_input();
        return false;
    }
    const AVCodecID codec_id = format_context->streams[video_stream_index]->codecpar->codec_id;
    if (codec_id != AV_CODEC_ID_H264 && codec_id != AV_CODEC_ID_HEVC) {
        failure_detail = QStringLiteral("MP4 视频轨不是 H.264/H.265");
        close_input();
        return false;
    }

    AVPacket *packet = av_packet_alloc();
    if (packet == nullptr) {
        failure_detail = QStringLiteral("无法分配 MP4 验证数据包");
        close_input();
        return false;
    }
    bool readable_video_packet_found = false;
    for (int packet_count = 0; packet_count < 100 && av_read_frame(format_context, packet) >= 0; ++packet_count) {
        if (packet->stream_index == video_stream_index && packet->size > 0) {
            readable_video_packet_found = true;
        }
        av_packet_unref(packet);
        if (readable_video_packet_found) {
            break;
        }
    }
    av_packet_free(&packet);
    close_input();
    if (!readable_video_packet_found) {
        failure_detail = QStringLiteral("MP4 视频轨没有可读数据包");
    }
    return readable_video_packet_found;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    const QString stream_url = application.arguments().value(1, QStringLiteral("rtsp://127.0.0.1:8554/live/stream"));
    const bool expect_reconnect = application.arguments().contains(QStringLiteral("--expect-reconnect"));
    utms::RtspController controller;
    int exit_code = 1;
    bool recording_request_sent = false;
    bool recording_started = false;
    bool recording_saved = false;
    bool reconnect_verification_started = false;
    bool shutdown_requested = false;

    const auto request_shutdown = [&](int requested_exit_code) {
        if (shutdown_requested) {
            return;
        }
        shutdown_requested = true;
        exit_code = requested_exit_code;
        controller.shutdown();
    };

    QObject::connect(&controller, &utms::RtspController::stateChanged, &application,
                     [&](utms::RtspConnectionState state, const QString &detail) {
                         QTextStream(stdout) << "RTSP_STATE=" << static_cast<int>(state) << " " << detail << Qt::endl;
                         if (state == utms::RtspConnectionState::kPlaying && !recording_request_sent) {
                             recording_request_sent = true;
                             controller.startRecording();
                         } else if (state == utms::RtspConnectionState::kPlaying && expect_reconnect &&
                                    recording_saved && !reconnect_verification_started) {
                             reconnect_verification_started = true;
                             QTimer::singleShot(1'500, &application, [&]() {
                                 const bool recording_not_resumed =
                                     controller.recordingState() == utms::VideoRecordingState::kIdle;
                                 QTextStream(stdout)
                                     << "RECONNECT_RECORDING_STATE=" << static_cast<int>(controller.recordingState())
                                     << Qt::endl;
                                 request_shutdown(recording_not_resumed ? 0 : 5);
                             });
                         }
                     });
    QObject::connect(
        &controller, &utms::RtspController::recordingStateChanged, &application,
        [&](utms::VideoRecordingState state, const QString &detail, const QString &output_path) {
            QTextStream(stdout) << "RECORDING_STATE=" << static_cast<int>(state) << " " << detail << " " << output_path
                                << Qt::endl;
            if (state == utms::VideoRecordingState::kRecording && !recording_started) {
                recording_started = true;
                const int recording_duration_ms =
                    expect_reconnect ? kReconnectRecordingDurationMs : kRecordingDurationMs;
                QTimer::singleShot(recording_duration_ms, &controller, &utms::RtspController::stopRecording);
            } else if (state == utms::VideoRecordingState::kIdle && recording_started && !output_path.isEmpty()) {
                const QFileInfo recording_file(output_path);
                QTextStream(stdout) << "RECORDING_PATH=" << recording_file.absoluteFilePath() << Qt::endl;
                QString verification_failure;
                const bool recording_verified = recording_file.exists() && recording_file.size() > 0 &&
                                                verifyPlayableVideoRecording(output_path, verification_failure);
                QTextStream(stdout) << "RECORDING_VERIFIED=" << recording_verified << " " << verification_failure
                                    << Qt::endl;
                if (!recording_verified) {
                    request_shutdown(2);
                } else if (expect_reconnect) {
                    recording_saved = true;
                } else {
                    request_shutdown(0);
                }
            } else if (state == utms::VideoRecordingState::kError) {
                QTextStream(stderr) << "RECORDING_ERROR=" << detail << Qt::endl;
                request_shutdown(3);
            }
        });
    QObject::connect(&controller, &utms::RtspController::stopped, &application, [&]() { application.exit(exit_code); });
    QTimer::singleShot(kAcceptanceTimeoutMs, &application, [&]() {
        QTextStream(stderr) << "ACCEPTANCE_ERROR=timed out" << Qt::endl;
        request_shutdown(4);
    });

    controller.connectToStream(stream_url);
    return application.exec();
}
