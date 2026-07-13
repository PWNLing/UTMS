#include "media/RtspDecoderWorker.h"

#include <limits>
#include <memory>

#include <QByteArray>
#include <QDebug>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace utms {
namespace {

constexpr qint64 kNetworkTimeoutUs = 3'000'000;

QString ffmpegError(int error_code)
{
    char error_buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    if (av_strerror(error_code, error_buffer, sizeof(error_buffer)) < 0) {
        return QStringLiteral("FFmpeg error %1").arg(error_code);
    }
    return QString::fromUtf8(error_buffer);
}

struct FormatContextDeleter {
    void operator()(AVFormatContext *context) const { avformat_close_input(&context); }
};

struct CodecContextDeleter {
    void operator()(AVCodecContext *context) const { avcodec_free_context(&context); }
};

struct PacketDeleter {
    void operator()(AVPacket *packet) const { av_packet_free(&packet); }
};

struct FrameDeleter {
    void operator()(AVFrame *frame) const { av_frame_free(&frame); }
};

struct SwsContextDeleter {
    void operator()(SwsContext *context) const { sws_freeContext(context); }
};

struct DictionaryDeleter {
    void operator()(AVDictionary *dictionary) const { av_dict_free(&dictionary); }
};

using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;
using DictionaryPtr = std::unique_ptr<AVDictionary, DictionaryDeleter>;

bool setDictionaryOption(AVDictionary **options, const char *key, const QByteArray &value, QString &error)
{
    const int result = av_dict_set(options, key, value.constData(), 0);
    if (result >= 0) {
        return true;
    }
    error = QStringLiteral("无法设置 FFmpeg 选项 %1: %2").arg(QString::fromLatin1(key), ffmpegError(result));
    return false;
}

FormatContextPtr openRtspInput(const QString &stream_url, const AVIOInterruptCB &interrupt_callback,
                               QString &failure_detail)
{
    AVFormatContext *raw_format_context = avformat_alloc_context();
    if (raw_format_context == nullptr) {
        failure_detail = QObject::tr("无法分配 FFmpeg 输入上下文");
        return {};
    }
    raw_format_context->interrupt_callback = interrupt_callback;

    AVDictionary *raw_options = nullptr;
    const bool options_configured =
        setDictionaryOption(&raw_options, "rtsp_transport", QByteArrayLiteral("tcp"), failure_detail) &&
        setDictionaryOption(&raw_options, "rw_timeout", QByteArray::number(kNetworkTimeoutUs), failure_detail);
    if (!options_configured) {
        DictionaryPtr options(raw_options);
        avformat_free_context(raw_format_context);
        return {};
    }

    const QByteArray encoded_url = stream_url.toUtf8();
    const int open_result = avformat_open_input(&raw_format_context, encoded_url.constData(), nullptr, &raw_options);
    DictionaryPtr options(raw_options);
    if (open_result < 0) {
        if (raw_format_context != nullptr) {
            avformat_close_input(&raw_format_context);
        }
        failure_detail = QObject::tr("RTSP 连接失败: %1").arg(ffmpegError(open_result));
        return {};
    }
    return FormatContextPtr(raw_format_context);
}

struct VideoDecoderContext {
    int stream_index = -1;
    CodecContextPtr codec_context;
};

VideoDecoderContext openVideoDecoder(AVFormatContext &format_context, QString &failure_detail)
{
    VideoDecoderContext result;
    const int stream_info_result = avformat_find_stream_info(&format_context, nullptr);
    if (stream_info_result < 0) {
        failure_detail = QObject::tr("无法读取 RTSP 流信息: %1").arg(ffmpegError(stream_info_result));
        return result;
    }

    const AVCodec *decoder = nullptr;
    result.stream_index = av_find_best_stream(&format_context, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (result.stream_index < 0 || decoder == nullptr) {
        failure_detail = QObject::tr("RTSP 流不包含可解码的视频轨道: %1").arg(ffmpegError(result.stream_index));
        return result;
    }

    result.codec_context.reset(avcodec_alloc_context3(decoder));
    if (result.codec_context == nullptr) {
        failure_detail = QObject::tr("无法分配视频解码器上下文");
        return result;
    }

    const int parameters_result = avcodec_parameters_to_context(result.codec_context.get(),
                                                                format_context.streams[result.stream_index]->codecpar);
    if (parameters_result < 0) {
        failure_detail = QObject::tr("无法配置视频解码器: %1").arg(ffmpegError(parameters_result));
        return result;
    }

    const int decoder_result = avcodec_open2(result.codec_context.get(), decoder, nullptr);
    if (decoder_result < 0) {
        failure_detail = QObject::tr("无法打开视频解码器: %1").arg(ffmpegError(decoder_result));
    }
    return result;
}

QImage convertFrameToRgb(AVFrame &decoded_frame, SwsContextPtr &conversion_context, QString &failure_detail)
{
    QImage image(decoded_frame.width, decoded_frame.height, QImage::Format_RGB888);
    if (image.isNull()) {
        failure_detail = QObject::tr("无法分配视频画面缓冲区");
        return {};
    }

    conversion_context.reset(
        sws_getCachedContext(conversion_context.release(), decoded_frame.width, decoded_frame.height,
                             static_cast<AVPixelFormat>(decoded_frame.format), decoded_frame.width,
                             decoded_frame.height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr));
    if (conversion_context == nullptr) {
        failure_detail = QObject::tr("无法初始化视频像素格式转换");
        return {};
    }
    if (image.bytesPerLine() > std::numeric_limits<int>::max()) {
        failure_detail = QObject::tr("视频帧行跨度超过解码器限制");
        return {};
    }

    uint8_t *destination_data[] = {image.bits(), nullptr, nullptr, nullptr};
    const int destination_strides_bytes[] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};
    const int converted_height_px = sws_scale(conversion_context.get(), decoded_frame.data, decoded_frame.linesize, 0,
                                              decoded_frame.height, destination_data, destination_strides_bytes);
    if (converted_height_px != decoded_frame.height) {
        failure_detail = QObject::tr("视频帧像素格式转换失败");
        return {};
    }
    return image;
}

} // namespace

RtspDecoderWorker::RtspDecoderWorker(QObject *parent) : QObject(parent) {}

void RtspDecoderWorker::prepareStart() { stop_requested_.store(false, std::memory_order_release); }

void RtspDecoderWorker::requestStop() { stop_requested_.store(true, std::memory_order_release); }

void RtspDecoderWorker::decode(quint64 attempt_id, const QString &stream_url)
{
    if (stop_requested_.load(std::memory_order_acquire)) {
        emit decodingFinished(attempt_id);
        return;
    }

    bool playback_started = false;
    QString failure_detail;

    const int network_init_result = avformat_network_init();
    if (network_init_result < 0) {
        failure_detail = tr("FFmpeg 网络模块初始化失败: %1").arg(ffmpegError(network_init_result));
    }

    const AVIOInterruptCB interrupt_callback{&RtspDecoderWorker::interruptCallback, this};
    FormatContextPtr format_context;
    if (failure_detail.isEmpty()) {
        format_context = openRtspInput(stream_url, interrupt_callback, failure_detail);
    }

    VideoDecoderContext video_decoder;
    if (failure_detail.isEmpty() && !stop_requested_.load(std::memory_order_acquire)) {
        video_decoder = openVideoDecoder(*format_context, failure_detail);
    }

    PacketPtr packet(av_packet_alloc());
    FramePtr decoded_frame(av_frame_alloc());
    SwsContextPtr conversion_context;
    if (failure_detail.isEmpty() && (packet == nullptr || decoded_frame == nullptr)) {
        failure_detail = tr("无法分配视频解码缓冲区");
    }

    if (failure_detail.isEmpty() && !stop_requested_.load(std::memory_order_acquire)) {
        playback_started = true;
        emit playbackStarted(attempt_id);
    }

    while (playback_started && !stop_requested_.load(std::memory_order_acquire)) {
        const int read_result = av_read_frame(format_context.get(), packet.get());
        if (read_result < 0) {
            if (!stop_requested_.load(std::memory_order_acquire)) {
                failure_detail = tr("RTSP 视频流中断: %1").arg(ffmpegError(read_result));
            }
            break;
        }

        if (packet->stream_index != video_decoder.stream_index) {
            av_packet_unref(packet.get());
            continue;
        }

        const int send_result = avcodec_send_packet(video_decoder.codec_context.get(), packet.get());
        av_packet_unref(packet.get());
        if (send_result < 0 && send_result != AVERROR(EAGAIN)) {
            failure_detail = tr("视频数据送入解码器失败: %1").arg(ffmpegError(send_result));
            break;
        }

        while (!stop_requested_.load(std::memory_order_acquire)) {
            const int receive_result = avcodec_receive_frame(video_decoder.codec_context.get(), decoded_frame.get());
            if (receive_result == AVERROR(EAGAIN) || receive_result == AVERROR_EOF) {
                break;
            }
            if (receive_result < 0) {
                failure_detail = tr("视频帧解码失败: %1").arg(ffmpegError(receive_result));
                break;
            }

            const QImage image = convertFrameToRgb(*decoded_frame, conversion_context, failure_detail);
            if (image.isNull()) {
                break;
            }

            emit frameDecoded(attempt_id, image);
            av_frame_unref(decoded_frame.get());
        }

        if (!failure_detail.isEmpty()) {
            break;
        }
    }

    if (!stop_requested_.load(std::memory_order_acquire) && !failure_detail.isEmpty()) {
        if (playback_started) {
            emit playbackInterrupted(attempt_id, failure_detail);
        } else {
            emit connectionFailed(attempt_id, failure_detail);
        }
    }

    if (network_init_result >= 0) {
        const int deinit_result = avformat_network_deinit();
        if (deinit_result < 0) {
            qWarning() << "RtspDecoderWorker: FFmpeg network deinitialization failed:" << ffmpegError(deinit_result);
        }
    }
    emit decodingFinished(attempt_id);
}

int RtspDecoderWorker::interruptCallback(void *opaque)
{
    const auto *worker = static_cast<RtspDecoderWorker *>(opaque);
    return worker->stop_requested_.load(std::memory_order_acquire) ? 1 : 0;
}

} // namespace utms
