#include <QtTest>

#include <QFile>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include "core/RadarTypes.h"
#include "history/HistoryController.h"
#include "history/HistorySamplingPolicy.h"
#include "history/HistoryStore.h"

namespace {

utms::RadarFrame makeHistoryFrame(qint64 received_at_ms, qint64 sequence, const QVector<utms::TrackData> &tracks) {
    utms::RadarFrame frame;
    frame.received_at = QDateTime::fromMSecsSinceEpoch(received_at_ms, QTimeZone::UTC);
    frame.sender_timestamp_seconds = static_cast<double>(received_at_ms - 100) / 1'000.0;
    frame.sequence = sequence;
    frame.ego_position = utms::GeoPosition{25.31, 110.41};
    frame.tracks = tracks;
    frame.statistics = utms::calculateTargetStatistics(tracks);
    return frame;
}

utms::TrackData makeHistoryTrack(qint64 track_id, utms::TargetType type, double latitude, double longitude,
                                 std::optional<double> velocity_mps = std::nullopt,
                                 std::optional<double> distance_m = std::nullopt) {
    utms::TrackData track;
    track.track_id = track_id;
    track.type = type;
    track.position = {latitude, longitude};
    track.velocity_mps = velocity_mps;
    track.distance_m = distance_m;
    track.first_seen_at = QDateTime::fromMSecsSinceEpoch(1'000, QTimeZone::UTC);
    return track;
}

} // namespace

class HistoryStoreTest : public QObject {
    Q_OBJECT

private slots:
    void freshDatabaseUsesRequiredDefaults();
    void configurationPersistsAcrossReopen_data();
    void configurationPersistsAcrossReopen();
    void invalidConfigurationIsRejected();
    void samplingPolicyHonorsConfiguredRates_data();
    void samplingPolicyHonorsConfiguredRates();
    void sampledSnapshotsCanBeFilteredByTimeSessionTrackAndType();
    void emptySnapshotsRemainQueryable();
    void batchWritesRollbackAtomically();
    void csvExportIsLimitedToTheQueryOrSelectedTrack();
    void retentionDeletionAndDatabaseSizeManageStoredHistory();
    void deleteAllSessionsRequiresNoActiveSessionAndDeletesCascade();
    void controllerSamplesAcceptedFramesWithoutThrottlingTheCaller();
    void controllerDropsOutageFramesAndRecordsOnlyNewFramesAfterRecovery();
    void sessionLifecycleCreatesAndClosesOneDurableRecord();
    void abandonedSessionIsMarkedAbnormalWithoutDataLoss();
    void controllerDegradesSafelyWhenDatabaseIsUnavailable();
    void controllerRetriesInitializationAfterDatabaseRecovers();
};

void HistoryStoreTest::freshDatabaseUsesRequiredDefaults() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());

    utms::HistoryStore store(temporary_directory.filePath(QStringLiteral("history.sqlite")));
    QString error;
    QVERIFY2(store.initialize(&error), qPrintable(error));

    const std::optional<utms::HistoryConfiguration> configuration = store.loadConfiguration(&error);

    QVERIFY2(configuration.has_value(), qPrintable(error));
    QCOMPARE(configuration->sampling_rate, utms::HistorySamplingRate::kTwoFps);
    QCOMPARE(configuration->retention_days, 7);
}

void HistoryStoreTest::configurationPersistsAcrossReopen_data() {
    QTest::addColumn<utms::HistorySamplingRate>("sampling_rate");
    QTest::addColumn<int>("retention_days");

    QTest::newRow("one-fps") << utms::HistorySamplingRate::kOneFps << 1;
    QTest::newRow("two-fps") << utms::HistorySamplingRate::kTwoFps << 7;
    QTest::newRow("five-fps") << utms::HistorySamplingRate::kFiveFps << 30;
    QTest::newRow("every-frame") << utms::HistorySamplingRate::kEveryFrame << 14;
}

void HistoryStoreTest::configurationPersistsAcrossReopen() {
    QFETCH(utms::HistorySamplingRate, sampling_rate);
    QFETCH(int, retention_days);

    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());
    const QString database_path = temporary_directory.filePath(QStringLiteral("history.sqlite"));
    QString error;

    {
        utms::HistoryStore store(database_path);
        QVERIFY2(store.initialize(&error), qPrintable(error));
        const utms::HistoryConfiguration configuration{sampling_rate, retention_days};
        QVERIFY2(store.saveConfiguration(configuration, &error), qPrintable(error));
    }

    utms::HistoryStore reopened_store(database_path);
    QVERIFY2(reopened_store.initialize(&error), qPrintable(error));
    const std::optional<utms::HistoryConfiguration> configuration = reopened_store.loadConfiguration(&error);

    QVERIFY2(configuration.has_value(), qPrintable(error));
    QCOMPARE(configuration->sampling_rate, sampling_rate);
    QCOMPARE(configuration->retention_days, retention_days);
}

void HistoryStoreTest::invalidConfigurationIsRejected() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());

    utms::HistoryStore store(temporary_directory.filePath(QStringLiteral("history.sqlite")));
    QString error;
    QVERIFY2(store.initialize(&error), qPrintable(error));

    QVERIFY(!store.saveConfiguration({utms::HistorySamplingRate::kTwoFps, 0}, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!store.saveConfiguration({utms::HistorySamplingRate::kTwoFps, 31}, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!store.saveConfiguration({static_cast<utms::HistorySamplingRate>(99), 7}, &error));
    QVERIFY(!error.isEmpty());

    const std::optional<utms::HistoryConfiguration> configuration = store.loadConfiguration(&error);
    QVERIFY2(configuration.has_value(), qPrintable(error));
    QCOMPARE(configuration->sampling_rate, utms::HistorySamplingRate::kTwoFps);
    QCOMPARE(configuration->retention_days, 7);
}

void HistoryStoreTest::samplingPolicyHonorsConfiguredRates_data() {
    QTest::addColumn<utms::HistorySamplingRate>("sampling_rate");
    QTest::addColumn<int>("interval_ms");

    QTest::newRow("one-fps") << utms::HistorySamplingRate::kOneFps << 1'000;
    QTest::newRow("two-fps") << utms::HistorySamplingRate::kTwoFps << 500;
    QTest::newRow("five-fps") << utms::HistorySamplingRate::kFiveFps << 200;
    QTest::newRow("every-frame") << utms::HistorySamplingRate::kEveryFrame << 0;
}

void HistoryStoreTest::samplingPolicyHonorsConfiguredRates() {
    QFETCH(utms::HistorySamplingRate, sampling_rate);
    QFETCH(int, interval_ms);

    utms::HistorySamplingPolicy policy;
    QCOMPARE(policy.intervalMs(sampling_rate), std::optional<int>(interval_ms));
    QCOMPARE(policy.intervalMs(static_cast<utms::HistorySamplingRate>(99)), std::optional<int>());
}

void HistoryStoreTest::sampledSnapshotsCanBeFilteredByTimeSessionTrackAndType() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());

    utms::HistoryStore store(temporary_directory.filePath(QStringLiteral("history.sqlite")));
    QString error;
    QVERIFY2(store.initialize(&error), qPrintable(error));
    const std::optional<qint64> first_session_id =
        store.startSession(QDateTime::fromMSecsSinceEpoch(1'000, QTimeZone::UTC), &error);
    QVERIFY2(first_session_id.has_value(), qPrintable(error));
    const utms::RadarFrame first_frame =
        makeHistoryFrame(10'000, 1,
                         {makeHistoryTrack(101, utms::TargetType::kCar, 25.1, 110.1, 12.5, 50.0),
                          makeHistoryTrack(102, utms::TargetType::kPedestrian, 25.2, 110.2)});
    const utms::RadarFrame second_frame =
        makeHistoryFrame(20'000, 2, {makeHistoryTrack(103, utms::TargetType::kCar, 25.3, 110.3, 8.0, 20.0)});
    QVERIFY2(store.appendFrame(first_session_id.value(), first_frame, &error), qPrintable(error));
    QVERIFY2(store.appendFrame(first_session_id.value(), second_frame, &error), qPrintable(error));
    QVERIFY2(store.closeActiveSession(QDateTime::fromMSecsSinceEpoch(25'000, QTimeZone::UTC), &error),
             qPrintable(error));

    const std::optional<qint64> second_session_id =
        store.startSession(QDateTime::fromMSecsSinceEpoch(30'000, QTimeZone::UTC), &error);
    QVERIFY2(second_session_id.has_value(), qPrintable(error));
    const utms::RadarFrame third_frame =
        makeHistoryFrame(30'000, 3, {makeHistoryTrack(101, utms::TargetType::kTruck, 25.4, 110.4, 6.0, 10.0)});
    QVERIFY2(store.appendFrame(second_session_id.value(), third_frame, &error), qPrintable(error));

    const std::optional<utms::HistoryQueryResult> all_records = store.queryHistory({}, &error);
    QVERIFY2(all_records.has_value(), qPrintable(error));
    QCOMPARE(all_records->frames.size(), 3);
    QCOMPARE(all_records->targetCount(), 4);
    QCOMPARE(all_records->frames.at(0).session_id, first_session_id.value());
    QCOMPARE(all_records->frames.at(0).sequence, std::optional<qint64>(1));
    QCOMPARE(all_records->frames.at(0).tracks.at(0).track_id, 101);
    QCOMPARE(all_records->frames.at(0).tracks.at(0).velocity_mps, std::optional<double>(12.5));

    utms::HistoryQuery time_query;
    time_query.start_time = QDateTime::fromMSecsSinceEpoch(15'000, QTimeZone::UTC);
    time_query.end_time = QDateTime::fromMSecsSinceEpoch(25'000, QTimeZone::UTC);
    const std::optional<utms::HistoryQueryResult> time_records = store.queryHistory(time_query, &error);
    QVERIFY2(time_records.has_value(), qPrintable(error));
    QCOMPARE(time_records->frames.size(), 1);
    QCOMPARE(time_records->targetCount(), 1);
    QCOMPARE(time_records->frames.at(0).tracks.at(0).track_id, 103);

    utms::HistoryQuery session_query;
    session_query.session_id = first_session_id;
    const std::optional<utms::HistoryQueryResult> session_records = store.queryHistory(session_query, &error);
    QVERIFY2(session_records.has_value(), qPrintable(error));
    QCOMPARE(session_records->frames.size(), 2);
    QCOMPARE(session_records->targetCount(), 3);

    utms::HistoryQuery track_query;
    track_query.track_id = 101;
    const std::optional<utms::HistoryQueryResult> track_records = store.queryHistory(track_query, &error);
    QVERIFY2(track_records.has_value(), qPrintable(error));
    QCOMPARE(track_records->frames.size(), 2);
    QCOMPARE(track_records->targetCount(), 2);

    utms::HistoryQuery type_query;
    type_query.target_type = utms::TargetType::kCar;
    const std::optional<utms::HistoryQueryResult> type_records = store.queryHistory(type_query, &error);
    QVERIFY2(type_records.has_value(), qPrintable(error));
    QCOMPARE(type_records->frames.size(), 2);
    QCOMPARE(type_records->targetCount(), 2);
}

void HistoryStoreTest::emptySnapshotsRemainQueryable() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());

    utms::HistoryStore store(temporary_directory.filePath(QStringLiteral("history.sqlite")));
    QString error;
    QVERIFY2(store.initialize(&error), qPrintable(error));
    const std::optional<qint64> session_id =
        store.startSession(QDateTime::fromMSecsSinceEpoch(1'000, QTimeZone::UTC), &error);
    QVERIFY2(session_id.has_value(), qPrintable(error));
    QVERIFY2(store.appendFrame(session_id.value(), makeHistoryFrame(2'000, 1, {}), &error), qPrintable(error));

    const std::optional<utms::HistoryQueryResult> result = store.queryHistory({}, &error);

    QVERIFY2(result.has_value(), qPrintable(error));
    QCOMPARE(result->frames.size(), 1);
    QCOMPARE(result->frames.at(0).sequence, std::optional<qint64>(1));
    QVERIFY(result->frames.at(0).tracks.isEmpty());
}

void HistoryStoreTest::batchWritesRollbackAtomically() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());

    utms::HistoryStore store(temporary_directory.filePath(QStringLiteral("history.sqlite")));
    QString error;
    QVERIFY2(store.initialize(&error), qPrintable(error));
    const std::optional<qint64> session_id =
        store.startSession(QDateTime::fromMSecsSinceEpoch(1'000, QTimeZone::UTC), &error);
    QVERIFY2(session_id.has_value(), qPrintable(error));

    const utms::TrackData duplicate_track = makeHistoryTrack(71, utms::TargetType::kCar, 25.1, 110.1);
    const QVector<utms::RadarFrame> frames = {
        makeHistoryFrame(2'000, 1, {makeHistoryTrack(70, utms::TargetType::kCar, 25.0, 110.0)}),
        makeHistoryFrame(3'000, 2, {duplicate_track, duplicate_track}),
    };

    QVERIFY(!store.appendFrames(session_id.value(), frames, &error));
    QVERIFY(!error.isEmpty());
    const std::optional<utms::HistoryQueryResult> result = store.queryHistory({}, &error);
    QVERIFY2(result.has_value(), qPrintable(error));
    QVERIFY(result->frames.isEmpty());
}

void HistoryStoreTest::csvExportIsLimitedToTheQueryOrSelectedTrack() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());

    utms::HistoryStore store(temporary_directory.filePath(QStringLiteral("history.sqlite")));
    QString error;
    QVERIFY2(store.initialize(&error), qPrintable(error));
    const std::optional<qint64> session_id =
        store.startSession(QDateTime::fromMSecsSinceEpoch(1'000, QTimeZone::UTC), &error);
    QVERIFY2(session_id.has_value(), qPrintable(error));
    QVERIFY2(store.appendFrame(session_id.value(),
                               makeHistoryFrame(10'000, 7,
                                                {makeHistoryTrack(201, utms::TargetType::kCar, 25.1, 110.1),
                                                 makeHistoryTrack(202, utms::TargetType::kTruck, 25.2, 110.2)}),
                               &error),
             qPrintable(error));

    const QString query_csv_path = temporary_directory.filePath(QStringLiteral("query.csv"));
    utms::HistoryQuery query;
    query.target_type = utms::TargetType::kCar;
    const std::optional<int> query_export_count = store.exportCsv(query, std::nullopt, query_csv_path, &error);
    QVERIFY2(query_export_count.has_value(), qPrintable(error));
    QCOMPARE(query_export_count.value(), 1);

    QFile query_csv_file(query_csv_path);
    QVERIFY(query_csv_file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString query_csv = QString::fromUtf8(query_csv_file.readAll());
    QVERIFY(query_csv.contains(QStringLiteral("session_id,frame_time,received_at,sequence,track_id,type,latitude,"
                                              "longitude,velocity_mps,distance_m")));
    QVERIFY(query_csv.contains(QStringLiteral(",201,")));
    QVERIFY(!query_csv.contains(QStringLiteral(",202,")));
    QVERIFY(!query_csv.contains(QStringLiteral("altitude"), Qt::CaseInsensitive));

    const QString selected_csv_path = temporary_directory.filePath(QStringLiteral("selected.csv"));
    const std::optional<int> selected_export_count =
        store.exportCsv({}, std::optional<qint64>(202), selected_csv_path, &error);
    QVERIFY2(selected_export_count.has_value(), qPrintable(error));
    QCOMPARE(selected_export_count.value(), 1);

    QFile selected_csv_file(selected_csv_path);
    QVERIFY(selected_csv_file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString selected_csv = QString::fromUtf8(selected_csv_file.readAll());
    QVERIFY(!selected_csv.contains(QStringLiteral(",201,")));
    QVERIFY(selected_csv.contains(QStringLiteral(",202,")));
}

void HistoryStoreTest::retentionDeletionAndDatabaseSizeManageStoredHistory() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());

    utms::HistoryStore store(temporary_directory.filePath(QStringLiteral("history.sqlite")));
    QString error;
    QVERIFY2(store.initialize(&error), qPrintable(error));
    const QDateTime old_time = QDateTime::fromMSecsSinceEpoch(1'000, QTimeZone::UTC);
    const QDateTime recent_time = QDateTime::fromMSecsSinceEpoch(200'000, QTimeZone::UTC);

    const std::optional<qint64> old_session_id = store.startSession(old_time, &error);
    QVERIFY2(old_session_id.has_value(), qPrintable(error));
    QVERIFY2(store.appendFrame(
                 old_session_id.value(),
                 makeHistoryFrame(10'000, 1, {makeHistoryTrack(301, utms::TargetType::kUnknown, 25.1, 110.1)}), &error),
             qPrintable(error));
    QVERIFY2(store.closeActiveSession(old_time.addSecs(10), &error), qPrintable(error));

    const std::optional<qint64> recent_session_id = store.startSession(recent_time, &error);
    QVERIFY2(recent_session_id.has_value(), qPrintable(error));
    QVERIFY2(store.appendFrame(
                 recent_session_id.value(),
                 makeHistoryFrame(200'000, 2, {makeHistoryTrack(302, utms::TargetType::kCar, 25.2, 110.2)}), &error),
             qPrintable(error));
    QVERIFY2(store.closeActiveSession(recent_time.addSecs(10), &error), qPrintable(error));

    const std::optional<int> cleanup_count =
        store.cleanupExpiredHistory(QDateTime::fromMSecsSinceEpoch(100'000, QTimeZone::UTC), &error);
    QVERIFY2(cleanup_count.has_value(), qPrintable(error));
    QCOMPARE(cleanup_count.value(), 1);
    std::optional<QVector<utms::HistorySession>> sessions = store.loadSessions(&error);
    QVERIFY2(sessions.has_value(), qPrintable(error));
    QCOMPARE(sessions->size(), 1);
    QCOMPARE(sessions->at(0).id, recent_session_id.value());

    const std::optional<qint64> active_session_id = store.startSession(recent_time.addSecs(20), &error);
    QVERIFY2(active_session_id.has_value(), qPrintable(error));
    QVERIFY(!store.deleteSession(active_session_id.value(), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY2(store.deleteSession(recent_session_id.value(), &error), qPrintable(error));

    sessions = store.loadSessions(&error);
    QVERIFY2(sessions.has_value(), qPrintable(error));
    QCOMPARE(sessions->size(), 1);
    QCOMPARE(sessions->at(0).id, active_session_id.value());
    QVERIFY(store.databaseSizeBytes() > 0);
}

void HistoryStoreTest::deleteAllSessionsRequiresNoActiveSessionAndDeletesCascade() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());

    utms::HistoryStore store(temporary_directory.filePath(QStringLiteral("history.sqlite")));
    QString error;
    QVERIFY2(store.initialize(&error), qPrintable(error));

    const std::optional<qint64> first_session_id =
        store.startSession(QDateTime::fromMSecsSinceEpoch(1'000, QTimeZone::UTC), &error);
    QVERIFY2(first_session_id.has_value(), qPrintable(error));
    QVERIFY2(store.appendFrame(first_session_id.value(),
                               makeHistoryFrame(2'000, 1, {makeHistoryTrack(901, utms::TargetType::kCar, 25.1, 110.1)}),
                               &error),
             qPrintable(error));
    QVERIFY2(store.closeActiveSession(QDateTime::fromMSecsSinceEpoch(3'000, QTimeZone::UTC), &error),
             qPrintable(error));

    const std::optional<qint64> second_session_id =
        store.startSession(QDateTime::fromMSecsSinceEpoch(4'000, QTimeZone::UTC), &error);
    QVERIFY2(second_session_id.has_value(), qPrintable(error));
    QVERIFY2(store.appendFrame(
                 second_session_id.value(),
                 makeHistoryFrame(5'000, 2, {makeHistoryTrack(902, utms::TargetType::kTruck, 25.2, 110.2)}), &error),
             qPrintable(error));

    const std::optional<int> rejected_count = store.deleteAllSessions(&error);
    QVERIFY(!rejected_count.has_value());
    QVERIFY(error.contains(QStringLiteral("正在记录")));
    std::optional<QVector<utms::HistorySession>> sessions = store.loadSessions(&error);
    QVERIFY2(sessions.has_value(), qPrintable(error));
    QCOMPARE(sessions->size(), 2);

    QVERIFY2(store.closeActiveSession(QDateTime::fromMSecsSinceEpoch(6'000, QTimeZone::UTC), &error),
             qPrintable(error));
    const std::optional<int> deleted_count = store.deleteAllSessions(&error);
    QVERIFY2(deleted_count.has_value(), qPrintable(error));
    QCOMPARE(deleted_count.value(), 2);

    sessions = store.loadSessions(&error);
    QVERIFY2(sessions.has_value(), qPrintable(error));
    QVERIFY(sessions->isEmpty());
    const std::optional<utms::HistoryQueryResult> history = store.queryHistory({}, &error);
    QVERIFY2(history.has_value(), qPrintable(error));
    QVERIFY(history->frames.isEmpty());
}

void HistoryStoreTest::controllerSamplesAcceptedFramesWithoutThrottlingTheCaller() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());
    const QString database_path = temporary_directory.filePath(QStringLiteral("history.sqlite"));

    utms::HistoryController controller;
    QSignalSpy query_spy(&controller, &utms::HistoryController::queryCompleted);
    controller.initialize(database_path);
    controller.saveConfiguration({utms::HistorySamplingRate::kFiveFps, 7});
    controller.startSession();

    controller.recordAcceptedFrame(
        makeHistoryFrame(10'000, 1, {makeHistoryTrack(401, utms::TargetType::kCar, 25.0, 110.0)}));
    QTest::qWait(100);
    controller.recordAcceptedFrame(
        makeHistoryFrame(10'100, 2, {makeHistoryTrack(402, utms::TargetType::kCar, 25.0, 110.0)}));
    QTest::qWait(130);
    controller.queryHistory({});

    QCOMPARE(query_spy.count(), 1);
    utms::HistoryQueryResult result = qvariant_cast<utms::HistoryQueryResult>(query_spy.at(0).at(0));
    QCOMPARE(result.frames.size(), 1);
    QCOMPARE(result.frames.at(0).sequence, std::optional<qint64>(2));

    controller.recordAcceptedFrame(
        makeHistoryFrame(10'300, 3, {makeHistoryTrack(403, utms::TargetType::kCar, 25.0, 110.0)}));
    QTest::qWait(60);
    controller.recordAcceptedFrame(
        makeHistoryFrame(10'400, 4, {makeHistoryTrack(404, utms::TargetType::kCar, 25.0, 110.0)}));
    QTest::qWait(140);
    controller.queryHistory({});

    QCOMPARE(query_spy.count(), 2);
    result = qvariant_cast<utms::HistoryQueryResult>(query_spy.at(1).at(0));
    QCOMPARE(result.frames.size(), 2);
    QCOMPARE(result.frames.at(1).sequence, std::optional<qint64>(4));
}

void HistoryStoreTest::controllerDropsOutageFramesAndRecordsOnlyNewFramesAfterRecovery() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());
    const QString database_path = temporary_directory.filePath(QStringLiteral("history.sqlite"));

    utms::HistoryController controller;
    QSignalSpy availability_spy(&controller, &utms::HistoryController::availabilityChanged);
    controller.initialize(database_path);
    controller.saveConfiguration({utms::HistorySamplingRate::kEveryFrame, 7});
    controller.startSession();

    const QString lock_connection_name =
        QStringLiteral("history_lock_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase lock_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), lock_connection_name);
        lock_database.setDatabaseName(database_path);
        QVERIFY(lock_database.open());
        QSqlQuery lock_query(lock_database);
        QVERIFY(lock_query.exec(QStringLiteral("PRAGMA busy_timeout = 0")));
        QVERIFY(lock_query.exec(QStringLiteral("BEGIN IMMEDIATE")));

        controller.recordAcceptedFrame(
            makeHistoryFrame(20'000, 1, {makeHistoryTrack(501, utms::TargetType::kCar, 25.0, 110.0)}));
        controller.recordAcceptedFrame(
            makeHistoryFrame(21'000, 2, {makeHistoryTrack(502, utms::TargetType::kTruck, 25.1, 110.1)}));
        controller.queryHistory({});

        QVERIFY(availability_spy.count() >= 2);
        QCOMPARE(availability_spy.last().at(0).toBool(), false);
        QVERIFY(lock_query.exec(QStringLiteral("ROLLBACK")));
        lock_database.close();
    }
    QSqlDatabase::removeDatabase(lock_connection_name);

    controller.retryPendingOperations();
    QCOMPARE(availability_spy.last().at(0).toBool(), true);
    controller.recordAcceptedFrame(
        makeHistoryFrame(22'000, 3, {makeHistoryTrack(503, utms::TargetType::kPedestrian, 25.2, 110.2)}));
    controller.queryHistory({});

    utms::HistoryStore verifier(database_path);
    QString error;
    QVERIFY2(verifier.initialize(&error), qPrintable(error));
    const std::optional<utms::HistoryQueryResult> result = verifier.queryHistory({}, &error);
    QVERIFY2(result.has_value(), qPrintable(error));
    QCOMPARE(result->frames.size(), 1);
    QCOMPARE(result->targetCount(), 1);
    QCOMPARE(result->frames.at(0).sequence, std::optional<qint64>(3));
}

void HistoryStoreTest::sessionLifecycleCreatesAndClosesOneDurableRecord() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());

    utms::HistoryStore store(temporary_directory.filePath(QStringLiteral("history.sqlite")));
    QString error;
    QVERIFY2(store.initialize(&error), qPrintable(error));
    const QDateTime started_at = QDateTime::fromMSecsSinceEpoch(1'000, QTimeZone::UTC);
    const QDateTime ended_at = QDateTime::fromMSecsSinceEpoch(9'000, QTimeZone::UTC);

    const std::optional<qint64> session_id = store.startSession(started_at, &error);
    QVERIFY2(session_id.has_value(), qPrintable(error));
    QVERIFY2(store.closeActiveSession(ended_at, &error), qPrintable(error));

    const std::optional<QVector<utms::HistorySession>> sessions = store.loadSessions(&error);
    QVERIFY2(sessions.has_value(), qPrintable(error));
    QCOMPARE(sessions->size(), 1);
    QCOMPARE(sessions->at(0).id, session_id.value());
    QCOMPARE(sessions->at(0).started_at, started_at);
    QCOMPARE(sessions->at(0).ended_at, std::optional<QDateTime>(ended_at));
    QCOMPARE(sessions->at(0).state, utms::HistorySessionState::kClosed);
}

void HistoryStoreTest::abandonedSessionIsMarkedAbnormalWithoutDataLoss() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());
    const QString database_path = temporary_directory.filePath(QStringLiteral("history.sqlite"));
    const QDateTime started_at = QDateTime::fromMSecsSinceEpoch(2'000, QTimeZone::UTC);
    const QDateTime recovered_at = QDateTime::fromMSecsSinceEpoch(12'000, QTimeZone::UTC);
    QString error;
    qint64 original_session_id = 0;

    {
        utms::HistoryStore store(database_path);
        QVERIFY2(store.initialize(&error), qPrintable(error));
        const std::optional<qint64> session_id = store.startSession(started_at, &error);
        QVERIFY2(session_id.has_value(), qPrintable(error));
        original_session_id = session_id.value();
    }

    utms::HistoryStore reopened_store(database_path);
    QVERIFY2(reopened_store.initialize(&error), qPrintable(error));
    const std::optional<int> recovered_count = reopened_store.recoverAbandonedSessions(recovered_at, &error);
    QVERIFY2(recovered_count.has_value(), qPrintable(error));
    QCOMPARE(recovered_count.value(), 1);
    const std::optional<QVector<utms::HistorySession>> sessions = reopened_store.loadSessions(&error);

    QVERIFY2(sessions.has_value(), qPrintable(error));
    QCOMPARE(sessions->size(), 1);
    QCOMPARE(sessions->at(0).id, original_session_id);
    QCOMPARE(sessions->at(0).started_at, started_at);
    QCOMPARE(sessions->at(0).ended_at, std::optional<QDateTime>(recovered_at));
    QCOMPARE(sessions->at(0).state, utms::HistorySessionState::kAbnormal);
}

void HistoryStoreTest::controllerDegradesSafelyWhenDatabaseIsUnavailable() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());
    QFile path_blocker(temporary_directory.filePath(QStringLiteral("not-a-directory")));
    QVERIFY(path_blocker.open(QIODevice::WriteOnly));
    path_blocker.close();

    utms::HistoryController controller;
    QSignalSpy error_spy(&controller, &utms::HistoryController::errorOccurred);
    QSignalSpy configuration_spy(&controller, &utms::HistoryController::configurationLoaded);

    controller.initialize(path_blocker.fileName() + QStringLiteral("/history.sqlite"));

    QCOMPARE(error_spy.count(), 1);
    QCOMPARE(configuration_spy.count(), 1);
    const utms::HistoryConfiguration fallback_configuration =
        qvariant_cast<utms::HistoryConfiguration>(configuration_spy.at(0).at(0));
    QCOMPARE(fallback_configuration.sampling_rate, utms::HistorySamplingRate::kTwoFps);
    QCOMPARE(fallback_configuration.retention_days, 7);

    controller.startSession();
    QCOMPARE(error_spy.count(), 1);
}

void HistoryStoreTest::controllerRetriesInitializationAfterDatabaseRecovers() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());
    QFile path_blocker(temporary_directory.filePath(QStringLiteral("not-a-directory")));
    QVERIFY(path_blocker.open(QIODevice::WriteOnly));
    path_blocker.close();
    const QString database_path = path_blocker.fileName() + QStringLiteral("/history.sqlite");

    utms::HistoryController controller;
    QSignalSpy availability_spy(&controller, &utms::HistoryController::availabilityChanged);
    controller.initialize(database_path);
    QCOMPARE(availability_spy.count(), 1);
    QCOMPARE(availability_spy.at(0).at(0).toBool(), false);

    QVERIFY(path_blocker.remove());
    controller.retryPendingOperations();
    QCOMPARE(availability_spy.count(), 2);
    QCOMPARE(availability_spy.at(1).at(0).toBool(), true);

    controller.startSession();
    utms::HistoryStore verifier(database_path);
    QString error;
    QVERIFY2(verifier.initialize(&error), qPrintable(error));
    const std::optional<QVector<utms::HistorySession>> sessions = verifier.loadSessions(&error);
    QVERIFY2(sessions.has_value(), qPrintable(error));
    QCOMPARE(sessions->size(), 1);
    QCOMPARE(sessions->at(0).state, utms::HistorySessionState::kActive);
}

QTEST_GUILESS_MAIN(HistoryStoreTest)

#include "test_history_store.moc"
