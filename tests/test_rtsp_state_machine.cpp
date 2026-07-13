#include <QSignalSpy>
#include <QtTest>

#include "media/RtspStateMachine.h"

class RtspStateMachineTest : public QObject {
    Q_OBJECT

private slots:
    void startsDisconnected();
    void rejectsInvalidStreamUrls_data();
    void rejectsInvalidStreamUrls();
    void manuallyConnectsAndStartsPlayback();
    void reconnectsThreeSecondsAfterFailure();
    void reconnectsAfterPlaybackInterruption();
    void manualDisconnectPreventsFurtherReconnects();
};

void RtspStateMachineTest::startsDisconnected()
{
    utms::RtspStateMachine state_machine;

    QCOMPARE(state_machine.state(), utms::RtspConnectionState::kDisconnected);
    QVERIFY(!state_machine.isConnectionDesired());
    QVERIFY(state_machine.streamUrl().isEmpty());
}

void RtspStateMachineTest::rejectsInvalidStreamUrls_data()
{
    QTest::addColumn<QString>("stream_url");

    QTest::newRow("empty") << QString();
    QTest::newRow("http") << QStringLiteral("http://camera/live");
    QTest::newRow("local-path") << QStringLiteral("C:/video.mp4");
    QTest::newRow("missing-host") << QStringLiteral("rtsp:///camera_1");
}

void RtspStateMachineTest::rejectsInvalidStreamUrls()
{
    QFETCH(QString, stream_url);
    utms::RtspStateMachine state_machine;
    QSignalSpy attempt_spy(&state_machine, &utms::RtspStateMachine::connectionAttemptRequested);

    QVERIFY(!state_machine.requestConnect(stream_url));
    QCOMPARE(state_machine.state(), utms::RtspConnectionState::kDisconnected);
    QVERIFY(!state_machine.isConnectionDesired());
    QCOMPARE(attempt_spy.count(), 0);
}

void RtspStateMachineTest::manuallyConnectsAndStartsPlayback()
{
    utms::RtspStateMachine state_machine;
    QSignalSpy attempt_spy(&state_machine, &utms::RtspStateMachine::connectionAttemptRequested);
    const QString stream_url = QStringLiteral("rtsp://192.168.1.101:8554/camera_1");

    QVERIFY(state_machine.requestConnect(stream_url));
    QCOMPARE(state_machine.state(), utms::RtspConnectionState::kConnecting);
    QVERIFY(state_machine.isConnectionDesired());
    QCOMPARE(state_machine.streamUrl(), stream_url);
    QCOMPARE(attempt_spy.count(), 1);
    QCOMPARE(attempt_spy.constFirst().constFirst().toString(), stream_url);

    state_machine.reportPlaybackStarted();
    QCOMPARE(state_machine.state(), utms::RtspConnectionState::kPlaying);
}

void RtspStateMachineTest::reconnectsThreeSecondsAfterFailure()
{
    utms::RtspStateMachine state_machine;
    QSignalSpy attempt_spy(&state_machine, &utms::RtspStateMachine::connectionAttemptRequested);
    QSignalSpy reconnect_spy(&state_machine, &utms::RtspStateMachine::reconnectScheduled);
    const QString stream_url = QStringLiteral("rtsp://camera/live");

    QVERIFY(state_machine.requestConnect(stream_url));
    state_machine.reportConnectionFailure(QStringLiteral("连接超时"));

    QCOMPARE(state_machine.state(), utms::RtspConnectionState::kReconnecting);
    QCOMPARE(reconnect_spy.count(), 1);
    QCOMPARE(reconnect_spy.constFirst().constFirst().toInt(), utms::RtspStateMachine::kReconnectIntervalMs);
    QCOMPARE(utms::RtspStateMachine::kReconnectIntervalMs, 3'000);

    QVERIFY(state_machine.requestReconnect());
    QCOMPARE(state_machine.state(), utms::RtspConnectionState::kConnecting);
    QCOMPARE(attempt_spy.count(), 2);
    QCOMPARE(attempt_spy.constLast().constFirst().toString(), stream_url);
}

void RtspStateMachineTest::reconnectsAfterPlaybackInterruption()
{
    utms::RtspStateMachine state_machine;
    QSignalSpy reconnect_spy(&state_machine, &utms::RtspStateMachine::reconnectScheduled);

    QVERIFY(state_machine.requestConnect(QStringLiteral("rtsp://camera/live")));
    state_machine.reportPlaybackStarted();
    state_machine.reportPlaybackInterrupted(QStringLiteral("视频流中断"));

    QCOMPARE(state_machine.state(), utms::RtspConnectionState::kReconnecting);
    QCOMPARE(reconnect_spy.count(), 1);
}

void RtspStateMachineTest::manualDisconnectPreventsFurtherReconnects()
{
    utms::RtspStateMachine state_machine;
    QSignalSpy stop_spy(&state_machine, &utms::RtspStateMachine::decoderStopRequested);
    QSignalSpy reconnect_spy(&state_machine, &utms::RtspStateMachine::reconnectScheduled);

    QVERIFY(state_machine.requestConnect(QStringLiteral("rtsp://camera/live")));
    state_machine.reportConnectionFailure(QStringLiteral("连接失败"));
    QCOMPARE(reconnect_spy.count(), 1);

    state_machine.requestDisconnect();

    QCOMPARE(state_machine.state(), utms::RtspConnectionState::kDisconnected);
    QVERIFY(!state_machine.isConnectionDesired());
    QCOMPARE(stop_spy.count(), 1);
    QVERIFY(!state_machine.requestReconnect());

    state_machine.reportPlaybackInterrupted(QStringLiteral("过期的中断通知"));
    QCOMPARE(state_machine.state(), utms::RtspConnectionState::kDisconnected);
    QCOMPARE(reconnect_spy.count(), 1);
}

QTEST_MAIN(RtspStateMachineTest)

#include "test_rtsp_state_machine.moc"
