#include <cmath>

#include <QApplication>
#include <QMouseEvent>
#include <QtTest>

#include "core/GeofenceTypes.h"
#include "history/HistoryTypes.h"
#include "map/OnlineMapState.h"
#include "map/WebMercator.h"
#include "ui/MapPanel.h"
#include "ui/OfflineMapWidget.h"

class MapInteractionTest : public QObject
{
    Q_OBJECT

  private slots:
    void liveFrameDoesNotCancelUserDrag();
    void trajectoryStateSurvivesMapModeSwitch();
    void replayStateSurvivesMapSwitchAndIsNotOverwrittenByLiveFrames();
    void geofenceStateAndLocationSurviveMapModeSwitch();
    void alertHighlightIsTransientAndDoesNotChangeSelection();
    void offlineGeofenceCanBeDraggedAndEmitsUpdatedGeometry();
    void offlineRectangleAndPolygonHandlesCanBeDragged();
    void stalePersistenceEchoDoesNotOverwriteActiveGeofenceEdit();
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

void MapInteractionTest::geofenceStateAndLocationSurviveMapModeSwitch()
{
    utms::Geofence circle;
    circle.id = 7;
    circle.name = QStringLiteral("重点区域");
    circle.enabled = false;
    circle.visible = true;
    circle.geometry = utms::CircleGeofence{{25.31, 110.42}, 100.0};

    utms::Geofence polygon;
    polygon.id = 8;
    polygon.name = QStringLiteral("隐藏区域");
    polygon.visible = false;
    polygon.geometry = utms::PolygonGeofence{{{25.30, 110.40}, {25.32, 110.41}, {25.31, 110.43}}};

    utms::MapPanel panel;
    panel.setGeofences({circle, polygon});
    QCOMPARE(panel.geofences().size(), 2);
    QVERIFY(panel.locateGeofence(7));
    QCOMPARE(panel.center().latitude, 25.31);
    QCOMPARE(panel.center().longitude, 110.42);

    panel.setMapMode(utms::MapMode::kOffline);
    panel.setMapMode(utms::MapMode::kOnline);

    QCOMPARE(panel.geofences().size(), 2);
    QCOMPARE(panel.geofences().at(0).enabled, false);
    QCOMPARE(panel.geofences().at(1).visible, false);
    QVERIFY(!panel.locateGeofence(999));
}

void MapInteractionTest::alertHighlightIsTransientAndDoesNotChangeSelection()
{
    utms::RadarFrame frame;
    frame.received_at = QDateTime::currentDateTime();
    frame.tracks = {{42, utms::TargetType::kCar, {25.31, 110.41}}, {99, utms::TargetType::kTruck, {25.32, 110.42}}};

    utms::MapPanel panel;
    panel.setFrame(frame);
    QVERIFY(panel.selectTarget(99, false));
    QVERIFY(panel.flashAlertTarget(42, 50));
    QCOMPARE(panel.alertTrackIds(), QSet<qint64>({42}));
    QCOMPARE(panel.selectedTrackId(), std::optional<qint64>(99));
    QVERIFY(panel.flashAlertTargets({42, 99}, 50));
    QCOMPARE(panel.alertTrackIds(), QSet<qint64>({42, 99}));

    panel.setMapMode(utms::MapMode::kOffline);
    QCOMPARE(panel.alertTrackIds(), QSet<qint64>({42, 99}));
    QTRY_VERIFY(panel.alertTrackIds().isEmpty());
    QCOMPARE(panel.selectedTrackId(), std::optional<qint64>(99));
    QVERIFY(!panel.flashAlertTarget(1234, 50));
}

void MapInteractionTest::offlineGeofenceCanBeDraggedAndEmitsUpdatedGeometry()
{
    utms::OfflineMapWidget widget;
    widget.resize(800, 600);
    widget.show();

    utms::Geofence geofence;
    geofence.id = 17;
    geofence.name = QStringLiteral("可拖动圆形");
    geofence.geometry = utms::CircleGeofence{{25.311724, 110.416819}, 100.0};
    widget.setView({25.311724, 110.416819}, 17);
    widget.setGeofences({geofence});
    widget.setEditableGeofenceId(geofence.id);
    QCoreApplication::processEvents();

    QSignalSpy edit_spy(&widget, &utms::OfflineMapWidget::geofenceEdited);
    // Start inside the circle but away from its center/radius handles to exercise
    // whole-shape movement.
    const QPoint start = widget.viewport()->rect().center() + QPoint(0, 30);
    const QPoint destination = start + QPoint(45, 25);
    QTest::mousePress(widget.viewport(), Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(widget.viewport(), destination, 50);
    QTest::mouseRelease(widget.viewport(), Qt::LeftButton, Qt::NoModifier, destination);

    QTRY_COMPARE(edit_spy.count(), 1);
    const utms::Geofence edited = qvariant_cast<utms::Geofence>(edit_spy.takeFirst().at(0));
    const auto circle = std::get<utms::CircleGeofence>(edited.geometry);
    QVERIFY(circle.center.longitude > 110.416819);
    QVERIFY(circle.center.latitude < 25.311724);
    QCOMPARE(circle.radius_m, 100.0);
}

void MapInteractionTest::offlineRectangleAndPolygonHandlesCanBeDragged()
{
    {
        utms::OfflineMapWidget widget;
        widget.resize(800, 600);
        widget.show();
        widget.setView({25.311724, 110.416819}, 17);

        utms::Geofence rectangle;
        rectangle.id = 21;
        rectangle.name = QStringLiteral("可编辑矩形");
        rectangle.geometry = utms::RectangleGeofence{{25.3107, 110.4158}, {25.3127, 110.4178}};
        widget.setGeofences({rectangle});
        widget.setEditableGeofenceId(rectangle.id);
        QCoreApplication::processEvents();

        QSignalSpy edit_spy(&widget, &utms::OfflineMapWidget::geofenceEdited);
        const QPoint start = widget.mapFromScene(utms::WebMercator::geoToGlobalPixel({25.3107, 110.4158}, 17));
        const QPoint destination = start + QPoint(-18, 16);
        QTest::mousePress(widget.viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(widget.viewport(), destination, 50);
        QTest::mouseRelease(widget.viewport(), Qt::LeftButton, Qt::NoModifier, destination);

        QTRY_COMPARE(edit_spy.count(), 1);
        const utms::Geofence edited = qvariant_cast<utms::Geofence>(edit_spy.takeFirst().at(0));
        const auto geometry = std::get<utms::RectangleGeofence>(edited.geometry);
        QVERIFY(geometry.southwest.latitude < 25.3107);
        QVERIFY(geometry.southwest.longitude < 110.4158);
        QCOMPARE(geometry.northeast.latitude, 25.3127);
        QCOMPARE(geometry.northeast.longitude, 110.4178);
    }

    {
        utms::OfflineMapWidget widget;
        widget.resize(800, 600);
        widget.show();
        widget.setView({25.311724, 110.416819}, 17);

        utms::Geofence polygon;
        polygon.id = 22;
        polygon.name = QStringLiteral("可编辑多边形");
        polygon.geometry = utms::PolygonGeofence{{{25.3107, 110.4158}, {25.3127, 110.4168}, {25.3107, 110.4178}}};
        widget.setGeofences({polygon});
        widget.setEditableGeofenceId(polygon.id);
        QCoreApplication::processEvents();

        QSignalSpy edit_spy(&widget, &utms::OfflineMapWidget::geofenceEdited);
        const QPoint start = widget.mapFromScene(utms::WebMercator::geoToGlobalPixel({25.3107, 110.4158}, 17));
        const QPoint destination = start + QPoint(-16, 12);
        QTest::mousePress(widget.viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(widget.viewport(), destination, 50);
        QTest::mouseRelease(widget.viewport(), Qt::LeftButton, Qt::NoModifier, destination);

        QTRY_COMPARE(edit_spy.count(), 1);
        const utms::Geofence edited = qvariant_cast<utms::Geofence>(edit_spy.takeFirst().at(0));
        const auto geometry = std::get<utms::PolygonGeofence>(edited.geometry);
        QVERIFY(geometry.vertices.constFirst().latitude < 25.3107);
        QVERIFY(geometry.vertices.constFirst().longitude < 110.4158);
        QCOMPARE(geometry.vertices.size(), 3);
    }
}

void MapInteractionTest::stalePersistenceEchoDoesNotOverwriteActiveGeofenceEdit()
{
    utms::Geofence original;
    original.id = 31;
    original.name = QStringLiteral("异步保存围栏");
    original.geometry = utms::CircleGeofence{{25.311724, 110.416819}, 100.0};

    utms::MapPanel panel;
    panel.setMapMode(utms::MapMode::kOffline);
    panel.setGeofences({original});
    QVERIFY(panel.setEditableGeofenceId(original.id));

    auto *offline_map = panel.findChild<utms::OfflineMapWidget *>();
    QVERIFY(offline_map != nullptr);
    utms::Geofence edited = original;
    edited.geometry = utms::CircleGeofence{{25.3125, 110.4180}, 140.0};
    offline_map->geofenceEdited(edited);

    QCOMPARE(std::get<utms::CircleGeofence>(panel.geofences().constFirst().geometry).radius_m, 140.0);
    panel.setGeofences({original});
    QCOMPARE(std::get<utms::CircleGeofence>(panel.geofences().constFirst().geometry).radius_m, 140.0);

    panel.discardPendingGeofenceEdits();
    QCOMPARE(std::get<utms::CircleGeofence>(panel.geofences().constFirst().geometry).radius_m, 100.0);

    QVERIFY(panel.setEditableGeofenceId(original.id));
    offline_map->geofenceEdited(edited);
    utms::Geofence disabled = original;
    disabled.enabled = false;
    panel.setGeofences({disabled});
    QCOMPARE(std::get<utms::CircleGeofence>(panel.geofences().constFirst().geometry).radius_m, 140.0);
    QVERIFY(!panel.geofences().constFirst().enabled);

    panel.setGeofences({edited});
    panel.setGeofences({original});
    QCOMPARE(std::get<utms::CircleGeofence>(panel.geofences().constFirst().geometry).radius_m, 100.0);
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
