#include "workbench/TrackTableWidget.h"

#include <array>

#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>

#include "workbench/TrackFilterProxyModel.h"
#include "workbench/TrackTableModel.h"

namespace utms
{

namespace
{

class TargetTypeBadgeDelegate : public QStyledItemDelegate
{
    public:
    explicit TargetTypeBadgeDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem base_option(option);
        initStyleOption(&base_option, index);
        const QString label = base_option.text;
        base_option.text.clear();
        QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &base_option, painter, option.widget);

        const int badge_width_px = qMin(72, option.rect.width() - 8);
        const QRect badge_rect(option.rect.center().x() - badge_width_px / 2, option.rect.center().y() - 11,
                               badge_width_px, 22);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(
            QColor(targetTypeColorName(static_cast<TargetType>(index.data(TrackTableModel::kTargetTypeRole).toInt()))));
        painter->drawRoundedRect(badge_rect, 6, 6);
        painter->setPen(Qt::white);
        painter->drawText(badge_rect, Qt::AlignCenter, label);
        painter->restore();
    }
};

class TrackTableView : public QTableView
{
    public:
    explicit TrackTableView(QWidget *parent = nullptr)
        : QTableView(parent)
    {
    }

    protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QTableView::resizeEvent(event);
        const int unused_width_px = viewport()->width() - horizontalHeader()->length();
        if (unused_width_px <= 0)
        {
            return;
        }

        constexpr std::array<int, 3> kStretchColumns = {TrackTableModel::kTrackIdColumn,
                                                        TrackTableModel::kLongitudeColumn,
                                                        TrackTableModel::kLatitudeColumn};
        const QSignalBlocker blocker(horizontalHeader());
        int remaining_width_px = unused_width_px;
        for (std::size_t index = 0; index < kStretchColumns.size(); ++index)
        {
            const int remaining_columns = static_cast<int>(kStretchColumns.size() - index);
            const int added_width_px = remaining_width_px / remaining_columns;
            const int column = kStretchColumns.at(index);
            horizontalHeader()->resizeSection(column, horizontalHeader()->sectionSize(column) + added_width_px);
            remaining_width_px -= added_width_px;
        }
    }
};

} // namespace

TrackTableWidget::TrackTableWidget(QWidget *parent)
    : QWidget(parent), source_model_(new TrackTableModel(this)), proxy_model_(new TrackFilterProxyModel(this)),
      type_filter_combo_box_(new QComboBox(this)), table_view_(new TrackTableView(this)),
      target_count_label_(new QLabel(this))
{
    proxy_model_->setSourceModel(source_model_);

    type_filter_combo_box_->addItem(tr("全部类型"));
    type_filter_combo_box_->addItem(tr("汽车"), static_cast<int>(TargetType::kCar));
    type_filter_combo_box_->addItem(tr("卡车"), static_cast<int>(TargetType::kTruck));
    type_filter_combo_box_->addItem(tr("行人"), static_cast<int>(TargetType::kPedestrian));
    type_filter_combo_box_->addItem(tr("自行车"), static_cast<int>(TargetType::kBicycle));
    type_filter_combo_box_->addItem(tr("未知"), static_cast<int>(TargetType::kUnknown));

    auto *filter_layout = new QHBoxLayout();
    filter_layout->addWidget(new QLabel(tr("类型筛选"), this));
    filter_layout->addWidget(type_filter_combo_box_);
    filter_layout->addStretch();

    table_view_->setModel(proxy_model_);
    table_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_view_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_view_->setAlternatingRowColors(true);
    table_view_->setSortingEnabled(true);
    table_view_->horizontalHeader()->setSectionsMovable(false);
    table_view_->horizontalHeader()->setStretchLastSection(false);
    table_view_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_view_->setColumnWidth(TrackTableModel::kSequenceColumn, 60);
    table_view_->setColumnWidth(TrackTableModel::kTrackIdColumn, 100);
    table_view_->setColumnWidth(TrackTableModel::kTypeColumn, 90);
    table_view_->setColumnWidth(TrackTableModel::kLongitudeColumn, 120);
    table_view_->setColumnWidth(TrackTableModel::kLatitudeColumn, 120);
    table_view_->setColumnWidth(TrackTableModel::kVelocityColumn, 100);
    table_view_->setColumnWidth(TrackTableModel::kDistanceColumn, 100);
    table_view_->setColumnWidth(TrackTableModel::kFirstSeenColumn, 90);
    table_view_->setItemDelegateForColumn(TrackTableModel::kTypeColumn, new TargetTypeBadgeDelegate(table_view_));
    proxy_model_->sort(TrackTableModel::kTrackIdColumn, Qt::AscendingOrder);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(filter_layout);
    layout->addWidget(table_view_, 1);
    layout->addWidget(target_count_label_);
    target_count_label_->setText(tr("当前目标：0"));

    connect(type_filter_combo_box_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index)
    {
        const QVariant target_type = type_filter_combo_box_->itemData(index);
        proxy_model_->setTargetTypeFilter(
            target_type.isValid() ? std::optional<TargetType>(static_cast<TargetType>(target_type.toInt()))
                                  : std::nullopt);
        restoreVisibleSelection();
    });
    connect(table_view_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            &TrackTableWidget::handleCurrentRowChanged);
    connect(table_view_, &QTableView::clicked, this, &TrackTableWidget::handleTableClicked);
}

void TrackTableWidget::replaceTracks(const QVector<TrackData> &tracks)
{
    const int vertical_scroll = table_view_->verticalScrollBar()->value();
    const int horizontal_scroll = table_view_->horizontalScrollBar()->value();

    restoring_selection_ = true;
    source_model_->replaceTracks(tracks);
    restoring_selection_ = false;
    target_count_label_->setText(tr("当前目标：%1").arg(tracks.size()));

    if (selected_track_id_.has_value() && !sourceContainsTrack(selected_track_id_.value()))
    {
        selected_track_id_.reset();
        table_view_->clearSelection();
        table_view_->setCurrentIndex({});
        emit targetSelectionCleared();
    }
    else
    {
        restoreVisibleSelection();
    }

    table_view_->verticalScrollBar()->setValue(vertical_scroll);
    table_view_->horizontalScrollBar()->setValue(horizontal_scroll);
}

void TrackTableWidget::setTargetTypeFilter(std::optional<TargetType> target_type)
{
    const int index = target_type.has_value()
                          ? type_filter_combo_box_->findData(static_cast<int>(target_type.value()))
                          : 0;
    if (index >= 0)
    {
        type_filter_combo_box_->setCurrentIndex(index);
    }
}

std::optional<TargetType> TrackTableWidget::targetTypeFilter() const
{
    return proxy_model_->targetTypeFilter();
}

std::optional<qint64> TrackTableWidget::selectedTrackId() const
{
    return selected_track_id_;
}

bool TrackTableWidget::selectTrackById(qint64 track_id)
{
    if (!sourceContainsTrack(track_id))
    {
        clearTargetSelection();
        return false;
    }

    selected_track_id_ = track_id;
    const QModelIndex proxy_index = proxyIndexForTrack(track_id);
    restoring_selection_ = true;
    if (!proxy_index.isValid())
    {
        table_view_->clearSelection();
        table_view_->setCurrentIndex({});
        restoring_selection_ = false;
        return false;
    }

    table_view_->setCurrentIndex(proxy_index);
    table_view_->selectRow(proxy_index.row());
    table_view_->scrollTo(proxy_index, QAbstractItemView::PositionAtCenter);
    restoring_selection_ = false;
    return true;
}

void TrackTableWidget::clearTargetSelection()
{
    const bool had_selection = selected_track_id_.has_value() || table_view_->currentIndex().isValid();
    restoring_selection_ = true;
    selected_track_id_.reset();
    table_view_->clearSelection();
    table_view_->setCurrentIndex({});
    restoring_selection_ = false;
    if (had_selection)
    {
        emit targetSelectionCleared();
    }
}

QModelIndex TrackTableWidget::proxyIndexForTrack(qint64 track_id) const
{
    for (int row = 0; row < proxy_model_->rowCount(); ++row)
    {
        const QModelIndex index = proxy_model_->index(row, TrackTableModel::kTrackIdColumn);
        if (index.data(TrackTableModel::kTrackIdRole).toLongLong() == track_id)
        {
            return index;
        }
    }
    return {};
}

bool TrackTableWidget::sourceContainsTrack(qint64 track_id) const
{
    for (int row = 0; row < source_model_->rowCount(); ++row)
    {
        if (source_model_->index(row, TrackTableModel::kTrackIdColumn)
                .data(TrackTableModel::kTrackIdRole)
                .toLongLong() == track_id)
        {
            return true;
        }
    }
    return false;
}

void TrackTableWidget::restoreVisibleSelection()
{
    if (!selected_track_id_.has_value())
    {
        return;
    }
    const bool selected_row_visible = selectTrackById(selected_track_id_.value());
    if (!selected_row_visible)
    {
        return;
    }
}

void TrackTableWidget::handleCurrentRowChanged(const QModelIndex &current)
{
    if (restoring_selection_ || !current.isValid())
    {
        return;
    }

    selected_track_id_ = current.data(TrackTableModel::kTrackIdRole).toLongLong();
}

void TrackTableWidget::handleTableClicked(const QModelIndex &index)
{
    if (!index.isValid())
    {
        return;
    }
    selected_track_id_ = index.data(TrackTableModel::kTrackIdRole).toLongLong();
    emit targetSelected(selected_track_id_.value());
}

} // namespace utms
