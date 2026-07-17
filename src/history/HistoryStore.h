#pragma once

#include <optional>

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVector>

namespace utms {

enum class HistorySamplingRate
{
    kEveryFrame,
    kOneFps,
    kTwoFps,
    kFiveFps
};

struct HistoryConfiguration
{
    HistorySamplingRate sampling_rate = HistorySamplingRate::kTwoFps;
    int retention_days = 7;
};

enum class HistorySessionState
{
    kActive,
    kClosed,
    kAbnormal
};

struct HistorySession
{
    qint64 id = 0;
    QDateTime started_at;
    std::optional<QDateTime> ended_at;
    HistorySessionState state = HistorySessionState::kActive;
};

class HistoryStore
{
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

private:
    void close();

    QString database_path_;
    QString connection_name_;
    bool initialized_ = false;
};

} // namespace utms

Q_DECLARE_METATYPE(utms::HistorySamplingRate)
Q_DECLARE_METATYPE(utms::HistoryConfiguration)
Q_DECLARE_METATYPE(utms::HistorySessionState)
