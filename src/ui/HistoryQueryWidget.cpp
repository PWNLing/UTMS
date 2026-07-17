#include "ui/HistoryQueryWidget.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

namespace utms {
namespace {

constexpr int kTimelineSteps = 10'000;

QString sessionStateText(HistorySessionState state) {
    switch (state) {
    case HistorySessionState::kActive:
        return HistoryQueryWidget::tr("记录中");
    case HistorySessionState::kClosed:
        return HistoryQueryWidget::tr("已关闭");
    case HistorySessionState::kAbnormal:
        return HistoryQueryWidget::tr("异常结束");
    }
    return HistoryQueryWidget::tr("未知");
}

QString formatByteSize(qint64 size_bytes) {
    constexpr double kBytesPerKib = 1'024.0;
    constexpr double kBytesPerMib = kBytesPerKib * 1'024.0;
    constexpr double kBytesPerGib = kBytesPerMib * 1'024.0;
    if (size_bytes >= static_cast<qint64>(kBytesPerGib)) {
        return QStringLiteral("%1 GiB").arg(static_cast<double>(size_bytes) / kBytesPerGib, 0, 'f', 2);
    }
    if (size_bytes >= static_cast<qint64>(kBytesPerMib)) {
        return QStringLiteral("%1 MiB").arg(static_cast<double>(size_bytes) / kBytesPerMib, 0, 'f', 2);
    }
    if (size_bytes >= static_cast<qint64>(kBytesPerKib)) {
        return QStringLiteral("%1 KiB").arg(static_cast<double>(size_bytes) / kBytesPerKib, 0, 'f', 2);
    }
    return QStringLiteral("%1 B").arg(size_bytes);
}

} // namespace

HistoryQueryWidget::HistoryQueryWidget(QWidget *parent)
    : QWidget(parent), time_range_check_box_(new QCheckBox(tr("限定时间范围"), this)),
      start_time_edit_(new QDateTimeEdit(this)), end_time_edit_(new QDateTimeEdit(this)),
      session_combo_box_(new QComboBox(this)), track_id_line_edit_(new QLineEdit(this)),
      target_type_combo_box_(new QComboBox(this)), query_button_(new QPushButton(tr("查询"), this)),
      refresh_button_(new QPushButton(tr("刷新会话"), this)),
      delete_session_button_(new QPushButton(tr("删除选中会话"), this)),
      delete_all_sessions_button_(new QPushButton(tr("删除所有会话"), this)),
      export_query_button_(new QPushButton(tr("导出当前查询"), this)), export_track_combo_box_(new QComboBox(this)),
      export_track_button_(new QPushButton(tr("导出选中航迹"), this)),
      enter_replay_button_(new QPushButton(tr("进入回放"), this)),
      return_live_button_(new QPushButton(tr("返回实时"), this)), play_button_(new QPushButton(tr("播放"), this)),
      pause_button_(new QPushButton(tr("暂停"), this)), previous_frame_button_(new QPushButton(tr("上一帧"), this)),
      next_frame_button_(new QPushButton(tr("下一帧"), this)),
      playback_timeline_slider_(new QSlider(Qt::Horizontal, this)), playback_rate_combo_box_(new QComboBox(this)),
      playback_mode_label_(new QLabel(tr("实时模式"), this)), playback_position_label_(new QLabel(tr("-- / --"), this)),
      database_size_label_(new QLabel(tr("数据库占用：--"), this)), result_label_(new QLabel(tr("尚未查询"), this)),
      status_label_(new QLabel(tr("历史数据库初始化中"), this)) {
    delete_session_button_->setObjectName(QStringLiteral("deleteSessionButton"));
    delete_all_sessions_button_->setObjectName(QStringLiteral("deleteAllSessionsButton"));
    enter_replay_button_->setObjectName(QStringLiteral("enterReplayButton"));
    return_live_button_->setObjectName(QStringLiteral("returnLiveButton"));
    play_button_->setObjectName(QStringLiteral("playButton"));
    pause_button_->setObjectName(QStringLiteral("pauseButton"));
    previous_frame_button_->setObjectName(QStringLiteral("previousFrameButton"));
    next_frame_button_->setObjectName(QStringLiteral("nextFrameButton"));
    playback_timeline_slider_->setObjectName(QStringLiteral("playbackTimelineSlider"));
    playback_rate_combo_box_->setObjectName(QStringLiteral("playbackRateComboBox"));
    time_range_check_box_->setChecked(true);
    const QDateTime current_time = QDateTime::currentDateTime();
    start_time_edit_->setDateTime(current_time.addDays(-1));
    end_time_edit_->setDateTime(current_time);
    start_time_edit_->setCalendarPopup(true);
    end_time_edit_->setCalendarPopup(true);
    start_time_edit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    end_time_edit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    start_time_edit_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    end_time_edit_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

    session_combo_box_->addItem(tr("全部会话"));
    track_id_line_edit_->setPlaceholderText(tr("全部航迹"));
    track_id_line_edit_->setValidator(
        new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[1-9][0-9]{0,18}")), track_id_line_edit_));
    target_type_combo_box_->addItem(tr("全部类别"));
    target_type_combo_box_->addItem(tr("汽车"), static_cast<int>(TargetType::kCar));
    target_type_combo_box_->addItem(tr("卡车"), static_cast<int>(TargetType::kTruck));
    target_type_combo_box_->addItem(tr("行人"), static_cast<int>(TargetType::kPedestrian));
    target_type_combo_box_->addItem(tr("自行车"), static_cast<int>(TargetType::kBicycle));
    target_type_combo_box_->addItem(tr("未知"), static_cast<int>(TargetType::kUnknown));
    export_track_combo_box_->setEnabled(false);
    export_query_button_->setEnabled(false);
    export_track_button_->setEnabled(false);
    playback_timeline_slider_->setRange(0, kTimelineSteps);
    playback_rate_combo_box_->addItem(tr("0.5×"), 0.5);
    playback_rate_combo_box_->addItem(tr("1×"), 1.0);
    playback_rate_combo_box_->addItem(tr("2×"), 2.0);
    playback_rate_combo_box_->addItem(tr("4×"), 4.0);
    playback_rate_combo_box_->setCurrentIndex(1);
    playback_mode_label_->setStyleSheet(QStringLiteral("QLabel { color: #208a4b; font-weight: 700; }"));

    auto *query_group = new QGroupBox(tr("查询条件"), this);
    auto *query_layout = new QGridLayout(query_group);
    query_layout->addWidget(time_range_check_box_, 0, 0, 1, 2);
    query_layout->addWidget(new QLabel(tr("开始"), query_group), 1, 0);
    query_layout->addWidget(start_time_edit_, 1, 1);
    query_layout->addWidget(new QLabel(tr("结束"), query_group), 2, 0);
    query_layout->addWidget(end_time_edit_, 2, 1);
    query_layout->addWidget(new QLabel(tr("会话"), query_group), 3, 0);
    query_layout->addWidget(session_combo_box_, 3, 1);
    query_layout->addWidget(new QLabel(tr("航迹 ID"), query_group), 4, 0);
    query_layout->addWidget(track_id_line_edit_, 4, 1);
    query_layout->addWidget(new QLabel(tr("类别"), query_group), 5, 0);
    query_layout->addWidget(target_type_combo_box_, 5, 1);
    auto *query_actions_layout = new QHBoxLayout();
    query_actions_layout->addStretch();
    query_actions_layout->addWidget(query_button_);
    query_actions_layout->addWidget(refresh_button_);
    query_layout->addLayout(query_actions_layout, 6, 0, 1, 2);
    query_layout->setColumnStretch(1, 1);

    auto *playback_group = new QGroupBox(tr("历史回放"), this);
    playback_group->setObjectName(QStringLiteral("historyPlaybackGroupBox"));
    auto *playback_layout = new QVBoxLayout(playback_group);
    auto *mode_layout = new QHBoxLayout();
    mode_layout->addWidget(playback_mode_label_);
    mode_layout->addWidget(enter_replay_button_);
    mode_layout->addWidget(return_live_button_);
    mode_layout->addStretch();
    mode_layout->addWidget(new QLabel(tr("速度"), playback_group));
    mode_layout->addWidget(playback_rate_combo_box_);
    auto *navigation_layout = new QHBoxLayout();
    navigation_layout->addWidget(previous_frame_button_);
    navigation_layout->addWidget(play_button_);
    navigation_layout->addWidget(pause_button_);
    navigation_layout->addWidget(next_frame_button_);
    navigation_layout->addStretch();
    auto *timeline_layout = new QHBoxLayout();
    timeline_layout->addWidget(playback_timeline_slider_, 1);
    timeline_layout->addWidget(playback_position_label_);
    playback_layout->addLayout(mode_layout);
    playback_layout->addLayout(navigation_layout);
    playback_layout->addLayout(timeline_layout);
    playback_layout->setContentsMargins(8, 8, 8, 8);
    playback_layout->setSpacing(4);

    auto *storage_group = new QGroupBox(tr("存储管理"), this);
    auto *storage_layout = new QHBoxLayout(storage_group);
    storage_layout->addWidget(database_size_label_);
    storage_layout->addStretch();
    storage_layout->addWidget(delete_session_button_);
    storage_layout->addWidget(delete_all_sessions_button_);

    auto *export_group = new QGroupBox(tr("CSV 导出"), this);
    auto *export_layout = new QGridLayout(export_group);
    export_layout->addWidget(export_query_button_, 0, 0);
    export_layout->addWidget(new QLabel(tr("航迹"), export_group), 1, 0);
    export_layout->addWidget(export_track_combo_box_, 1, 1);
    export_layout->addWidget(export_track_button_, 1, 2);
    export_layout->setColumnStretch(1, 1);

    auto *management_content = new QWidget(this);
    auto *management_content_layout = new QVBoxLayout(management_content);
    management_content_layout->setContentsMargins(0, 0, 4, 0);
    management_content_layout->addWidget(query_group);
    management_content_layout->addWidget(storage_group);
    management_content_layout->addWidget(export_group);
    management_content_layout->addWidget(result_label_);
    management_content_layout->addWidget(status_label_);
    management_content_layout->addStretch();

    auto *management_scroll_area = new QScrollArea(this);
    management_scroll_area->setObjectName(QStringLiteral("historyManagementScrollArea"));
    management_scroll_area->setWidgetResizable(true);
    management_scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    management_scroll_area->setFrameShape(QFrame::NoFrame);
    management_scroll_area->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    management_scroll_area->setMinimumHeight(0);
    management_scroll_area->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    management_scroll_area->setWidget(management_content);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    layout->addWidget(playback_group);
    layout->addWidget(management_scroll_area, 1);

    connect(time_range_check_box_, &QCheckBox::toggled, start_time_edit_, &QWidget::setEnabled);
    connect(time_range_check_box_, &QCheckBox::toggled, end_time_edit_, &QWidget::setEnabled);
    connect(query_button_, &QPushButton::clicked, this, &HistoryQueryWidget::handleQueryRequested);
    connect(refresh_button_, &QPushButton::clicked, this, &HistoryQueryWidget::refreshRequested);
    connect(delete_session_button_, &QPushButton::clicked, this, &HistoryQueryWidget::handleDeleteSessionRequested);
    connect(delete_all_sessions_button_, &QPushButton::clicked, this,
            &HistoryQueryWidget::handleDeleteAllSessionsRequested);
    connect(export_query_button_, &QPushButton::clicked, this, [this]() { handleExportRequested(false); });
    connect(export_track_button_, &QPushButton::clicked, this, [this]() { handleExportRequested(true); });
    connect(session_combo_box_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { updateSessionActions(); });
    connect(enter_replay_button_, &QPushButton::clicked, this, [this]() {
        if (last_result_.has_value() && !last_result_->frames.isEmpty()) {
            emit replayRequested(last_result_.value());
        }
    });
    connect(return_live_button_, &QPushButton::clicked, this, &HistoryQueryWidget::returnLiveRequested);
    connect(play_button_, &QPushButton::clicked, this, &HistoryQueryWidget::playRequested);
    connect(pause_button_, &QPushButton::clicked, this, &HistoryQueryWidget::pauseRequested);
    connect(previous_frame_button_, &QPushButton::clicked, this, &HistoryQueryWidget::previousFrameRequested);
    connect(next_frame_button_, &QPushButton::clicked, this, &HistoryQueryWidget::nextFrameRequested);
    connect(playback_timeline_slider_, &QSlider::sliderPressed, this, &HistoryQueryWidget::pauseRequested);
    connect(playback_timeline_slider_, &QSlider::sliderReleased, this, &HistoryQueryWidget::handleTimelineReleased);
    connect(playback_rate_combo_box_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        emit playbackRateRequested(playback_rate_combo_box_->itemData(index).toDouble());
    });
    setAvailable(false);
    updatePlaybackActions();
}

HistoryQuery HistoryQueryWidget::currentQuery() const {
    HistoryQuery query;
    if (time_range_check_box_->isChecked()) {
        query.start_time = start_time_edit_->dateTime().toUTC();
        query.end_time = end_time_edit_->dateTime().toUTC();
    }
    if (session_combo_box_->currentData().isValid()) {
        query.session_id = session_combo_box_->currentData().toLongLong();
    }
    if (!track_id_line_edit_->text().trimmed().isEmpty()) {
        bool converted = false;
        const qint64 track_id = track_id_line_edit_->text().toLongLong(&converted);
        if (converted && track_id > 0) {
            query.track_id = track_id;
        }
    }
    if (target_type_combo_box_->currentData().isValid()) {
        query.target_type = static_cast<TargetType>(target_type_combo_box_->currentData().toInt());
    }
    return query;
}

void HistoryQueryWidget::setSessions(const QVector<HistorySession> &sessions) {
    const QVariant selected_session_id = session_combo_box_->currentData();
    sessions_ = sessions;
    session_combo_box_->clear();
    session_combo_box_->addItem(tr("全部会话"));
    for (auto iterator = sessions.crbegin(); iterator != sessions.crend(); ++iterator) {
        const QString label =
            tr("#%1 · %2 · %3")
                .arg(iterator->id)
                .arg(iterator->started_at.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                     sessionStateText(iterator->state));
        session_combo_box_->addItem(label, iterator->id);
    }
    if (selected_session_id.isValid()) {
        const int selected_index = session_combo_box_->findData(selected_session_id);
        if (selected_index >= 0) {
            session_combo_box_->setCurrentIndex(selected_index);
        }
    }
    updateSessionActions();
}

void HistoryQueryWidget::setDatabaseSizeBytes(qint64 size_bytes) {
    database_size_label_->setText(tr("数据库占用：%1").arg(formatByteSize(qMax<qint64>(0, size_bytes))));
}

void HistoryQueryWidget::setAvailable(bool available) {
    available_ = available;
    query_button_->setEnabled(available);
    refresh_button_->setEnabled(available);
    const bool has_frames = available && last_result_.has_value() && !last_result_->frames.isEmpty();
    const bool has_tracks = has_frames && last_result_->targetCount() > 0;
    export_query_button_->setEnabled(has_frames);
    export_track_combo_box_->setEnabled(has_tracks);
    export_track_button_->setEnabled(has_tracks);
    updateSessionActions();
    updatePlaybackActions();
}

void HistoryQueryWidget::applyQueryResult(const HistoryQueryResult &result) {
    last_result_ = result;
    result_label_->setText(tr("查询结果：%1 帧，%2 个目标点").arg(result.frames.size()).arg(result.targetCount()));

    const QVariant selected_track_id = export_track_combo_box_->currentData();
    QSet<qint64> track_ids;
    for (const HistoryFrameRecord &frame : result.frames) {
        for (const TrackData &track : frame.tracks) {
            track_ids.insert(track.track_id);
        }
    }
    QList<qint64> sorted_track_ids = track_ids.values();
    std::sort(sorted_track_ids.begin(), sorted_track_ids.end());
    export_track_combo_box_->clear();
    for (const qint64 track_id : sorted_track_ids) {
        export_track_combo_box_->addItem(QString::number(track_id), track_id);
    }
    const int previous_index = export_track_combo_box_->findData(selected_track_id);
    if (previous_index >= 0) {
        export_track_combo_box_->setCurrentIndex(previous_index);
    }

    const bool has_frames = available_ && !result.frames.isEmpty();
    const bool has_tracks = has_frames && result.targetCount() > 0;
    export_query_button_->setEnabled(has_frames);
    export_track_combo_box_->setEnabled(has_tracks);
    export_track_button_->setEnabled(has_tracks);
    playback_frame_index_ = -1;
    playback_frame_count_ = result.frames.size();
    if (!result.frames.isEmpty()) {
        playback_position_label_->setText(tr("-- / %1").arg(result.frames.size()));
    }
    updatePlaybackActions();
    showStatus(tr("查询完成"), false);
}

void HistoryQueryWidget::setReplayMode(bool replay_mode) {
    replay_mode_ = replay_mode;
    if (!replay_mode_) {
        playing_ = false;
        playback_frame_index_ = -1;
        playback_mode_label_->setText(tr("实时模式"));
        playback_mode_label_->setStyleSheet(QStringLiteral("QLabel { color: #208a4b; font-weight: 700; }"));
    } else {
        playback_mode_label_->setText(tr("历史回放模式"));
        playback_mode_label_->setStyleSheet(QStringLiteral("QLabel { color: #d35400; font-weight: 700; }"));
    }
    updatePlaybackActions();
}

void HistoryQueryWidget::setPlaying(bool playing) {
    playing_ = replay_mode_ && playing;
    updatePlaybackActions();
}

void HistoryQueryWidget::setPlaybackPosition(int frame_index, int frame_count, const QDateTime &frame_time) {
    playback_frame_index_ = frame_index;
    playback_frame_count_ = frame_count;
    if (last_result_.has_value() && !last_result_->frames.isEmpty()) {
        const QDateTime start_time = last_result_->frames.constFirst().frame_time;
        const QDateTime end_time = last_result_->frames.constLast().frame_time;
        const qint64 duration_ms = start_time.msecsTo(end_time);
        const int slider_position =
            duration_ms > 0
                ? static_cast<int>(qBound<qint64>(0LL, start_time.msecsTo(frame_time) * kTimelineSteps / duration_ms,
                                                 static_cast<qint64>(kTimelineSteps)))
                : 0;
        const QSignalBlocker blocker(playback_timeline_slider_);
        playback_timeline_slider_->setValue(slider_position);
    }
    playback_position_label_->setText(
        tr("%1 / %2 · %3")
            .arg(frame_index + 1)
            .arg(frame_count)
            .arg(frame_time.toLocalTime().toString(QStringLiteral("HH:mm:ss"))));
    updatePlaybackActions();
}

void HistoryQueryWidget::showDataGap(qint64 gap_ms) {
    showStatus(tr("数据中断 %1 秒，已跳到下一帧").arg(static_cast<double>(gap_ms) / 1'000.0, 0, 'f', 1), true);
}

void HistoryQueryWidget::showExportCompleted(const QString &output_path, int record_count) {
    showStatus(tr("已导出 %1 条记录：%2").arg(record_count).arg(output_path), false);
}

void HistoryQueryWidget::showStatus(const QString &detail, bool error) {
    status_label_->setText(detail);
    const QString color = error ? QStringLiteral("#c0392b") : QStringLiteral("#208a4b");
    status_label_->setStyleSheet(QStringLiteral("QLabel { color: %1; font-weight: 600; }").arg(color));
}

void HistoryQueryWidget::handleQueryRequested() {
    const HistoryQuery query = currentQuery();
    if (!track_id_line_edit_->text().trimmed().isEmpty() && !query.track_id.has_value()) {
        showStatus(tr("航迹 ID 必须是有效的正整数"), true);
        return;
    }
    if (query.start_time.has_value() && query.end_time.has_value() &&
        query.start_time.value() > query.end_time.value()) {
        showStatus(tr("开始时间不能晚于结束时间"), true);
        return;
    }
    emit queryRequested(query);
    showStatus(tr("正在查询…"), false);
}

void HistoryQueryWidget::handleExportRequested(bool selected_track_only) {
    if (!last_result_.has_value()) {
        showStatus(tr("请先执行查询"), true);
        return;
    }
    const QString output_path =
        QFileDialog::getSaveFileName(this, tr("导出历史 CSV"), QStringLiteral("history.csv"), tr("CSV 文件 (*.csv)"));
    if (output_path.isEmpty()) {
        return;
    }

    HistoryExportRequest request;
    request.query = last_result_->query;
    request.output_path = output_path;
    if (selected_track_only) {
        if (!export_track_combo_box_->currentData().isValid()) {
            showStatus(tr("没有可导出的航迹"), true);
            return;
        }
        request.selected_track_id = export_track_combo_box_->currentData().toLongLong();
    }
    emit exportRequested(request);
    showStatus(tr("正在导出…"), false);
}

void HistoryQueryWidget::handleDeleteSessionRequested() {
    if (!session_combo_box_->currentData().isValid()) {
        showStatus(tr("请选择要删除的历史会话"), true);
        return;
    }
    const qint64 session_id = session_combo_box_->currentData().toLongLong();
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this, tr("确认删除历史会话"), tr("确定删除历史会话 #%1 及其全部采样帧吗？此操作不可撤销。").arg(session_id),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (choice == QMessageBox::Yes) {
        emit deleteSessionRequested(session_id);
        showStatus(tr("正在删除会话 #%1…").arg(session_id), false);
    }
}

void HistoryQueryWidget::handleDeleteAllSessionsRequested() {
    const QMessageBox::StandardButton choice =
        QMessageBox::question(this, tr("确认删除所有历史会话"),
                              tr("确定删除全部 %1 个历史会话及其全部采样帧吗？此操作不可撤销。").arg(sessions_.size()),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (choice == QMessageBox::Yes) {
        emit deleteAllSessionsRequested();
        showStatus(tr("正在删除所有历史会话…"), false);
    }
}

void HistoryQueryWidget::updateSessionActions() {
    bool selected_session_is_deletable = false;
    bool has_active_session = false;
    const QVariant selected_session_id = session_combo_box_->currentData();
    for (const HistorySession &session : sessions_) {
        if (selected_session_id.isValid() && session.id == selected_session_id.toLongLong()) {
            selected_session_is_deletable = session.state != HistorySessionState::kActive;
        }
        has_active_session = has_active_session || session.state == HistorySessionState::kActive;
    }
    delete_session_button_->setEnabled(available_ && selected_session_is_deletable);
    delete_all_sessions_button_->setEnabled(available_ && !sessions_.isEmpty() && !has_active_session);
    delete_all_sessions_button_->setToolTip(has_active_session ? tr("请先停止 UDP 监听再删除所有会话") : QString());
}

void HistoryQueryWidget::updatePlaybackActions() {
    const bool has_frames = available_ && last_result_.has_value() && !last_result_->frames.isEmpty();
    query_button_->setEnabled(available_ && !replay_mode_);
    enter_replay_button_->setEnabled(has_frames && !replay_mode_);
    return_live_button_->setEnabled(replay_mode_);
    play_button_->setEnabled(replay_mode_ && !playing_ && playback_frame_index_ >= 0 &&
                             playback_frame_index_ < playback_frame_count_ - 1);
    pause_button_->setEnabled(replay_mode_ && playing_);
    previous_frame_button_->setEnabled(replay_mode_ && playback_frame_index_ > 0);
    next_frame_button_->setEnabled(replay_mode_ && playback_frame_index_ >= 0 &&
                                    playback_frame_index_ < playback_frame_count_ - 1);
    playback_timeline_slider_->setEnabled(replay_mode_ && playback_frame_count_ > 1);
    playback_rate_combo_box_->setEnabled(replay_mode_);
}

void HistoryQueryWidget::handleTimelineReleased() {
    if (!replay_mode_ || !last_result_.has_value() || last_result_->frames.isEmpty()) {
        return;
    }

    const QDateTime start_time = last_result_->frames.constFirst().frame_time;
    const QDateTime end_time = last_result_->frames.constLast().frame_time;
    const qint64 duration_ms = start_time.msecsTo(end_time);
    const qint64 selected_offset_ms =
        duration_ms * playback_timeline_slider_->value() / playback_timeline_slider_->maximum();
    emit seekRequested(start_time.addMSecs(selected_offset_ms));
}

} // namespace utms
