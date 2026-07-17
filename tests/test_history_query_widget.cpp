#include <QtTest>

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include "ui/HistoryQueryWidget.h"

class HistoryQueryWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void deleteAllSessionsRequiresConfirmation();
    void deleteAllSessionsIsDisabledWhileRecording();
    void queriedFramesEnableTheRequiredPlaybackControls();
    void constrainedHeightKeepsPlaybackControlsVisibleAndScrollsManagementContent();
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

void HistoryQueryWidgetTest::queriedFramesEnableTheRequiredPlaybackControls() {
    utms::HistoryQueryResult result;
    utms::HistoryFrameRecord first_frame;
    first_frame.frame_time = QDateTime::fromMSecsSinceEpoch(1'000, QTimeZone::UTC);
    first_frame.received_at = first_frame.frame_time;
    utms::HistoryFrameRecord second_frame = first_frame;
    second_frame.frame_time = QDateTime::fromMSecsSinceEpoch(2'000, QTimeZone::UTC);
    second_frame.received_at = second_frame.frame_time;
    result.frames = {first_frame, second_frame};

    utms::HistoryQueryWidget widget;
    widget.setAvailable(true);
    widget.applyQueryResult(result);

    auto *enter_replay_button = widget.findChild<QPushButton *>(QStringLiteral("enterReplayButton"));
    auto *return_live_button = widget.findChild<QPushButton *>(QStringLiteral("returnLiveButton"));
    auto *play_button = widget.findChild<QPushButton *>(QStringLiteral("playButton"));
    auto *pause_button = widget.findChild<QPushButton *>(QStringLiteral("pauseButton"));
    auto *previous_button = widget.findChild<QPushButton *>(QStringLiteral("previousFrameButton"));
    auto *next_button = widget.findChild<QPushButton *>(QStringLiteral("nextFrameButton"));
    auto *timeline_slider = widget.findChild<QSlider *>(QStringLiteral("playbackTimelineSlider"));
    auto *rate_combo_box = widget.findChild<QComboBox *>(QStringLiteral("playbackRateComboBox"));
    QVERIFY(enter_replay_button != nullptr);
    QVERIFY(return_live_button != nullptr);
    QVERIFY(play_button != nullptr);
    QVERIFY(pause_button != nullptr);
    QVERIFY(previous_button != nullptr);
    QVERIFY(next_button != nullptr);
    QVERIFY(timeline_slider != nullptr);
    QVERIFY(rate_combo_box != nullptr);
    QVERIFY(enter_replay_button->isEnabled());

    QSignalSpy replay_spy(&widget, &utms::HistoryQueryWidget::replayRequested);
    enter_replay_button->click();
    QCOMPARE(replay_spy.count(), 1);

    widget.setReplayMode(true);
    widget.setPlaying(false);
    widget.setPlaybackPosition(0, 2, first_frame.frame_time);
    QVERIFY(!enter_replay_button->isEnabled());
    QVERIFY(return_live_button->isEnabled());
    QVERIFY(play_button->isEnabled());
    QVERIFY(!pause_button->isEnabled());
    QVERIFY(!previous_button->isEnabled());
    QVERIFY(next_button->isEnabled());
    QVERIFY(timeline_slider->isEnabled());
    QCOMPARE(rate_combo_box->count(), 4);
    QCOMPARE(rate_combo_box->itemData(0).toDouble(), 0.5);
    QCOMPARE(rate_combo_box->itemData(3).toDouble(), 4.0);
}

void HistoryQueryWidgetTest::constrainedHeightKeepsPlaybackControlsVisibleAndScrollsManagementContent() {
    utms::HistoryQueryWidget widget;
    widget.resize(480, 320);
    widget.show();
    QTest::qWait(20);

    auto *playback_group = widget.findChild<QWidget *>(QStringLiteral("historyPlaybackGroupBox"));
    auto *management_scroll_area =
        widget.findChild<QScrollArea *>(QStringLiteral("historyManagementScrollArea"));
    QVERIFY(playback_group != nullptr);
    QVERIFY(management_scroll_area != nullptr);
    QCOMPARE(widget.size(), QSize(480, 320));
    QVERIFY(playback_group->isVisibleTo(&widget));
    QVERIFY(management_scroll_area->isVisibleTo(&widget));
    QVERIFY(management_scroll_area->verticalScrollBar()->maximum() > 0);
    QCOMPARE(management_scroll_area->horizontalScrollBar()->maximum(), 0);

    auto *outer_layout = qobject_cast<QVBoxLayout *>(widget.layout());
    QVERIFY(outer_layout != nullptr);
    QCOMPARE(outer_layout->indexOf(playback_group), 0);
    QCOMPARE(outer_layout->indexOf(management_scroll_area), 1);
}

QTEST_MAIN(HistoryQueryWidgetTest)

#include "test_history_query_widget.moc"
