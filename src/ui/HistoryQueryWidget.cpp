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
#include <QSet>
#include <QVBoxLayout>

namespace utms {
namespace {

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
      export_query_button_(new QPushButton(tr("导出当前查询"), this)), export_track_combo_box_(new QComboBox(this)),
      export_track_button_(new QPushButton(tr("导出选中航迹"), this)),
      database_size_label_(new QLabel(tr("数据库占用：--"), this)), result_label_(new QLabel(tr("尚未查询"), this)),
      status_label_(new QLabel(tr("历史数据库初始化中"), this)) {
    time_range_check_box_->setChecked(true);
    const QDateTime current_time = QDateTime::currentDateTime();
    start_time_edit_->setDateTime(current_time.addDays(-1));
    end_time_edit_->setDateTime(current_time);
    start_time_edit_->setCalendarPopup(true);
    end_time_edit_->setCalendarPopup(true);
    start_time_edit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    end_time_edit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));

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

    auto *query_group = new QGroupBox(tr("查询条件"), this);
    auto *query_layout = new QGridLayout(query_group);
    query_layout->addWidget(time_range_check_box_, 0, 0);
    query_layout->addWidget(new QLabel(tr("开始"), query_group), 0, 1);
    query_layout->addWidget(start_time_edit_, 0, 2);
    query_layout->addWidget(new QLabel(tr("结束"), query_group), 1, 1);
    query_layout->addWidget(end_time_edit_, 1, 2);
    query_layout->addWidget(new QLabel(tr("会话"), query_group), 2, 0);
    query_layout->addWidget(session_combo_box_, 2, 1, 1, 2);
    query_layout->addWidget(new QLabel(tr("航迹 ID"), query_group), 3, 0);
    query_layout->addWidget(track_id_line_edit_, 3, 1);
    query_layout->addWidget(new QLabel(tr("类别"), query_group), 3, 2);
    query_layout->addWidget(target_type_combo_box_, 3, 3);
    query_layout->addWidget(query_button_, 4, 2);
    query_layout->addWidget(refresh_button_, 4, 3);
    query_layout->setColumnStretch(2, 1);

    auto *management_group = new QGroupBox(tr("存储管理"), this);
    auto *management_layout = new QHBoxLayout(management_group);
    management_layout->addWidget(database_size_label_);
    management_layout->addStretch();
    management_layout->addWidget(delete_session_button_);

    auto *export_group = new QGroupBox(tr("CSV 导出"), this);
    auto *export_layout = new QGridLayout(export_group);
    export_layout->addWidget(export_query_button_, 0, 0);
    export_layout->addWidget(new QLabel(tr("航迹"), export_group), 1, 0);
    export_layout->addWidget(export_track_combo_box_, 1, 1);
    export_layout->addWidget(export_track_button_, 1, 2);
    export_layout->setColumnStretch(1, 1);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(query_group);
    layout->addWidget(management_group);
    layout->addWidget(export_group);
    layout->addWidget(result_label_);
    layout->addWidget(status_label_);
    layout->addStretch();

    connect(time_range_check_box_, &QCheckBox::toggled, start_time_edit_, &QWidget::setEnabled);
    connect(time_range_check_box_, &QCheckBox::toggled, end_time_edit_, &QWidget::setEnabled);
    connect(query_button_, &QPushButton::clicked, this, &HistoryQueryWidget::handleQueryRequested);
    connect(refresh_button_, &QPushButton::clicked, this, &HistoryQueryWidget::refreshRequested);
    connect(delete_session_button_, &QPushButton::clicked, this, &HistoryQueryWidget::handleDeleteSessionRequested);
    connect(export_query_button_, &QPushButton::clicked, this, [this]() { handleExportRequested(false); });
    connect(export_track_button_, &QPushButton::clicked, this, [this]() { handleExportRequested(true); });
    connect(session_combo_box_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { updateSessionActions(); });
    setAvailable(false);
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
    showStatus(tr("查询完成"), false);
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

void HistoryQueryWidget::updateSessionActions() {
    bool selected_session_is_deletable = false;
    if (session_combo_box_->currentData().isValid()) {
        const qint64 session_id = session_combo_box_->currentData().toLongLong();
        for (const HistorySession &session : sessions_) {
            if (session.id == session_id) {
                selected_session_is_deletable = session.state != HistorySessionState::kActive;
                break;
            }
        }
    }
    delete_session_button_->setEnabled(available_ && selected_session_is_deletable);
}

} // namespace utms
