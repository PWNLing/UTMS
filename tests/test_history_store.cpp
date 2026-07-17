#include <QtTest>

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "history/HistoryController.h"
#include "history/HistoryStore.h"

class HistoryStoreTest : public QObject
{
    Q_OBJECT

private slots:
    void freshDatabaseUsesRequiredDefaults();
    void configurationPersistsAcrossReopen_data();
    void configurationPersistsAcrossReopen();
    void invalidConfigurationIsRejected();
    void sessionLifecycleCreatesAndClosesOneDurableRecord();
    void abandonedSessionIsMarkedAbnormalWithoutDataLoss();
    void controllerDegradesSafelyWhenDatabaseIsUnavailable();
};

void HistoryStoreTest::freshDatabaseUsesRequiredDefaults()
{
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

void HistoryStoreTest::configurationPersistsAcrossReopen_data()
{
    QTest::addColumn<utms::HistorySamplingRate>("sampling_rate");
    QTest::addColumn<int>("retention_days");

    QTest::newRow("one-fps") << utms::HistorySamplingRate::kOneFps << 1;
    QTest::newRow("two-fps") << utms::HistorySamplingRate::kTwoFps << 7;
    QTest::newRow("five-fps") << utms::HistorySamplingRate::kFiveFps << 30;
    QTest::newRow("every-frame") << utms::HistorySamplingRate::kEveryFrame << 14;
}

void HistoryStoreTest::configurationPersistsAcrossReopen()
{
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

void HistoryStoreTest::invalidConfigurationIsRejected()
{
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

void HistoryStoreTest::sessionLifecycleCreatesAndClosesOneDurableRecord()
{
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

void HistoryStoreTest::abandonedSessionIsMarkedAbnormalWithoutDataLoss()
{
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
    QVERIFY2(reopened_store.recoverAbandonedSessions(recovered_at, &error), qPrintable(error));
    const std::optional<QVector<utms::HistorySession>> sessions = reopened_store.loadSessions(&error);

    QVERIFY2(sessions.has_value(), qPrintable(error));
    QCOMPARE(sessions->size(), 1);
    QCOMPARE(sessions->at(0).id, original_session_id);
    QCOMPARE(sessions->at(0).started_at, started_at);
    QCOMPARE(sessions->at(0).ended_at, std::optional<QDateTime>(recovered_at));
    QCOMPARE(sessions->at(0).state, utms::HistorySessionState::kAbnormal);
}

void HistoryStoreTest::controllerDegradesSafelyWhenDatabaseIsUnavailable()
{
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
    QCOMPARE(error_spy.count(), 2);
}

QTEST_GUILESS_MAIN(HistoryStoreTest)

#include "test_history_store.moc"
