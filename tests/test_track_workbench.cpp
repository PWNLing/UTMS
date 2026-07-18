#include <QApplication>
#include <QSignalSpy>
#include <QtTest>

#include "core/RadarTypes.h"
#include "workbench/TrackFilterProxyModel.h"
#include "workbench/TrackTableModel.h"
#include "workbench/TrackTableWidget.h"

namespace
{

utms::TrackData makeTrack(qint64 track_id, utms::TargetType type = utms::TargetType::kUnknown,
                          std::optional<double> velocity_mps = std::nullopt,
                          std::optional<double> distance_m = std::nullopt)
{
    utms::TrackData track;
    track.track_id = track_id;
    track.type = type;
    track.position = {25.311724, 110.416819};
    track.velocity_mps = velocity_mps;
    track.distance_m = distance_m;
    return track;
}

} // namespace

class TrackWorkbenchTest : public QObject
{
    Q_OBJECT

    private slots:
    void defaultsToTrackIdAscendingWithEightColumns();
    void filtersByTargetTypeAndRenumbersVisibleRows();
    void keepsMissingMeasurementsLastForBothSortOrders();
    void preservesSelectionUntilTargetDisappears();
    void clearsSelectionWhenRequestedTrackIsAbsent();
};

void TrackWorkbenchTest::defaultsToTrackIdAscendingWithEightColumns()
{
    utms::TrackTableModel source_model;
    utms::TrackFilterProxyModel proxy_model;
    proxy_model.setSourceModel(&source_model);
    source_model.replaceTracks({makeTrack(30), makeTrack(10), makeTrack(20)});

    QCOMPARE(proxy_model.columnCount(), 8);
    QCOMPARE(proxy_model.index(0, utms::TrackTableModel::kSequenceColumn).data().toInt(), 1);
    QCOMPARE(proxy_model.index(1, utms::TrackTableModel::kSequenceColumn).data().toInt(), 2);
    QCOMPARE(proxy_model.index(0, utms::TrackTableModel::kTrackIdColumn).data().toLongLong(), 10);
    QCOMPARE(proxy_model.index(1, utms::TrackTableModel::kTrackIdColumn).data().toLongLong(), 20);
    QCOMPARE(proxy_model.index(2, utms::TrackTableModel::kTrackIdColumn).data().toLongLong(), 30);
}

void TrackWorkbenchTest::filtersByTargetTypeAndRenumbersVisibleRows()
{
    utms::TrackTableModel source_model;
    utms::TrackFilterProxyModel proxy_model;
    proxy_model.setSourceModel(&source_model);
    source_model.replaceTracks({makeTrack(3, utms::TargetType::kCar), makeTrack(1, utms::TargetType::kPedestrian),
                                makeTrack(2, utms::TargetType::kCar)});
    proxy_model.setTargetTypeFilter(utms::TargetType::kCar);

    QCOMPARE(proxy_model.rowCount(), 2);
    QCOMPARE(proxy_model.index(0, utms::TrackTableModel::kSequenceColumn).data().toInt(), 1);
    QCOMPARE(proxy_model.index(1, utms::TrackTableModel::kSequenceColumn).data().toInt(), 2);
    QCOMPARE(proxy_model.index(0, utms::TrackTableModel::kTrackIdColumn).data().toLongLong(), 2);
    QCOMPARE(proxy_model.index(1, utms::TrackTableModel::kTrackIdColumn).data().toLongLong(), 3);
}

void TrackWorkbenchTest::keepsMissingMeasurementsLastForBothSortOrders()
{
    utms::TrackTableModel source_model;
    utms::TrackFilterProxyModel proxy_model;
    proxy_model.setSourceModel(&source_model);
    source_model.replaceTracks({makeTrack(1, utms::TargetType::kCar, std::nullopt, 8.0),
                                makeTrack(2, utms::TargetType::kCar, 4.0, std::nullopt),
                                makeTrack(3, utms::TargetType::kCar, 2.0, 3.0)});

    proxy_model.sort(utms::TrackTableModel::kVelocityColumn, Qt::AscendingOrder);
    QCOMPARE(proxy_model.index(0, utms::TrackTableModel::kTrackIdColumn).data().toLongLong(), 3);
    QCOMPARE(proxy_model.index(1, utms::TrackTableModel::kTrackIdColumn).data().toLongLong(), 2);
    QCOMPARE(proxy_model.index(2, utms::TrackTableModel::kVelocityColumn).data().toString(), QStringLiteral("--"));

    proxy_model.sort(utms::TrackTableModel::kVelocityColumn, Qt::DescendingOrder);
    QCOMPARE(proxy_model.index(0, utms::TrackTableModel::kTrackIdColumn).data().toLongLong(), 2);
    QCOMPARE(proxy_model.index(1, utms::TrackTableModel::kTrackIdColumn).data().toLongLong(), 3);
    QCOMPARE(proxy_model.index(2, utms::TrackTableModel::kVelocityColumn).data().toString(), QStringLiteral("--"));

    proxy_model.sort(utms::TrackTableModel::kDistanceColumn, Qt::DescendingOrder);
    QCOMPARE(proxy_model.index(0, utms::TrackTableModel::kTrackIdColumn).data().toLongLong(), 1);
    QCOMPARE(proxy_model.index(1, utms::TrackTableModel::kTrackIdColumn).data().toLongLong(), 3);
    QCOMPARE(proxy_model.index(2, utms::TrackTableModel::kDistanceColumn).data().toString(), QStringLiteral("--"));
}

void TrackWorkbenchTest::preservesSelectionUntilTargetDisappears()
{
    utms::TrackTableWidget workbench;
    workbench.replaceTracks({makeTrack(1), makeTrack(2)});
    QVERIFY(workbench.selectTrackById(2));
    QCOMPARE(workbench.selectedTrackId(), std::optional<qint64>(2));

    workbench.replaceTracks({makeTrack(2), makeTrack(3)});
    QCOMPARE(workbench.selectedTrackId(), std::optional<qint64>(2));

    workbench.replaceTracks({makeTrack(3)});
    QVERIFY(!workbench.selectedTrackId().has_value());
}

void TrackWorkbenchTest::clearsSelectionWhenRequestedTrackIsAbsent()
{
    utms::TrackTableWidget workbench;
    workbench.replaceTracks({makeTrack(1), makeTrack(2)});
    QVERIFY(workbench.selectTrackById(2));

    QSignalSpy cleared_spy(&workbench, &utms::TrackTableWidget::targetSelectionCleared);
    QVERIFY(!workbench.selectTrackById(99));
    QCOMPARE(cleared_spy.count(), 1);
    QVERIFY(!workbench.selectedTrackId().has_value());
}

int main(int argc, char *argv[])
{
    if (!qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("minimal")))
    {
        return 1;
    }
    QApplication application(argc, argv);
    TrackWorkbenchTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_track_workbench.moc"
