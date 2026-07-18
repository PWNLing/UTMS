#include <QtTest>

#include <cmath>
#include <limits>
#include <optional>

#include "alert/AlertEngine.h"
#include "alert/AlertWorker.h"

namespace {

utms::Geofence makeCircleGeofence() {
    utms::Geofence geofence;
    geofence.id = 7;
    geofence.name = QStringLiteral("重点区域");
    geofence.geometry = utms::CircleGeofence{{25.31, 110.41}, 100.0};
    return geofence;
}

utms::AlertRule makeEntryRule() {
    utms::AlertRule rule;
    rule.id = 11;
    rule.name = QStringLiteral("车辆进入");
    rule.type = utms::AlertRuleType::kStableEntry;
    rule.geofence_id = 7;
    rule.target_types = {utms::TargetType::kCar};
    rule.severity = utms::AlertSeverity::kSevere;
    rule.confirmation_ms = 1'000;
    return rule;
}

utms::RadarFrame makeFrame(qint64 time_ms, qint64 track_id, utms::TargetType type, double latitude, double longitude,
                           std::optional<double> velocity_mps = std::nullopt) {
    utms::TrackData track;
    track.track_id = track_id;
    track.type = type;
    track.position = {latitude, longitude};
    track.velocity_mps = velocity_mps;

    utms::RadarFrame frame;
    frame.received_at = QDateTime::fromMSecsSinceEpoch(time_ms, QTimeZone::UTC);
    frame.tracks = {track};
    frame.statistics = utms::calculateTargetStatistics(frame.tracks);
    return frame;
}

} // namespace

class AlertEngineTest : public QObject {
    Q_OBJECT

  private slots:
    void stableEntryRequiresConfirmationAndHonorsCategoryScope();
    void entrySupportsAllEnabledGeofenceShapes();
    void stableExitRequiresInsideBaselineMarginAndConfirmation();
    void disappearanceHoldsStateForThreeSecondsWithoutSynthesizingExit();
    void disablingGeofenceClearsHeldMembershipState();
    void dwellTimeoutUsesOnlyItsThresholdAndDeduplicatesContinuousPresence();
    void geofenceSpeedingRequiresInsideVelocityAndConfirmation();
    void recoveredConditionRetriggersOnlyAfterCooldown();
    void unchangedRuleRefreshPreservesContinuousAndCooldownState();
    void ruleValidationEnforcesDwellSpeedAndCooldownRanges();
    void workerEvaluatesFramesOffTheCallingThread();
};

void AlertEngineTest::stableEntryRequiresConfirmationAndHonorsCategoryScope() {
    utms::AlertEngine engine;
    engine.setGeofences({makeCircleGeofence()});
    engine.setRules({makeEntryRule()});

    QVERIFY(engine.evaluateFrame(makeFrame(0, 42, utms::TargetType::kCar, 25.312, 110.41)).isEmpty());
    QVERIFY(engine.evaluateFrame(makeFrame(100, 43, utms::TargetType::kPedestrian, 25.31, 110.41)).isEmpty());
    QVERIFY(engine.evaluateFrame(makeFrame(200, 42, utms::TargetType::kCar, 25.31, 110.41)).isEmpty());
    QVERIFY(engine.evaluateFrame(makeFrame(1'199, 42, utms::TargetType::kCar, 25.31, 110.41)).isEmpty());

    const QVector<utms::TargetAlert> alerts =
        engine.evaluateFrame(makeFrame(1'200, 42, utms::TargetType::kCar, 25.31, 110.41));

    QCOMPARE(alerts.size(), 1);
    QCOMPARE(alerts.constFirst().rule_id, 11);
    QCOMPARE(alerts.constFirst().geofence_id, 7);
    QCOMPARE(alerts.constFirst().track_id, 42);
    QCOMPARE(alerts.constFirst().severity, utms::AlertSeverity::kSevere);
    QCOMPARE(alerts.constFirst().occurred_at.toMSecsSinceEpoch(), 1'200);
}

void AlertEngineTest::entrySupportsAllEnabledGeofenceShapes() {
    utms::Geofence circle = makeCircleGeofence();
    circle.enabled = false;

    utms::Geofence rectangle;
    rectangle.id = 8;
    rectangle.name = QStringLiteral("矩形区域");
    rectangle.geometry = utms::RectangleGeofence{{25.30, 110.40}, {25.32, 110.42}};

    utms::Geofence polygon;
    polygon.id = 9;
    polygon.name = QStringLiteral("多边形区域");
    polygon.geometry = utms::PolygonGeofence{{{25.30, 110.40}, {25.32, 110.40}, {25.32, 110.42}, {25.30, 110.42}}};

    utms::AlertRule disabled_fence_rule = makeEntryRule();
    disabled_fence_rule.confirmation_ms = 0;
    utms::AlertRule rectangle_rule = disabled_fence_rule;
    rectangle_rule.id = 12;
    rectangle_rule.geofence_id = 8;
    utms::AlertRule polygon_rule = disabled_fence_rule;
    polygon_rule.id = 13;
    polygon_rule.geofence_id = 9;

    utms::AlertEngine engine;
    engine.setGeofences({circle, rectangle, polygon});
    engine.setRules({disabled_fence_rule, rectangle_rule, polygon_rule});

    const QVector<utms::TargetAlert> alerts =
        engine.evaluateFrame(makeFrame(2'000, 42, utms::TargetType::kCar, 25.31, 110.41));

    QCOMPARE(alerts.size(), 2);
    QCOMPARE(alerts.at(0).geofence_id, 8);
    QCOMPARE(alerts.at(1).geofence_id, 9);
}

void AlertEngineTest::stableExitRequiresInsideBaselineMarginAndConfirmation() {
    utms::AlertRule exit_rule = makeEntryRule();
    exit_rule.id = 14;
    exit_rule.name = QStringLiteral("车辆离开");
    exit_rule.type = utms::AlertRuleType::kStableExit;

    constexpr double kLatitude = 25.31;
    constexpr double kLongitude = 110.41;
    const double meters_per_longitude_degree = 111'320.0 * std::cos(kLatitude * 3.14159265358979323846 / 180.0);
    const auto longitude_at_east_meters = [meters_per_longitude_degree](double meters) {
        return kLongitude + meters / meters_per_longitude_degree;
    };

    utms::AlertEngine engine;
    engine.setGeofences({makeCircleGeofence()});
    engine.setRules({exit_rule});

    QVERIFY(engine.evaluateFrame(makeFrame(0, 42, utms::TargetType::kCar, kLatitude, kLongitude)).isEmpty());
    QVERIFY(engine.evaluateFrame(makeFrame(1'000, 42, utms::TargetType::kCar, kLatitude, kLongitude)).isEmpty());
    QVERIFY(
        engine.evaluateFrame(makeFrame(1'100, 42, utms::TargetType::kCar, kLatitude, longitude_at_east_meters(102.0)))
            .isEmpty());
    QVERIFY(
        engine.evaluateFrame(makeFrame(1'200, 42, utms::TargetType::kCar, kLatitude, longitude_at_east_meters(106.0)))
            .isEmpty());
    QVERIFY(
        engine.evaluateFrame(makeFrame(2'199, 42, utms::TargetType::kCar, kLatitude, longitude_at_east_meters(106.0)))
            .isEmpty());

    const QVector<utms::TargetAlert> alerts =
        engine.evaluateFrame(makeFrame(2'200, 42, utms::TargetType::kCar, kLatitude, longitude_at_east_meters(106.0)));

    QCOMPARE(alerts.size(), 1);
    QCOMPARE(alerts.constFirst().rule_type, utms::AlertRuleType::kStableExit);
    QVERIFY(
        engine.evaluateFrame(makeFrame(2'300, 42, utms::TargetType::kCar, kLatitude, longitude_at_east_meters(120.0)))
            .isEmpty());
}

void AlertEngineTest::disappearanceHoldsStateForThreeSecondsWithoutSynthesizingExit() {
    utms::AlertRule exit_rule = makeEntryRule();
    exit_rule.id = 15;
    exit_rule.type = utms::AlertRuleType::kStableExit;
    exit_rule.confirmation_ms = 0;

    constexpr double kOutsideLatitude = 25.312;
    utms::RadarFrame empty_frame;

    utms::AlertEngine held_engine;
    held_engine.setGeofences({makeCircleGeofence()});
    held_engine.setRules({exit_rule});
    QVERIFY(held_engine.evaluateFrame(makeFrame(0, 42, utms::TargetType::kCar, 25.31, 110.41)).isEmpty());
    empty_frame.received_at = QDateTime::fromMSecsSinceEpoch(2'000, QTimeZone::UTC);
    QVERIFY(held_engine.evaluateFrame(empty_frame).isEmpty());
    QCOMPARE(held_engine.evaluateFrame(makeFrame(2'500, 42, utms::TargetType::kCar, kOutsideLatitude, 110.41)).size(),
             1);

    utms::AlertEngine expired_engine;
    expired_engine.setGeofences({makeCircleGeofence()});
    expired_engine.setRules({exit_rule});
    QVERIFY(expired_engine.evaluateFrame(makeFrame(0, 42, utms::TargetType::kCar, 25.31, 110.41)).isEmpty());
    QVERIFY(
        expired_engine.evaluateFrame(makeFrame(3'001, 42, utms::TargetType::kCar, kOutsideLatitude, 110.41)).isEmpty());

    utms::AlertEngine expired_entry_engine;
    expired_entry_engine.setGeofences({makeCircleGeofence()});
    expired_entry_engine.setRules({makeEntryRule()});
    QVERIFY(expired_entry_engine.evaluateFrame(makeFrame(0, 42, utms::TargetType::kCar, 25.31, 110.41)).isEmpty());
    QVERIFY(expired_entry_engine.evaluateFrame(makeFrame(3'001, 42, utms::TargetType::kCar, 25.31, 110.41)).isEmpty());
    QCOMPARE(expired_entry_engine.evaluateFrame(makeFrame(4'001, 42, utms::TargetType::kCar, 25.31, 110.41)).size(), 1);
}

void AlertEngineTest::disablingGeofenceClearsHeldMembershipState() {
    utms::AlertRule exit_rule = makeEntryRule();
    exit_rule.id = 16;
    exit_rule.type = utms::AlertRuleType::kStableExit;
    exit_rule.confirmation_ms = 0;

    utms::Geofence geofence = makeCircleGeofence();
    utms::AlertEngine engine;
    engine.setGeofences({geofence});
    engine.setRules({exit_rule});
    QVERIFY(engine.evaluateFrame(makeFrame(0, 42, utms::TargetType::kCar, 25.31, 110.41)).isEmpty());

    geofence.enabled = false;
    engine.setGeofences({geofence});
    QVERIFY(engine.evaluateFrame(makeFrame(100, 42, utms::TargetType::kCar, 25.312, 110.41)).isEmpty());
    geofence.enabled = true;
    engine.setGeofences({geofence});
    QVERIFY(engine.evaluateFrame(makeFrame(200, 42, utms::TargetType::kCar, 25.312, 110.41)).isEmpty());
}

void AlertEngineTest::dwellTimeoutUsesOnlyItsThresholdAndDeduplicatesContinuousPresence() {
    utms::AlertRule rule = makeEntryRule();
    rule.id = 17;
    rule.type = utms::AlertRuleType::kDwellTimeout;
    rule.dwell_threshold_ms = 5'000;
    rule.confirmation_ms = 60'000;
    rule.cooldown_ms = 0;

    utms::AlertEngine engine;
    engine.setGeofences({makeCircleGeofence()});
    engine.setRules({rule});

    QVERIFY(engine.evaluateFrame(makeFrame(0, 42, utms::TargetType::kCar, 25.31, 110.41)).isEmpty());
    QVERIFY(engine.evaluateFrame(makeFrame(2'500, 42, utms::TargetType::kCar, 25.31, 110.41)).isEmpty());
    QVERIFY(engine.evaluateFrame(makeFrame(4'999, 42, utms::TargetType::kCar, 25.31, 110.41)).isEmpty());
    const QVector<utms::TargetAlert> first_alerts =
        engine.evaluateFrame(makeFrame(5'000, 42, utms::TargetType::kCar, 25.31, 110.41));
    QCOMPARE(first_alerts.size(), 1);
    QCOMPARE(first_alerts.constFirst().rule_type, utms::AlertRuleType::kDwellTimeout);
    QVERIFY(engine.evaluateFrame(makeFrame(6'000, 42, utms::TargetType::kCar, 25.31, 110.41)).isEmpty());

    QVERIFY(engine.evaluateFrame(makeFrame(6'100, 42, utms::TargetType::kCar, 25.312, 110.41)).isEmpty());
    QVERIFY(engine.evaluateFrame(makeFrame(6'200, 42, utms::TargetType::kCar, 25.31, 110.41)).isEmpty());
    QVERIFY(engine.evaluateFrame(makeFrame(8'700, 42, utms::TargetType::kCar, 25.31, 110.41)).isEmpty());
    QCOMPARE(engine.evaluateFrame(makeFrame(11'200, 42, utms::TargetType::kCar, 25.31, 110.41)).size(), 1);
}

void AlertEngineTest::geofenceSpeedingRequiresInsideVelocityAndConfirmation() {
    utms::AlertRule rule = makeEntryRule();
    rule.id = 18;
    rule.type = utms::AlertRuleType::kGeofenceSpeeding;
    rule.speed_threshold_mps = 10.0;
    rule.confirmation_ms = 1'000;
    rule.cooldown_ms = 0;

    utms::AlertEngine engine;
    engine.setGeofences({makeCircleGeofence()});
    engine.setRules({rule});

    QVERIFY(engine.evaluateFrame(makeFrame(0, 42, utms::TargetType::kCar, 25.312, 110.41, 20.0)).isEmpty());
    QVERIFY(engine.evaluateFrame(makeFrame(100, 42, utms::TargetType::kCar, 25.31, 110.41)).isEmpty());
    QVERIFY(engine.evaluateFrame(makeFrame(200, 42, utms::TargetType::kCar, 25.31, 110.41, 10.0)).isEmpty());
    QVERIFY(engine.evaluateFrame(makeFrame(300, 42, utms::TargetType::kCar, 25.31, 110.41, 10.1)).isEmpty());
    QVERIFY(engine.evaluateFrame(makeFrame(1'299, 42, utms::TargetType::kCar, 25.31, 110.41, 12.0)).isEmpty());

    const QVector<utms::TargetAlert> first_alerts =
        engine.evaluateFrame(makeFrame(1'300, 42, utms::TargetType::kCar, 25.31, 110.41, 12.0));
    QCOMPARE(first_alerts.size(), 1);
    QCOMPARE(first_alerts.constFirst().velocity_mps, std::optional<double>(12.0));
    QVERIFY(engine.evaluateFrame(makeFrame(2'000, 42, utms::TargetType::kCar, 25.31, 110.41, 30.0)).isEmpty());

    QVERIFY(engine.evaluateFrame(makeFrame(2'100, 42, utms::TargetType::kCar, 25.31, 110.41, 9.0)).isEmpty());
    QVERIFY(engine.evaluateFrame(makeFrame(2'200, 42, utms::TargetType::kCar, 25.31, 110.41, 11.0)).isEmpty());
    QCOMPARE(engine.evaluateFrame(makeFrame(3'200, 42, utms::TargetType::kCar, 25.31, 110.41, 11.0)).size(), 1);
}

void AlertEngineTest::recoveredConditionRetriggersOnlyAfterCooldown() {
    utms::AlertRule rule = makeEntryRule();
    rule.confirmation_ms = 0;
    rule.cooldown_ms = 30'000;

    utms::AlertEngine engine;
    engine.setGeofences({makeCircleGeofence()});
    engine.setRules({rule});

    QCOMPARE(engine.evaluateFrame(makeFrame(0, 42, utms::TargetType::kCar, 25.31, 110.41)).size(), 1);
    QVERIFY(engine.evaluateFrame(makeFrame(100, 42, utms::TargetType::kCar, 25.312, 110.41)).isEmpty());
    QVERIFY(engine.evaluateFrame(makeFrame(1'000, 42, utms::TargetType::kCar, 25.31, 110.41)).isEmpty());
    QVERIFY(engine.evaluateFrame(makeFrame(29'999, 42, utms::TargetType::kCar, 25.31, 110.41)).isEmpty());
    QCOMPARE(engine.evaluateFrame(makeFrame(30'000, 42, utms::TargetType::kCar, 25.31, 110.41)).size(), 1);
}

void AlertEngineTest::unchangedRuleRefreshPreservesContinuousAndCooldownState() {
    utms::AlertRule rule = makeEntryRule();
    rule.confirmation_ms = 0;
    rule.cooldown_ms = 30'000;

    utms::AlertEngine engine;
    engine.setGeofences({makeCircleGeofence()});
    engine.setRules({rule});

    QCOMPARE(engine.evaluateFrame(makeFrame(0, 42, utms::TargetType::kCar, 25.31, 110.41)).size(), 1);
    for (qint64 time_ms = 3'000; time_ms <= 30'000; time_ms += 3'000) {
        if (time_ms == 15'000) {
            engine.setRules({rule});
        }
        QVERIFY(engine.evaluateFrame(makeFrame(time_ms, 42, utms::TargetType::kCar, 25.31, 110.41)).isEmpty());
    }

    utms::AlertEngine recovered_engine;
    recovered_engine.setGeofences({makeCircleGeofence()});
    recovered_engine.setRules({rule});
    QCOMPARE(recovered_engine.evaluateFrame(makeFrame(0, 42, utms::TargetType::kCar, 25.31, 110.41)).size(), 1);
    QVERIFY(recovered_engine.evaluateFrame(makeFrame(100, 42, utms::TargetType::kCar, 25.312, 110.41)).isEmpty());
    recovered_engine.setRules({rule});
    QVERIFY(recovered_engine.evaluateFrame(makeFrame(200, 42, utms::TargetType::kCar, 25.31, 110.41)).isEmpty());
}

void AlertEngineTest::ruleValidationEnforcesDwellSpeedAndCooldownRanges() {
    utms::AlertRule rule = makeEntryRule();

    rule.type = utms::AlertRuleType::kDwellTimeout;
    rule.dwell_threshold_ms = 4'999;
    QVERIFY(!utms::validateAlertRule(rule).isEmpty());
    rule.dwell_threshold_ms = 5'000;
    QVERIFY(utms::validateAlertRule(rule).isEmpty());
    rule.dwell_threshold_ms = 86'400'001;
    QVERIFY(!utms::validateAlertRule(rule).isEmpty());

    rule.type = utms::AlertRuleType::kGeofenceSpeeding;
    rule.dwell_threshold_ms = 5'000;
    rule.speed_threshold_mps = 0.0;
    QVERIFY(!utms::validateAlertRule(rule).isEmpty());
    rule.speed_threshold_mps = 0.1;
    QVERIFY(utms::validateAlertRule(rule).isEmpty());
    rule.speed_threshold_mps = std::numeric_limits<double>::infinity();
    QVERIFY(!utms::validateAlertRule(rule).isEmpty());

    rule.speed_threshold_mps = 10.0;
    rule.cooldown_ms = -1;
    QVERIFY(!utms::validateAlertRule(rule).isEmpty());
    rule.cooldown_ms = 86'400'000;
    QVERIFY(utms::validateAlertRule(rule).isEmpty());
    rule.cooldown_ms = 86'400'001;
    QVERIFY(!utms::validateAlertRule(rule).isEmpty());
}

void AlertEngineTest::workerEvaluatesFramesOffTheCallingThread() {
    QThread thread;
    auto *worker = new utms::AlertWorker();
    QVERIFY(worker->moveToThread(&thread));
    connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
    QSignalSpy alert_spy(worker, &utms::AlertWorker::alertTriggered);
    QSignalSpy error_spy(worker, &utms::AlertWorker::errorOccurred);
    QSignalSpy stopped_spy(worker, &utms::AlertWorker::stopped);
    thread.start();

    utms::AlertRule rule = makeEntryRule();
    rule.confirmation_ms = 0;
    const utms::RadarFrame frame = makeFrame(5'000, 42, utms::TargetType::kCar, 25.31, 110.41);
    QMetaObject::invokeMethod(
        worker,
        [worker, rule, frame]() {
            worker->setGeofences({makeCircleGeofence()});
            utms::AlertRule invalid_rule = rule;
            invalid_rule.target_types.clear();
            worker->setRules({invalid_rule});
            worker->setRules({rule});
            worker->evaluateAcceptedFrame(frame);
        },
        Qt::QueuedConnection);

    QTRY_COMPARE(error_spy.count(), 1);
    QTRY_COMPARE(alert_spy.count(), 1);
    QVERIFY(worker->thread() != QThread::currentThread());
    QMetaObject::invokeMethod(worker, &utms::AlertWorker::shutdown, Qt::QueuedConnection);
    QTRY_COMPARE(stopped_spy.count(), 1);
    QVERIFY(thread.wait(2'000));
}

QTEST_GUILESS_MAIN(AlertEngineTest)

#include "test_alert_engine.moc"
