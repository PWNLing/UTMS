#include "ui/AlertRuleManagerWidget.h"

#include <algorithm>
#include <array>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace utms {
namespace {

QString targetTypeScopeText(const QVector<TargetType> &target_types) {
    QStringList names;
    for (TargetType type : target_types) {
        names.append(targetTypeDisplayName(type));
    }
    return names.join(QStringLiteral("、"));
}

QString geofenceName(const QVector<Geofence> &geofences, qint64 geofence_id) {
    const auto geofence = std::find_if(geofences.cbegin(), geofences.cend(), [geofence_id](const Geofence &candidate) {
        return candidate.id == geofence_id;
    });
    return geofence == geofences.cend() ? QStringLiteral("#%1").arg(geofence_id) : geofence->name;
}

QString thresholdText(const AlertRule &rule) {
    switch (rule.type) {
    case AlertRuleType::kDwellTimeout:
        return QCoreApplication::translate("AlertRuleManagerWidget", "%1 秒")
            .arg(rule.dwell_threshold_ms / 1'000.0, 0, 'f', 1);
    case AlertRuleType::kGeofenceSpeeding:
        return QCoreApplication::translate("AlertRuleManagerWidget", "%1 m/s").arg(rule.speed_threshold_mps, 0, 'f', 1);
    case AlertRuleType::kStableEntry:
    case AlertRuleType::kStableExit:
        return QStringLiteral("--");
    }
    return QStringLiteral("--");
}

QString confirmationText(const AlertRule &rule) {
    if (rule.type == AlertRuleType::kDwellTimeout) {
        return QCoreApplication::translate("AlertRuleManagerWidget", "不适用");
    }
    return QCoreApplication::translate("AlertRuleManagerWidget", "%1 秒")
        .arg(rule.confirmation_ms / 1'000.0, 0, 'f', 1);
}

class AlertRuleEditDialog : public QDialog {
  public:
    AlertRuleEditDialog(const QVector<Geofence> &geofences, const std::optional<AlertRule> &existing_rule,
                        QWidget *parent)
        : QDialog(parent), existing_id_(existing_rule.has_value() ? existing_rule->id : 0) {
        setWindowTitle(existing_rule.has_value() ? tr("编辑告警规则") : tr("新建告警规则"));
        setMinimumWidth(460);
        auto *layout = new QVBoxLayout(this);
        auto *form_layout = new QFormLayout();

        name_line_edit_ = new QLineEdit(this);
        type_combo_box_ = new QComboBox(this);
        type_combo_box_->addItem(tr("稳定进入"), static_cast<int>(AlertRuleType::kStableEntry));
        type_combo_box_->addItem(tr("稳定离开"), static_cast<int>(AlertRuleType::kStableExit));
        type_combo_box_->addItem(tr("围栏内停留超时"), static_cast<int>(AlertRuleType::kDwellTimeout));
        type_combo_box_->addItem(tr("围栏内超速"), static_cast<int>(AlertRuleType::kGeofenceSpeeding));
        geofence_combo_box_ = new QComboBox(this);
        for (const Geofence &geofence : geofences) {
            geofence_combo_box_->addItem(geofence.name, geofence.id);
        }
        severity_combo_box_ = new QComboBox(this);
        severity_combo_box_->addItem(tr("提示"), static_cast<int>(AlertSeverity::kInfo));
        severity_combo_box_->addItem(tr("警告"), static_cast<int>(AlertSeverity::kWarning));
        severity_combo_box_->addItem(tr("严重"), static_cast<int>(AlertSeverity::kSevere));
        threshold_label_ = new QLabel(tr("阈值"), this);
        threshold_spin_box_ = new QDoubleSpinBox(this);
        confirmation_spin_box_ = new QDoubleSpinBox(this);
        confirmation_spin_box_->setRange(0.0, 60.0);
        confirmation_spin_box_->setDecimals(1);
        confirmation_spin_box_->setSingleStep(0.5);
        confirmation_spin_box_->setSuffix(tr(" 秒"));
        confirmation_spin_box_->setValue(1.0);
        cooldown_spin_box_ = new QDoubleSpinBox(this);
        cooldown_spin_box_->setRange(0.0, 86'400.0);
        cooldown_spin_box_->setDecimals(1);
        cooldown_spin_box_->setSingleStep(1.0);
        cooldown_spin_box_->setSuffix(tr(" 秒"));
        cooldown_spin_box_->setValue(30.0);
        enabled_check_box_ = new QCheckBox(tr("启用规则"), this);
        enabled_check_box_->setChecked(true);
        note_line_edit_ = new QLineEdit(this);

        auto *scope_widget = new QWidget(this);
        auto *scope_layout = new QGridLayout(scope_widget);
        scope_layout->setContentsMargins(0, 0, 0, 0);
        for (std::size_t index = 0; index < kTargetTypes.size(); ++index) {
            target_type_check_boxes_[index] = new QCheckBox(targetTypeDisplayName(kTargetTypes[index]), scope_widget);
            target_type_check_boxes_[index]->setChecked(true);
            scope_layout->addWidget(target_type_check_boxes_[index], static_cast<int>(index / 3),
                                    static_cast<int>(index % 3));
        }

        form_layout->addRow(tr("名称"), name_line_edit_);
        form_layout->addRow(tr("类型"), type_combo_box_);
        form_layout->addRow(tr("关联围栏"), geofence_combo_box_);
        form_layout->addRow(tr("目标类别"), scope_widget);
        form_layout->addRow(tr("告警等级"), severity_combo_box_);
        form_layout->addRow(threshold_label_, threshold_spin_box_);
        form_layout->addRow(tr("确认时间"), confirmation_spin_box_);
        form_layout->addRow(tr("冷却时间"), cooldown_spin_box_);
        form_layout->addRow(enabled_check_box_);
        form_layout->addRow(tr("备注"), note_line_edit_);

        error_label_ = new QLabel(this);
        error_label_->setWordWrap(true);
        error_label_->setStyleSheet(QStringLiteral("QLabel { color: #c0392b; }"));
        error_label_->hide();
        auto *button_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(button_box, &QDialogButtonBox::accepted, this, &AlertRuleEditDialog::accept);
        connect(button_box, &QDialogButtonBox::rejected, this, &AlertRuleEditDialog::reject);
        connect(type_combo_box_, &QComboBox::currentIndexChanged, this, [this](int) { updateTypeControls(); });
        layout->addLayout(form_layout);
        layout->addWidget(error_label_);
        layout->addWidget(button_box);

        if (existing_rule.has_value()) {
            applyRule(existing_rule.value());
        } else {
            name_line_edit_->setText(tr("新建进入规则"));
            updateTypeControls();
        }
    }

    AlertRule rule() const { return rule_; }

  protected:
    void accept() override {
        AlertRule candidate;
        candidate.id = existing_id_;
        candidate.name = name_line_edit_->text().trimmed();
        candidate.type = static_cast<AlertRuleType>(type_combo_box_->currentData().toInt());
        candidate.geofence_id = geofence_combo_box_->currentData().toLongLong();
        for (std::size_t index = 0; index < kTargetTypes.size(); ++index) {
            if (target_type_check_boxes_[index]->isChecked()) {
                candidate.target_types.append(kTargetTypes[index]);
            }
        }
        candidate.severity = static_cast<AlertSeverity>(severity_combo_box_->currentData().toInt());
        if (candidate.type == AlertRuleType::kDwellTimeout) {
            candidate.dwell_threshold_ms = qRound(threshold_spin_box_->value() * 1'000.0);
            candidate.confirmation_ms = 0;
        } else {
            candidate.confirmation_ms = qRound(confirmation_spin_box_->value() * 1'000.0);
        }
        if (candidate.type == AlertRuleType::kGeofenceSpeeding) {
            candidate.speed_threshold_mps = threshold_spin_box_->value();
        }
        candidate.cooldown_ms = qRound(cooldown_spin_box_->value() * 1'000.0);
        candidate.enabled = enabled_check_box_->isChecked();
        candidate.note = note_line_edit_->text().trimmed();

        const QString validation_error = validateAlertRule(candidate);
        if (!validation_error.isEmpty()) {
            error_label_->setText(validation_error);
            error_label_->show();
            return;
        }
        rule_ = candidate;
        QDialog::accept();
    }

  private:
    void applyRule(const AlertRule &rule) {
        name_line_edit_->setText(rule.name);
        type_combo_box_->setCurrentIndex(type_combo_box_->findData(static_cast<int>(rule.type)));
        geofence_combo_box_->setCurrentIndex(geofence_combo_box_->findData(rule.geofence_id));
        severity_combo_box_->setCurrentIndex(severity_combo_box_->findData(static_cast<int>(rule.severity)));
        updateTypeControls();
        if (rule.type == AlertRuleType::kDwellTimeout) {
            threshold_spin_box_->setValue(rule.dwell_threshold_ms / 1'000.0);
        } else if (rule.type == AlertRuleType::kGeofenceSpeeding) {
            threshold_spin_box_->setValue(rule.speed_threshold_mps);
        }
        confirmation_spin_box_->setValue(rule.confirmation_ms / 1'000.0);
        cooldown_spin_box_->setValue(rule.cooldown_ms / 1'000.0);
        enabled_check_box_->setChecked(rule.enabled);
        note_line_edit_->setText(rule.note);
        for (std::size_t index = 0; index < kTargetTypes.size(); ++index) {
            target_type_check_boxes_[index]->setChecked(std::find(rule.target_types.cbegin(), rule.target_types.cend(),
                                                                  kTargetTypes[index]) != rule.target_types.cend());
        }
    }

    void updateTypeControls() {
        const AlertRuleType type = static_cast<AlertRuleType>(type_combo_box_->currentData().toInt());
        const bool has_threshold = type == AlertRuleType::kDwellTimeout || type == AlertRuleType::kGeofenceSpeeding;
        threshold_label_->setVisible(has_threshold);
        threshold_spin_box_->setVisible(has_threshold);
        confirmation_spin_box_->setEnabled(type != AlertRuleType::kDwellTimeout);
        confirmation_spin_box_->setToolTip(
            type == AlertRuleType::kDwellTimeout ? tr("停留规则达到停留阈值后直接触发，不叠加确认时间") : QString());
        if (type == AlertRuleType::kDwellTimeout) {
            threshold_label_->setText(tr("停留阈值"));
            threshold_spin_box_->setRange(5.0, 86'400.0);
            threshold_spin_box_->setDecimals(1);
            threshold_spin_box_->setSingleStep(1.0);
            threshold_spin_box_->setSuffix(tr(" 秒"));
            threshold_spin_box_->setValue(5.0);
        } else if (type == AlertRuleType::kGeofenceSpeeding) {
            threshold_label_->setText(tr("超速阈值"));
            threshold_spin_box_->setRange(0.1, 1'000'000.0);
            threshold_spin_box_->setDecimals(1);
            threshold_spin_box_->setSingleStep(0.5);
            threshold_spin_box_->setSuffix(tr(" m/s"));
            threshold_spin_box_->setValue(10.0);
        }
    }

    qint64 existing_id_ = 0;
    AlertRule rule_;
    QLineEdit *name_line_edit_ = nullptr;
    QComboBox *type_combo_box_ = nullptr;
    QComboBox *geofence_combo_box_ = nullptr;
    std::array<QCheckBox *, 5> target_type_check_boxes_{};
    QComboBox *severity_combo_box_ = nullptr;
    QLabel *threshold_label_ = nullptr;
    QDoubleSpinBox *threshold_spin_box_ = nullptr;
    QDoubleSpinBox *confirmation_spin_box_ = nullptr;
    QDoubleSpinBox *cooldown_spin_box_ = nullptr;
    QCheckBox *enabled_check_box_ = nullptr;
    QLineEdit *note_line_edit_ = nullptr;
    QLabel *error_label_ = nullptr;
};

} // namespace

AlertRuleManagerWidget::AlertRuleManagerWidget(QWidget *parent)
    : QWidget(parent), table_widget_(new QTableWidget(this)), create_button_(new QPushButton(tr("新建"), this)),
      edit_button_(new QPushButton(tr("编辑"), this)), enabled_button_(new QPushButton(this)),
      delete_button_(new QPushButton(tr("删除"), this)), status_label_(new QLabel(tr("告警规则初始化中"), this)) {
    auto *layout = new QVBoxLayout(this);
    auto *button_layout = new QHBoxLayout();
    button_layout->addWidget(create_button_);
    button_layout->addWidget(edit_button_);
    button_layout->addWidget(enabled_button_);
    button_layout->addWidget(delete_button_);
    button_layout->addStretch();

    table_widget_->setColumnCount(9);
    table_widget_->setHorizontalHeaderLabels({tr("名称"), tr("类型"), tr("围栏"), tr("类别范围"), tr("等级"),
                                              tr("阈值"), tr("确认"), tr("冷却"), tr("状态")});
    table_widget_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < table_widget_->columnCount(); ++column) {
        table_widget_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    table_widget_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_widget_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_widget_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_widget_->verticalHeader()->setVisible(false);
    status_label_->setWordWrap(true);

    layout->addLayout(button_layout);
    layout->addWidget(table_widget_, 1);
    layout->addWidget(status_label_);

    connect(create_button_, &QPushButton::clicked, this, &AlertRuleManagerWidget::createRule);
    connect(edit_button_, &QPushButton::clicked, this, &AlertRuleManagerWidget::editSelectedRule);
    connect(enabled_button_, &QPushButton::clicked, this, &AlertRuleManagerWidget::toggleSelectedEnabled);
    connect(delete_button_, &QPushButton::clicked, this, &AlertRuleManagerWidget::deleteSelectedRule);
    connect(table_widget_, &QTableWidget::itemSelectionChanged, this, &AlertRuleManagerWidget::updateActions);
    connect(table_widget_, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *) { editSelectedRule(); });
    updateActions();
}

void AlertRuleManagerWidget::setGeofences(const QVector<Geofence> &geofences) {
    geofences_ = geofences;
    setRules(rules_);
}

void AlertRuleManagerWidget::setRules(const QVector<AlertRule> &rules) {
    const std::optional<AlertRule> selected = selectedRule();
    rules_ = rules;
    table_widget_->setRowCount(rules_.size());
    for (qsizetype row = 0; row < rules_.size(); ++row) {
        const AlertRule &rule = rules_.at(row);
        auto *name_item = new QTableWidgetItem(rule.name);
        name_item->setData(Qt::UserRole, rule.id);
        table_widget_->setItem(row, 0, name_item);
        table_widget_->setItem(row, 1, new QTableWidgetItem(alertRuleTypeDisplayName(rule.type)));
        table_widget_->setItem(row, 2, new QTableWidgetItem(geofenceName(geofences_, rule.geofence_id)));
        table_widget_->setItem(row, 3, new QTableWidgetItem(targetTypeScopeText(rule.target_types)));
        table_widget_->setItem(row, 4, new QTableWidgetItem(alertSeverityDisplayName(rule.severity)));
        table_widget_->setItem(row, 5, new QTableWidgetItem(thresholdText(rule)));
        table_widget_->setItem(row, 6, new QTableWidgetItem(confirmationText(rule)));
        table_widget_->setItem(row, 7, new QTableWidgetItem(tr("%1 秒").arg(rule.cooldown_ms / 1'000.0, 0, 'f', 1)));
        table_widget_->setItem(row, 8, new QTableWidgetItem(rule.enabled ? tr("启用") : tr("禁用")));
        if (selected.has_value() && selected->id == rule.id) {
            table_widget_->selectRow(row);
        }
    }
    showStatus(tr("共 %1 条告警规则").arg(rules_.size()), false);
    updateActions();
}

void AlertRuleManagerWidget::setAvailable(bool available) {
    available_ = available;
    updateActions();
}

void AlertRuleManagerWidget::showStatus(const QString &message, bool error) {
    status_label_->setText(message);
    status_label_->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(error ? QStringLiteral("#c0392b") : QStringLiteral("#555555")));
}

std::optional<AlertRule> AlertRuleManagerWidget::selectedRule() const {
    const int row = table_widget_->currentRow();
    if (row < 0 || table_widget_->item(row, 0) == nullptr) {
        return std::nullopt;
    }
    const qint64 rule_id = table_widget_->item(row, 0)->data(Qt::UserRole).toLongLong();
    const auto rule = std::find_if(rules_.cbegin(), rules_.cend(),
                                   [rule_id](const AlertRule &candidate) { return candidate.id == rule_id; });
    return rule == rules_.cend() ? std::nullopt : std::optional<AlertRule>(*rule);
}

void AlertRuleManagerWidget::createRule() {
    AlertRuleEditDialog dialog(geofences_, std::nullopt, this);
    if (dialog.exec() == QDialog::Accepted) {
        emit createRequested(dialog.rule());
    }
}

void AlertRuleManagerWidget::editSelectedRule() {
    const std::optional<AlertRule> selected = selectedRule();
    if (!selected.has_value()) {
        return;
    }
    AlertRuleEditDialog dialog(geofences_, selected, this);
    if (dialog.exec() == QDialog::Accepted) {
        emit updateRequested(dialog.rule());
    }
}

void AlertRuleManagerWidget::toggleSelectedEnabled() {
    const std::optional<AlertRule> selected = selectedRule();
    if (selected.has_value()) {
        emit enabledChangeRequested(selected->id, !selected->enabled);
    }
}

void AlertRuleManagerWidget::deleteSelectedRule() {
    const std::optional<AlertRule> selected = selectedRule();
    if (!selected.has_value()) {
        return;
    }
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this, tr("删除告警规则"), tr("确定删除“%1”吗？已产生的告警不会被删除。").arg(selected->name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (choice == QMessageBox::Yes) {
        emit deleteRequested(selected->id);
    }
}

void AlertRuleManagerWidget::updateActions() {
    const std::optional<AlertRule> selected = selectedRule();
    create_button_->setEnabled(available_ && !geofences_.isEmpty());
    edit_button_->setEnabled(available_ && selected.has_value() && !geofences_.isEmpty());
    enabled_button_->setEnabled(available_ && selected.has_value());
    delete_button_->setEnabled(available_ && selected.has_value());
    enabled_button_->setText(selected.has_value() && selected->enabled ? tr("禁用") : tr("启用"));
}

} // namespace utms
