#include <algorithm>

#include <QDateTime>
#include <QtTest>

#include "map/RealtimeTrajectoryModel.h"

namespace
{

utms::RadarFrame makeFrame(qint64 timestamp_ms, double latitude, double longitude)
{
    utms::RadarFrame frame;
    frame.received_at = QDateTime::fromMSecsSinceEpoch(timestamp_ms);
    utms::TrackData track;
    track.track_id = 42;
    track.type = utms::TargetType::kCar;
    track.position = {latitude, longitude};
    frame.tracks.append(track);
    return frame;
}

void appendTrack(utms::RadarFrame &frame, qint64 track_id, utms::TargetType type, double latitude, double longitude)
{
    utms::TrackData track;
    track.track_id = track_id;
    track.type = type;
    track.position = {latitude, longitude};
    frame.tracks.append(track);
}

int visiblePointCount(const QVector<utms::RealtimeTrajectory> &trajectories)
{
    int point_count = 0;
    for (const utms::RealtimeTrajectory &trajectory : trajectories)
    {
        for (const utms::RealtimeTrajectorySegment &segment : trajectory.segments)
        {
            point_count += segment.points.size();
        }
    }
    return point_count;
}

} // namespace

class RealtimeTrajectoryTest : public QObject
{
    Q_OBJECT

private slots:
    void samplesEachTargetAtMostEveryFiveHundredMilliseconds();
    void appliesConfiguredRetentionAndSupportsTurningDisplayOff();
    void showsOnlySelectedTargetUnlessShowAllIsEnabled();
    void startsNewSegmentAfterMoreThanThreeSecondSampleGap();
    void startsNewSegmentAfterPositionJumpOverTwoHundredMeters();
    void retainsDisappearedTrajectoryForFiveSeconds();
    void splitsDisplayEveryTenPointsWithSharedEndpointsAndFading();
    void clearsSelectionEmphasisButRetainsRecentFocusedTrajectory();
    void resumesSameSegmentAfterBriefDisappearance();
};

void RealtimeTrajectoryTest::samplesEachTargetAtMostEveryFiveHundredMilliseconds()
{
    utms::RealtimeTrajectoryModel model;
    model.setSelectedTrackId(42);

    model.replaceFrame(makeFrame(0, 25.0, 110.0));
    model.replaceFrame(makeFrame(499, 25.0001, 110.0001));
    model.replaceFrame(makeFrame(500, 25.0002, 110.0002));

    const QVector<utms::RealtimeTrajectory> trajectories =
        model.visibleTrajectories(QDateTime::fromMSecsSinceEpoch(500));
    QCOMPARE(trajectories.size(), 1);
    QCOMPARE(visiblePointCount(trajectories), 2);
}

void RealtimeTrajectoryTest::appliesConfiguredRetentionAndSupportsTurningDisplayOff()
{
    utms::RealtimeTrajectoryModel model;
    model.setSelectedTrackId(42);
    model.replaceFrame(makeFrame(0, 25.0, 110.0));
    model.replaceFrame(makeFrame(500, 25.0001, 110.0001));
    model.replaceFrame(makeFrame(3'500, 25.0002, 110.0002));
    model.replaceFrame(makeFrame(6'500, 25.0003, 110.0003));
    model.replaceFrame(makeFrame(9'500, 25.0004, 110.0004));
    model.replaceFrame(makeFrame(10'500, 25.0005, 110.0005));

    const QDateTime now = QDateTime::fromMSecsSinceEpoch(10'500);
    QCOMPARE(visiblePointCount(model.visibleTrajectories(now)), 6);

    model.setDuration(utms::RealtimeTrajectoryDuration::kTenSeconds);
    QCOMPARE(visiblePointCount(model.visibleTrajectories(now)), 5);

    model.setDuration(utms::RealtimeTrajectoryDuration::kOff);
    QVERIFY(model.visibleTrajectories(now).isEmpty());
}

void RealtimeTrajectoryTest::showsOnlySelectedTargetUnlessShowAllIsEnabled()
{
    utms::RealtimeTrajectoryModel model;
    model.setSelectedTrackId(42);
    for (qint64 timestamp_ms : {0, 500})
    {
        utms::RadarFrame frame = makeFrame(timestamp_ms, 25.0, 110.0 + timestamp_ms / 10'000'000.0);
        appendTrack(frame, 7, utms::TargetType::kPedestrian, 25.1, 110.1 + timestamp_ms / 10'000'000.0);
        model.replaceFrame(frame);
    }

    const QDateTime now = QDateTime::fromMSecsSinceEpoch(500);
    const QVector<utms::RealtimeTrajectory> selected_only = model.visibleTrajectories(now);
    QCOMPARE(selected_only.size(), 1);
    QCOMPARE(selected_only.constFirst().track_id, 42);
    QVERIFY(selected_only.constFirst().selected);

    model.setShowAllTargets(true);
    const QVector<utms::RealtimeTrajectory> all_targets = model.visibleTrajectories(now);
    QCOMPARE(all_targets.size(), 2);
    QCOMPARE(std::count_if(all_targets.cbegin(), all_targets.cend(),
                           [](const utms::RealtimeTrajectory &trajectory) { return trajectory.selected; }),
             1);
}

void RealtimeTrajectoryTest::startsNewSegmentAfterMoreThanThreeSecondSampleGap()
{
    utms::RealtimeTrajectoryModel model;
    model.setSelectedTrackId(42);
    model.replaceFrame(makeFrame(0, 25.0, 110.0));
    model.replaceFrame(makeFrame(500, 25.0001, 110.0001));
    model.replaceFrame(makeFrame(4'001, 25.0002, 110.0002));
    model.replaceFrame(makeFrame(4'501, 25.0003, 110.0003));

    const QVector<utms::RealtimeTrajectory> trajectories =
        model.visibleTrajectories(QDateTime::fromMSecsSinceEpoch(4'501));
    QCOMPARE(trajectories.size(), 1);
    QCOMPARE(trajectories.constFirst().segments.size(), 2);
    QCOMPARE(trajectories.constFirst().segments.at(0).points.size(), 2);
    QCOMPARE(trajectories.constFirst().segments.at(1).points.size(), 2);
}

void RealtimeTrajectoryTest::startsNewSegmentAfterPositionJumpOverTwoHundredMeters()
{
    utms::RealtimeTrajectoryModel model;
    model.setSelectedTrackId(42);
    model.replaceFrame(makeFrame(0, 25.0, 110.0));
    model.replaceFrame(makeFrame(500, 25.0001, 110.0001));
    model.replaceFrame(makeFrame(1'000, 25.0030, 110.0001));
    model.replaceFrame(makeFrame(1'500, 25.0031, 110.0002));

    const QVector<utms::RealtimeTrajectory> trajectories =
        model.visibleTrajectories(QDateTime::fromMSecsSinceEpoch(1'500));
    QCOMPARE(trajectories.size(), 1);
    QCOMPARE(trajectories.constFirst().segments.size(), 2);
    QCOMPARE(trajectories.constFirst().segments.at(0).points.size(), 2);
    QCOMPARE(trajectories.constFirst().segments.at(1).points.size(), 2);
}

void RealtimeTrajectoryTest::retainsDisappearedTrajectoryForFiveSeconds()
{
    utms::RealtimeTrajectoryModel model;
    model.setSelectedTrackId(42);
    model.replaceFrame(makeFrame(0, 25.0, 110.0));
    model.replaceFrame(makeFrame(500, 25.0001, 110.0001));

    utms::RadarFrame missing_frame;
    missing_frame.received_at = QDateTime::fromMSecsSinceEpoch(1'000);
    model.replaceFrame(missing_frame);

    QVERIFY(!model.visibleTrajectories(QDateTime::fromMSecsSinceEpoch(6'000)).isEmpty());
    QVERIFY(model.visibleTrajectories(QDateTime::fromMSecsSinceEpoch(6'001)).isEmpty());
}

void RealtimeTrajectoryTest::splitsDisplayEveryTenPointsWithSharedEndpointsAndFading()
{
    utms::RealtimeTrajectoryModel model;
    model.setSelectedTrackId(42);
    for (int sample_index = 0; sample_index < 20; ++sample_index)
    {
        model.replaceFrame(makeFrame(sample_index * 500, 25.0, 110.0 + sample_index * 0.000001));
    }

    const QVector<utms::RealtimeTrajectory> trajectories =
        model.visibleTrajectories(QDateTime::fromMSecsSinceEpoch(9'500));
    QCOMPARE(trajectories.size(), 1);
    const QVector<utms::RealtimeTrajectorySegment> &segments = trajectories.constFirst().segments;
    QCOMPARE(segments.size(), 3);
    QCOMPARE(segments.at(0).points.size(), 10);
    QCOMPARE(segments.at(1).points.size(), 10);
    QCOMPARE(segments.at(2).points.size(), 2);
    QCOMPARE(segments.at(0).points.constLast().longitude, segments.at(1).points.constFirst().longitude);
    QCOMPARE(segments.at(1).points.constLast().longitude, segments.at(2).points.constFirst().longitude);
    QVERIFY(segments.at(0).opacity < segments.at(1).opacity);
    QVERIFY(segments.at(1).opacity < segments.at(2).opacity);
    QCOMPARE(segments.at(2).opacity, 1.0);
}

void RealtimeTrajectoryTest::clearsSelectionEmphasisButRetainsRecentFocusedTrajectory()
{
    utms::RealtimeTrajectoryModel model;
    model.setSelectedTrackId(42);
    model.replaceFrame(makeFrame(0, 25.0, 110.0));
    model.replaceFrame(makeFrame(500, 25.0001, 110.0001));

    model.clearSelectionRetainingFocusedTrajectory(QDateTime::fromMSecsSinceEpoch(1'000));

    const QVector<utms::RealtimeTrajectory> retained =
        model.visibleTrajectories(QDateTime::fromMSecsSinceEpoch(6'000));
    QCOMPARE(retained.size(), 1);
    QVERIFY(!retained.constFirst().selected);
    QVERIFY(model.visibleTrajectories(QDateTime::fromMSecsSinceEpoch(6'001)).isEmpty());
}

void RealtimeTrajectoryTest::resumesSameSegmentAfterBriefDisappearance()
{
    utms::RealtimeTrajectoryModel model;
    model.setSelectedTrackId(42);
    model.replaceFrame(makeFrame(0, 25.0, 110.0));
    model.replaceFrame(makeFrame(500, 25.0001, 110.0001));

    utms::RadarFrame missing_frame;
    missing_frame.received_at = QDateTime::fromMSecsSinceEpoch(1'000);
    model.replaceFrame(missing_frame);
    model.replaceFrame(makeFrame(2'500, 25.0002, 110.0002));
    model.replaceFrame(makeFrame(3'000, 25.0003, 110.0003));

    const QVector<utms::RealtimeTrajectory> trajectories =
        model.visibleTrajectories(QDateTime::fromMSecsSinceEpoch(3'000));
    QCOMPARE(trajectories.size(), 1);
    QCOMPARE(trajectories.constFirst().segments.size(), 1);
    QCOMPARE(trajectories.constFirst().segments.constFirst().points.size(), 4);
}

QTEST_APPLESS_MAIN(RealtimeTrajectoryTest)

#include "test_realtime_trajectory.moc"
