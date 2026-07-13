#pragma once

#include <optional>

#include <QSortFilterProxyModel>

#include "core/RadarTypes.h"

namespace utms
{

class TrackFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

    public:
    explicit TrackFilterProxyModel(QObject *parent = nullptr);

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
    void setTargetTypeFilter(std::optional<TargetType> target_type);
    std::optional<TargetType> targetTypeFilter() const;

    protected:
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override;
    bool lessThan(const QModelIndex &source_left, const QModelIndex &source_right) const override;

    private:
    std::optional<TargetType> target_type_filter_;
    Qt::SortOrder sort_order_ = Qt::AscendingOrder;
};

} // namespace utms
