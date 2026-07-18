#pragma once

#include <optional>

#include <QWidget>

#include "alert/AlertTypes.h"
#include "core/GeofenceTypes.h"

class QLabel;
class QPushButton;
class QTableWidget;

namespace utms {

class AlertRuleManagerWidget : public QWidget {
    Q_OBJECT

  public:
    explicit AlertRuleManagerWidget(QWidget *parent = nullptr);

    void setGeofences(const QVector<Geofence> &geofences);
    void setRules(const QVector<AlertRule> &rules);
    void setAvailable(bool available);
    void showStatus(const QString &message, bool error);

  signals:
    void createRequested(const utms::AlertRule &rule);
    void updateRequested(const utms::AlertRule &rule);
    void enabledChangeRequested(qint64 rule_id, bool enabled);
    void deleteRequested(qint64 rule_id);

  private:
    std::optional<AlertRule> selectedRule() const;
    void createRule();
    void editSelectedRule();
    void toggleSelectedEnabled();
    void deleteSelectedRule();
    void updateActions();

    QTableWidget *table_widget_ = nullptr;
    QPushButton *create_button_ = nullptr;
    QPushButton *edit_button_ = nullptr;
    QPushButton *enabled_button_ = nullptr;
    QPushButton *delete_button_ = nullptr;
    QLabel *status_label_ = nullptr;
    QVector<Geofence> geofences_;
    QVector<AlertRule> rules_;
    bool available_ = false;
};

} // namespace utms
