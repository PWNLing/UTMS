#pragma once

#include <QImage>
#include <QWidget>

#include "media/RtspStateMachine.h"

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
    void setFrame(const QImage &frame);

signals:
    void connectRequested(const QString &stream_url);
    void disconnectRequested();

private:
    QLineEdit *stream_url_line_edit_ = nullptr;
    QPushButton *connect_button_ = nullptr;
    QPushButton *disconnect_button_ = nullptr;
    QLabel *status_label_ = nullptr;
    VideoFrameWidget *frame_widget_ = nullptr;
};

} // namespace utms
