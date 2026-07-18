#include <QtTest>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "alert/AlertTypes.h"
#include "history/HistoryStore.h"

class AlertPersistenceTest : public QObject {
    Q_OBJECT

  private slots:
    void rulesAndTriggeredAlertsPersistAcrossReopen();
    void phase23AlertSchemaMigratesWithoutLosingRules();
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
        rule.cooldown_ms = 45'000;
        const std::optional<qint64> created_rule_id = store.createAlertRule(rule, &error);
        QVERIFY2(created_rule_id.has_value(), qPrintable(error));
        rule_id = created_rule_id.value();

        utms::AlertRule speed_rule;
        speed_rule.name = QStringLiteral("围栏内超速");
        speed_rule.type = utms::AlertRuleType::kGeofenceSpeeding;
        speed_rule.geofence_id = geofence_id.value();
        speed_rule.target_types = {utms::TargetType::kCar};
        speed_rule.speed_threshold_mps = 12.5;
        speed_rule.confirmation_ms = 2'000;
        speed_rule.cooldown_ms = 0;
        QVERIFY2(store.createAlertRule(speed_rule, &error).has_value(), qPrintable(error));

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
    QCOMPARE(rules->size(), 2);
    QCOMPARE(rules->constFirst().id, rule_id);
    QCOMPARE(rules->constFirst().target_types,
             QVector<utms::TargetType>({utms::TargetType::kCar, utms::TargetType::kTruck}));
    QCOMPARE(rules->constFirst().confirmation_ms, 1'000);
    QCOMPARE(rules->constFirst().cooldown_ms, 45'000);
    QCOMPARE(rules->at(1).type, utms::AlertRuleType::kGeofenceSpeeding);
    QCOMPARE(rules->at(1).speed_threshold_mps, 12.5);
    QCOMPARE(rules->at(1).confirmation_ms, 2'000);
    QCOMPARE(rules->at(1).cooldown_ms, 0);

    const std::optional<utms::TargetAlert> persisted_alert = reopened_store.loadTargetAlert(alert_id, &error);
    QVERIFY2(persisted_alert.has_value(), qPrintable(error));
    QCOMPARE(persisted_alert->track_id, 42);
    QCOMPARE(persisted_alert->severity, utms::AlertSeverity::kSevere);
    QCOMPARE(persisted_alert->velocity_mps, std::optional<double>(8.5));
}

void AlertPersistenceTest::phase23AlertSchemaMigratesWithoutLosingRules() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());
    const QString database_path = temporary_directory.filePath(QStringLiteral("history.sqlite"));
    const QString connection_name = QStringLiteral("phase23_alert_schema");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_name);
        database.setDatabaseName(database_path);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral("PRAGMA foreign_keys = ON")));
        QVERIFY(query.exec(QStringLiteral("CREATE TABLE app_config(key TEXT PRIMARY KEY, value TEXT NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral("CREATE TABLE geofences(id INTEGER PRIMARY KEY AUTOINCREMENT, name "
                                          "TEXT NOT NULL, "
                                          "shape TEXT NOT NULL, geometry_json TEXT NOT NULL, enabled INTEGER NOT "
                                          "NULL, visible INTEGER NOT NULL, "
                                          "created_at_ms INTEGER NOT NULL, updated_at_ms INTEGER NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO geofences(id, name, shape, geometry_json, "
                                          "enabled, visible, created_at_ms, updated_at_ms) "
                                          "VALUES(7, '重点区域', 'circle', '{}', 1, 1, 1, 1)")));
        QVERIFY(query.exec(QStringLiteral("CREATE TABLE alert_rules(id INTEGER PRIMARY KEY AUTOINCREMENT, name "
                                          "TEXT NOT NULL, "
                                          "rule_type INTEGER NOT NULL CHECK(rule_type BETWEEN 0 AND 1), "
                                          "geofence_id INTEGER NOT NULL, "
                                          "target_type_mask INTEGER NOT NULL, severity INTEGER NOT NULL, "
                                          "confirmation_ms INTEGER NOT NULL, enabled INTEGER NOT NULL, note TEXT "
                                          "NOT NULL DEFAULT '', "
                                          "created_at_ms INTEGER NOT NULL, updated_at_ms INTEGER NOT NULL, "
                                          "FOREIGN KEY(geofence_id) REFERENCES geofences(id) ON DELETE "
                                          "CASCADE)")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO alert_rules(id, name, rule_type, geofence_id, "
                                          "target_type_mask, severity, "
                                          "confirmation_ms, enabled, note, created_at_ms, updated_at_ms) "
                                          "VALUES(11, '车辆进入', 0, 7, 1, 1, 1000, 1, '', 1, 1)")));
        QVERIFY(query.exec(QStringLiteral("CREATE TABLE target_alerts(id INTEGER PRIMARY KEY "
                                          "AUTOINCREMENT, occurred_at_ms INTEGER NOT NULL, "
                                          "rule_id INTEGER NOT NULL, rule_name TEXT NOT NULL, "
                                          "rule_type INTEGER NOT NULL CHECK(rule_type BETWEEN 0 "
                                          "AND 1), severity INTEGER NOT NULL, "
                                          "geofence_id INTEGER NOT NULL, geofence_name TEXT NOT "
                                          "NULL, track_id INTEGER NOT NULL, "
                                          "target_type INTEGER NOT NULL, latitude REAL NOT NULL, "
                                          "longitude REAL NOT NULL, velocity_mps REAL, "
                                          "distance_m REAL, description TEXT NOT NULL, "
                                          "acknowledged INTEGER NOT NULL DEFAULT 0, "
                                          "acknowledged_at_ms INTEGER, acknowledged_by TEXT, "
                                          "handling_note TEXT NOT NULL DEFAULT '')")));
        database.close();
    }
    QSqlDatabase::removeDatabase(connection_name);

    QString error;
    utms::HistoryStore store(database_path);
    QVERIFY2(store.initialize(&error), qPrintable(error));
    const std::optional<QVector<utms::AlertRule>> migrated_rules = store.loadAlertRules(&error);
    QVERIFY2(migrated_rules.has_value(), qPrintable(error));
    QCOMPARE(migrated_rules->size(), 1);
    QCOMPARE(migrated_rules->constFirst().id, 11);
    QCOMPARE(migrated_rules->constFirst().cooldown_ms, 30'000);

    utms::AlertRule speed_rule;
    speed_rule.name = QStringLiteral("围栏内超速");
    speed_rule.type = utms::AlertRuleType::kGeofenceSpeeding;
    speed_rule.geofence_id = 7;
    speed_rule.target_types = {utms::TargetType::kCar};
    speed_rule.speed_threshold_mps = 12.5;
    const std::optional<qint64> speed_rule_id = store.createAlertRule(speed_rule, &error);
    QVERIFY2(speed_rule_id.has_value(), qPrintable(error));

    utms::TargetAlert alert;
    alert.occurred_at = QDateTime::fromMSecsSinceEpoch(12'345, QTimeZone::UTC);
    alert.rule_id = speed_rule_id.value();
    alert.rule_name = speed_rule.name;
    alert.rule_type = speed_rule.type;
    alert.geofence_id = 7;
    alert.geofence_name = QStringLiteral("重点区域");
    alert.track_id = 42;
    alert.target_type = utms::TargetType::kCar;
    alert.position = {25.31, 110.41};
    alert.velocity_mps = 15.0;
    alert.description = QStringLiteral("目标 42 围栏内超速");
    QVERIFY2(store.appendTargetAlert(alert, &error).has_value(), qPrintable(error));
}

QTEST_GUILESS_MAIN(AlertPersistenceTest)

#include "test_alert_persistence.moc"
