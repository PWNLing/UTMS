#include <QtTest>

#include <QApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>

#include "ui/AlertCenterWidget.h"

class AlertCenterWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void queriedAlertsEnableSingleAndBatchAcknowledgementWithNotes();
};

void AlertCenterWidgetTest::queriedAlertsEnableSingleAndBatchAcknowledgementWithNotes() {
    utms::TargetAlert first_alert;
    first_alert.id = 17;
    first_alert.occurred_at = QDateTime::currentDateTimeUtc();
    first_alert.rule_name = QStringLiteral("车辆进入");
    first_alert.geofence_name = QStringLiteral("重点区域");
    first_alert.track_id = 42;
    first_alert.target_type = utms::TargetType::kCar;
    first_alert.description = QStringLiteral("目标进入");

    utms::TargetAlert second_alert = first_alert;
    second_alert.id = 18;
    second_alert.track_id = 99;
    second_alert.description = QStringLiteral("目标离开");

    utms::AlertQueryResult result;
    result.alerts = {first_alert, second_alert};
    result.unacknowledged_count = 2;

    utms::AlertCenterWidget widget;
    widget.setAvailable(true);
    widget.applyQueryResult(result);

    auto *table = widget.findChild<QTableWidget *>(QStringLiteral("alertTableWidget"));
    auto *note_edit = widget.findChild<QLineEdit *>(QStringLiteral("alertHandlingNoteLineEdit"));
    auto *acknowledge_button = widget.findChild<QPushButton *>(QStringLiteral("acknowledgeAlertButton"));
    auto *acknowledge_all_button = widget.findChild<QPushButton *>(QStringLiteral("acknowledgeAllAlertsButton"));
    QVERIFY(table != nullptr);
    QVERIFY(note_edit != nullptr);
    QVERIFY(acknowledge_button != nullptr);
    QVERIFY(acknowledge_all_button != nullptr);

    table->selectRow(0);
    note_edit->setText(QStringLiteral("已现场核实"));
    QSignalSpy acknowledgement_spy(&widget, &utms::AlertCenterWidget::acknowledgeRequested);
    acknowledge_button->click();
    QCOMPARE(acknowledgement_spy.count(), 1);
    utms::AlertAcknowledgementRequest request =
        acknowledgement_spy.takeFirst().constFirst().value<utms::AlertAcknowledgementRequest>();
    QCOMPARE(request.alert_ids, QVector<qint64>({17}));
    QCOMPARE(request.handling_note, QStringLiteral("已现场核实"));

    acknowledge_all_button->click();
    QCOMPARE(acknowledgement_spy.count(), 1);
    request = acknowledgement_spy.takeFirst().constFirst().value<utms::AlertAcknowledgementRequest>();
    QCOMPARE(request.alert_ids, QVector<qint64>({17, 18}));
}

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("minimal"));
    QApplication application(argc, argv);
    AlertCenterWidgetTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_alert_center_widget.moc"
