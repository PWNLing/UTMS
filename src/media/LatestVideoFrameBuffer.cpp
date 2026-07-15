#include "media/LatestVideoFrameBuffer.h"

#include <QMutexLocker>

namespace utms {

void LatestVideoFrameBuffer::replace(const QImage &frame, quint64 generation)
{
    QMutexLocker locker(&mutex_);
    latest_frame_ = PendingVideoFrame{frame, generation};
}

void LatestVideoFrameBuffer::clear()
{
    QMutexLocker locker(&mutex_);
    latest_frame_.reset();
}

bool LatestVideoFrameBuffer::tryBeginProcessing()
{
    QMutexLocker locker(&mutex_);
    if (processing_ || !latest_frame_.has_value()) {
        return false;
    }
    processing_ = true;
    return true;
}

std::optional<PendingVideoFrame> LatestVideoFrameBuffer::takeLatest()
{
    QMutexLocker locker(&mutex_);
    if (!latest_frame_.has_value()) {
        processing_ = false;
        return std::nullopt;
    }

    auto result = std::move(latest_frame_);
    latest_frame_.reset();
    return result;
}

void LatestVideoFrameBuffer::finishProcessing()
{
    QMutexLocker locker(&mutex_);
    latest_frame_.reset();
    processing_ = false;
}

} // namespace utms
