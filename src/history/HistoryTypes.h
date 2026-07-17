#pragma once

#include <optional>

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVector>

#include "core/RadarTypes.h"

namespace utms {

enum class HistorySamplingRate { kEveryFrame, kOneFps, kTwoFps, kFiveFps };

struct HistoryConfiguration {
    HistorySamplingRate sampling_rate = HistorySamplingRate::kTwoFps;
    int retention_days = 7;
};

enum class HistorySessionState { kActive, kClosed, kAbnormal };

struct HistorySession {
    qint64 id = 0;
    QDateTime started_at;
    std::optional<QDateTime> ended_at;
    HistorySessionState state = HistorySessionState::kActive;
};

struct HistoryQuery {
    std::optional<QDateTime> start_time;
    std::optional<QDateTime> end_time;
    std::optional<qint64> session_id;
    std::optional<qint64> track_id;
    std::optional<TargetType> target_type;
};

struct HistoryFrameRecord {
    qint64 frame_id = 0;
    qint64 session_id = 0;
    QDateTime frame_time;
    QDateTime received_at;
    std::optional<double> sender_timestamp_seconds;
    std::optional<qint64> sequence;
    std::optional<GeoPosition> ego_position;
    QVector<TrackData> tracks;
};

struct HistoryQueryResult {
    HistoryQuery query;
    QVector<HistoryFrameRecord> frames;

    int targetCount() const {
        int count = 0;
        for (const HistoryFrameRecord &frame : frames) {
            count += frame.tracks.size();
        }
        return count;
    }
};

struct HistoryExportRequest {
    HistoryQuery query;
    std::optional<qint64> selected_track_id;
    QString output_path;
};

} // namespace utms

Q_DECLARE_METATYPE(utms::HistorySamplingRate)
Q_DECLARE_METATYPE(utms::HistoryConfiguration)
Q_DECLARE_METATYPE(utms::HistorySessionState)
Q_DECLARE_METATYPE(utms::HistorySession)
Q_DECLARE_METATYPE(utms::HistoryQuery)
Q_DECLARE_METATYPE(utms::HistoryFrameRecord)
Q_DECLARE_METATYPE(utms::HistoryQueryResult)
Q_DECLARE_METATYPE(utms::HistoryExportRequest)
