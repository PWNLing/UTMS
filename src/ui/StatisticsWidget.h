#pragma once

#include <array>

#include <QWidget>

class QBarSet;
class QLabel;
class QPieSlice;
class QStackedLayout;
class QValueAxis;

namespace utms {

struct TargetStatistics;

class StatisticsWidget : public QWidget {
public:
    explicit StatisticsWidget(QWidget *parent = nullptr);

    void updateStatistics(const TargetStatistics &statistics);

private:
    std::array<QBarSet *, 5> bar_sets_{};
    std::array<QPieSlice *, 3> pie_slices_{};
    QValueAxis *bar_value_axis_ = nullptr;
    QLabel *empty_pie_label_ = nullptr;
    QStackedLayout *pie_stack_ = nullptr;
};

} // namespace utms
