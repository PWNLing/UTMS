#include "ui/mainwindow.h"

#include <algorithm>
#include <optional>

#include <QAbstractItemView>
#include <QCloseEvent>
#include <QComboBox>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#include "core/RadarTypes.h"
#include "map/OnlineMapState.h"
#include "network/UdpReceiver.h"
#include "ui/MapPanel.h"

namespace {

QString optionalMeasurement(const std::optional<double> &measurement);

class NumericTableWidgetItem : public QTableWidgetItem
{
public:
    explicit NumericTableWidgetItem(const QString &text, const QVariant &sort_value)
        : QTableWidgetItem(text)
        , sort_value_(sort_value)
    {
    }

    const QVariant &sortValue() const
    {
        return sort_value_;
    }

    void setNumericValue(const QString &text, const QVariant &sort_value)
    {
        setText(text);
        sort_value_ = sort_value;
    }

private:
    QVariant sort_value_;
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

    void replaceTracks(const QVector<utms::TrackData> &tracks)
    {
        if (!hasSameTrackIds(tracks)) {
            rebuildRows(tracks);
            sortCurrentRows();
            return;
        }

        bool sort_value_changed = false;
        QHash<qint64, const utms::TrackData *> tracks_by_id;
        tracks_by_id.reserve(tracks.size());
        for (const utms::TrackData &track : tracks) {
            tracks_by_id.insert(track.track_id, &track);
        }

        for (int row = 0; row < rowCount(); ++row) {
            const qint64 track_id = item(row, 0)->text().toLongLong();
            const utms::TrackData &track = *tracks_by_id.value(track_id);
            const QString previous_sort_text = item(row, sort_column_)->text();
            QVariant previous_numeric_sort_value;
            if (const auto *numeric_item = dynamic_cast<const NumericTableWidgetItem *>(item(row, sort_column_));
                numeric_item != nullptr) {
                previous_numeric_sort_value = numeric_item->sortValue();
            }
            updateRow(row, track);
            const auto *current_numeric_item =
                dynamic_cast<const NumericTableWidgetItem *>(item(row, sort_column_));
            const bool numeric_sort_value_changed = current_numeric_item != nullptr &&
                                                    current_numeric_item->sortValue() != previous_numeric_sort_value;
            sort_value_changed = sort_value_changed || numeric_sort_value_changed ||
                                 item(row, sort_column_)->text() != previous_sort_text;
        }

        if (sort_value_changed) {
            sortCurrentRows();
        }
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
    bool hasSameTrackIds(const QVector<utms::TrackData> &tracks) const
    {
        if (static_cast<qsizetype>(rowCount()) != tracks.size()) {
            return false;
        }

        QSet<qint64> track_ids;
        track_ids.reserve(tracks.size());
        for (const utms::TrackData &track : tracks) {
            track_ids.insert(track.track_id);
        }
        for (int row = 0; row < rowCount(); ++row) {
            if (item(row, 0) == nullptr || !track_ids.contains(item(row, 0)->text().toLongLong())) {
                return false;
            }
        }
        return true;
    }

    void rebuildRows(const QVector<utms::TrackData> &tracks)
    {
        setRowCount(tracks.size());
        for (qsizetype row = 0; row < tracks.size(); ++row) {
            updateRow(static_cast<int>(row), tracks.at(row));
        }
    }

    void updateRow(int row, const utms::TrackData &track)
    {
        const QStringList values = {
            QString::number(track.track_id),
            utms::targetTypeDisplayName(track.type),
            QString::number(track.position.longitude, 'f', 7),
            QString::number(track.position.latitude, 'f', 7),
            optionalMeasurement(track.velocity_mps),
            optionalMeasurement(track.distance_m),
            track.first_seen_at.toString(QStringLiteral("HH:mm:ss"))};

        for (qsizetype column = 0; column < values.size(); ++column) {
            QTableWidgetItem *table_item = item(row, column);
            if (column == 0 || column == 4 || column == 5) {
                QVariant sort_value;
                if (column == 0) {
                    sort_value = QVariant::fromValue(track.track_id);
                } else {
                    const std::optional<double> &measurement = column == 4 ? track.velocity_mps : track.distance_m;
                    if (measurement.has_value()) {
                        sort_value = measurement.value();
                    }
                }
                auto *numeric_item = dynamic_cast<NumericTableWidgetItem *>(table_item);
                if (numeric_item == nullptr) {
                    numeric_item = new NumericTableWidgetItem(values.at(column), sort_value);
                    table_item = numeric_item;
                    setItem(row, column, table_item);
                } else {
                    numeric_item->setNumericValue(values.at(column), sort_value);
                }
            } else if (table_item == nullptr) {
                table_item = new QTableWidgetItem(values.at(column));
                setItem(row, column, table_item);
            } else {
                table_item->setText(values.at(column));
            }
            table_item->setTextAlignment(Qt::AlignCenter);
        }
    }

    static bool isMissingNumericValue(const QTableWidgetItem *item)
    {
        const auto *numeric_item = dynamic_cast<const NumericTableWidgetItem *>(item);
        return numeric_item != nullptr && !numeric_item->sortValue().isValid();
    }

    static int compareItems(const QTableWidgetItem *left, const QTableWidgetItem *right)
    {
        const auto *left_numeric = dynamic_cast<const NumericTableWidgetItem *>(left);
        const auto *right_numeric = dynamic_cast<const NumericTableWidgetItem *>(right);
        if (left_numeric != nullptr && right_numeric != nullptr) {
            const auto &left_value = left_numeric->sortValue();
            const auto &right_value = right_numeric->sortValue();
            if (left_value.isValid() != right_value.isValid()) {
                return left_value.isValid() ? -1 : 1;
            }
            if (!left_value.isValid()) {
                return 0;
            }
            if (left_value.metaType().id() == QMetaType::LongLong &&
                right_value.metaType().id() == QMetaType::LongLong) {
                const qint64 left_integer = left_value.toLongLong();
                const qint64 right_integer = right_value.toLongLong();
                return left_integer == right_integer ? 0 : (left_integer < right_integer ? -1 : 1);
            }
            const double left_number = left_value.toDouble();
            const double right_number = right_value.toDouble();
            return left_number == right_number ? 0 : (left_number < right_number ? -1 : 1);
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

void MainWindow::updateCurrentFrame(const utms::RadarFrame &frame)
{
    map_panel_->setFrame(frame);
    static_cast<TrackTableWidget *>(track_table_)->replaceTracks(frame.tracks);
}

void MainWindow::setupUi()
{
    setWindowTitle(tr("GUET-UTMS 实时雷达显示"));
    resize(1400, 800);
    setMinimumSize(800, 500);

    auto *central_widget = new QWidget(this);
    auto *main_layout = new QVBoxLayout(central_widget);
    auto *content_splitter = new QSplitter(Qt::Horizontal, central_widget);
    map_panel_ = new utms::MapPanel(content_splitter);
    map_panel_->setMinimumSize(400, 300);

    auto *data_splitter = new QSplitter(Qt::Vertical, content_splitter);
    auto *configuration_tabs = new QTabWidget(data_splitter);
    auto *system_config_widget = new QWidget(configuration_tabs);
    auto *system_config_layout = new QVBoxLayout(system_config_widget);
    auto *control_layout = new QHBoxLayout();

    auto *port_label = new QLabel(tr("UDP 端口"), system_config_widget);
    port_spin_box_ = new QSpinBox(system_config_widget);
    port_spin_box_->setRange(1, 65'535);
    port_spin_box_->setValue(10'000);
    start_button_ = new QPushButton(tr("启动监听"), system_config_widget);
    stop_button_ = new QPushButton(tr("停止监听"), system_config_widget);
    status_label_ = new QLabel(system_config_widget);
    map_layer_combo_box_ = new QComboBox(system_config_widget);
    map_layer_combo_box_->addItem(tr("街道图"), static_cast<int>(utms::OnlineMapLayer::kStreet));
    map_layer_combo_box_->addItem(tr("卫星图"), static_cast<int>(utms::OnlineMapLayer::kSatellite));
    locate_radar_button_ = new QPushButton(tr("定位雷达"), system_config_widget);

    control_layout->addWidget(port_label);
    control_layout->addWidget(port_spin_box_);
    control_layout->addWidget(start_button_);
    control_layout->addWidget(stop_button_);
    control_layout->addSpacing(12);
    control_layout->addWidget(status_label_);
    control_layout->addStretch();

    auto *map_control_layout = new QHBoxLayout();
    map_control_layout->addWidget(new QLabel(tr("在线图层"), system_config_widget));
    map_control_layout->addWidget(map_layer_combo_box_);
    map_control_layout->addWidget(locate_radar_button_);
    map_control_layout->addStretch();

    track_table_ = new TrackTableWidget(data_splitter);
    track_table_->setMinimumSize(350, 200);
    track_table_->setColumnCount(7);
    track_table_->setHorizontalHeaderLabels(
        {tr("航迹 ID"), tr("类别"), tr("经度"), tr("纬度"), tr("速度 (m/s)"), tr("距离 (m)"), tr("时间")});
    track_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    track_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    track_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    track_table_->setAlternatingRowColors(true);

    system_config_layout->addLayout(control_layout);
    system_config_layout->addLayout(map_control_layout);
    system_config_layout->addStretch();
    configuration_tabs->addTab(system_config_widget, tr("系统配置"));

    data_splitter->addWidget(track_table_);
    data_splitter->addWidget(configuration_tabs);
    data_splitter->setStretchFactor(0, 55);
    data_splitter->setStretchFactor(1, 45);
    data_splitter->setSizes({440, 360});

    content_splitter->addWidget(map_panel_);
    content_splitter->addWidget(data_splitter);
    content_splitter->setStretchFactor(0, 65);
    content_splitter->setStretchFactor(1, 35);
    content_splitter->setSizes({910, 490});
    main_layout->addWidget(content_splitter);
    setCentralWidget(central_widget);

    connect(start_button_, &QPushButton::clicked, this, [this]() {
        emit startListeningRequested(static_cast<quint16>(port_spin_box_->value()));
    });
    connect(stop_button_, &QPushButton::clicked, this, &MainWindow::stopListeningRequested);
    connect(map_layer_combo_box_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        map_panel_->setOnlineLayer(static_cast<utms::OnlineMapLayer>(map_layer_combo_box_->itemData(index).toInt()));
    });
    connect(locate_radar_button_, &QPushButton::clicked, this, [this]() { map_panel_->locateRadar(); });
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
    connect(udp_receiver_, &utms::UdpReceiver::frameReceived, this, &MainWindow::updateCurrentFrame);
    connect(udp_receiver_, &utms::UdpReceiver::stopped, this, &MainWindow::handleUdpWorkerStopped);
    connect(udp_thread_, &QThread::finished, udp_receiver_, &QObject::deleteLater);
    connect(udp_thread_, &QThread::finished, this, [this]() {
        shutdown_complete_ = true;
        close();
    });

    udp_thread_->start();
}
