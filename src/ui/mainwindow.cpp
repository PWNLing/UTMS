#include "ui/mainwindow.h"

#include <optional>

#include <QCloseEvent>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

#include "core/RadarTypes.h"
#include "map/OnlineMapState.h"
#include "network/UdpReceiver.h"
#include "ui/MapPanel.h"
#include "workbench/TrackTableWidget.h"

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
    track_table_->replaceTracks(frame.tracks);
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
    map_mode_combo_box_ = new QComboBox(system_config_widget);
    map_mode_combo_box_->addItem(tr("在线地图"), static_cast<int>(utms::MapMode::kOnline));
    map_mode_combo_box_->addItem(tr("离线地图"), static_cast<int>(utms::MapMode::kOffline));
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
    map_control_layout->addWidget(new QLabel(tr("地图模式"), system_config_widget));
    map_control_layout->addWidget(map_mode_combo_box_);
    map_control_layout->addWidget(new QLabel(tr("在线图层"), system_config_widget));
    map_control_layout->addWidget(map_layer_combo_box_);
    map_control_layout->addWidget(locate_radar_button_);
    map_control_layout->addStretch();

    track_table_ = new utms::TrackTableWidget(data_splitter);
    track_table_->setMinimumSize(350, 200);

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

    connect(start_button_, &QPushButton::clicked, this,
            [this]() { emit startListeningRequested(static_cast<quint16>(port_spin_box_->value())); });
    connect(stop_button_, &QPushButton::clicked, this, &MainWindow::stopListeningRequested);
    connect(map_mode_combo_box_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        const auto mode = static_cast<utms::MapMode>(map_mode_combo_box_->itemData(index).toInt());
        if (mode == utms::MapMode::kOffline) {
            const int street_index = map_layer_combo_box_->findData(static_cast<int>(utms::OnlineMapLayer::kStreet));
            if (street_index >= 0) {
                map_layer_combo_box_->setCurrentIndex(street_index);
            }
        }
        map_panel_->setMapMode(mode);
        map_layer_combo_box_->setEnabled(mode == utms::MapMode::kOnline);
    });
    connect(map_layer_combo_box_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        map_panel_->setOnlineLayer(static_cast<utms::OnlineMapLayer>(map_layer_combo_box_->itemData(index).toInt()));
    });
    connect(locate_radar_button_, &QPushButton::clicked, this, [this]() { map_panel_->locateRadar(); });
    connect(track_table_, &utms::TrackTableWidget::targetSelected, this, [this](qint64 track_id) {
        if (!map_panel_->selectTarget(track_id, true)) {
            qWarning() << "MainWindow: failed to select table track" << track_id;
        }
    });
    connect(map_panel_, &utms::MapPanel::targetClicked, this, [this](qint64 track_id) {
        const bool selected_row_visible = track_table_->selectTrackById(track_id);
        if (!selected_row_visible && track_table_->selectedTrackId() != std::optional<qint64>(track_id)) {
            qWarning() << "MainWindow: failed to select map track in table" << track_id;
        }
    });
    connect(track_table_, &utms::TrackTableWidget::targetSelectionCleared, this,
            [this]() {
                if (!map_panel_->setSelectedTrackId(std::nullopt)) {
                    qWarning() << "MainWindow: failed to clear map track selection";
                }
            });
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
