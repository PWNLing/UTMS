#pragma once

#include <QString>
#include <QVector>

#include "core/GeofenceTypes.h"
#include "history/HistoryTypes.h"

namespace utms {

class HistoryStore {
public:
    explicit HistoryStore(const QString &database_path);
    ~HistoryStore();

    HistoryStore(const HistoryStore &) = delete;
    HistoryStore &operator=(const HistoryStore &) = delete;

    bool initialize(QString *error_message = nullptr);
    std::optional<HistoryConfiguration> loadConfiguration(QString *error_message = nullptr) const;
    bool saveConfiguration(const HistoryConfiguration &configuration, QString *error_message = nullptr);
    std::optional<qint64> startSession(const QDateTime &started_at, QString *error_message = nullptr);
    bool closeActiveSession(const QDateTime &ended_at, QString *error_message = nullptr);
    std::optional<int> recoverAbandonedSessions(const QDateTime &recovered_at, QString *error_message = nullptr);
    std::optional<QVector<HistorySession>> loadSessions(QString *error_message = nullptr) const;
    bool appendFrame(qint64 session_id, const RadarFrame &frame, QString *error_message = nullptr);
    bool appendFrames(qint64 session_id, const QVector<RadarFrame> &frames, QString *error_message = nullptr);
    std::optional<HistoryQueryResult> queryHistory(const HistoryQuery &query, QString *error_message = nullptr) const;
    std::optional<int> exportCsv(const HistoryQuery &query, std::optional<qint64> selected_track_id,
                                 const QString &output_path, QString *error_message = nullptr) const;
    std::optional<int> cleanupExpiredHistory(const QDateTime &cutoff, QString *error_message = nullptr);
    bool deleteSession(qint64 session_id, QString *error_message = nullptr);
    std::optional<int> deleteAllSessions(QString *error_message = nullptr);
    std::optional<QVector<Geofence>> loadGeofences(QString *error_message = nullptr) const;
    std::optional<qint64> createGeofence(const Geofence &geofence, QString *error_message = nullptr);
    bool updateGeofence(const Geofence &geofence, QString *error_message = nullptr);
    bool updateGeofenceGeometry(const Geofence &geofence, QString *error_message = nullptr);
    bool setGeofenceEnabled(qint64 geofence_id, bool enabled, QString *error_message = nullptr);
    bool setGeofenceVisible(qint64 geofence_id, bool visible, QString *error_message = nullptr);
    bool deleteGeofence(qint64 geofence_id, QString *error_message = nullptr);
    bool probeWriteAccess(QString *error_message = nullptr);
    qint64 databaseSizeBytes() const;

private:
    void close();

    QString database_path_;
    QString connection_name_;
    bool initialized_ = false;
};

} // namespace utms
