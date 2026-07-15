#include "ui/mainwindow.h"

#include <optional>

#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
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
#include "media/RtspController.h"
#include "network/UdpReceiver.h"
#include "ui/BottomStatusBar.h"
#include "ui/MapPanel.h"
#include "ui/StatisticsWidget.h"
#include "ui/SystemMonitorWidget.h"
#include "ui/VideoStreamWidget.h"
#include "workbench/TrackTableWidget.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    qRegisterMetaType<utms::RadarFrame>();
    qRegisterMetaType<utms::UdpStatus>();
    qRegisterMetaType<utms::RtspConnectionState>();
    setupUi();
    handleUdpStatusChanged(utms::UdpStatus::kStopped, tr("UDP 未启动"));
    setupUdpWorker();
    setupVideoController();
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
    const bool udp_stopped = udp_shutdown_complete_ || udp_thread_ == nullptr || !udp_thread_->isRunning();
    const bool video_stopped = video_shutdown_complete_ || rtsp_controller_ == nullptr;
    const bool monitor_stopped =
        monitor_shutdown_complete_ || system_monitor_widget_ == nullptr || system_monitor_widget_->isShutdownComplete();
    if (udp_stopped && video_stopped && monitor_stopped) {
        event->accept();
        return;
    }

    event->ignore();
    if (!shutdown_started_) {
        shutdown_started_ = true;
        setEnabled(false);
        if (udp_thread_ != nullptr && udp_thread_->isRunning()) {
            emit stopListeningRequested();
        } else {
            udp_shutdown_complete_ = true;
        }
        if (rtsp_controller_ != nullptr) {
            rtsp_controller_->shutdown();
        } else {
            video_shutdown_complete_ = true;
        }
        if (system_monitor_widget_ != nullptr) {
            system_monitor_widget_->shutdown();
        } else {
            monitor_shutdown_complete_ = true;
        }
        completeShutdownIfReady();
    }
}

void MainWindow::handleUdpWorkerStopped()
{
    if (shutdown_started_ && udp_thread_ != nullptr) {
        udp_thread_->quit();
    }
}

void MainWindow::handleVideoWorkerStopped()
{
    video_shutdown_complete_ = true;
    completeShutdownIfReady();
}

void MainWindow::handleSystemMonitorStopped()
{
    monitor_shutdown_complete_ = true;
    completeShutdownIfReady();
}

void MainWindow::completeShutdownIfReady()
{
    if (shutdown_started_ && udp_shutdown_complete_ && video_shutdown_complete_ && monitor_shutdown_complete_) {
        close();
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
    config_status_label_->setText(detail);
    config_status_label_->setStyleSheet(QStringLiteral("QLabel { color: %1; font-weight: 600; }").arg(color));
    bottom_status_bar_->setUdpStatus(status);
}

void MainWindow::updateCurrentFrame(const utms::RadarFrame &frame)
{
    map_panel_->setFrame(frame);
    track_table_->replaceTracks(frame.tracks);
    statistics_widget_->updateStatistics(frame.statistics);
    bottom_status_bar_->updateStatistics(frame.statistics);
}

void MainWindow::setupUi()
{
    setWindowTitle(tr("GUET-UTMS 实时雷达显示"));
    resize(1400, 800);
    setMinimumSize(800, 500);

    auto *central_widget = new QWidget(this);
    auto *main_layout = new QVBoxLayout(central_widget);
    main_layout->setContentsMargins(6, 6, 6, 4);
    main_layout->setSpacing(4);
    auto *content_splitter = new QSplitter(Qt::Horizontal, central_widget);
    map_panel_ = new utms::MapPanel(content_splitter);
    map_panel_->setMinimumSize(400, 300);

    auto *data_splitter = new QSplitter(Qt::Vertical, content_splitter);
    auto *configuration_tabs = new QTabWidget(data_splitter);
    auto *system_config_widget = new QWidget(configuration_tabs);
    auto *system_config_layout = new QVBoxLayout(system_config_widget);

    auto *udp_group = new QGroupBox(tr("UDP 监听"), system_config_widget);
    auto *udp_layout = new QHBoxLayout(udp_group);

    auto *port_label = new QLabel(tr("端口"), udp_group);
    port_spin_box_ = new QSpinBox(udp_group);
    port_spin_box_->setRange(1, 65'535);
    port_spin_box_->setValue(10'000);
    start_button_ = new QPushButton(tr("启动监听"), udp_group);
    stop_button_ = new QPushButton(tr("停止监听"), udp_group);
    config_status_label_ = new QLabel(udp_group);
    udp_layout->addWidget(port_label);
    udp_layout->addWidget(port_spin_box_);
    udp_layout->addWidget(start_button_);
    udp_layout->addWidget(stop_button_);
    udp_layout->addWidget(config_status_label_, 1);

    auto *map_center_group = new QGroupBox(tr("地图中心"), system_config_widget);
    auto *map_center_layout = new QGridLayout(map_center_group);
    longitude_spin_box_ = new QDoubleSpinBox(map_center_group);
    longitude_spin_box_->setRange(-180.0, 180.0);
    longitude_spin_box_->setDecimals(6);
    longitude_spin_box_->setSingleStep(0.0001);
    longitude_spin_box_->setValue(110.416819);
    latitude_spin_box_ = new QDoubleSpinBox(map_center_group);
    latitude_spin_box_->setRange(-90.0, 90.0);
    latitude_spin_box_->setDecimals(6);
    latitude_spin_box_->setSingleStep(0.0001);
    latitude_spin_box_->setValue(25.311724);
    auto *locate_position_button = new QPushButton(tr("定位到该位置"), map_center_group);
    locate_radar_button_ = new QPushButton(tr("定位雷达"), map_center_group);
    map_center_layout->addWidget(new QLabel(tr("经度"), map_center_group), 0, 0);
    map_center_layout->addWidget(longitude_spin_box_, 0, 1);
    map_center_layout->addWidget(new QLabel(tr("纬度"), map_center_group), 1, 0);
    map_center_layout->addWidget(latitude_spin_box_, 1, 1);
    map_center_layout->addWidget(locate_position_button, 0, 2);
    map_center_layout->addWidget(locate_radar_button_, 1, 2);
    map_center_layout->setColumnStretch(1, 1);

    auto *map_display_group = new QGroupBox(tr("地图显示"), system_config_widget);
    auto *map_display_layout = new QHBoxLayout(map_display_group);
    map_mode_combo_box_ = new QComboBox(map_display_group);
    map_mode_combo_box_->addItem(tr("在线地图"), static_cast<int>(utms::MapMode::kOnline));
    map_mode_combo_box_->addItem(tr("离线地图"), static_cast<int>(utms::MapMode::kOffline));
    map_layer_combo_box_ = new QComboBox(map_display_group);
    map_layer_combo_box_->addItem(tr("街道图"), static_cast<int>(utms::OnlineMapLayer::kStreet));
    map_layer_combo_box_->addItem(tr("卫星图"), static_cast<int>(utms::OnlineMapLayer::kSatellite));
    map_display_layout->addWidget(new QLabel(tr("地图模式"), map_display_group));
    map_display_layout->addWidget(map_mode_combo_box_);
    map_display_layout->addSpacing(12);
    map_display_layout->addWidget(new QLabel(tr("在线图层"), map_display_group));
    map_display_layout->addWidget(map_layer_combo_box_);
    map_display_layout->addStretch();

    track_table_ = new utms::TrackTableWidget(data_splitter);
    track_table_->setMinimumSize(350, 200);

    system_config_layout->addWidget(udp_group);
    system_config_layout->addWidget(map_center_group);
    system_config_layout->addWidget(map_display_group);
    system_config_layout->addStretch();
    configuration_tabs->addTab(system_config_widget, tr("系统配置"));

    statistics_widget_ = new utms::StatisticsWidget(configuration_tabs);
    configuration_tabs->addTab(statistics_widget_, tr("统计图表"));

    video_stream_widget_ = new utms::VideoStreamWidget(configuration_tabs);
    configuration_tabs->addTab(video_stream_widget_, tr("视频流"));

    system_monitor_widget_ = new utms::SystemMonitorWidget(configuration_tabs);
    configuration_tabs->addTab(system_monitor_widget_, tr("系统监控"));
    connect(system_monitor_widget_, &utms::SystemMonitorWidget::stopped, this, &MainWindow::handleSystemMonitorStopped);

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
    main_layout->addWidget(content_splitter, 1);
    bottom_status_bar_ = new utms::BottomStatusBar(central_widget);
    main_layout->addWidget(bottom_status_bar_);
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
    connect(locate_position_button, &QPushButton::clicked, this,
            [this]() { map_panel_->setCenter({latitude_spin_box_->value(), longitude_spin_box_->value()}); });
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
    connect(track_table_, &utms::TrackTableWidget::targetSelectionCleared, this, [this]() {
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
        udp_shutdown_complete_ = true;
        completeShutdownIfReady();
    });

    udp_thread_->start();
}

void MainWindow::setupVideoController()
{
    rtsp_controller_ = new utms::RtspController(this);
    connect(video_stream_widget_, &utms::VideoStreamWidget::connectRequested, rtsp_controller_,
            &utms::RtspController::connectToStream);
    connect(video_stream_widget_, &utms::VideoStreamWidget::disconnectRequested, rtsp_controller_,
            &utms::RtspController::disconnectFromStream);
    connect(video_stream_widget_, &utms::VideoStreamWidget::detectionEnabledRequested, rtsp_controller_,
            &utms::RtspController::setDetectionEnabled);
    connect(rtsp_controller_, &utms::RtspController::stateChanged, video_stream_widget_,
            &utms::VideoStreamWidget::setConnectionState);
    connect(rtsp_controller_, &utms::RtspController::detectionStateChanged, video_stream_widget_,
            &utms::VideoStreamWidget::setDetectionState);
    connect(rtsp_controller_, &utms::RtspController::frameReady, video_stream_widget_,
            &utms::VideoStreamWidget::setFrame);
    connect(rtsp_controller_, &utms::RtspController::stopped, this, &MainWindow::handleVideoWorkerStopped);
}
