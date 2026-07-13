#include "map/WebMercator.h"

#include <algorithm>
#include <cmath>

namespace utms
{

qreal WebMercator::worldSizePx(int zoom)
{
    return static_cast<qreal>(kTileSizePx) * static_cast<qreal>(quint64{1} << std::clamp(zoom, 0, 30));
}

QPointF WebMercator::geoToGlobalPixel(const GeoPosition &position, int zoom)
{
    // Web Mercator 只在约 ±85.05° 内有限；先裁剪输入，避免极区对数发散。
    constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
    const double latitude = std::clamp(position.latitude, -kMaximumLatitude, kMaximumLatitude);
    const double longitude = std::clamp(position.longitude, -180.0, 180.0);
    const double sine = std::sin(latitude * kDegreesToRadians);
    const qreal world_size_px = worldSizePx(zoom);
    const double x = (longitude + 180.0) / 360.0;
    const double y = 0.5 - std::log((1.0 + sine) / (1.0 - sine)) / (4.0 * 3.14159265358979323846);
    return {std::clamp(x * world_size_px, 0.0, static_cast<double>(world_size_px)),
            std::clamp(y * world_size_px, 0.0, static_cast<double>(world_size_px))};
}

GeoPosition WebMercator::globalPixelToGeo(const QPointF &pixel, int zoom)
{
    // 反投影与正投影使用同一全局像素空间，保证模式切换时中心点不会漂移。
    constexpr double kRadiansToDegrees = 180.0 / 3.14159265358979323846;
    const qreal world_size_px = worldSizePx(zoom);
    const double longitude = pixel.x() / world_size_px * 360.0 - 180.0;
    const double mercator_y = 3.14159265358979323846 * (1.0 - 2.0 * pixel.y() / world_size_px);
    const double latitude = std::atan(std::sinh(mercator_y)) * kRadiansToDegrees;
    return {std::clamp(latitude, -kMaximumLatitude, kMaximumLatitude), std::clamp(longitude, -180.0, 180.0)};
}

} // namespace utms
