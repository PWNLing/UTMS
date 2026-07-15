#pragma once

#include <QImage>
#include <QWidget>

#include "media/RtspStateMachine.h"
#include "media/VideoDetection.h"
#include "media/VideoInferenceController.h"
#include "media/VideoRecordingSession.h"

class QLabel;
class QLineEdit;
class QPushButton;

namespace utms {

class VideoFrameWidget;

class VideoStreamWidget : public QWidget {
    Q_OBJECT

public:
    static constexpr const char *kDefaultStreamUrl = "rtsp://192.168.1.101:8554/camera_1";

    explicit VideoStreamWidget(QWidget *parent = nullptr);

    QString streamUrl() const;
    void setConnectionState(RtspConnectionState state, const QString &detail);
    void setDetectionState(VideoDetectionState state, const QString &detail);
    void setRecordingState(VideoRecordingState state, const QString &detail, const QString &output_path);
    void setRecordingDuration(qint64 duration_seconds);
    void setFrame(const QImage &frame, const QVector<VideoDetection> &detections = {});

signals:
    void connectRequested(const QString &stream_url);
    void disconnectRequested();
    void detectionEnabledRequested(bool enabled);
    void startRecordingRequested();
    void stopRecordingRequested();
    void openRecordingDirectoryRequested();

private:
    void updateRecordingControls();

    QLineEdit *stream_url_line_edit_ = nullptr;
    QPushButton *connect_button_ = nullptr;
    QPushButton *disconnect_button_ = nullptr;
    QPushButton *detection_button_ = nullptr;
    QPushButton *recording_button_ = nullptr;
    QPushButton *open_recording_directory_button_ = nullptr;
    QLabel *status_label_ = nullptr;
    QLabel *detection_status_label_ = nullptr;
    QLabel *recording_status_label_ = nullptr;
    QLabel *recording_duration_label_ = nullptr;
    VideoFrameWidget *frame_widget_ = nullptr;
    bool video_playing_ = false;
    bool detection_active_ = false;
    bool detection_loading_ = false;
    VideoRecordingState recording_state_ = VideoRecordingState::kIdle;
};

} // namespace utms
