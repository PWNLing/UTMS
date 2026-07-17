#include <cmath>

#include <QApplication>
#include <QMouseEvent>
#include <QtTest>

#include "history/HistoryTypes.h"
#include "map/OnlineMapState.h"
#include "ui/MapPanel.h"
#include "ui/OfflineMapWidget.h"

class MapInteractionTest : public QObject
{
    Q_OBJECT

    private slots:
    void liveFrameDoesNotCancelUserDrag();
    void trajectoryStateSurvivesMapModeSwitch();
    void replayStateSurvivesMapSwitchAndIsNotOverwrittenByLiveFrames();
};

void MapInteractionTest::liveFrameDoesNotCancelUserDrag()
{
    utms::OnlineMapState state;
    utms::RadarFrame frame;
    frame.ego_position = utms::GeoPosition{25.311724, 110.416819};
    state.replaceFrame(frame);

    utms::OfflineMapWidget widget;
    widget.resize(800, 600);
    widget.show();
    QTest::qWait(50);

    utms::GeoPosition released_center = state.center();
    connect(&widget, &utms::OfflineMapWidget::viewChanged, this,
            [&released_center](const utms::GeoPosition &center, int) { released_center = center; });

    const auto drag_center = [&](bool inject_live_frame)
    {
        widget.setView(state.center(), state.zoom());
        widget.renderState(state);
        QCoreApplication::processEvents();

        const QPoint start_px = widget.viewport()->rect().center();
        const QPoint end_px = start_px + QPoint(120, 0);
        QTest::mousePress(widget.viewport(), Qt::LeftButton, Qt::NoModifier, start_px);
        QMouseEvent move_event(QEvent::MouseMove, end_px, widget.viewport()->mapToGlobal(end_px), Qt::NoButton,
                               Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(widget.viewport(), &move_event);
        if (inject_live_frame)
        {
            for (int frame_index = 0; frame_index < 20; ++frame_index)
            {
                widget.renderState(state);
            }
        }
        QMouseEvent release_event(QEvent::MouseButtonRelease, end_px, widget.viewport()->mapToGlobal(end_px),
                                  Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(widget.viewport(), &release_event);
        QCoreApplication::processEvents();
        return released_center;
    };

    const utms::GeoPosition uninterrupted_center = drag_center(false);
    const utms::GeoPosition live_frame_center = drag_center(true);
    QVERIFY2(std::abs(live_frame_center.longitude - uninterrupted_center.longitude) < 1e-7,
             "A live frame changed the result of the same user drag");
}

void MapInteractionTest::trajectoryStateSurvivesMapModeSwitch()
{
    const QDateTime first_time = QDateTime::currentDateTime();
    utms::RadarFrame first_frame;
    first_frame.received_at = first_time;
    utms::TrackData first_track;
    first_track.track_id = 42;
    first_track.type = utms::TargetType::kCar;
    first_track.position = {25.0, 110.0};
    first_frame.tracks.append(first_track);

    utms::RadarFrame second_frame = first_frame;
    second_frame.received_at = first_time.addMSecs(500);
    second_frame.tracks[0].position = {25.0001, 110.0001};

    utms::MapPanel panel;
    panel.setFrame(first_frame);
    panel.setFrame(second_frame);
    QVERIFY(panel.selectTarget(42, false));
    QCOMPARE(panel.realtimeTrajectories(second_frame.received_at).size(), 1);

    panel.setMapMode(utms::MapMode::kOffline);
    panel.setMapMode(utms::MapMode::kOnline);

    QCOMPARE(panel.selectedTrackId(), std::optional<qint64>(42));
    const QVector<utms::RealtimeTrajectory> trajectories = panel.realtimeTrajectories(second_frame.received_at);
    QCOMPARE(trajectories.size(), 1);
    QCOMPARE(trajectories.constFirst().segments.constFirst().points.size(), 2);
}

void MapInteractionTest::replayStateSurvivesMapSwitchAndIsNotOverwrittenByLiveFrames()
{
    utms::RadarFrame live_frame;
    live_frame.received_at = QDateTime::fromMSecsSinceEpoch(10'000, QTimeZone::UTC);
    live_frame.sequence = 10;
    live_frame.tracks = {{1, utms::TargetType::kCar, {25.0, 110.0}}};

    utms::RadarFrame replay_frame;
    replay_frame.received_at = QDateTime::fromMSecsSinceEpoch(2'000, QTimeZone::UTC);
    replay_frame.sequence = 2;
    replay_frame.tracks = {{42, utms::TargetType::kPedestrian, {25.1, 110.1}}};

    utms::HistoryReplayTrajectory trajectory;
    trajectory.track_id = 42;
    trajectory.type = utms::TargetType::kPedestrian;
    trajectory.segments = {{{25.0, 110.0}, {25.1, 110.1}}};

    utms::MapPanel panel;
    panel.setFrame(live_frame);
    panel.setReplayMode(true);
    panel.setReplayFrame(replay_frame);
    panel.setReplayTrajectory(trajectory);

    live_frame.sequence = 11;
    live_frame.received_at = QDateTime::fromMSecsSinceEpoch(11'000, QTimeZone::UTC);
    panel.setFrame(live_frame);
    QCOMPARE(panel.displayedFrame().sequence, std::optional<qint64>(2));

    panel.setMapMode(utms::MapMode::kOffline);
    panel.setMapMode(utms::MapMode::kOnline);
    QVERIFY(panel.isReplayMode());
    QCOMPARE(panel.replayTrajectory()->track_id, 42);
    QCOMPARE(panel.replayTrajectory()->segments.constFirst().size(), 2);

    panel.setReplayMode(false);
    QVERIFY(!panel.isReplayMode());
    QCOMPARE(panel.displayedFrame().sequence, std::optional<qint64>(11));
    QVERIFY(!panel.replayTrajectory().has_value());
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("minimal"));
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication application(argc, argv);
    MapInteractionTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_map_interaction.moc"
