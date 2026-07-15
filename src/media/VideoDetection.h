#pragma once

#include <QColor>
#include <QMetaType>
#include <QRectF>
#include <QString>
#include <QVector>

namespace utms {

enum class VideoDetectionCategory { kPedestrian, kBicycle, kCar, kTruck, kUnknown };

struct VideoDetection {
    QRectF bounding_box;
    VideoDetectionCategory category = VideoDetectionCategory::kUnknown;
    float confidence = 0.0F;
};

VideoDetectionCategory videoDetectionCategoryFromClassName(const QString &class_name);
QString videoDetectionCategoryText(VideoDetectionCategory category);
QColor videoDetectionCategoryColor(VideoDetectionCategory category);

} // namespace utms

Q_DECLARE_METATYPE(utms::VideoDetection)
Q_DECLARE_METATYPE(QVector<utms::VideoDetection>)
