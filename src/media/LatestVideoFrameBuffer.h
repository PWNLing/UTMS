#pragma once

#include <optional>

#include <QImage>
#include <QMutex>

namespace utms {

struct PendingVideoFrame {
    QImage frame;
    quint64 generation = 0;
};

// 解码端和推理端只共享一个可替换槽位，推理落后时不会积压旧视频帧。
class LatestVideoFrameBuffer {
public:
    void replace(const QImage &frame, quint64 generation);
    void clear();

    bool tryBeginProcessing();
    std::optional<PendingVideoFrame> takeLatest();
    void finishProcessing();

private:
    QMutex mutex_;
    std::optional<PendingVideoFrame> latest_frame_;
    bool processing_ = false;
};

} // namespace utms
