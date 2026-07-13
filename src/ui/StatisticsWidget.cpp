#include "ui/StatisticsWidget.h"

#include <algorithm>
#include <array>

#include <QBarCategoryAxis>
#include <QBarSeries>
#include <QBarSet>
#include <QChart>
#include <QChartView>
#include <QCursor>
#include <QHBoxLayout>
#include <QLabel>
#include <QLegend>
#include <QPainter>
#include <QPieSeries>
#include <QPieSlice>
#include <QStackedLayout>
#include <QToolTip>
#include <QValueAxis>

#include "core/RadarTypes.h"

namespace utms {

StatisticsWidget::StatisticsWidget(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(6);

    auto *bar_chart = new QChart();
    auto *bar_series = new QBarSeries(bar_chart);
    for (std::size_t index = 0; index < kTargetTypes.size(); ++index) {
        const TargetType type = kTargetTypes[index];
        bar_sets_[index] = new QBarSet(targetTypeDisplayName(type), bar_series);
        bar_sets_[index]->append(0);
        bar_sets_[index]->setColor(QColor(targetTypeColorName(type)));
        bar_series->append(bar_sets_[index]);
        connect(bar_sets_[index], &QBarSet::hovered, this, [this, index](bool hovered, int) {
            if (!hovered) {
                QToolTip::hideText();
                return;
            }
            QToolTip::showText(
                QCursor::pos(),
                tr("%1：%2").arg(targetTypeDisplayName(kTargetTypes[index])).arg(bar_sets_[index]->at(0)));
        });
    }
    bar_chart->addSeries(bar_series);
    bar_chart->setTitle(tr("当前帧目标类别"));
    bar_chart->legend()->setAlignment(Qt::AlignBottom);
    bar_chart->setAnimationOptions(QChart::NoAnimation);

    auto *bar_category_axis = new QBarCategoryAxis(bar_chart);
    bar_category_axis->append(tr("当前帧"));
    bar_value_axis_ = new QValueAxis(bar_chart);
    bar_value_axis_->setRange(0, 5);
    bar_value_axis_->setLabelFormat(QStringLiteral("%d"));
    bar_value_axis_->setTickCount(6);
    bar_chart->addAxis(bar_category_axis, Qt::AlignBottom);
    bar_chart->addAxis(bar_value_axis_, Qt::AlignLeft);
    bar_series->attachAxis(bar_category_axis);
    bar_series->attachAxis(bar_value_axis_);

    auto *bar_chart_view = new QChartView(bar_chart, this);
    bar_chart_view->setRenderHint(QPainter::Antialiasing);
    bar_chart_view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *pie_chart = new QChart();
    auto *pie_series = new QPieSeries(pie_chart);
    pie_slices_[0] = pie_series->append(tr("车辆"), 0);
    pie_slices_[1] = pie_series->append(tr("行人"), 0);
    pie_slices_[2] = pie_series->append(tr("未知"), 0);
    pie_slices_[0]->setColor(QColor(QStringLiteral("#3498db")));
    pie_slices_[1]->setColor(QColor(QStringLiteral("#2ecc71")));
    pie_slices_[2]->setColor(QColor(QStringLiteral("#95a5a6")));
    pie_chart->addSeries(pie_series);
    pie_chart->setTitle(tr("当前帧目标占比"));
    pie_chart->legend()->setAlignment(Qt::AlignBottom);
    pie_chart->setAnimationOptions(QChart::NoAnimation);
    connect(pie_series, &QPieSeries::hovered, this, [](QPieSlice *slice, bool hovered) {
        if (!hovered || slice == nullptr) {
            QToolTip::hideText();
            return;
        }
        QToolTip::showText(QCursor::pos(), slice->label());
    });

    auto *pie_chart_view = new QChartView(pie_chart, this);
    pie_chart_view->setRenderHint(QPainter::Antialiasing);
    pie_chart_view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *pie_container = new QWidget(this);
    pie_stack_ = new QStackedLayout(pie_container);
    pie_stack_->setContentsMargins(0, 0, 0, 0);
    pie_stack_->addWidget(pie_chart_view);
    empty_pie_label_ = new QLabel(tr("暂无目标数据"), pie_container);
    empty_pie_label_->setAlignment(Qt::AlignCenter);
    empty_pie_label_->setStyleSheet(QStringLiteral("QLabel { color: #666666; font-size: 16px; }"));
    pie_stack_->addWidget(empty_pie_label_);

    layout->addWidget(bar_chart_view, 1);
    layout->addWidget(pie_container, 1);
    updateStatistics(TargetStatistics{});
}

void StatisticsWidget::updateStatistics(const TargetStatistics &statistics)
{
    int maximum_count = 0;
    for (std::size_t index = 0; index < kTargetTypes.size(); ++index) {
        const int count = statistics.count(kTargetTypes[index]);
        bar_sets_[index]->replace(0, count);
        maximum_count = std::max(maximum_count, count);
    }
    const int axis_maximum = std::max(5, ((maximum_count + 4) / 5) * 5);
    bar_value_axis_->setRange(0, axis_maximum);
    bar_value_axis_->setTickCount(6);

    const std::array<int, 3> pie_counts{statistics.vehicleGroupCount(), statistics.pedestrian_count,
                                        statistics.unknown_count};
    const int total_count = statistics.totalCount();
    for (std::size_t index = 0; index < pie_slices_.size(); ++index) {
        const int count = pie_counts[index];
        const double percentage = total_count > 0 ? 100.0 * count / total_count : 0.0;
        pie_slices_[index]->setValue(count);
        pie_slices_[index]->setLabel(tr("%1：%2（%3%）")
                                         .arg(index == 0   ? tr("车辆")
                                              : index == 1 ? tr("行人")
                                                           : tr("未知"))
                                         .arg(count)
                                         .arg(percentage, 0, 'f', 1));
        pie_slices_[index]->setLabelVisible(total_count > 0);
    }
    pie_stack_->setCurrentIndex(total_count == 0 ? 1 : 0);
}

} // namespace utms
