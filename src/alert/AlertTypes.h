#pragma once

#include <optional>

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVector>

#include "core/RadarTypes.h"

namespace utms {

enum class AlertRuleType { kStableEntry, kStableExit, kDwellTimeout, kGeofenceSpeeding };

enum class AlertSeverity { kInfo, kWarning, kSevere };

struct AlertRule {
    qint64 id = 0;
    QString name;
    AlertRuleType type = AlertRuleType::kStableEntry;
    qint64 geofence_id = 0;
    QVector<TargetType> target_types;
    AlertSeverity severity = AlertSeverity::kWarning;
    int dwell_threshold_ms = 5'000;
    double speed_threshold_mps = 1.0;
    int confirmation_ms = 1'000;
    int cooldown_ms = 30'000;
    bool enabled = true;
    QString note;
};

struct TargetAlert {
    qint64 id = 0;
    QDateTime occurred_at;
    qint64 rule_id = 0;
    QString rule_name;
    AlertRuleType rule_type = AlertRuleType::kStableEntry;
    AlertSeverity severity = AlertSeverity::kWarning;
    qint64 geofence_id = 0;
    QString geofence_name;
    qint64 track_id = 0;
    TargetType target_type = TargetType::kUnknown;
    GeoPosition position;
    std::optional<double> velocity_mps;
    std::optional<double> distance_m;
    QString description;
    bool acknowledged = false;
    std::optional<QDateTime> acknowledged_at;
    QString acknowledged_by;
    QString handling_note;
};

struct AlertQuery {
    std::optional<QDateTime> start_time;
    std::optional<QDateTime> end_time;
    std::optional<AlertSeverity> severity;
    std::optional<qint64> rule_id;
    std::optional<qint64> geofence_id;
    std::optional<qint64> track_id;
    std::optional<TargetType> target_type;
    std::optional<bool> acknowledged;
};

struct AlertQueryResult {
    AlertQuery query;
    QVector<TargetAlert> alerts;
    int unacknowledged_count = 0;
};

struct AlertAcknowledgementRequest {
    QVector<qint64> alert_ids;
    QString handling_note;
};

struct AlertExportRequest {
    AlertQuery query;
    QString output_path;
};

QString alertRuleTypeDisplayName(AlertRuleType type);
QString alertSeverityDisplayName(AlertSeverity severity);
QString validateAlertRule(const AlertRule &rule);

} // namespace utms

Q_DECLARE_METATYPE(utms::AlertRule)
Q_DECLARE_METATYPE(utms::TargetAlert)
Q_DECLARE_METATYPE(utms::AlertQuery)
Q_DECLARE_METATYPE(utms::AlertQueryResult)
Q_DECLARE_METATYPE(utms::AlertAcknowledgementRequest)
Q_DECLARE_METATYPE(utms::AlertExportRequest)
Q_DECLARE_METATYPE(QVector<utms::AlertRule>)
Q_DECLARE_METATYPE(QVector<utms::TargetAlert>)
