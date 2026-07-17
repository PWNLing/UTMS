#pragma once

#include <QWidget>

#include "history/HistoryTypes.h"

class QCheckBox;
class QComboBox;
class QDateTimeEdit;
class QLabel;
class QLineEdit;
class QPushButton;

namespace utms {

class HistoryQueryWidget : public QWidget {
    Q_OBJECT

public:
    explicit HistoryQueryWidget(QWidget *parent = nullptr);

    HistoryQuery currentQuery() const;
    void setSessions(const QVector<HistorySession> &sessions);
    void setDatabaseSizeBytes(qint64 size_bytes);
    void setAvailable(bool available);
    void applyQueryResult(const HistoryQueryResult &result);
    void showExportCompleted(const QString &output_path, int record_count);
    void showStatus(const QString &detail, bool error);

signals:
    void queryRequested(const utms::HistoryQuery &query);
    void exportRequested(const utms::HistoryExportRequest &request);
    void deleteSessionRequested(qint64 session_id);
    void deleteAllSessionsRequested();
    void refreshRequested();

private:
    void handleQueryRequested();
    void handleExportRequested(bool selected_track_only);
    void handleDeleteSessionRequested();
    void handleDeleteAllSessionsRequested();
    void updateSessionActions();

    QCheckBox *time_range_check_box_ = nullptr;
    QDateTimeEdit *start_time_edit_ = nullptr;
    QDateTimeEdit *end_time_edit_ = nullptr;
    QComboBox *session_combo_box_ = nullptr;
    QLineEdit *track_id_line_edit_ = nullptr;
    QComboBox *target_type_combo_box_ = nullptr;
    QPushButton *query_button_ = nullptr;
    QPushButton *refresh_button_ = nullptr;
    QPushButton *delete_session_button_ = nullptr;
    QPushButton *delete_all_sessions_button_ = nullptr;
    QPushButton *export_query_button_ = nullptr;
    QComboBox *export_track_combo_box_ = nullptr;
    QPushButton *export_track_button_ = nullptr;
    QLabel *database_size_label_ = nullptr;
    QLabel *result_label_ = nullptr;
    QLabel *status_label_ = nullptr;
    QVector<HistorySession> sessions_;
    std::optional<HistoryQueryResult> last_result_;
    bool available_ = false;
};

} // namespace utms
