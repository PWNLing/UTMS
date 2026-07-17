#pragma once

#include <optional>

#include <QWidget>

#include "core/GeofenceTypes.h"

class QLabel;
class QPushButton;
class QTableWidget;
class QTimer;

namespace utms {

class GeofenceManagerWidget : public QWidget {
    Q_OBJECT

public:
    explicit GeofenceManagerWidget(QWidget *parent = nullptr);

    void setGeofences(const QVector<Geofence> &geofences);
    void applyMapEditedGeofence(const Geofence &geofence);
    void setAvailable(bool available);
    void setEditingGeofenceId(std::optional<qint64> geofence_id);
    void showStatus(const QString &message, bool error);

signals:
    void createRequested(const utms::Geofence &geofence);
    void updateRequested(const utms::Geofence &geofence);
    void enabledChangeRequested(qint64 geofence_id, bool enabled);
    void visibilityChangeRequested(qint64 geofence_id, bool visible);
    void deleteRequested(qint64 geofence_id);
    void locateRequested(qint64 geofence_id);
    void mapEditRequested(std::optional<qint64> geofence_id);

private:
    std::optional<Geofence> selectedGeofence() const;
    void createGeofence();
    void editSelectedGeofence();
    void locateSelectedGeofence();
    void toggleMapEditing();
    void toggleSelectedEnabled();
    void toggleSelectedVisible();
    void deleteSelectedGeofence();
    void updateActions();

    QTableWidget *table_widget_ = nullptr;
    QPushButton *create_button_ = nullptr;
    QPushButton *edit_button_ = nullptr;
    QPushButton *locate_button_ = nullptr;
    QPushButton *map_edit_button_ = nullptr;
    QPushButton *enabled_button_ = nullptr;
    QPushButton *visible_button_ = nullptr;
    QPushButton *delete_button_ = nullptr;
    QLabel *status_label_ = nullptr;
    QTimer *map_edit_settlement_timer_ = nullptr;
    QVector<Geofence> geofences_;
    bool available_ = false;
    bool map_edit_settling_ = false;
    std::optional<qint64> editing_geofence_id_;
};

} // namespace utms
