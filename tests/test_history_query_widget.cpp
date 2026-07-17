#include <QtTest>

#include <QAbstractButton>
#include <QApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTimer>

#include "ui/HistoryQueryWidget.h"

class HistoryQueryWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void deleteAllSessionsRequiresConfirmation();
    void deleteAllSessionsIsDisabledWhileRecording();
};

void HistoryQueryWidgetTest::deleteAllSessionsRequiresConfirmation() {
    utms::HistoryQueryWidget widget;
    widget.setAvailable(true);
    widget.setSessions(
        {{1, QDateTime::currentDateTimeUtc(), QDateTime::currentDateTimeUtc(), utms::HistorySessionState::kClosed}});

    auto *delete_all_button = widget.findChild<QPushButton *>(QStringLiteral("deleteAllSessionsButton"));
    QVERIFY(delete_all_button != nullptr);
    QVERIFY(delete_all_button->isEnabled());
    QSignalSpy delete_spy(&widget, &utms::HistoryQueryWidget::deleteAllSessionsRequested);

    bool cancel_confirmation_seen = false;
    QTimer::singleShot(0, [&cancel_confirmation_seen]() {
        auto *message_box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (message_box == nullptr) {
            return;
        }
        cancel_confirmation_seen = message_box->text().contains(HistoryQueryWidgetTest::tr("此操作不可撤销"));
        message_box->button(QMessageBox::No)->click();
    });
    delete_all_button->click();
    QVERIFY(cancel_confirmation_seen);
    QCOMPARE(delete_spy.count(), 0);

    bool delete_confirmation_seen = false;
    QTimer::singleShot(0, [&delete_confirmation_seen]() {
        auto *message_box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (message_box == nullptr) {
            return;
        }
        delete_confirmation_seen = message_box->text().contains(HistoryQueryWidgetTest::tr("全部 1 个历史会话"));
        message_box->button(QMessageBox::Yes)->click();
    });
    delete_all_button->click();
    QVERIFY(delete_confirmation_seen);
    QCOMPARE(delete_spy.count(), 1);
}

void HistoryQueryWidgetTest::deleteAllSessionsIsDisabledWhileRecording() {
    utms::HistoryQueryWidget widget;
    widget.setAvailable(true);
    widget.setSessions(
        {{1, QDateTime::currentDateTimeUtc(), std::nullopt, utms::HistorySessionState::kActive},
         {2, QDateTime::currentDateTimeUtc(), QDateTime::currentDateTimeUtc(), utms::HistorySessionState::kClosed}});

    auto *delete_all_button = widget.findChild<QPushButton *>(QStringLiteral("deleteAllSessionsButton"));
    QVERIFY(delete_all_button != nullptr);
    QVERIFY(!delete_all_button->isEnabled());
    QVERIFY(delete_all_button->toolTip().contains(tr("停止 UDP 监听")));
}

QTEST_MAIN(HistoryQueryWidgetTest)

#include "test_history_query_widget.moc"
