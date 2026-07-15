#include "ui/VideoStreamWidget.h"

#include <algorithm>

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace utms {
namespace {

QString formatRecordingDuration(qint64 duration_seconds)
{
    const qint64 safe_duration_seconds = std::max<qint64>(0, duration_seconds);
    const qint64 hours = safe_duration_seconds / 3'600;
    const qint64 minutes = (safe_duration_seconds % 3'600) / 60;
    const qint64 seconds = safe_duration_seconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

} // namespace

class VideoFrameWidget : public QWidget {
public:
    explicit VideoFrameWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(320, 180);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setFrame(const QImage &frame)
    {
        frame_ = frame;
        detections_.clear();
        update();
    }

    void setFrame(const QImage &frame, const QVector<VideoDetection> &detections)
    {
        frame_ = frame;
        detections_ = detections;
        update();
    }

    void clearFrame()
    {
        if (!frame_.isNull()) {
            frame_ = QImage();
            detections_.clear();
            update();
        }
    }

    void clearDetections()
    {
        if (!detections_.isEmpty()) {
            detections_.clear();
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        QPainter painter(this);
        painter.fillRect(rect(), QColor(QStringLiteral("#20252b")));
        if (frame_.isNull()) {
            painter.setPen(QColor(QStringLiteral("#c7cbd1")));
            painter.drawText(rect(), Qt::AlignCenter, tr("无视频"));
            return;
        }

        const QSize target_size = frame_.size().scaled(size(), Qt::KeepAspectRatio);
        const QRect target_rect(QPoint((width() - target_size.width()) / 2, (height() - target_size.height()) / 2),
                                target_size);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(target_rect, frame_);

        const qreal scale_x = static_cast<qreal>(target_rect.width()) / frame_.width();
        const qreal scale_y = static_cast<qreal>(target_rect.height()) / frame_.height();
        painter.setRenderHint(QPainter::Antialiasing);
        for (const VideoDetection &detection : detections_) {
            const QRectF box(target_rect.x() + detection.bounding_box.x() * scale_x,
                             target_rect.y() + detection.bounding_box.y() * scale_y,
                             detection.bounding_box.width() * scale_x, detection.bounding_box.height() * scale_y);
            const QColor color = videoDetectionCategoryColor(detection.category);
            QPen pen(color);
            pen.setWidth(2);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(box);

            const QString label = tr("%1 %2%")
                                      .arg(videoDetectionCategoryText(detection.category))
                                      .arg(detection.confidence * 100.0F, 0, 'f', 1);
            const QFontMetrics metrics(painter.font());
            const QRect text_rect = metrics.boundingRect(label).adjusted(-4, -2, 4, 2);
            const QPoint label_origin(static_cast<int>(box.left()),
                                      std::max(target_rect.top() + text_rect.height(), static_cast<int>(box.top())));
            const QRect label_rect(label_origin.x(), label_origin.y() - text_rect.height(), text_rect.width(),
                                   text_rect.height());
            painter.fillRect(label_rect, color);
            painter.setPen(Qt::white);
            painter.drawText(label_rect, Qt::AlignCenter, label);
        }
    }

private:
    QImage frame_;
    QVector<VideoDetection> detections_;
};

VideoStreamWidget::VideoStreamWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *main_layout = new QVBoxLayout(this);
    auto *controls_layout = new QHBoxLayout();
    stream_url_line_edit_ = new QLineEdit(QString::fromLatin1(kDefaultStreamUrl), this);
    stream_url_line_edit_->setPlaceholderText(tr("请输入 RTSP 地址"));
    connect_button_ = new QPushButton(tr("连接"), this);
    disconnect_button_ = new QPushButton(tr("断开"), this);
    detection_button_ = new QPushButton(tr("开启检测"), this);
    recording_button_ = new QPushButton(tr("开始录制"), this);
    open_recording_directory_button_ = new QPushButton(tr("打开录制目录"), this);
    status_label_ = new QLabel(tr("已断开"), this);
    detection_status_label_ = new QLabel(tr("检测已关闭"), this);
    recording_status_label_ = new QLabel(tr("未录制"), this);
    recording_duration_label_ = new QLabel(tr("录像时长：00:00:00"), this);
    frame_widget_ = new VideoFrameWidget(this);

    controls_layout->addWidget(new QLabel(tr("RTSP 地址"), this));
    controls_layout->addWidget(stream_url_line_edit_, 1);
    controls_layout->addWidget(connect_button_);
    controls_layout->addWidget(disconnect_button_);
    controls_layout->addWidget(detection_button_);
    controls_layout->addWidget(recording_button_);
    controls_layout->addWidget(open_recording_directory_button_);
    main_layout->addLayout(controls_layout);
    main_layout->addWidget(status_label_);
    main_layout->addWidget(detection_status_label_);
    auto *recording_status_layout = new QHBoxLayout();
    recording_status_layout->addWidget(recording_status_label_, 1);
    recording_status_layout->addWidget(recording_duration_label_);
    main_layout->addLayout(recording_status_layout);
    main_layout->addWidget(frame_widget_, 1);

    connect(connect_button_, &QPushButton::clicked, this,
            [this]() { emit connectRequested(stream_url_line_edit_->text()); });
    connect(disconnect_button_, &QPushButton::clicked, this, &VideoStreamWidget::disconnectRequested);
    connect(detection_button_, &QPushButton::clicked, this,
            [this]() { emit detectionEnabledRequested(!detection_active_); });
    connect(recording_button_, &QPushButton::clicked, this, [this]() {
        if (recording_state_ == VideoRecordingState::kStarting || recording_state_ == VideoRecordingState::kRecording) {
            emit stopRecordingRequested();
        } else {
            emit startRecordingRequested();
        }
    });
    connect(open_recording_directory_button_, &QPushButton::clicked, this,
            &VideoStreamWidget::openRecordingDirectoryRequested);
    setConnectionState(RtspConnectionState::kDisconnected, tr("已断开"));
    setDetectionState(VideoDetectionState::kDisabled, {});
    setRecordingState(VideoRecordingState::kIdle, {}, {});
}

QString VideoStreamWidget::streamUrl() const { return stream_url_line_edit_->text(); }

void VideoStreamWidget::setConnectionState(RtspConnectionState state, const QString &detail)
{
    const bool disconnected = state == RtspConnectionState::kDisconnected;
    video_playing_ = state == RtspConnectionState::kPlaying;
    stream_url_line_edit_->setEnabled(disconnected);
    connect_button_->setEnabled(disconnected);
    disconnect_button_->setEnabled(!disconnected);

    QString state_text;
    QString color;
    switch (state) {
    case RtspConnectionState::kDisconnected:
        state_text = tr("已断开");
        color = QStringLiteral("#c0392b");
        break;
    case RtspConnectionState::kConnecting:
        state_text = tr("连接中");
        color = QStringLiteral("#d4a017");
        break;
    case RtspConnectionState::kPlaying:
        state_text = tr("播放中");
        color = QStringLiteral("#208a4b");
        break;
    case RtspConnectionState::kReconnecting:
        state_text = tr("重连中");
        color = QStringLiteral("#d4a017");
        break;
    }

    status_label_->setText(detail.isEmpty() ? state_text : tr("%1：%2").arg(state_text, detail));
    status_label_->setStyleSheet(QStringLiteral("QLabel { color: %1; font-weight: 600; }").arg(color));
    if (state != RtspConnectionState::kPlaying) {
        frame_widget_->clearFrame();
    }
    detection_button_->setEnabled(video_playing_ && !detection_loading_);
    updateRecordingControls();
}

void VideoStreamWidget::setDetectionState(VideoDetectionState state, const QString &detail)
{
    QString state_text;
    QString color;
    detection_active_ = state == VideoDetectionState::kEnabled;
    detection_loading_ = state == VideoDetectionState::kLoading;
    switch (state) {
    case VideoDetectionState::kDisabled:
        state_text = tr("检测已关闭");
        color = QStringLiteral("#70757a");
        frame_widget_->clearDetections();
        break;
    case VideoDetectionState::kLoading:
        state_text = tr("检测加载中");
        color = QStringLiteral("#d4a017");
        break;
    case VideoDetectionState::kEnabled:
        state_text = tr("检测已开启");
        color = QStringLiteral("#208a4b");
        break;
    case VideoDetectionState::kError:
        state_text = tr("检测不可用");
        color = QStringLiteral("#c0392b");
        frame_widget_->clearDetections();
        break;
    }

    detection_status_label_->setText(detail.isEmpty() ? state_text : tr("%1：%2").arg(state_text, detail));
    detection_status_label_->setStyleSheet(QStringLiteral("QLabel { color: %1; font-weight: 600; }").arg(color));
    detection_button_->setText(detection_active_ ? tr("关闭检测") : tr("开启检测"));
    detection_button_->setEnabled(video_playing_ && !detection_loading_);
}

void VideoStreamWidget::setRecordingState(VideoRecordingState state, const QString &detail, const QString &output_path)
{
    recording_state_ = state;
    QString state_text;
    QString color;
    switch (state) {
    case VideoRecordingState::kIdle:
        state_text = detail.isEmpty() ? tr("未录制") : detail;
        color = detail.isEmpty() ? QStringLiteral("#70757a") : QStringLiteral("#208a4b");
        break;
    case VideoRecordingState::kStarting:
        state_text = tr("准备录制");
        color = QStringLiteral("#d4a017");
        setRecordingDuration(0);
        break;
    case VideoRecordingState::kRecording:
        state_text = tr("录制中");
        color = QStringLiteral("#c0392b");
        break;
    case VideoRecordingState::kStopping:
        state_text = tr("正在停止");
        color = QStringLiteral("#d4a017");
        break;
    case VideoRecordingState::kError:
        state_text = detail.isEmpty() ? tr("录制失败") : tr("录制失败：%1").arg(detail);
        color = QStringLiteral("#c0392b");
        break;
    }

    recording_status_label_->setText(state_text);
    recording_status_label_->setToolTip(output_path);
    recording_status_label_->setStyleSheet(QStringLiteral("QLabel { color: %1; font-weight: 600; }").arg(color));
    updateRecordingControls();
}

void VideoStreamWidget::setRecordingDuration(qint64 duration_seconds)
{
    recording_duration_label_->setText(tr("录像时长：%1").arg(formatRecordingDuration(duration_seconds)));
}

void VideoStreamWidget::updateRecordingControls()
{
    const bool can_stop =
        recording_state_ == VideoRecordingState::kStarting || recording_state_ == VideoRecordingState::kRecording;
    recording_button_->setText(can_stop ? tr("停止录制") : tr("开始录制"));
    recording_button_->setEnabled(can_stop || (video_playing_ && recording_state_ != VideoRecordingState::kStopping));
}

void VideoStreamWidget::setFrame(const QImage &frame, const QVector<VideoDetection> &detections)
{
    frame_widget_->setFrame(frame, detections);
}

} // namespace utms
