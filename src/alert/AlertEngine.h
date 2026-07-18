#pragma once

#include <optional>

#include <QHash>
#include <QVector>

#include "alert/AlertTypes.h"
#include "core/GeofenceTypes.h"

namespace utms {

class AlertEngine {
  public:
    void setGeofences(const QVector<Geofence> &geofences);
    void setRules(const QVector<AlertRule> &rules);
    QVector<TargetAlert> evaluateFrame(const RadarFrame &frame);
    void clearEvaluationState();

  private:
    struct RuleTrackState {
        std::optional<QDateTime> candidate_since;
        std::optional<QDateTime> inside_candidate_since;
        QDateTime last_seen_at;
        bool inside_confirmed = false;
        bool triggered = false;
    };

    static bool appliesToTarget(const AlertRule &rule, TargetType type);
    static bool contains(const Geofence &geofence, const GeoPosition &position);
    static double outsideDistanceMeters(const Geofence &geofence, const GeoPosition &position);
    static TargetAlert createAlert(const AlertRule &rule, const Geofence &geofence, const TrackData &track,
                                   const QDateTime &occurred_at);

    QHash<qint64, Geofence> geofences_by_id_;
    QVector<AlertRule> rules_;
    QHash<qint64, QHash<qint64, RuleTrackState>> states_by_rule_;
};

} // namespace utms
