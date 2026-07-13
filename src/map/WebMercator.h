#pragma once

#include <QPointF>

#include "core/RadarTypes.h"

namespace utms
{

class WebMercator
{
    public:
    static constexpr int kTileSizePx = 256;
    static constexpr double kMaximumLatitude = 85.05112878;

    static QPointF geoToGlobalPixel(const GeoPosition &position, int zoom);
    static GeoPosition globalPixelToGeo(const QPointF &pixel, int zoom);
    static qreal worldSizePx(int zoom);
};

} // namespace utms
