#include <QtTest>

#include <QTemporaryDir>

#include "alert/AlertTypes.h"
#include "history/HistoryStore.h"

class AlertPersistenceTest : public QObject {
    Q_OBJECT

    private slots:
    void rulesAndTriggeredAlertsPersistAcrossReopen();
};

void AlertPersistenceTest::rulesAndTriggeredAlertsPersistAcrossReopen() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());
    const QString database_path = temporary_directory.filePath(QStringLiteral("history.sqlite"));
    QString error;
    qint64 rule_id = 0;
    qint64 alert_id = 0;

    {
        utms::HistoryStore store(database_path);
        QVERIFY2(store.initialize(&error), qPrintable(error));

        utms::Geofence geofence;
        geofence.name = QStringLiteral("重点区域");
        geofence.geometry = utms::CircleGeofence{{25.31, 110.41}, 100.0};
        const std::optional<qint64> geofence_id = store.createGeofence(geofence, &error);
        QVERIFY2(geofence_id.has_value(), qPrintable(error));

        utms::AlertRule rule;
        rule.name = QStringLiteral("车辆进入");
        rule.type = utms::AlertRuleType::kStableEntry;
        rule.geofence_id = geofence_id.value();
        rule.target_types = {utms::TargetType::kCar, utms::TargetType::kTruck};
        rule.severity = utms::AlertSeverity::kSevere;
        const std::optional<qint64> created_rule_id = store.createAlertRule(rule, &error);
        QVERIFY2(created_rule_id.has_value(), qPrintable(error));
        rule_id = created_rule_id.value();

        utms::TargetAlert alert;
        alert.occurred_at = QDateTime::fromMSecsSinceEpoch(12'345, QTimeZone::UTC);
        alert.rule_id = rule_id;
        alert.rule_name = rule.name;
        alert.rule_type = rule.type;
        alert.severity = rule.severity;
        alert.geofence_id = geofence_id.value();
        alert.geofence_name = geofence.name;
        alert.track_id = 42;
        alert.target_type = utms::TargetType::kCar;
        alert.position = {25.31, 110.41};
        alert.velocity_mps = 8.5;
        alert.description = QStringLiteral("目标 42 稳定进入围栏");
        const std::optional<qint64> created_alert_id = store.appendTargetAlert(alert, &error);
        QVERIFY2(created_alert_id.has_value(), qPrintable(error));
        alert_id = created_alert_id.value();
    }

    utms::HistoryStore reopened_store(database_path);
    QVERIFY2(reopened_store.initialize(&error), qPrintable(error));
    const std::optional<QVector<utms::AlertRule>> rules = reopened_store.loadAlertRules(&error);
    QVERIFY2(rules.has_value(), qPrintable(error));
    QCOMPARE(rules->size(), 1);
    QCOMPARE(rules->constFirst().id, rule_id);
    QCOMPARE(rules->constFirst().target_types,
             QVector<utms::TargetType>({utms::TargetType::kCar, utms::TargetType::kTruck}));
    QCOMPARE(rules->constFirst().confirmation_ms, 1'000);

    const std::optional<utms::TargetAlert> persisted_alert = reopened_store.loadTargetAlert(alert_id, &error);
    QVERIFY2(persisted_alert.has_value(), qPrintable(error));
    QCOMPARE(persisted_alert->track_id, 42);
    QCOMPARE(persisted_alert->severity, utms::AlertSeverity::kSevere);
    QCOMPARE(persisted_alert->velocity_mps, std::optional<double>(8.5));
}

QTEST_GUILESS_MAIN(AlertPersistenceTest)

#include "test_alert_persistence.moc"
