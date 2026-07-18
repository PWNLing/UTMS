#pragma once

#include <optional>

#include <QWidget>

#include "alert/AlertTypes.h"
#include "core/GeofenceTypes.h"

class QCheckBox;
class QComboBox;
class QDateTimeEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace utms {

class AlertCenterWidget : public QWidget {
    Q_OBJECT

public:
    explicit AlertCenterWidget(QWidget *parent = nullptr);

    AlertQuery currentQuery() const;
    void setRules(const QVector<AlertRule> &rules);
    void setGeofences(const QVector<Geofence> &geofences);
    void setAvailable(bool available);
    void applyQueryResult(const AlertQueryResult &result);
    void showExportCompleted(const QString &output_path, int record_count);
    void showStatus(const QString &message, bool error);

signals:
    void queryRequested(const utms::AlertQuery &query);
    void acknowledgeRequested(const utms::AlertAcknowledgementRequest &request);
    void exportRequested(const utms::AlertExportRequest &request);
    void alertSelected(const utms::TargetAlert &alert);
    void ruleManagementRequested();
    void geofenceManagementRequested();

private:
    void refreshQuery();
    void acknowledgeSelected();
    void acknowledgeAll();
    void exportCurrentQuery();
    void updateActions();
    QVector<qint64> selectedAlertIds() const;
    std::optional<TargetAlert> selectedAlert() const;

    QCheckBox *time_range_check_box_ = nullptr;
    QDateTimeEdit *start_time_edit_ = nullptr;
    QDateTimeEdit *end_time_edit_ = nullptr;
    QComboBox *severity_combo_box_ = nullptr;
    QComboBox *rule_combo_box_ = nullptr;
    QComboBox *geofence_combo_box_ = nullptr;
    QLineEdit *track_id_line_edit_ = nullptr;
    QComboBox *target_type_combo_box_ = nullptr;
    QComboBox *acknowledgement_combo_box_ = nullptr;
    QTableWidget *table_widget_ = nullptr;
    QLineEdit *handling_note_line_edit_ = nullptr;
    QPushButton *query_button_ = nullptr;
    QPushButton *export_button_ = nullptr;
    QPushButton *acknowledge_button_ = nullptr;
    QPushButton *acknowledge_all_button_ = nullptr;
    QLabel *unacknowledged_count_label_ = nullptr;
    QLabel *status_label_ = nullptr;
    QVector<AlertRule> rules_;
    QVector<Geofence> geofences_;
    QVector<TargetAlert> alerts_;
    std::optional<AlertQueryResult> last_result_;
    bool available_ = false;
};

} // namespace utms
