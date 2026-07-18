#include "alert/AlertEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <QCoreApplication>
#include <QPointF>
#include <QSet>

namespace utms {
namespace {

constexpr double kEarthRadiusM = 6'371'000.0;
constexpr double kPi = 3.14159265358979323846;
constexpr int kMissingStateRetentionMs = 3'000;

double degreesToRadians(double degrees) { return degrees * kPi / 180.0; }

double distanceMeters(const GeoPosition &first, const GeoPosition &second) {
    const double latitude_delta = degreesToRadians(second.latitude - first.latitude);
    const double longitude_delta = degreesToRadians(second.longitude - first.longitude);
    const double first_latitude = degreesToRadians(first.latitude);
    const double second_latitude = degreesToRadians(second.latitude);
    const double haversine =
        std::pow(std::sin(latitude_delta / 2.0), 2.0) +
        std::cos(first_latitude) * std::cos(second_latitude) * std::pow(std::sin(longitude_delta / 2.0), 2.0);
    return 2.0 * kEarthRadiusM * std::asin(std::min(1.0, std::sqrt(haversine)));
}

double orientation(const GeoPosition &first, const GeoPosition &second, const GeoPosition &third) {
    return (second.longitude - first.longitude) * (third.latitude - first.latitude) -
           (second.latitude - first.latitude) * (third.longitude - first.longitude);
}

bool pointOnSegment(const GeoPosition &start, const GeoPosition &end, const GeoPosition &point) {
    constexpr double kCoordinateEpsilon = 1e-12;
    return std::abs(orientation(start, end, point)) <= kCoordinateEpsilon &&
           point.longitude >= std::min(start.longitude, end.longitude) - kCoordinateEpsilon &&
           point.longitude <= std::max(start.longitude, end.longitude) + kCoordinateEpsilon &&
           point.latitude >= std::min(start.latitude, end.latitude) - kCoordinateEpsilon &&
           point.latitude <= std::max(start.latitude, end.latitude) + kCoordinateEpsilon;
}

bool polygonContains(const QVector<GeoPosition> &vertices, const GeoPosition &position) {
    bool inside = false;
    for (qsizetype index = 0, previous = vertices.size() - 1; index < vertices.size(); previous = index++) {
        const GeoPosition &current_vertex = vertices.at(index);
        const GeoPosition &previous_vertex = vertices.at(previous);
        if (pointOnSegment(previous_vertex, current_vertex, position)) {
            return true;
        }
        const bool crosses_latitude =
            (current_vertex.latitude > position.latitude) != (previous_vertex.latitude > position.latitude);
        if (crosses_latitude) {
            const double crossing_longitude = (previous_vertex.longitude - current_vertex.longitude) *
                                                  (position.latitude - current_vertex.latitude) /
                                                  (previous_vertex.latitude - current_vertex.latitude) +
                                              current_vertex.longitude;
            if (position.longitude < crossing_longitude) {
                inside = !inside;
            }
        }
    }
    return inside;
}

QPointF localMeters(const GeoPosition &origin, const GeoPosition &position) {
    const double latitude_radians = degreesToRadians(origin.latitude);
    return {degreesToRadians(position.longitude - origin.longitude) * kEarthRadiusM * std::cos(latitude_radians),
            degreesToRadians(position.latitude - origin.latitude) * kEarthRadiusM};
}

double distanceToSegmentMeters(const GeoPosition &position, const GeoPosition &start, const GeoPosition &end) {
    const QPointF start_m = localMeters(position, start);
    const QPointF end_m = localMeters(position, end);
    const QPointF segment = end_m - start_m;
    const double squared_length = QPointF::dotProduct(segment, segment);
    if (squared_length <= 0.0) {
        return std::hypot(start_m.x(), start_m.y());
    }
    const double projection = std::clamp(-QPointF::dotProduct(start_m, segment) / squared_length, 0.0, 1.0);
    const QPointF closest = start_m + projection * segment;
    return std::hypot(closest.x(), closest.y());
}

QVector<GeoPosition> boundaryVertices(const Geofence &geofence) {
    if (const auto *rectangle = std::get_if<RectangleGeofence>(&geofence.geometry); rectangle != nullptr) {
        return {{rectangle->southwest.latitude, rectangle->southwest.longitude},
                {rectangle->southwest.latitude, rectangle->northeast.longitude},
                {rectangle->northeast.latitude, rectangle->northeast.longitude},
                {rectangle->northeast.latitude, rectangle->southwest.longitude}};
    }
    return std::get<PolygonGeofence>(geofence.geometry).vertices;
}

bool positionsEqual(const GeoPosition &left, const GeoPosition &right) {
    return left.latitude == right.latitude && left.longitude == right.longitude;
}

bool geometriesEqual(const GeofenceGeometry &left, const GeofenceGeometry &right) {
    if (left.index() != right.index()) {
        return false;
    }
    if (const auto *left_circle = std::get_if<CircleGeofence>(&left); left_circle != nullptr) {
        const auto &right_circle = std::get<CircleGeofence>(right);
        return positionsEqual(left_circle->center, right_circle.center) &&
               left_circle->radius_m == right_circle.radius_m;
    }
    if (const auto *left_rectangle = std::get_if<RectangleGeofence>(&left); left_rectangle != nullptr) {
        const auto &right_rectangle = std::get<RectangleGeofence>(right);
        return positionsEqual(left_rectangle->southwest, right_rectangle.southwest) &&
               positionsEqual(left_rectangle->northeast, right_rectangle.northeast);
    }
    const auto &left_vertices = std::get<PolygonGeofence>(left).vertices;
    const auto &right_vertices = std::get<PolygonGeofence>(right).vertices;
    if (left_vertices.size() != right_vertices.size()) {
        return false;
    }
    for (qsizetype index = 0; index < left_vertices.size(); ++index) {
        if (!positionsEqual(left_vertices.at(index), right_vertices.at(index))) {
            return false;
        }
    }
    return true;
}

bool evaluationConditionsEqual(const AlertRule &left, const AlertRule &right) {
    return left.type == right.type && left.geofence_id == right.geofence_id &&
           left.target_types == right.target_types && left.dwell_threshold_ms == right.dwell_threshold_ms &&
           left.speed_threshold_mps == right.speed_threshold_mps && left.confirmation_ms == right.confirmation_ms &&
           left.enabled == right.enabled;
}

} // namespace

void AlertEngine::setGeofences(const QVector<Geofence> &geofences) {
    QHash<qint64, Geofence> next_geofences_by_id;
    for (const Geofence &geofence : geofences) {
        next_geofences_by_id.insert(geofence.id, geofence);
    }

    QSet<qint64> changed_geofence_ids;
    for (auto iterator = geofences_by_id_.cbegin(); iterator != geofences_by_id_.cend(); ++iterator) {
        const auto next = next_geofences_by_id.constFind(iterator.key());
        if (next == next_geofences_by_id.cend() || iterator->enabled != next->enabled ||
            !geometriesEqual(iterator->geometry, next->geometry)) {
            changed_geofence_ids.insert(iterator.key());
        }
    }
    for (const AlertRule &rule : std::as_const(rules_)) {
        if (changed_geofence_ids.contains(rule.geofence_id)) {
            states_by_rule_.remove(rule.id);
        }
    }
    geofences_by_id_ = std::move(next_geofences_by_id);
}

void AlertEngine::setRules(const QVector<AlertRule> &rules) {
    QVector<AlertRule> next_rules;
    for (const AlertRule &rule : rules) {
        if (validateAlertRule(rule).isEmpty()) {
            next_rules.append(rule);
        }
    }

    QSet<qint64> next_rule_ids;
    for (const AlertRule &rule : std::as_const(next_rules)) {
        next_rule_ids.insert(rule.id);
        const auto previous = std::find_if(rules_.cbegin(), rules_.cend(),
                                           [&rule](const AlertRule &candidate) { return candidate.id == rule.id; });
        if (previous == rules_.cend() || !evaluationConditionsEqual(*previous, rule)) {
            states_by_rule_.remove(rule.id);
        }
    }
    for (const AlertRule &rule : std::as_const(rules_)) {
        if (!next_rule_ids.contains(rule.id)) {
            states_by_rule_.remove(rule.id);
            last_alerts_by_rule_.remove(rule.id);
        }
    }
    rules_ = std::move(next_rules);
}

QVector<TargetAlert> AlertEngine::evaluateFrame(const RadarFrame &frame) {
    // 调用方须在同一工作线程串行配置并传入已接收帧。本函数仅持有规则/航迹值状态，
    // 输出本帧首次确认的告警；无效规则会在配置阶段过滤，航迹暂失只清理状态而不合成离开告警。
    QVector<TargetAlert> alerts;
    for (const AlertRule &rule : std::as_const(rules_)) {
        if (!rule.enabled) {
            continue;
        }
        const auto geofence_iterator = geofences_by_id_.constFind(rule.geofence_id);
        if (geofence_iterator == geofences_by_id_.cend() || !geofence_iterator->enabled) {
            continue;
        }

        QHash<qint64, RuleTrackState> &rule_states = states_by_rule_[rule.id];
        QSet<qint64> seen_track_ids;
        for (const TrackData &track : frame.tracks) {
            if (!appliesToTarget(rule, track.type)) {
                continue;
            }
            seen_track_ids.insert(track.track_id);
            auto existing_state = rule_states.find(track.track_id);
            if (existing_state != rule_states.end() && existing_state->last_seen_at.isValid() &&
                existing_state->last_seen_at.msecsTo(frame.received_at) > kMissingStateRetentionMs) {
                rule_states.erase(existing_state);
            }
            RuleTrackState &state = rule_states[track.track_id];
            state.last_seen_at = frame.received_at;
            const bool inside = contains(geofence_iterator.value(), track.position);
            const double outside_distance_m =
                inside ? 0.0 : outsideDistanceMeters(geofence_iterator.value(), track.position);

            if (rule.type == AlertRuleType::kStableEntry) {
                if (!inside) {
                    state.candidate_since.reset();
                    if (outside_distance_m >= 5.0) {
                        state.triggered = false;
                    }
                    continue;
                }
                if (!state.candidate_since.has_value()) {
                    state.candidate_since = frame.received_at;
                }
                if (!state.triggered && state.candidate_since->msecsTo(frame.received_at) >= rule.confirmation_ms) {
                    state.triggered =
                        appendAlertIfCooldownElapsed(rule, geofence_iterator.value(), track, frame.received_at, alerts);
                }
                continue;
            }

            if (rule.type == AlertRuleType::kDwellTimeout) {
                if (!inside) {
                    state.candidate_since.reset();
                    state.triggered = false;
                    continue;
                }
                if (!state.candidate_since.has_value()) {
                    state.candidate_since = frame.received_at;
                }
                if (!state.triggered && state.candidate_since->msecsTo(frame.received_at) >= rule.dwell_threshold_ms) {
                    state.triggered =
                        appendAlertIfCooldownElapsed(rule, geofence_iterator.value(), track, frame.received_at, alerts);
                }
                continue;
            }

            if (rule.type == AlertRuleType::kGeofenceSpeeding) {
                const bool speeding = inside && track.velocity_mps.has_value() &&
                                      std::isfinite(track.velocity_mps.value()) &&
                                      track.velocity_mps.value() > rule.speed_threshold_mps;
                if (!speeding) {
                    state.candidate_since.reset();
                    state.triggered = false;
                    continue;
                }
                if (!state.candidate_since.has_value()) {
                    state.candidate_since = frame.received_at;
                }
                if (!state.triggered && state.candidate_since->msecsTo(frame.received_at) >= rule.confirmation_ms) {
                    state.triggered =
                        appendAlertIfCooldownElapsed(rule, geofence_iterator.value(), track, frame.received_at, alerts);
                }
                continue;
            }

            if (inside) {
                state.candidate_since.reset();
                if (!state.inside_candidate_since.has_value()) {
                    state.inside_candidate_since = frame.received_at;
                }
                if (state.inside_candidate_since->msecsTo(frame.received_at) >= rule.confirmation_ms) {
                    state.inside_confirmed = true;
                    state.triggered = false;
                }
                continue;
            }

            state.inside_candidate_since.reset();
            if (!state.inside_confirmed || state.triggered || outside_distance_m < 5.0) {
                state.candidate_since.reset();
                continue;
            }
            if (!state.candidate_since.has_value()) {
                state.candidate_since = frame.received_at;
            }
            if (state.candidate_since->msecsTo(frame.received_at) >= rule.confirmation_ms) {
                if (appendAlertIfCooldownElapsed(rule, geofence_iterator.value(), track, frame.received_at, alerts)) {
                    state.inside_confirmed = false;
                    state.triggered = true;
                }
            }
        }
        for (auto state_iterator = rule_states.begin(); state_iterator != rule_states.end();) {
            if (!seen_track_ids.contains(state_iterator.key()) && state_iterator->last_seen_at.isValid() &&
                state_iterator->last_seen_at.msecsTo(frame.received_at) > kMissingStateRetentionMs) {
                state_iterator = rule_states.erase(state_iterator);
            } else {
                ++state_iterator;
            }
        }
    }
    return alerts;
}

void AlertEngine::clearEvaluationState() {
    states_by_rule_.clear();
    last_alerts_by_rule_.clear();
}

bool AlertEngine::appliesToTarget(const AlertRule &rule, TargetType type) {
    return std::find(rule.target_types.cbegin(), rule.target_types.cend(), type) != rule.target_types.cend();
}

bool AlertEngine::contains(const Geofence &geofence, const GeoPosition &position) {
    if (const auto *circle = std::get_if<CircleGeofence>(&geofence.geometry); circle != nullptr) {
        return distanceMeters(circle->center, position) <= circle->radius_m;
    }
    if (const auto *rectangle = std::get_if<RectangleGeofence>(&geofence.geometry); rectangle != nullptr) {
        return position.latitude >= rectangle->southwest.latitude &&
               position.latitude <= rectangle->northeast.latitude &&
               position.longitude >= rectangle->southwest.longitude &&
               position.longitude <= rectangle->northeast.longitude;
    }
    return polygonContains(std::get<PolygonGeofence>(geofence.geometry).vertices, position);
}

double AlertEngine::outsideDistanceMeters(const Geofence &geofence, const GeoPosition &position) {
    if (contains(geofence, position)) {
        return 0.0;
    }
    if (const auto *circle = std::get_if<CircleGeofence>(&geofence.geometry); circle != nullptr) {
        return std::max(0.0, distanceMeters(circle->center, position) - circle->radius_m);
    }

    const QVector<GeoPosition> vertices = boundaryVertices(geofence);
    double minimum_distance_m = std::numeric_limits<double>::max();
    for (qsizetype index = 0; index < vertices.size(); ++index) {
        minimum_distance_m =
            std::min(minimum_distance_m,
                     distanceToSegmentMeters(position, vertices.at(index), vertices.at((index + 1) % vertices.size())));
    }
    return minimum_distance_m;
}

TargetAlert AlertEngine::createAlert(const AlertRule &rule, const Geofence &geofence, const TrackData &track,
                                     const QDateTime &occurred_at) {
    TargetAlert alert;
    alert.occurred_at = occurred_at;
    alert.rule_id = rule.id;
    alert.rule_name = rule.name;
    alert.rule_type = rule.type;
    alert.severity = rule.severity;
    alert.geofence_id = geofence.id;
    alert.geofence_name = geofence.name;
    alert.track_id = track.track_id;
    alert.target_type = track.type;
    alert.position = track.position;
    alert.velocity_mps = track.velocity_mps;
    alert.distance_m = track.distance_m;
    switch (rule.type) {
    case AlertRuleType::kStableEntry:
    case AlertRuleType::kStableExit:
        alert.description = QCoreApplication::translate("AlertEngine", "目标 %1 %2围栏“%3”")
                                .arg(track.track_id)
                                .arg(alertRuleTypeDisplayName(rule.type), geofence.name);
        break;
    case AlertRuleType::kDwellTimeout:
        alert.description = QCoreApplication::translate("AlertEngine", "目标 %1 在围栏“%2”内停留超过 %3 秒")
                                .arg(track.track_id)
                                .arg(geofence.name)
                                .arg(rule.dwell_threshold_ms / 1'000.0, 0, 'f', 1);
        break;
    case AlertRuleType::kGeofenceSpeeding:
        alert.description =
            QCoreApplication::translate("AlertEngine", "目标 %1 在围栏“%2”内速度 %3 m/s 超过阈值 %4 m/s")
                .arg(track.track_id)
                .arg(geofence.name)
                .arg(track.velocity_mps.value_or(0.0), 0, 'f', 1)
                .arg(rule.speed_threshold_mps, 0, 'f', 1);
        break;
    }
    return alert;
}

bool AlertEngine::appendAlertIfCooldownElapsed(const AlertRule &rule, const Geofence &geofence, const TrackData &track,
                                               const QDateTime &occurred_at, QVector<TargetAlert> &alerts) {
    const auto rule_alerts = last_alerts_by_rule_.constFind(rule.id);
    if (rule_alerts != last_alerts_by_rule_.cend()) {
        const auto last_alert = rule_alerts->constFind(track.track_id);
        if (last_alert != rule_alerts->cend() && last_alert->isValid() &&
            last_alert->msecsTo(occurred_at) < rule.cooldown_ms) {
            return false;
        }
    }
    alerts.append(createAlert(rule, geofence, track, occurred_at));
    last_alerts_by_rule_[rule.id].insert(track.track_id, occurred_at);
    return true;
}

} // namespace utms
