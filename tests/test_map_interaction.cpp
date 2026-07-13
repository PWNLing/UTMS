#include <cmath>

#include <QApplication>
#include <QMouseEvent>
#include <QtTest>

#include "map/OnlineMapState.h"
#include "ui/OfflineMapWidget.h"

class MapInteractionTest : public QObject
{
    Q_OBJECT

    private slots:
    void liveFrameDoesNotCancelUserDrag();
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

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("minimal"));
    QApplication application(argc, argv);
    MapInteractionTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_map_interaction.moc"
