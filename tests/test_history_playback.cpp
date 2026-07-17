#include <QtTest>

#include "history/HistoryPlaybackController.h"

namespace {

utms::HistoryFrameRecord makePlaybackFrame(qint64 frame_time_ms, qint64 sequence) {
    utms::HistoryFrameRecord frame;
    frame.frame_id = sequence;
    frame.session_id = 1;
    frame.frame_time = QDateTime::fromMSecsSinceEpoch(frame_time_ms, QTimeZone::UTC);
    frame.received_at = frame.frame_time;
    frame.sequence = sequence;
    return frame;
}

utms::TrackData makePlaybackTrack(qint64 track_id, double latitude, double longitude) {
    utms::TrackData track;
    track.track_id = track_id;
    track.type = utms::TargetType::kCar;
    track.position = {latitude, longitude};
    return track;
}

} // namespace

class HistoryPlaybackTest : public QObject {
    Q_OBJECT

private slots:
    void operatorCanNavigateAndSeekToTheNearestQueriedFrame();
    void playbackUsesSupportedRatesAndSkipsLongGapsWithoutInterpolation();
    void selectedTargetTrajectoryCoversTheQueryStartThroughTheCurrentReplayTime();
    void selectedTrajectoryBreaksAcrossMissingSamplesAndLongGaps();
    void clearingSelectionClearsTheReplayTrajectory();
    void seekingAcrossALongGapShowsTheInterruption();
    void returningToLiveStopsPlaybackAndClearsReplayState();
    void playbackResumesNormalTimingAfterSkippingAGap();
};

void HistoryPlaybackTest::operatorCanNavigateAndSeekToTheNearestQueriedFrame() {
    utms::HistoryQueryResult result;
    result.frames = {
        makePlaybackFrame(1'000, 1),
        makePlaybackFrame(3'000, 2),
        makePlaybackFrame(10'000, 3),
    };

    utms::HistoryPlaybackController controller;

    QVERIFY(controller.beginReplay(result));
    QVERIFY(controller.isReplayMode());
    QCOMPARE(controller.currentFrameIndex(), 0);
    QCOMPARE(controller.currentFrame()->sequence, std::optional<qint64>(1));

    controller.nextFrame();
    QCOMPARE(controller.currentFrameIndex(), 1);
    QCOMPARE(controller.currentFrame()->sequence, std::optional<qint64>(2));

    controller.previousFrame();
    QCOMPARE(controller.currentFrameIndex(), 0);

    controller.seekTo(QDateTime::fromMSecsSinceEpoch(9'400, QTimeZone::UTC));
    QCOMPARE(controller.currentFrameIndex(), 2);
    QCOMPARE(controller.currentFrame()->sequence, std::optional<qint64>(3));
}

void HistoryPlaybackTest::playbackUsesSupportedRatesAndSkipsLongGapsWithoutInterpolation() {
    utms::HistoryQueryResult result;
    result.frames = {
        makePlaybackFrame(1'000, 1),
        makePlaybackFrame(1'100, 2),
        makePlaybackFrame(7'100, 3),
    };

    utms::HistoryPlaybackController controller;
    QSignalSpy gap_spy(&controller, &utms::HistoryPlaybackController::dataGapSkipped);

    QVERIFY(controller.beginReplay(result));
    QVERIFY(controller.setPlaybackRate(4.0));
    QCOMPARE(controller.playbackRate(), 4.0);
    QVERIFY(!controller.setPlaybackRate(3.0));

    controller.play();
    QTRY_COMPARE_WITH_TIMEOUT(controller.currentFrameIndex(), 2, 250);
    QCOMPARE(gap_spy.count(), 1);
    QCOMPARE(gap_spy.constFirst().constFirst().toLongLong(), 6'000);
    QVERIFY(!controller.isPlaying());
}

void HistoryPlaybackTest::selectedTargetTrajectoryCoversTheQueryStartThroughTheCurrentReplayTime() {
    utms::HistoryQueryResult result;
    result.frames = {
        makePlaybackFrame(1'000, 1),
        makePlaybackFrame(2'000, 2),
        makePlaybackFrame(3'000, 3),
    };
    result.frames[0].tracks = {makePlaybackTrack(42, 25.0, 110.0)};
    result.frames[1].tracks = {makePlaybackTrack(42, 25.1, 110.1), makePlaybackTrack(99, 26.0, 111.0)};
    result.frames[2].tracks = {makePlaybackTrack(99, 26.1, 111.1)};

    utms::HistoryPlaybackController controller;
    QVERIFY(controller.beginReplay(result));
    controller.setSelectedTrackId(42);
    controller.nextFrame();
    controller.nextFrame();

    const std::optional<utms::HistoryReplayTrajectory> trajectory = controller.selectedTrajectory();
    QVERIFY(trajectory.has_value());
    QCOMPARE(trajectory->track_id, 42);
    QCOMPARE(trajectory->segments.size(), 1);
    QCOMPARE(trajectory->segments.constFirst().size(), 2);
    QCOMPARE(trajectory->segments.constFirst().at(0).latitude, 25.0);
    QCOMPARE(trajectory->segments.constFirst().at(1).longitude, 110.1);

    controller.seekTo(QDateTime::fromMSecsSinceEpoch(1'000, QTimeZone::UTC));
    QCOMPARE(controller.selectedTrajectory()->segments.constFirst().size(), 1);
}

void HistoryPlaybackTest::selectedTrajectoryBreaksAcrossMissingSamplesAndLongGaps() {
    utms::HistoryQueryResult result;
    result.frames = {
        makePlaybackFrame(1'000, 1),
        makePlaybackFrame(2'000, 2),
        makePlaybackFrame(3'000, 3),
        makePlaybackFrame(9'000, 4),
    };
    result.frames[0].tracks = {makePlaybackTrack(42, 25.0, 110.0)};
    result.frames[2].tracks = {makePlaybackTrack(42, 25.2, 110.2)};
    result.frames[3].tracks = {makePlaybackTrack(42, 25.3, 110.3)};

    utms::HistoryPlaybackController controller;
    QVERIFY(controller.beginReplay(result));
    controller.setSelectedTrackId(42);
    controller.seekTo(result.frames.constLast().frame_time);

    const std::optional<utms::HistoryReplayTrajectory> trajectory = controller.selectedTrajectory();
    QVERIFY(trajectory.has_value());
    QCOMPARE(trajectory->segments.size(), 3);
    QCOMPARE(trajectory->segments.at(0).constFirst().latitude, 25.0);
    QCOMPARE(trajectory->segments.at(1).constFirst().latitude, 25.2);
    QCOMPARE(trajectory->segments.at(2).constFirst().latitude, 25.3);
}

void HistoryPlaybackTest::clearingSelectionClearsTheReplayTrajectory() {
    utms::HistoryQueryResult result;
    result.frames = {makePlaybackFrame(1'000, 1)};
    result.frames[0].tracks = {makePlaybackTrack(42, 25.0, 110.0)};

    utms::HistoryPlaybackController controller;
    QSignalSpy cleared_spy(&controller, &utms::HistoryPlaybackController::selectedTrajectoryCleared);
    QVERIFY(controller.beginReplay(result));
    controller.setSelectedTrackId(42);
    cleared_spy.clear();

    controller.clearSelectedTrackId();

    QVERIFY(!controller.selectedTrajectory().has_value());
    QCOMPARE(cleared_spy.count(), 1);
}

void HistoryPlaybackTest::seekingAcrossALongGapShowsTheInterruption() {
    utms::HistoryQueryResult result;
    result.frames = {
        makePlaybackFrame(1'000, 1),
        makePlaybackFrame(2'000, 2),
        makePlaybackFrame(8'000, 3),
    };

    utms::HistoryPlaybackController controller;
    QSignalSpy gap_spy(&controller, &utms::HistoryPlaybackController::dataGapSkipped);
    QVERIFY(controller.beginReplay(result));

    controller.seekTo(result.frames.constLast().frame_time);

    QCOMPARE(gap_spy.count(), 1);
    QCOMPARE(gap_spy.constFirst().constFirst().toLongLong(), 6'000);
}

void HistoryPlaybackTest::returningToLiveStopsPlaybackAndClearsReplayState() {
    utms::HistoryQueryResult result;
    result.frames = {makePlaybackFrame(1'000, 1), makePlaybackFrame(2'000, 2)};

    utms::HistoryPlaybackController controller;
    QVERIFY(controller.beginReplay(result));
    controller.play();
    QVERIFY(controller.isPlaying());

    controller.returnToLive();

    QVERIFY(!controller.isReplayMode());
    QVERIFY(!controller.isPlaying());
    QVERIFY(!controller.currentFrame().has_value());
    QVERIFY(!controller.selectedTrajectory().has_value());
}

void HistoryPlaybackTest::playbackResumesNormalTimingAfterSkippingAGap() {
    utms::HistoryQueryResult result;
    result.frames = {
        makePlaybackFrame(1'000, 1),
        makePlaybackFrame(1'400, 2),
        makePlaybackFrame(7'400, 3),
        makePlaybackFrame(7'800, 4),
    };

    utms::HistoryPlaybackController controller;
    QVERIFY(controller.beginReplay(result));
    QVERIFY(controller.setPlaybackRate(4.0));
    controller.play();

    QTest::qWait(150);
    QCOMPARE(controller.currentFrameIndex(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(controller.currentFrameIndex(), 3, 200);
}

QTEST_GUILESS_MAIN(HistoryPlaybackTest)

#include "test_history_playback.moc"
