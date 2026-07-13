#pragma once

#include <optional>

#include <QWidget>

#include "core/RadarTypes.h"

class QComboBox;
class QLabel;
class QModelIndex;
class QTableView;

namespace utms
{

class TrackFilterProxyModel;
class TrackTableModel;

class TrackTableWidget : public QWidget
{
    Q_OBJECT

    public:
    explicit TrackTableWidget(QWidget *parent = nullptr);

    void replaceTracks(const QVector<TrackData> &tracks);
    void setTargetTypeFilter(std::optional<TargetType> target_type);
    std::optional<TargetType> targetTypeFilter() const;
    std::optional<qint64> selectedTrackId() const;
    bool selectTrackById(qint64 track_id);

    signals:
    void targetSelected(qint64 track_id);
    void targetSelectionCleared();

    private:
    QModelIndex proxyIndexForTrack(qint64 track_id) const;
    bool sourceContainsTrack(qint64 track_id) const;
    void restoreVisibleSelection();
    void handleCurrentRowChanged(const QModelIndex &current);
    void handleTableClicked(const QModelIndex &index);

    TrackTableModel *source_model_ = nullptr;
    TrackFilterProxyModel *proxy_model_ = nullptr;
    QComboBox *type_filter_combo_box_ = nullptr;
    QTableView *table_view_ = nullptr;
    QLabel *target_count_label_ = nullptr;
    std::optional<qint64> selected_track_id_;
    bool restoring_selection_ = false;
};

} // namespace utms
