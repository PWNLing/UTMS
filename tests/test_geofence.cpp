#include <QtTest>

#include <QTemporaryDir>

#include "core/GeofenceGeometry.h"
#include "history/HistoryStore.h"

namespace {

utms::Geofence makeCircle(const QString &name) {
    utms::Geofence geofence;
    geofence.name = name;
    geofence.geometry = utms::CircleGeofence{{25.311724, 110.416819}, 120.0};
    return geofence;
}

utms::Geofence makeRectangle(const QString &name) {
    utms::Geofence geofence;
    geofence.name = name;
    geofence.geometry = utms::RectangleGeofence{{25.30, 110.40}, {25.32, 110.43}};
    return geofence;
}

utms::Geofence makePolygon(const QString &name) {
    utms::Geofence geofence;
    geofence.name = name;
    geofence.geometry = utms::PolygonGeofence{{{25.30, 110.40}, {25.32, 110.41}, {25.31, 110.44}, {25.29, 110.42}}};
    return geofence;
}

} // namespace

class GeofenceTest : public QObject {
    Q_OBJECT

private slots:
    void acceptsRequiredShapesAndComputesTheirCenters();
    void rejectsInvalidCircleRectangleAndPolygonGeometry();
    void persistsCreatesUpdatesVisibilityAndDeletionAcrossReopen();
    void rejectsInvalidGeofenceBeforePersistence();
};

void GeofenceTest::acceptsRequiredShapesAndComputesTheirCenters() {
    const utms::Geofence circle = makeCircle(QStringLiteral("圆形重点区"));
    const utms::Geofence rectangle = makeRectangle(QStringLiteral("矩形重点区"));
    const utms::Geofence polygon = makePolygon(QStringLiteral("多边形重点区"));

    QVERIFY(utms::validateGeofence(circle).isEmpty());
    QVERIFY(utms::validateGeofence(rectangle).isEmpty());
    QVERIFY(utms::validateGeofence(polygon).isEmpty());
    QCOMPARE(utms::geofenceShape(circle), utms::GeofenceShape::kCircle);
    QCOMPARE(utms::geofenceShape(rectangle), utms::GeofenceShape::kRectangle);
    QCOMPARE(utms::geofenceShape(polygon), utms::GeofenceShape::kPolygon);

    const utms::GeoPosition rectangle_center = utms::geofenceCenter(rectangle);
    QCOMPARE(rectangle_center.latitude, 25.31);
    QCOMPARE(rectangle_center.longitude, 110.415);

    const utms::GeoPosition polygon_center = utms::geofenceCenter(polygon);
    QVERIFY(qAbs(polygon_center.latitude - 25.305) < 1e-9);
    QVERIFY(qAbs(polygon_center.longitude - 110.4175) < 1e-9);
}

void GeofenceTest::rejectsInvalidCircleRectangleAndPolygonGeometry() {
    utms::Geofence unnamed = makeCircle({});
    QVERIFY(!utms::validateGeofence(unnamed).isEmpty());

    utms::Geofence invalid_circle = makeCircle(QStringLiteral("非法圆"));
    invalid_circle.geometry = utms::CircleGeofence{{25.31, 110.41}, 0.0};
    QVERIFY(!utms::validateGeofence(invalid_circle).isEmpty());

    utms::Geofence invalid_rectangle = makeRectangle(QStringLiteral("非法矩形"));
    invalid_rectangle.geometry = utms::RectangleGeofence{{25.32, 110.43}, {25.30, 110.40}};
    QVERIFY(!utms::validateGeofence(invalid_rectangle).isEmpty());

    utms::Geofence too_small_polygon = makePolygon(QStringLiteral("顶点不足"));
    too_small_polygon.geometry = utms::PolygonGeofence{{{25.30, 110.40}, {25.31, 110.41}}};
    QVERIFY(!utms::validateGeofence(too_small_polygon).isEmpty());

    QVector<utms::GeoPosition> too_many_vertices;
    for (int index = 0; index < 21; ++index) {
        too_many_vertices.append({25.0 + index * 0.001, 110.0 + index * 0.001});
    }
    utms::Geofence too_large_polygon = makePolygon(QStringLiteral("顶点过多"));
    too_large_polygon.geometry = utms::PolygonGeofence{too_many_vertices};
    QVERIFY(!utms::validateGeofence(too_large_polygon).isEmpty());

    utms::Geofence self_intersecting_polygon = makePolygon(QStringLiteral("自相交"));
    self_intersecting_polygon.geometry =
        utms::PolygonGeofence{{{25.30, 110.40}, {25.32, 110.42}, {25.30, 110.42}, {25.32, 110.40}}};
    const QString polygon_error = utms::validateGeofence(self_intersecting_polygon);
    QVERIFY(polygon_error.contains(QStringLiteral("自相交")));

    utms::Geofence zero_area_polygon = makePolygon(QStringLiteral("零面积"));
    zero_area_polygon.geometry = utms::PolygonGeofence{{{25.30, 110.40}, {25.31, 110.41}, {25.32, 110.42}}};
    QVERIFY(!utms::validateGeofence(zero_area_polygon).isEmpty());
}

void GeofenceTest::persistsCreatesUpdatesVisibilityAndDeletionAcrossReopen() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());
    const QString database_path = temporary_directory.filePath(QStringLiteral("history.sqlite"));
    QString error;
    qint64 circle_id = 0;
    qint64 rectangle_id = 0;
    qint64 polygon_id = 0;

    {
        utms::HistoryStore store(database_path);
        QVERIFY2(store.initialize(&error), qPrintable(error));
        const std::optional<qint64> created_circle = store.createGeofence(makeCircle(QStringLiteral("圆形")), &error);
        const std::optional<qint64> created_rectangle =
            store.createGeofence(makeRectangle(QStringLiteral("矩形")), &error);
        const std::optional<qint64> created_polygon =
            store.createGeofence(makePolygon(QStringLiteral("多边形")), &error);
        QVERIFY2(created_circle.has_value(), qPrintable(error));
        QVERIFY2(created_rectangle.has_value(), qPrintable(error));
        QVERIFY2(created_polygon.has_value(), qPrintable(error));
        circle_id = created_circle.value();
        rectangle_id = created_rectangle.value();
        polygon_id = created_polygon.value();

        std::optional<QVector<utms::Geofence>> geofences = store.loadGeofences(&error);
        QVERIFY2(geofences.has_value(), qPrintable(error));
        QCOMPARE(geofences->size(), 3);

        utms::Geofence updated_circle = geofences->constFirst();
        QCOMPARE(updated_circle.id, circle_id);
        updated_circle.name = QStringLiteral("已更新圆形");
        updated_circle.enabled = false;
        updated_circle.visible = false;
        updated_circle.geometry = utms::CircleGeofence{{25.4, 110.5}, 260.0};
        QVERIFY2(store.updateGeofence(updated_circle, &error), qPrintable(error));

        utms::Geofence stale_map_edit = updated_circle;
        stale_map_edit.enabled = true;
        stale_map_edit.visible = true;
        stale_map_edit.geometry = utms::CircleGeofence{{25.41, 110.51}, 300.0};
        QVERIFY2(store.updateGeofenceGeometry(stale_map_edit, &error), qPrintable(error));
        QVERIFY2(store.setGeofenceEnabled(circle_id, true, &error), qPrintable(error));
        QVERIFY2(store.setGeofenceVisible(circle_id, true, &error), qPrintable(error));
        stale_map_edit.enabled = false;
        stale_map_edit.visible = false;
        stale_map_edit.geometry = utms::CircleGeofence{{25.42, 110.52}, 320.0};
        QVERIFY2(store.updateGeofenceGeometry(stale_map_edit, &error), qPrintable(error));
        QVERIFY2(store.deleteGeofence(rectangle_id, &error), qPrintable(error));
    }

    utms::HistoryStore reopened_store(database_path);
    QVERIFY2(reopened_store.initialize(&error), qPrintable(error));
    const std::optional<QVector<utms::Geofence>> geofences = reopened_store.loadGeofences(&error);
    QVERIFY2(geofences.has_value(), qPrintable(error));
    QCOMPARE(geofences->size(), 2);
    QCOMPARE(geofences->at(0).id, circle_id);
    QCOMPARE(geofences->at(0).name, QStringLiteral("已更新圆形"));
    QVERIFY(geofences->at(0).enabled);
    QVERIFY(geofences->at(0).visible);
    const auto &circle = std::get<utms::CircleGeofence>(geofences->at(0).geometry);
    QCOMPARE(circle.center.latitude, 25.42);
    QCOMPARE(circle.center.longitude, 110.52);
    QCOMPARE(circle.radius_m, 320.0);
    QCOMPARE(geofences->at(1).id, polygon_id);
}

void GeofenceTest::rejectsInvalidGeofenceBeforePersistence() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());

    utms::HistoryStore store(temporary_directory.filePath(QStringLiteral("history.sqlite")));
    QString error;
    QVERIFY2(store.initialize(&error), qPrintable(error));

    utms::Geofence invalid_polygon = makePolygon(QStringLiteral("非法多边形"));
    invalid_polygon.geometry =
        utms::PolygonGeofence{{{25.30, 110.40}, {25.32, 110.42}, {25.30, 110.42}, {25.32, 110.40}}};
    QVERIFY(!store.createGeofence(invalid_polygon, &error).has_value());
    QVERIFY(error.contains(QStringLiteral("自相交")));

    const std::optional<QVector<utms::Geofence>> geofences = store.loadGeofences(&error);
    QVERIFY2(geofences.has_value(), qPrintable(error));
    QVERIFY(geofences->isEmpty());
}

QTEST_GUILESS_MAIN(GeofenceTest)

#include "test_geofence.moc"
