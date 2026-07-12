#include "ui/mainwindow.h"

#include <algorithm>
#include <optional>

#include <QAbstractItemView>
#include <QCloseEvent>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

#include "core/RadarTypes.h"
#include "network/UdpReceiver.h"

namespace {

class NumericTableWidgetItem : public QTableWidgetItem
{
public:
    explicit NumericTableWidgetItem(const QString &text, std::optional<double> sort_value)
        : QTableWidgetItem(text)
        , sort_value_(sort_value)
    {
    }

    const std::optional<double> &sortValue() const
    {
        return sort_value_;
    }

private:
    std::optional<double> sort_value_;
};

class TrackTableWidget : public QTableWidget
{
public:
    explicit TrackTableWidget(QWidget *parent)
        : QTableWidget(parent)
    {
        horizontalHeader()->setSortIndicatorShown(true);
        horizontalHeader()->setSortIndicator(sort_column_, sort_order_);
        connect(horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int column) {
            if (column == sort_column_) {
                sort_order_ = sort_order_ == Qt::AscendingOrder ? Qt::DescendingOrder : Qt::AscendingOrder;
            } else {
                sort_column_ = column;
                sort_order_ = Qt::AscendingOrder;
            }
            horizontalHeader()->setSortIndicator(sort_column_, sort_order_);
            sortCurrentRows();
        });
    }

    void sortCurrentRows()
    {
        // 临时取出单元格以保持整行重排；缺失数值始终置底，重新插入后所有权交还表格。
        QVector<QVector<QTableWidgetItem *>> rows;
        rows.reserve(rowCount());
        for (int row = 0; row < rowCount(); ++row) {
            QVector<QTableWidgetItem *> items;
            items.reserve(columnCount());
            for (int column = 0; column < columnCount(); ++column) {
                items.append(takeItem(row, column));
            }
            rows.append(items);
        }

        std::stable_sort(rows.begin(), rows.end(), [this](const auto &left, const auto &right) {
            const bool left_missing = isMissingNumericValue(left.at(sort_column_));
            const bool right_missing = isMissingNumericValue(right.at(sort_column_));
            if (left_missing != right_missing) {
                return !left_missing;
            }
            const int comparison = compareItems(left.at(sort_column_), right.at(sort_column_));
            if (comparison == 0 && sort_column_ != 0) {
                return compareItems(left.at(0), right.at(0)) < 0;
            }
            return sort_order_ == Qt::AscendingOrder ? comparison < 0 : comparison > 0;
        });

        setRowCount(0);
        setRowCount(rows.size());
        for (qsizetype row = 0; row < rows.size(); ++row) {
            for (qsizetype column = 0; column < rows.at(row).size(); ++column) {
                setItem(row, column, rows.at(row).at(column));
            }
        }
    }

private:
    static bool isMissingNumericValue(const QTableWidgetItem *item)
    {
        const auto *numeric_item = dynamic_cast<const NumericTableWidgetItem *>(item);
        return numeric_item != nullptr && !numeric_item->sortValue().has_value();
    }

    static int compareItems(const QTableWidgetItem *left, const QTableWidgetItem *right)
    {
        const auto *left_numeric = dynamic_cast<const NumericTableWidgetItem *>(left);
        const auto *right_numeric = dynamic_cast<const NumericTableWidgetItem *>(right);
        if (left_numeric != nullptr && right_numeric != nullptr) {
            const auto &left_value = left_numeric->sortValue();
            const auto &right_value = right_numeric->sortValue();
            if (left_value.has_value() != right_value.has_value()) {
                return left_value.has_value() ? -1 : 1;
            }
            if (!left_value.has_value() || left_value.value() == right_value.value()) {
                return 0;
            }
            return left_value.value() < right_value.value() ? -1 : 1;
        }
        return QString::localeAwareCompare(left->text(), right->text());
    }

    int sort_column_ = 0;
    Qt::SortOrder sort_order_ = Qt::AscendingOrder;
};

QString optionalMeasurement(const std::optional<double> &measurement)
{
    return measurement.has_value() ? QString::number(measurement.value(), 'f', 2) : QStringLiteral("--");
}

}  // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    qRegisterMetaType<utms::RadarFrame>();
    qRegisterMetaType<utms::UdpStatus>();
    setupUi();
    handleUdpStatusChanged(utms::UdpStatus::kStopped, tr("UDP 未启动"));
    setupUdpWorker();
}

MainWindow::~MainWindow()
{
    if (udp_thread_ != nullptr && udp_thread_->isRunning()) {
        connect(udp_thread_, &QThread::finished, udp_thread_, &QObject::deleteLater);
        udp_thread_->setParent(nullptr);
        emit shutdownUdpWorkerRequested();
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (shutdown_complete_ || udp_thread_ == nullptr || !udp_thread_->isRunning()) {
        event->accept();
        return;
    }

    event->ignore();
    if (!shutdown_started_) {
        shutdown_started_ = true;
        setEnabled(false);
        emit stopListeningRequested();
    }
}

void MainWindow::handleUdpWorkerStopped()
{
    if (shutdown_started_ && udp_thread_ != nullptr) {
        udp_thread_->quit();
    }
}

void MainWindow::handleUdpStatusChanged(utms::UdpStatus status, const QString &detail)
{
    const bool listening = status != utms::UdpStatus::kStopped;
    port_spin_box_->setEnabled(!listening);
    start_button_->setEnabled(!listening);
    stop_button_->setEnabled(listening);

    QString color;
    switch (status) {
    case utms::UdpStatus::kStopped:
        color = QStringLiteral("#c0392b");
        break;
    case utms::UdpStatus::kListeningNoData:
        color = QStringLiteral("#d4a017");
        break;
    case utms::UdpStatus::kReceiving:
        color = QStringLiteral("#208a4b");
        break;
    }
    status_label_->setText(detail);
    status_label_->setStyleSheet(QStringLiteral("QLabel { color: %1; font-weight: 600; }").arg(color));
}

void MainWindow::updateTrackTable(const utms::RadarFrame &frame)
{
    track_table_->setRowCount(frame.tracks.size());

    for (qsizetype row = 0; row < frame.tracks.size(); ++row) {
        const utms::TrackData &track = frame.tracks.at(row);
        const QStringList values = {
            QString::number(track.track_id),
            utms::targetTypeDisplayName(track.type),
            QString::number(track.position.longitude, 'f', 7),
            QString::number(track.position.latitude, 'f', 7),
            optionalMeasurement(track.velocity_mps),
            optionalMeasurement(track.distance_m),
            track.first_seen_at.toString(QStringLiteral("HH:mm:ss"))};

        for (qsizetype column = 0; column < values.size(); ++column) {
            QTableWidgetItem *item = nullptr;
            if (column == 0) {
                item = new NumericTableWidgetItem(values.at(column), static_cast<double>(track.track_id));
            } else if (column == 4) {
                item = new NumericTableWidgetItem(values.at(column), track.velocity_mps);
            } else if (column == 5) {
                item = new NumericTableWidgetItem(values.at(column), track.distance_m);
            } else {
                item = new QTableWidgetItem(values.at(column));
            }
            item->setTextAlignment(Qt::AlignCenter);
            track_table_->setItem(row, column, item);
        }
    }

    static_cast<TrackTableWidget *>(track_table_)->sortCurrentRows();
}

void MainWindow::setupUi()
{
    setWindowTitle(tr("GUET-UTMS 实时雷达显示"));
    resize(1100, 700);
    setMinimumSize(800, 500);

    auto *central_widget = new QWidget(this);
    auto *main_layout = new QVBoxLayout(central_widget);
    auto *control_layout = new QHBoxLayout();

    auto *port_label = new QLabel(tr("UDP 端口"), central_widget);
    port_spin_box_ = new QSpinBox(central_widget);
    port_spin_box_->setRange(1, 65'535);
    port_spin_box_->setValue(10'000);
    start_button_ = new QPushButton(tr("启动监听"), central_widget);
    stop_button_ = new QPushButton(tr("停止监听"), central_widget);
    status_label_ = new QLabel(central_widget);

    control_layout->addWidget(port_label);
    control_layout->addWidget(port_spin_box_);
    control_layout->addWidget(start_button_);
    control_layout->addWidget(stop_button_);
    control_layout->addSpacing(20);
    control_layout->addWidget(status_label_);
    control_layout->addStretch();

    track_table_ = new TrackTableWidget(central_widget);
    track_table_->setColumnCount(7);
    track_table_->setHorizontalHeaderLabels(
        {tr("航迹 ID"), tr("类别"), tr("经度"), tr("纬度"), tr("速度 (m/s)"), tr("距离 (m)"), tr("时间")});
    track_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    track_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    track_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    track_table_->setAlternatingRowColors(true);

    main_layout->addLayout(control_layout);
    main_layout->addWidget(track_table_);
    setCentralWidget(central_widget);

    connect(start_button_, &QPushButton::clicked, this, [this]() {
        emit startListeningRequested(static_cast<quint16>(port_spin_box_->value()));
    });
    connect(stop_button_, &QPushButton::clicked, this, &MainWindow::stopListeningRequested);
}

void MainWindow::setupUdpWorker()
{
    udp_thread_ = new QThread(this);
    udp_receiver_ = new utms::UdpReceiver();
    if (!udp_receiver_->moveToThread(udp_thread_)) {
        delete udp_receiver_;
        udp_receiver_ = nullptr;
        handleUdpStatusChanged(utms::UdpStatus::kStopped, tr("UDP 工作线程初始化失败"));
        start_button_->setEnabled(false);
        return;
    }

    connect(this, &MainWindow::startListeningRequested, udp_receiver_, &utms::UdpReceiver::startListening);
    connect(this, &MainWindow::stopListeningRequested, udp_receiver_, &utms::UdpReceiver::stopListening);
    connect(this, &MainWindow::shutdownUdpWorkerRequested, udp_receiver_, &utms::UdpReceiver::shutdown);
    connect(udp_receiver_, &utms::UdpReceiver::statusChanged, this, &MainWindow::handleUdpStatusChanged);
    connect(udp_receiver_, &utms::UdpReceiver::frameReceived, this, &MainWindow::updateTrackTable);
    connect(udp_receiver_, &utms::UdpReceiver::stopped, this, &MainWindow::handleUdpWorkerStopped);
    connect(udp_thread_, &QThread::finished, udp_receiver_, &QObject::deleteLater);
    connect(udp_thread_, &QThread::finished, this, [this]() {
        shutdown_complete_ = true;
        close();
    });

    udp_thread_->start();
}
