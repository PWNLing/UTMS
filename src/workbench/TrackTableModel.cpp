#include "workbench/TrackTableModel.h"

#include <array>

namespace utms
{

TrackTableModel::TrackTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int TrackTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : tracks_.size();
}

int TrackTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : kColumnCount;
}

QVariant TrackTableModel::data(const QModelIndex &index, int role) const
{
    const TrackData *track = trackAt(index.row());
    if (track == nullptr || index.column() < 0 || index.column() >= kColumnCount)
    {
        return {};
    }

    if (role == kTrackIdRole)
    {
        return track->track_id;
    }
    if (role == kTargetTypeRole)
    {
        return static_cast<int>(track->type);
    }
    if (role == kMissingValueRole)
    {
        return (index.column() == kVelocityColumn && !track->velocity_mps.has_value()) ||
               (index.column() == kDistanceColumn && !track->distance_m.has_value());
    }
    if (role == Qt::TextAlignmentRole)
    {
        return static_cast<int>(Qt::AlignCenter);
    }

    const bool display_role = role == Qt::DisplayRole;
    const bool sort_role = role == kSortValueRole;
    if (!display_role && !sort_role)
    {
        return {};
    }

    switch (index.column())
    {
    case kSequenceColumn:
        return index.row() + 1;
    case kTrackIdColumn:
        return track->track_id;
    case kTypeColumn:
        return sort_role ? QVariant(static_cast<int>(track->type)) : QVariant(targetTypeDisplayName(track->type));
    case kLongitudeColumn:
        return sort_role ? QVariant(track->position.longitude)
                         : QVariant(QString::number(track->position.longitude, 'f', 6));
    case kLatitudeColumn:
        return sort_role ? QVariant(track->position.latitude)
                         : QVariant(QString::number(track->position.latitude, 'f', 6));
    case kVelocityColumn:
        if (!track->velocity_mps.has_value())
        {
            return display_role ? QVariant(QStringLiteral("--")) : QVariant();
        }
        return sort_role ? QVariant(track->velocity_mps.value())
                         : QVariant(QString::number(track->velocity_mps.value(), 'f', 2));
    case kDistanceColumn:
        if (!track->distance_m.has_value())
        {
            return display_role ? QVariant(QStringLiteral("--")) : QVariant();
        }
        return sort_role ? QVariant(track->distance_m.value())
                         : QVariant(QString::number(track->distance_m.value(), 'f', 2));
    case kFirstSeenColumn:
        return sort_role ? QVariant(track->first_seen_at)
                         : QVariant(track->first_seen_at.toString(QStringLiteral("HH:mm:ss")));
    default:
        return {};
    }
}

QVariant TrackTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section < 0 || section >= kColumnCount)
    {
        return QAbstractTableModel::headerData(section, orientation, role);
    }

    static const std::array<const char *, kColumnCount> kHeaders = {
        QT_TR_NOOP("序号"),       QT_TR_NOOP("航迹 ID"), QT_TR_NOOP("类别"),      QT_TR_NOOP("经度"),
        QT_TR_NOOP("纬度"),       QT_TR_NOOP("速度 (m/s)"), QT_TR_NOOP("距离 (m)"), QT_TR_NOOP("时间")};
    return tr(kHeaders.at(static_cast<std::size_t>(section)));
}

Qt::ItemFlags TrackTableModel::flags(const QModelIndex &index) const
{
    return QAbstractTableModel::flags(index) & ~Qt::ItemIsEditable;
}

void TrackTableModel::replaceTracks(const QVector<TrackData> &tracks)
{
    beginResetModel();
    tracks_ = tracks;
    endResetModel();
}

const TrackData *TrackTableModel::trackAt(int row) const
{
    return row >= 0 && row < tracks_.size() ? &tracks_.at(row) : nullptr;
}

} // namespace utms
