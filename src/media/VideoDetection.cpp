#include "media/VideoDetection.h"

#include <QCoreApplication>

namespace utms {

VideoDetectionCategory videoDetectionCategoryFromClassName(const QString &class_name)
{
    const QString normalized_name = class_name.trimmed().toLower();
    if (normalized_name == QStringLiteral("person")) {
        return VideoDetectionCategory::kPedestrian;
    }
    if (normalized_name == QStringLiteral("bicycle")) {
        return VideoDetectionCategory::kBicycle;
    }
    if (normalized_name == QStringLiteral("car")) {
        return VideoDetectionCategory::kCar;
    }
    if (normalized_name == QStringLiteral("truck")) {
        return VideoDetectionCategory::kTruck;
    }
    return VideoDetectionCategory::kUnknown;
}

QString videoDetectionCategoryText(VideoDetectionCategory category)
{
    switch (category) {
    case VideoDetectionCategory::kPedestrian:
        return QCoreApplication::translate("VideoDetection", "行人");
    case VideoDetectionCategory::kBicycle:
        return QCoreApplication::translate("VideoDetection", "自行车");
    case VideoDetectionCategory::kCar:
        return QCoreApplication::translate("VideoDetection", "汽车");
    case VideoDetectionCategory::kTruck:
        return QCoreApplication::translate("VideoDetection", "卡车");
    case VideoDetectionCategory::kUnknown:
        return QCoreApplication::translate("VideoDetection", "未知");
    }
    return QCoreApplication::translate("VideoDetection", "未知");
}

QColor videoDetectionCategoryColor(VideoDetectionCategory category)
{
    switch (category) {
    case VideoDetectionCategory::kPedestrian:
        return QColor(QStringLiteral("#2ecc71"));
    case VideoDetectionCategory::kBicycle:
        return QColor(QStringLiteral("#9b59b6"));
    case VideoDetectionCategory::kCar:
        return QColor(QStringLiteral("#3498db"));
    case VideoDetectionCategory::kTruck:
        return QColor(QStringLiteral("#e67e22"));
    case VideoDetectionCategory::kUnknown:
        return QColor(QStringLiteral("#95a5a6"));
    }
    return QColor(QStringLiteral("#95a5a6"));
}

} // namespace utms
