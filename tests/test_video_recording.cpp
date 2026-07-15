#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "media/VideoRecordingFileNamer.h"
#include "media/VideoRecordingSession.h"

class VideoRecordingTest : public QObject {
    Q_OBJECT

private slots:
    void startsOnlyOnKeyframeAndMeasuresFromFirstWrittenPacket();
    void failsAfterTenSecondsWithoutKeyframe();
    void stopCancelsPendingAndFinalizesActiveRecording();
    void interruptionFinalizesActiveRecording();
    void fileNamesNeverOverwriteExistingRecordings();
};

void VideoRecordingTest::startsOnlyOnKeyframeAndMeasuresFromFirstWrittenPacket()
{
    utms::VideoRecordingSession session;

    QVERIFY(session.requestStart(1'000));
    QCOMPARE(session.state(), utms::VideoRecordingState::kStarting);
    QCOMPARE(session.handleVideoPacket(false, 5'000), utms::VideoRecordingAction::kNone);
    QCOMPARE(session.durationSeconds(9'500), 0);

    QCOMPARE(session.handleVideoPacket(true, 9'500), utms::VideoRecordingAction::kBeginWriting);
    QCOMPARE(session.state(), utms::VideoRecordingState::kRecording);
    QCOMPARE(session.durationSeconds(9'500), 0);
    QCOMPARE(session.durationSeconds(12'999), 3);
    QCOMPARE(session.handleVideoPacket(false, 13'000), utms::VideoRecordingAction::kWritePacket);
}

void VideoRecordingTest::failsAfterTenSecondsWithoutKeyframe()
{
    utms::VideoRecordingSession session;

    QVERIFY(session.requestStart(2'000));
    QCOMPARE(session.checkKeyframeTimeout(11'999), utms::VideoRecordingAction::kNone);
    QCOMPARE(session.checkKeyframeTimeout(12'000), utms::VideoRecordingAction::kKeyframeTimeout);
    QCOMPARE(session.state(), utms::VideoRecordingState::kError);
}

void VideoRecordingTest::stopCancelsPendingAndFinalizesActiveRecording()
{
    utms::VideoRecordingSession pending_session;
    QVERIFY(pending_session.requestStart(0));
    QCOMPARE(pending_session.requestStop(), utms::VideoRecordingAction::kCancelPending);
    QCOMPARE(pending_session.state(), utms::VideoRecordingState::kIdle);

    utms::VideoRecordingSession active_session;
    QVERIFY(active_session.requestStart(0));
    QCOMPARE(active_session.handleVideoPacket(true, 100), utms::VideoRecordingAction::kBeginWriting);
    QCOMPARE(active_session.requestStop(), utms::VideoRecordingAction::kFinalize);
    QCOMPARE(active_session.state(), utms::VideoRecordingState::kStopping);
    active_session.reportFinalized(true);
    QCOMPARE(active_session.state(), utms::VideoRecordingState::kIdle);
}

void VideoRecordingTest::interruptionFinalizesActiveRecording()
{
    utms::VideoRecordingSession session;
    QVERIFY(session.requestStart(0));
    QCOMPARE(session.handleVideoPacket(true, 100), utms::VideoRecordingAction::kBeginWriting);

    QCOMPARE(session.requestInterruptionStop(), utms::VideoRecordingAction::kFinalize);
    QCOMPARE(session.state(), utms::VideoRecordingState::kStopping);
}

void VideoRecordingTest::fileNamesNeverOverwriteExistingRecordings()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QDateTime timestamp(QDate(2026, 7, 15), QTime(13, 14, 15));

    const QString first_path = utms::nextVideoRecordingPath(directory.path(), timestamp);
    QCOMPARE(QFileInfo(first_path).fileName(), QStringLiteral("UTMS_20260715_131415.mp4"));
    QFile first_file(first_path);
    QVERIFY(first_file.open(QIODevice::WriteOnly));
    first_file.close();

    const QString second_path = utms::nextVideoRecordingPath(directory.path(), timestamp);
    QCOMPARE(QFileInfo(second_path).fileName(), QStringLiteral("UTMS_20260715_131415_01.mp4"));
    QFile second_file(second_path);
    QVERIFY(second_file.open(QIODevice::WriteOnly));
    second_file.close();

    QCOMPARE(QFileInfo(utms::nextVideoRecordingPath(directory.path(), timestamp)).fileName(),
             QStringLiteral("UTMS_20260715_131415_02.mp4"));
}

QTEST_MAIN(VideoRecordingTest)

#include "test_video_recording.moc"
