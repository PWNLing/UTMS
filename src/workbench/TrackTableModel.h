#pragma once

#include <QAbstractTableModel>

#include "core/RadarTypes.h"

namespace utms
{

class TrackTableModel : public QAbstractTableModel
{
    Q_OBJECT

    public:
    static constexpr int kSequenceColumn = 0;
    static constexpr int kTrackIdColumn = 1;
    static constexpr int kTypeColumn = 2;
    static constexpr int kLongitudeColumn = 3;
    static constexpr int kLatitudeColumn = 4;
    static constexpr int kVelocityColumn = 5;
    static constexpr int kDistanceColumn = 6;
    static constexpr int kFirstSeenColumn = 7;
    static constexpr int kColumnCount = 8;

    static constexpr int kTrackIdRole = Qt::UserRole + 1;
    static constexpr int kTargetTypeRole = Qt::UserRole + 2;
    static constexpr int kSortValueRole = Qt::UserRole + 3;
    static constexpr int kMissingValueRole = Qt::UserRole + 4;

    explicit TrackTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    void replaceTracks(const QVector<TrackData> &tracks);
    const TrackData *trackAt(int row) const;

    private:
    QVector<TrackData> tracks_;
};

} // namespace utms
