#include "workbench/TrackFilterProxyModel.h"

#include "workbench/TrackTableModel.h"

namespace utms
{

TrackFilterProxyModel::TrackFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    setSortRole(TrackTableModel::kSortValueRole);
    sort(TrackTableModel::kTrackIdColumn, Qt::AscendingOrder);
}

QVariant TrackFilterProxyModel::data(const QModelIndex &index, int role) const
{
    if (index.isValid() && index.column() == TrackTableModel::kSequenceColumn && role == Qt::DisplayRole)
    {
        return index.row() + 1;
    }
    return QSortFilterProxyModel::data(index, role);
}

void TrackFilterProxyModel::sort(int column, Qt::SortOrder order)
{
    sort_order_ = order;
    QSortFilterProxyModel::sort(column, order);
}

void TrackFilterProxyModel::setTargetTypeFilter(std::optional<TargetType> target_type)
{
    if (target_type_filter_ == target_type)
    {
        return;
    }
    target_type_filter_ = target_type;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
}

std::optional<TargetType> TrackFilterProxyModel::targetTypeFilter() const
{
    return target_type_filter_;
}

bool TrackFilterProxyModel::filterAcceptsRow(int source_row, const QModelIndex &source_parent) const
{
    if (!target_type_filter_.has_value())
    {
        return true;
    }
    const QModelIndex source_index = sourceModel()->index(source_row, TrackTableModel::kTypeColumn, source_parent);
    return source_index.data(TrackTableModel::kTargetTypeRole).toInt() ==
           static_cast<int>(target_type_filter_.value());
}

bool TrackFilterProxyModel::lessThan(const QModelIndex &source_left, const QModelIndex &source_right) const
{
    const bool left_missing = source_left.data(TrackTableModel::kMissingValueRole).toBool();
    const bool right_missing = source_right.data(TrackTableModel::kMissingValueRole).toBool();
    if (left_missing != right_missing)
    {
        return sort_order_ == Qt::AscendingOrder ? !left_missing : left_missing;
    }

    const QVariant left_value = source_left.data(TrackTableModel::kSortValueRole);
    const QVariant right_value = source_right.data(TrackTableModel::kSortValueRole);
    switch (source_left.column())
    {
    case TrackTableModel::kSequenceColumn:
    case TrackTableModel::kTrackIdColumn:
        return left_value.toLongLong() < right_value.toLongLong();
    case TrackTableModel::kTypeColumn:
        return left_value.toInt() < right_value.toInt();
    case TrackTableModel::kLongitudeColumn:
    case TrackTableModel::kLatitudeColumn:
    case TrackTableModel::kVelocityColumn:
    case TrackTableModel::kDistanceColumn:
        return left_value.toDouble() < right_value.toDouble();
    case TrackTableModel::kFirstSeenColumn:
        return left_value.toDateTime() < right_value.toDateTime();
    default:
        return QSortFilterProxyModel::lessThan(source_left, source_right);
    }
}

} // namespace utms
