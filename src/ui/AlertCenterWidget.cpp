#include "ui/AlertCenterWidget.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>

namespace utms {

AlertCenterWidget::AlertCenterWidget(QWidget *parent)
    : QWidget(parent),
      time_range_check_box_(new QCheckBox(tr("限定时间范围"), this)),
      start_time_edit_(new QDateTimeEdit(this)),
      end_time_edit_(new QDateTimeEdit(this)),
      severity_combo_box_(new QComboBox(this)),
      rule_combo_box_(new QComboBox(this)),
      geofence_combo_box_(new QComboBox(this)),
      track_id_line_edit_(new QLineEdit(this)),
      target_type_combo_box_(new QComboBox(this)),
      acknowledgement_combo_box_(new QComboBox(this)),
      table_widget_(new QTableWidget(this)),
      handling_note_line_edit_(new QLineEdit(this)),
      query_button_(new QPushButton(tr("查询"), this)),
      export_button_(new QPushButton(tr("导出 CSV"), this)),
      acknowledge_button_(new QPushButton(tr("确认选中"), this)),
      acknowledge_all_button_(new QPushButton(tr("全部确认"), this)),
      unacknowledged_count_label_(new QLabel(tr("未确认：0"), this)),
      status_label_(new QLabel(tr("告警中心初始化中"), this)) {
    table_widget_->setObjectName(QStringLiteral("alertTableWidget"));
    handling_note_line_edit_->setObjectName(QStringLiteral("alertHandlingNoteLineEdit"));
    acknowledge_button_->setObjectName(QStringLiteral("acknowledgeAlertButton"));
    acknowledge_all_button_->setObjectName(QStringLiteral("acknowledgeAllAlertsButton"));
    time_range_check_box_->setChecked(false);
    const QDateTime now = QDateTime::currentDateTime();
    start_time_edit_->setDateTime(now.addDays(-1));
    end_time_edit_->setDateTime(now);
    for (QDateTimeEdit *edit : {start_time_edit_, end_time_edit_}) {
        edit->setCalendarPopup(true);
        edit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        edit->setEnabled(false);
    }

    severity_combo_box_->addItem(tr("全部等级"));
    severity_combo_box_->addItem(tr("提示"), static_cast<int>(AlertSeverity::kInfo));
    severity_combo_box_->addItem(tr("警告"), static_cast<int>(AlertSeverity::kWarning));
    severity_combo_box_->addItem(tr("严重"), static_cast<int>(AlertSeverity::kSevere));
    rule_combo_box_->addItem(tr("全部规则"));
    geofence_combo_box_->addItem(tr("全部围栏"));
    track_id_line_edit_->setPlaceholderText(tr("全部航迹"));
    track_id_line_edit_->setValidator(
        new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[1-9][0-9]{0,18}")), track_id_line_edit_));
    target_type_combo_box_->addItem(tr("全部类别"));
    for (TargetType type : kTargetTypes) {
        target_type_combo_box_->addItem(targetTypeDisplayName(type), static_cast<int>(type));
    }
    acknowledgement_combo_box_->addItem(tr("全部状态"));
    acknowledgement_combo_box_->addItem(tr("未确认"), false);
    acknowledgement_combo_box_->addItem(tr("已确认"), true);

    auto *query_group = new QGroupBox(tr("告警查询"), this);
    auto *query_layout = new QGridLayout(query_group);
    query_layout->addWidget(time_range_check_box_, 0, 0, 1, 2);
    query_layout->addWidget(new QLabel(tr("开始"), query_group), 1, 0);
    query_layout->addWidget(start_time_edit_, 1, 1);
    query_layout->addWidget(new QLabel(tr("结束"), query_group), 1, 2);
    query_layout->addWidget(end_time_edit_, 1, 3);
    query_layout->addWidget(new QLabel(tr("等级"), query_group), 2, 0);
    query_layout->addWidget(severity_combo_box_, 2, 1);
    query_layout->addWidget(new QLabel(tr("规则"), query_group), 2, 2);
    query_layout->addWidget(rule_combo_box_, 2, 3);
    query_layout->addWidget(new QLabel(tr("围栏"), query_group), 3, 0);
    query_layout->addWidget(geofence_combo_box_, 3, 1);
    query_layout->addWidget(new QLabel(tr("航迹 ID"), query_group), 3, 2);
    query_layout->addWidget(track_id_line_edit_, 3, 3);
    query_layout->addWidget(new QLabel(tr("类别"), query_group), 4, 0);
    query_layout->addWidget(target_type_combo_box_, 4, 1);
    query_layout->addWidget(new QLabel(tr("确认状态"), query_group), 4, 2);
    query_layout->addWidget(acknowledgement_combo_box_, 4, 3);
    query_layout->setColumnStretch(1, 1);
    query_layout->setColumnStretch(3, 1);

    table_widget_->setColumnCount(8);
    table_widget_->setHorizontalHeaderLabels(
        {tr("时间"), tr("等级"), tr("规则"), tr("围栏"), tr("航迹 ID"), tr("类别"), tr("确认状态"), tr("描述")});
    table_widget_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_widget_->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Stretch);
    table_widget_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_widget_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_widget_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_widget_->verticalHeader()->setVisible(false);

    auto *action_layout = new QGridLayout();
    auto *rule_management_button = new QPushButton(tr("规则管理"), this);
    auto *geofence_management_button = new QPushButton(tr("围栏管理"), this);
    handling_note_line_edit_->setPlaceholderText(tr("处理备注（可选）"));
    action_layout->addWidget(unacknowledged_count_label_, 0, 0);
    action_layout->addWidget(handling_note_line_edit_, 0, 1, 1, 3);
    action_layout->addWidget(acknowledge_button_, 0, 4);
    action_layout->addWidget(acknowledge_all_button_, 0, 5);
    action_layout->addWidget(query_button_, 1, 0);
    action_layout->addWidget(export_button_, 1, 1);
    action_layout->addWidget(rule_management_button, 1, 2);
    action_layout->addWidget(geofence_management_button, 1, 3);
    action_layout->setColumnStretch(3, 1);

    status_label_->setWordWrap(true);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(query_group);
    layout->addLayout(action_layout);
    layout->addWidget(table_widget_, 1);
    layout->addWidget(status_label_);

    connect(time_range_check_box_, &QCheckBox::toggled, start_time_edit_, &QWidget::setEnabled);
    connect(time_range_check_box_, &QCheckBox::toggled, end_time_edit_, &QWidget::setEnabled);
    connect(query_button_, &QPushButton::clicked, this, &AlertCenterWidget::refreshQuery);
    connect(export_button_, &QPushButton::clicked, this, &AlertCenterWidget::exportCurrentQuery);
    connect(acknowledge_button_, &QPushButton::clicked, this, &AlertCenterWidget::acknowledgeSelected);
    connect(acknowledge_all_button_, &QPushButton::clicked, this, &AlertCenterWidget::acknowledgeAll);
    connect(rule_management_button, &QPushButton::clicked, this, &AlertCenterWidget::ruleManagementRequested);
    connect(geofence_management_button, &QPushButton::clicked, this, &AlertCenterWidget::geofenceManagementRequested);
    connect(table_widget_, &QTableWidget::itemSelectionChanged, this, [this]() {
        updateActions();
        const std::optional<TargetAlert> alert = selectedAlert();
        if (alert.has_value()) {
            emit alertSelected(alert.value());
        }
    });
    setAvailable(false);
}

AlertQuery AlertCenterWidget::currentQuery() const {
    AlertQuery query;
    if (time_range_check_box_->isChecked()) {
        query.start_time = start_time_edit_->dateTime().toUTC();
        query.end_time = end_time_edit_->dateTime().toUTC();
    }
    if (severity_combo_box_->currentData().isValid()) {
        query.severity = static_cast<AlertSeverity>(severity_combo_box_->currentData().toInt());
    }
    if (rule_combo_box_->currentData().isValid()) {
        query.rule_id = rule_combo_box_->currentData().toLongLong();
    }
    if (geofence_combo_box_->currentData().isValid()) {
        query.geofence_id = geofence_combo_box_->currentData().toLongLong();
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
    if (acknowledgement_combo_box_->currentData().isValid()) {
        query.acknowledged = acknowledgement_combo_box_->currentData().toBool();
    }
    return query;
}

void AlertCenterWidget::setRules(const QVector<AlertRule> &rules) {
    const QVariant selected_rule_id = rule_combo_box_->currentData();
    rules_ = rules;
    rule_combo_box_->clear();
    rule_combo_box_->addItem(tr("全部规则"));
    for (const AlertRule &rule : rules_) {
        rule_combo_box_->addItem(rule.name, rule.id);
    }
    const int selected_index = rule_combo_box_->findData(selected_rule_id);
    if (selected_index >= 0) {
        rule_combo_box_->setCurrentIndex(selected_index);
    }
}

void AlertCenterWidget::setGeofences(const QVector<Geofence> &geofences) {
    const QVariant selected_geofence_id = geofence_combo_box_->currentData();
    geofences_ = geofences;
    geofence_combo_box_->clear();
    geofence_combo_box_->addItem(tr("全部围栏"));
    for (const Geofence &geofence : geofences_) {
        geofence_combo_box_->addItem(geofence.name, geofence.id);
    }
    const int selected_index = geofence_combo_box_->findData(selected_geofence_id);
    if (selected_index >= 0) {
        geofence_combo_box_->setCurrentIndex(selected_index);
    }
}

void AlertCenterWidget::setAvailable(bool available) {
    available_ = available;
    updateActions();
}

void AlertCenterWidget::applyQueryResult(const AlertQueryResult &result) {
    last_result_ = result;
    alerts_ = result.alerts;
    for (const TargetAlert &alert : alerts_) {
        if (rule_combo_box_->findData(alert.rule_id) < 0) {
            rule_combo_box_->addItem(tr("%1（历史）").arg(alert.rule_name), alert.rule_id);
        }
        if (geofence_combo_box_->findData(alert.geofence_id) < 0) {
            geofence_combo_box_->addItem(tr("%1（历史）").arg(alert.geofence_name), alert.geofence_id);
        }
    }
    table_widget_->setRowCount(alerts_.size());
    for (qsizetype row = 0; row < alerts_.size(); ++row) {
        const TargetAlert &alert = alerts_.at(row);
        auto *time_item =
            new QTableWidgetItem(alert.occurred_at.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        time_item->setData(Qt::UserRole, alert.id);
        table_widget_->setItem(row, 0, time_item);
        table_widget_->setItem(row, 1, new QTableWidgetItem(alertSeverityDisplayName(alert.severity)));
        table_widget_->setItem(row, 2, new QTableWidgetItem(alert.rule_name));
        table_widget_->setItem(row, 3, new QTableWidgetItem(alert.geofence_name));
        table_widget_->setItem(row, 4, new QTableWidgetItem(QString::number(alert.track_id)));
        table_widget_->setItem(row, 5, new QTableWidgetItem(targetTypeDisplayName(alert.target_type)));
        table_widget_->setItem(row, 6, new QTableWidgetItem(alert.acknowledged ? tr("已确认") : tr("未确认")));
        table_widget_->setItem(row, 7, new QTableWidgetItem(alert.description));
    }
    unacknowledged_count_label_->setText(tr("未确认：%1").arg(result.unacknowledged_count));
    showStatus(tr("查询完成，共 %1 条告警").arg(alerts_.size()), false);
    updateActions();
}

void AlertCenterWidget::showExportCompleted(const QString &output_path, int record_count) {
    showStatus(tr("已导出 %1 条告警：%2").arg(record_count).arg(output_path), false);
}

void AlertCenterWidget::showStatus(const QString &message, bool error) {
    status_label_->setText(message);
    status_label_->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(error ? QStringLiteral("#c0392b") : QStringLiteral("#208a4b")));
}

void AlertCenterWidget::refreshQuery() {
    const AlertQuery query = currentQuery();
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
    showStatus(tr("正在查询告警…"), false);
}

void AlertCenterWidget::acknowledgeSelected() {
    AlertAcknowledgementRequest request;
    request.alert_ids = selectedAlertIds();
    request.handling_note = handling_note_line_edit_->text().trimmed();
    if (!request.alert_ids.isEmpty()) {
        emit acknowledgeRequested(request);
    }
}

void AlertCenterWidget::acknowledgeAll() {
    AlertAcknowledgementRequest request;
    for (const TargetAlert &alert : alerts_) {
        if (!alert.acknowledged) {
            request.alert_ids.append(alert.id);
        }
    }
    request.handling_note = handling_note_line_edit_->text().trimmed();
    if (!request.alert_ids.isEmpty()) {
        emit acknowledgeRequested(request);
    }
}

void AlertCenterWidget::exportCurrentQuery() {
    if (!last_result_.has_value()) {
        return;
    }
    const QString output_path =
        QFileDialog::getSaveFileName(this, tr("导出告警 CSV"), QStringLiteral("alerts.csv"), tr("CSV 文件 (*.csv)"));
    if (output_path.isEmpty()) {
        return;
    }
    emit exportRequested({last_result_->query, output_path});
    showStatus(tr("正在导出告警…"), false);
}

void AlertCenterWidget::updateActions() {
    query_button_->setEnabled(available_);
    export_button_->setEnabled(available_ && last_result_.has_value() && !alerts_.isEmpty());
    const QVector<qint64> selected_ids = selectedAlertIds();
    acknowledge_button_->setEnabled(available_ && !selected_ids.isEmpty());
    const bool has_unacknowledged =
        std::any_of(alerts_.cbegin(), alerts_.cend(), [](const TargetAlert &alert) { return !alert.acknowledged; });
    acknowledge_all_button_->setEnabled(available_ && has_unacknowledged);
}

QVector<qint64> AlertCenterWidget::selectedAlertIds() const {
    QVector<qint64> alert_ids;
    QSet<qint64> unique_ids;
    const QModelIndexList selected_rows = table_widget_->selectionModel()->selectedRows();
    for (const QModelIndex &index : selected_rows) {
        const qint64 alert_id = table_widget_->item(index.row(), 0)->data(Qt::UserRole).toLongLong();
        const auto alert = std::find_if(alerts_.cbegin(), alerts_.cend(),
                                        [alert_id](const TargetAlert &candidate) { return candidate.id == alert_id; });
        if (alert != alerts_.cend() && !alert->acknowledged && !unique_ids.contains(alert_id)) {
            unique_ids.insert(alert_id);
            alert_ids.append(alert_id);
        }
    }
    std::sort(alert_ids.begin(), alert_ids.end());
    return alert_ids;
}

std::optional<TargetAlert> AlertCenterWidget::selectedAlert() const {
    const int row = table_widget_->currentRow();
    if (row < 0 || table_widget_->item(row, 0) == nullptr) {
        return std::nullopt;
    }
    const qint64 alert_id = table_widget_->item(row, 0)->data(Qt::UserRole).toLongLong();
    const auto alert = std::find_if(alerts_.cbegin(), alerts_.cend(),
                                    [alert_id](const TargetAlert &candidate) { return candidate.id == alert_id; });
    return alert == alerts_.cend() ? std::nullopt : std::optional<TargetAlert>(*alert);
}

} // namespace utms
