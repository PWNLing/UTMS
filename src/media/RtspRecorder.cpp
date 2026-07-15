#include "media/RtspRecorder.h"

extern "C" {
#include <libavcodec/codec_id.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "media/VideoRecordingFileNamer.h"

namespace utms {
namespace {

QString ffmpegError(int error_code)
{
    char error_buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    if (av_strerror(error_code, error_buffer, sizeof(error_buffer)) < 0) {
        return QStringLiteral("FFmpeg error %1").arg(error_code);
    }
    return QString::fromUtf8(error_buffer);
}

QString formatDuration(qint64 duration_seconds)
{
    const qint64 hours = duration_seconds / 3'600;
    const qint64 minutes = (duration_seconds % 3'600) / 60;
    const qint64 seconds = duration_seconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

bool isSupportedVideoCodec(AVCodecID codec_id) { return codec_id == AV_CODEC_ID_H264 || codec_id == AV_CODEC_ID_HEVC; }

} // namespace

struct RtspRecorder::OutputContext {
    ~OutputContext()
    {
        if (format_context != nullptr) {
            if (format_context->pb != nullptr) {
                const int close_result = avio_closep(&format_context->pb);
                if (close_result < 0) {
                    qWarning() << "RtspRecorder: failed to close output during cleanup" << output_path
                               << ffmpegError(close_result);
                }
            }
            avformat_free_context(format_context);
        }
    }

    AVFormatContext *format_context = nullptr;
    AVStream *output_stream = nullptr;
    AVRational input_time_base{0, 1};
    QString output_path;
    qint64 first_timestamp = AV_NOPTS_VALUE;
    qint64 written_packet_count = 0;
    bool header_written = false;
};

RtspRecorder::RtspRecorder(QObject *parent)
    : QObject(parent)
{}

RtspRecorder::~RtspRecorder()
{
    if (output_ != nullptr) {
        const QString incomplete_path = output_->output_path;
        output_.reset();
        if (!incomplete_path.isEmpty() && QFileInfo::exists(incomplete_path) && !QFile::remove(incomplete_path)) {
            qWarning() << "RtspRecorder: failed to remove incomplete recording "
                          "during cleanup"
                       << incomplete_path;
        }
    }
}

VideoRecordingState RtspRecorder::state() const { return session_.state(); }

void RtspRecorder::requestStart(const QString &output_directory, qint64 requested_at_ms)
{
    if (!session_.requestStart(requested_at_ms)) {
        return;
    }

    output_directory_ = QDir::cleanPath(output_directory);
    QDir directory(output_directory_);
    if ((!directory.exists() && !directory.mkpath(QStringLiteral("."))) || !directory.exists()) {
        failRecording(tr("无法创建录制目录：%1").arg(output_directory_));
        return;
    }

    last_duration_seconds_ = -1;
    qInfo() << "RtspRecorder: recording requested; waiting for next keyframe in" << output_directory_;
    emit stateChanged(VideoRecordingState::kStarting, tr("准备录制"), {});
}

void RtspRecorder::requestStop() { applyStopRequest({}); }

void RtspRecorder::interrupt(const QString &reason) { applyStopRequest(reason); }

void RtspRecorder::applyStopRequest(const QString &reason)
{
    const VideoRecordingAction action = reason.isEmpty() ? session_.requestStop() : session_.requestInterruptionStop();
    if (action == VideoRecordingAction::kCancelPending) {
        if (reason.isEmpty()) {
            qInfo() << "RtspRecorder: pending recording cancelled before first keyframe";
        } else {
            qInfo() << "RtspRecorder: pending recording cancelled because" << reason;
        }
        emit stateChanged(VideoRecordingState::kIdle, {}, {});
    } else if (action == VideoRecordingAction::kFinalize) {
        if (!reason.isEmpty()) {
            qInfo() << "RtspRecorder: finalizing recording because" << reason;
        }
        emit stateChanged(VideoRecordingState::kStopping, tr("正在停止"), output_->output_path);
        finalizeRecording();
    } else if (reason.isEmpty() && session_.state() == VideoRecordingState::kIdle) {
        emit stateChanged(VideoRecordingState::kIdle, {}, {});
    }
}

void RtspRecorder::checkKeyframeTimeout()
{
    if (session_.checkKeyframeTimeout(videoRecordingMonotonicTimeMs()) == VideoRecordingAction::kKeyframeTimeout) {
        failRecording(tr("等待关键帧超时"));
    }
}

void RtspRecorder::processVideoPacket(const AVPacket &packet, const AVStream &input_stream)
{
    const bool is_keyframe = (packet.flags & AV_PKT_FLAG_KEY) != 0;
    const VideoRecordingAction action = session_.handleVideoPacket(is_keyframe, videoRecordingMonotonicTimeMs());
    if (action == VideoRecordingAction::kKeyframeTimeout) {
        failRecording(tr("等待关键帧超时"));
        return;
    }
    if (action != VideoRecordingAction::kBeginWriting && action != VideoRecordingAction::kWritePacket) {
        return;
    }

    QString failure_detail;
    if (action == VideoRecordingAction::kBeginWriting && !openOutput(input_stream, failure_detail)) {
        failRecording(failure_detail);
        return;
    }
    if (!writePacket(packet, failure_detail)) {
        failRecording(failure_detail);
        return;
    }

    if (action == VideoRecordingAction::kBeginWriting) {
        qInfo() << "RtspRecorder: recording started" << output_->output_path;
        emit stateChanged(VideoRecordingState::kRecording, tr("录制中"), output_->output_path);
    }
    emitDurationIfChanged();
}

bool RtspRecorder::openOutput(const AVStream &input_stream, QString &failure_detail)
{
    // 仅在解码线程调用；OutputContext 独占 FFmpeg 输出资源。先原子预留文件名，任一步失败均由调用方
    // 丢弃上下文和空文件，避免覆盖既有录像或影响主 RTSP 会话。
    if (input_stream.codecpar == nullptr || !isSupportedVideoCodec(input_stream.codecpar->codec_id)) {
        failure_detail = tr("仅支持 H.264/H.265 视频录像");
        return false;
    }

    output_ = std::make_unique<OutputContext>();
    while (output_->output_path.isEmpty()) {
        const QString candidate_path = nextVideoRecordingPath(output_directory_);
        QFile reservation_file(candidate_path);
        if (reservation_file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            reservation_file.close();
            output_->output_path = candidate_path;
        } else if (!QFileInfo::exists(candidate_path)) {
            failure_detail = tr("无法预留录像文件：%1").arg(reservation_file.errorString());
            return false;
        }
    }
    const QByteArray encoded_path = QFile::encodeName(output_->output_path);
    const int allocation_result =
        avformat_alloc_output_context2(&output_->format_context, nullptr, "mp4", encoded_path.constData());
    if (allocation_result < 0 || output_->format_context == nullptr) {
        failure_detail = tr("无法创建 MP4 输出：%1").arg(ffmpegError(allocation_result));
        return false;
    }

    output_->output_stream = avformat_new_stream(output_->format_context, nullptr);
    if (output_->output_stream == nullptr) {
        failure_detail = tr("无法创建 MP4 视频轨道");
        return false;
    }
    const int copy_result = avcodec_parameters_copy(output_->output_stream->codecpar, input_stream.codecpar);
    if (copy_result < 0) {
        failure_detail = tr("无法复制视频编码参数：%1").arg(ffmpegError(copy_result));
        return false;
    }
    output_->output_stream->codecpar->codec_tag = 0;
    output_->output_stream->time_base = input_stream.time_base;
    output_->input_time_base = input_stream.time_base;

    if ((output_->format_context->oformat->flags & AVFMT_NOFILE) == 0) {
        const int open_result = avio_open(&output_->format_context->pb, encoded_path.constData(), AVIO_FLAG_WRITE);
        if (open_result < 0) {
            failure_detail = tr("无法创建录像文件：%1").arg(ffmpegError(open_result));
            return false;
        }
    }
    const int header_result = avformat_write_header(output_->format_context, nullptr);
    if (header_result < 0) {
        failure_detail = tr("无法写入 MP4 文件头：%1").arg(ffmpegError(header_result));
        return false;
    }
    output_->header_written = true;
    return true;
}

bool RtspRecorder::writePacket(const AVPacket &packet, QString &failure_detail)
{
    if (output_ == nullptr || !output_->header_written || output_->output_stream == nullptr) {
        failure_detail = tr("录像输出尚未初始化");
        return false;
    }

    AVPacket output_packet{};
    const int reference_result = av_packet_ref(&output_packet, &packet);
    if (reference_result < 0) {
        failure_detail = tr("无法复制录像数据包：%1").arg(ffmpegError(reference_result));
        return false;
    }

    if (output_->first_timestamp == AV_NOPTS_VALUE) {
        output_->first_timestamp = packet.dts != AV_NOPTS_VALUE ? packet.dts : packet.pts;
    }
    if (output_->first_timestamp != AV_NOPTS_VALUE) {
        if (output_packet.pts != AV_NOPTS_VALUE) {
            output_packet.pts -= output_->first_timestamp;
        }
        if (output_packet.dts != AV_NOPTS_VALUE) {
            output_packet.dts -= output_->first_timestamp;
        }
    }
    av_packet_rescale_ts(&output_packet, output_->input_time_base, output_->output_stream->time_base);
    output_packet.stream_index = output_->output_stream->index;
    output_packet.pos = -1;
    const int write_result = av_interleaved_write_frame(output_->format_context, &output_packet);
    av_packet_unref(&output_packet);
    if (write_result < 0) {
        failure_detail = tr("写入录像数据失败：%1").arg(ffmpegError(write_result));
        return false;
    }
    ++output_->written_packet_count;
    return true;
}

void RtspRecorder::finalizeRecording()
{
    // 仅在解码线程封尾并释放输出资源；trailer、关闭或有效性检查任一失败都删除本次无效文件，
    // 成功时才向控制器发布最终路径和从首个关键帧开始计算的时长。
    const qint64 duration_seconds = session_.durationSeconds(videoRecordingMonotonicTimeMs());
    QString failure_detail;
    bool succeeded = output_ != nullptr && output_->header_written && output_->written_packet_count > 0;
    if (!succeeded) {
        failure_detail = tr("录像文件不包含有效视频数据");
    }

    if (succeeded) {
        const int trailer_result = av_write_trailer(output_->format_context);
        if (trailer_result < 0) {
            succeeded = false;
            failure_detail = tr("无法写入 MP4 文件尾：%1").arg(ffmpegError(trailer_result));
        }
    }
    if (output_ != nullptr && output_->format_context != nullptr && output_->format_context->pb != nullptr) {
        const int close_result = avio_closep(&output_->format_context->pb);
        if (close_result < 0) {
            succeeded = false;
            failure_detail = tr("无法关闭录像文件：%1").arg(ffmpegError(close_result));
        }
    }

    const QString completed_path = output_ != nullptr ? output_->output_path : QString();
    output_.reset();
    if (succeeded && (completed_path.isEmpty() || QFileInfo(completed_path).size() <= 0)) {
        succeeded = false;
        failure_detail = tr("录像文件无效");
    }
    session_.reportFinalized(succeeded);

    if (!succeeded) {
        if (!completed_path.isEmpty() && QFileInfo::exists(completed_path) && !QFile::remove(completed_path)) {
            qWarning() << "RtspRecorder: failed to remove invalid recording" << completed_path;
        }
        qWarning() << "RtspRecorder: recording finalization failed" << failure_detail << completed_path;
        emit stateChanged(VideoRecordingState::kError, failure_detail, {});
        return;
    }

    qInfo() << "RtspRecorder: recording saved" << completed_path << "duration seconds" << duration_seconds;
    emit durationChanged(duration_seconds);
    emit stateChanged(VideoRecordingState::kIdle, tr("保存完成（%1）").arg(formatDuration(duration_seconds)),
                      completed_path);
}

void RtspRecorder::failRecording(const QString &detail)
{
    const QString failed_path = output_ != nullptr ? output_->output_path : QString();
    session_.reportFailure();
    discardOutput();
    qWarning() << "RtspRecorder: recording failed" << detail << failed_path;
    emit stateChanged(VideoRecordingState::kError, detail, {});
}

void RtspRecorder::discardOutput()
{
    const QString failed_path = output_ != nullptr ? output_->output_path : QString();
    output_.reset();
    if (!failed_path.isEmpty() && QFileInfo::exists(failed_path) && !QFile::remove(failed_path)) {
        qWarning() << "RtspRecorder: failed to remove invalid recording" << failed_path;
    }
}

void RtspRecorder::emitDurationIfChanged()
{
    const qint64 duration_seconds = session_.durationSeconds(videoRecordingMonotonicTimeMs());
    if (duration_seconds != last_duration_seconds_) {
        last_duration_seconds_ = duration_seconds;
        emit durationChanged(duration_seconds);
    }
}

} // namespace utms
