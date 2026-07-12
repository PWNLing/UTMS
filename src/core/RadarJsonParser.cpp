#include "core/RadarJsonParser.h"

#include <cmath>
#include <limits>

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace utms {
namespace {

std::optional<double> finiteNumber(const QJsonValue &value)
{
    double number = 0.0;
    if (value.isDouble()) {
        number = value.toDouble();
    } else if (value.isString()) {
        bool converted = false;
        number = value.toString().toDouble(&converted);
        if (!converted) {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }

    if (!std::isfinite(number)) {
        return std::nullopt;
    }
    return number;
}

std::optional<qint64> integerValue(const QJsonValue &value)
{
    const std::optional<double> number = finiteNumber(value);
    if (!number.has_value() || std::floor(number.value()) != number.value()
        || number.value() < static_cast<double>(std::numeric_limits<qint64>::min())
        || number.value() > static_cast<double>(std::numeric_limits<qint64>::max())) {
        return std::nullopt;
    }
    return static_cast<qint64>(number.value());
}

bool isValidCoordinate(double latitude, double longitude)
{
    return latitude >= -90.0 && latitude <= 90.0 && longitude >= -180.0
           && longitude <= 180.0 && (latitude != 0.0 || longitude != 0.0);
}

std::optional<GeoPosition> parsePosition(const QJsonValue &value)
{
    if (!value.isObject()) {
        return std::nullopt;
    }

    const QJsonObject object = value.toObject();
    const std::optional<double> latitude = finiteNumber(object.value(QStringLiteral("latitude")));
    const std::optional<double> longitude = finiteNumber(object.value(QStringLiteral("longitude")));
    if (!latitude.has_value() || !longitude.has_value()
        || !isValidCoordinate(latitude.value(), longitude.value())) {
        return std::nullopt;
    }

    return GeoPosition{latitude.value(), longitude.value()};
}

TargetType parseTargetType(const QJsonValue &value)
{
    const QString type = value.isString() ? value.toString().trimmed().toUpper() : QString();
    if (type == QStringLiteral("CAR")) {
        return TargetType::kCar;
    }
    if (type == QStringLiteral("TRUCK")) {
        return TargetType::kTruck;
    }
    if (type == QStringLiteral("PEDESTRIAN")) {
        return TargetType::kPedestrian;
    }
    if (type == QStringLiteral("BICYCLE")) {
        return TargetType::kBicycle;
    }
    return TargetType::kUnknown;
}

std::optional<double> parseMeasurement(const QJsonValue &value)
{
    const std::optional<double> measurement = finiteNumber(value);
    if (!measurement.has_value() || measurement.value() < 0.0) {
        return std::nullopt;
    }
    return measurement;
}

}  // namespace

RadarParseResult RadarJsonParser::parse(const QByteArray &payload, const QDateTime &received_at)
{
    RadarParseResult result;
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        result.error = QStringLiteral("JSON 解析失败: %1").arg(parse_error.errorString());
        return result;
    }
    if (!document.isObject()) {
        result.error = QStringLiteral("JSON 根节点必须是对象");
        return result;
    }

    const QJsonObject root = document.object();
    const QJsonValue tracks_value = root.value(QStringLiteral("tracks"));
    if (!tracks_value.isArray()) {
        result.error = QStringLiteral("缺少 tracks 数组");
        return result;
    }

    RadarFrame frame;
    frame.received_at = received_at;
    frame.sender_timestamp_seconds = finiteNumber(root.value(QStringLiteral("timestamp")));
    frame.ego_position = parsePosition(root.value(QStringLiteral("ego_position")));

    const QJsonValue header_value = root.value(QStringLiteral("header"));
    if (header_value.isObject()) {
        frame.sequence = integerValue(header_value.toObject().value(QStringLiteral("sequence")));
    }

    const QJsonArray tracks = tracks_value.toArray();
    QHash<qint64, qsizetype> track_indices;
    for (qsizetype index = 0; index < tracks.size(); ++index) {
        const QJsonValue track_value = tracks.at(index);
        if (!track_value.isObject()) {
            result.warnings.append(QStringLiteral("跳过索引 %1: 目标必须是对象").arg(index));
            continue;
        }

        const QJsonObject object = track_value.toObject();
        const std::optional<qint64> track_id = integerValue(object.value(QStringLiteral("track_id")));
        const std::optional<GeoPosition> position = parsePosition(object.value(QStringLiteral("position")));
        if (!track_id.has_value() || !position.has_value()) {
            result.warnings.append(QStringLiteral("跳过索引 %1: 航迹 ID 或坐标无效").arg(index));
            continue;
        }

        TrackData track;
        track.track_id = track_id.value();
        track.type = parseTargetType(object.value(QStringLiteral("type")));
        track.position = position.value();
        track.velocity_mps = parseMeasurement(object.value(QStringLiteral("velocity")));
        track.distance_m = parseMeasurement(object.value(QStringLiteral("distance")));

        const auto existing = track_indices.constFind(track.track_id);
        if (existing != track_indices.cend()) {
            frame.tracks[existing.value()] = track;
            result.warnings.append(QStringLiteral("重复航迹 ID %1，使用后项").arg(track.track_id));
        } else {
            track_indices.insert(track.track_id, frame.tracks.size());
            frame.tracks.append(track);
        }
    }

    const std::optional<qint64> target_count = integerValue(root.value(QStringLiteral("target_count")));
    if (target_count.has_value() && target_count.value() != tracks.size()) {
        result.warnings.append(QStringLiteral("target_count 与 tracks 数量不一致"));
    }

    result.frame = std::move(frame);
    return result;
}

}  // namespace utms
