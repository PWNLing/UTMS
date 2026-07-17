#include "core/GeofenceGeometry.h"

#include <algorithm>
#include <cmath>

namespace utms {
namespace {

constexpr double kCoordinateEpsilon = 1e-12;

bool isValidPosition(const GeoPosition &position) {
    return std::isfinite(position.latitude) && std::isfinite(position.longitude) && position.latitude >= -90.0 &&
           position.latitude <= 90.0 && position.longitude >= -180.0 && position.longitude <= 180.0 &&
           (std::abs(position.latitude) > kCoordinateEpsilon || std::abs(position.longitude) > kCoordinateEpsilon);
}

bool positionsEqual(const GeoPosition &left, const GeoPosition &right) {
    return std::abs(left.latitude - right.latitude) <= kCoordinateEpsilon &&
           std::abs(left.longitude - right.longitude) <= kCoordinateEpsilon;
}

double orientation(const GeoPosition &first, const GeoPosition &second, const GeoPosition &third) {
    return (second.longitude - first.longitude) * (third.latitude - first.latitude) -
           (second.latitude - first.latitude) * (third.longitude - first.longitude);
}

bool pointOnSegment(const GeoPosition &start, const GeoPosition &end, const GeoPosition &point) {
    if (std::abs(orientation(start, end, point)) > kCoordinateEpsilon) {
        return false;
    }
    return point.longitude >= std::min(start.longitude, end.longitude) - kCoordinateEpsilon &&
           point.longitude <= std::max(start.longitude, end.longitude) + kCoordinateEpsilon &&
           point.latitude >= std::min(start.latitude, end.latitude) - kCoordinateEpsilon &&
           point.latitude <= std::max(start.latitude, end.latitude) + kCoordinateEpsilon;
}

bool segmentsIntersect(const GeoPosition &first_start, const GeoPosition &first_end, const GeoPosition &second_start,
                       const GeoPosition &second_end) {
    const double first_orientation = orientation(first_start, first_end, second_start);
    const double second_orientation = orientation(first_start, first_end, second_end);
    const double third_orientation = orientation(second_start, second_end, first_start);
    const double fourth_orientation = orientation(second_start, second_end, first_end);

    const bool proper_intersection =
        ((first_orientation > kCoordinateEpsilon && second_orientation < -kCoordinateEpsilon) ||
         (first_orientation < -kCoordinateEpsilon && second_orientation > kCoordinateEpsilon)) &&
        ((third_orientation > kCoordinateEpsilon && fourth_orientation < -kCoordinateEpsilon) ||
         (third_orientation < -kCoordinateEpsilon && fourth_orientation > kCoordinateEpsilon));
    if (proper_intersection) {
        return true;
    }

    return pointOnSegment(first_start, first_end, second_start) || pointOnSegment(first_start, first_end, second_end) ||
           pointOnSegment(second_start, second_end, first_start) || pointOnSegment(second_start, second_end, first_end);
}

bool polygonSelfIntersects(const QVector<GeoPosition> &vertices) {
    const qsizetype vertex_count = vertices.size();
    for (qsizetype first_edge = 0; first_edge < vertex_count; ++first_edge) {
        const qsizetype first_end = (first_edge + 1) % vertex_count;
        for (qsizetype second_edge = first_edge + 1; second_edge < vertex_count; ++second_edge) {
            const qsizetype second_end = (second_edge + 1) % vertex_count;
            const bool adjacent = first_edge == second_edge || first_end == second_edge || second_end == first_edge;
            if (adjacent) {
                continue;
            }
            if (segmentsIntersect(vertices.at(first_edge), vertices.at(first_end), vertices.at(second_edge),
                                  vertices.at(second_end))) {
                return true;
            }
        }
    }
    return false;
}

double polygonSignedArea(const QVector<GeoPosition> &vertices) {
    double twice_area = 0.0;
    for (qsizetype index = 0; index < vertices.size(); ++index) {
        const GeoPosition &current = vertices.at(index);
        const GeoPosition &next = vertices.at((index + 1) % vertices.size());
        twice_area += current.longitude * next.latitude - next.longitude * current.latitude;
    }
    return twice_area / 2.0;
}

QString validatePolygon(const PolygonGeofence &polygon) {
    if (polygon.vertices.size() < 3 || polygon.vertices.size() > 20) {
        return QStringLiteral("多边形顶点数量必须为 3–20 个");
    }
    for (qsizetype index = 0; index < polygon.vertices.size(); ++index) {
        if (!isValidPosition(polygon.vertices.at(index))) {
            return QStringLiteral("多边形第 %1 个顶点坐标无效").arg(index + 1);
        }
        for (qsizetype previous_index = 0; previous_index < index; ++previous_index) {
            if (positionsEqual(polygon.vertices.at(index), polygon.vertices.at(previous_index))) {
                return QStringLiteral("多边形顶点不能重复");
            }
        }
    }
    if (polygonSelfIntersects(polygon.vertices)) {
        return QStringLiteral("多边形不能自相交");
    }
    if (std::abs(polygonSignedArea(polygon.vertices)) <= kCoordinateEpsilon) {
        return QStringLiteral("多边形面积必须大于 0");
    }
    return {};
}

} // namespace

GeofenceShape geofenceShape(const Geofence &geofence) {
    if (std::holds_alternative<CircleGeofence>(geofence.geometry)) {
        return GeofenceShape::kCircle;
    }
    if (std::holds_alternative<RectangleGeofence>(geofence.geometry)) {
        return GeofenceShape::kRectangle;
    }
    return GeofenceShape::kPolygon;
}

GeoPosition geofenceCenter(const Geofence &geofence) {
    if (const auto *circle = std::get_if<CircleGeofence>(&geofence.geometry); circle != nullptr) {
        return circle->center;
    }
    if (const auto *rectangle = std::get_if<RectangleGeofence>(&geofence.geometry); rectangle != nullptr) {
        return {(rectangle->southwest.latitude + rectangle->northeast.latitude) / 2.0,
                (rectangle->southwest.longitude + rectangle->northeast.longitude) / 2.0};
    }

    const auto &vertices = std::get<PolygonGeofence>(geofence.geometry).vertices;
    GeoPosition center;
    for (const GeoPosition &vertex : vertices) {
        center.latitude += vertex.latitude;
        center.longitude += vertex.longitude;
    }
    if (!vertices.isEmpty()) {
        center.latitude /= static_cast<double>(vertices.size());
        center.longitude /= static_cast<double>(vertices.size());
    }
    return center;
}

QString validateGeofence(const Geofence &geofence) {
    if (geofence.name.trimmed().isEmpty()) {
        return QStringLiteral("围栏名称不能为空");
    }
    if (const auto *circle = std::get_if<CircleGeofence>(&geofence.geometry); circle != nullptr) {
        if (!isValidPosition(circle->center)) {
            return QStringLiteral("圆形围栏中心坐标无效");
        }
        if (!std::isfinite(circle->radius_m) || circle->radius_m <= 0.0) {
            return QStringLiteral("圆形围栏半径必须大于 0 米");
        }
        return {};
    }
    if (const auto *rectangle = std::get_if<RectangleGeofence>(&geofence.geometry); rectangle != nullptr) {
        if (!isValidPosition(rectangle->southwest) || !isValidPosition(rectangle->northeast)) {
            return QStringLiteral("矩形围栏角点坐标无效");
        }
        if (rectangle->southwest.latitude >= rectangle->northeast.latitude ||
            rectangle->southwest.longitude >= rectangle->northeast.longitude) {
            return QStringLiteral("矩形围栏必须由西南角和东北角定义");
        }
        return {};
    }
    return validatePolygon(std::get<PolygonGeofence>(geofence.geometry));
}

} // namespace utms
