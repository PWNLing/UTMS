#include "ui/mainwindow.h"

#include <optional>

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

#include "core/RadarTypes.h"
#include "history/HistoryController.h"
#include "history/HistoryPlaybackController.h"
#include "history/HistoryStore.h"
#include "map/OnlineMapState.h"
#include "media/RtspController.h"
#include "network/UdpReceiver.h"
#include "ui/BottomStatusBar.h"
#include "ui/GeofenceManagerWidget.h"
#include "ui/HistoryQueryWidget.h"
#include "ui/MapPanel.h"
#include "ui/StatisticsWidget.h"
#include "ui/SystemMonitorWidget.h"
#include "ui/VideoStreamWidget.h"
#include "workbench/TrackTableWidget.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    qRegisterMetaType<utms::RadarFrame>();
    qRegisterMetaType<utms::UdpStatus>();
    qRegisterMetaType<utms::RtspConnectionState>();
    qRegisterMetaType<utms::HistoryConfiguration>();
    qRegisterMetaType<utms::Geofence>();
    setupUi();
    setupPlaybackController();
    handleUdpStatusChanged(utms::UdpStatus::kStopped, tr("UDP 未启动"));
    setupHistoryController();
    setupUdpWorker();
    setupVideoController();
}

MainWindow::~MainWindow() {
    if (udp_thread_ != nullptr && udp_thread_->isRunning()) {
        connect(udp_thread_, &QThread::finished, udp_thread_, &QObject::deleteLater);
        udp_thread_->setParent(nullptr);
        emit shutdownUdpWorkerRequested();
    }
    if (history_thread_ != nullptr && history_thread_->isRunning()) {
        connect(history_thread_, &QThread::finished, history_thread_, &QObject::deleteLater);
        history_thread_->setParent(nullptr);
        emit shutdownHistoryWorkerRequested();
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    const bool udp_stopped = udp_shutdown_complete_ || udp_thread_ == nullptr || !udp_thread_->isRunning();
    const bool history_stopped =
        history_shutdown_complete_ || history_thread_ == nullptr || !history_thread_->isRunning();
    const bool video_stopped = video_shutdown_complete_ || rtsp_controller_ == nullptr;
    const bool monitor_stopped =
        monitor_shutdown_complete_ || system_monitor_widget_ == nullptr || system_monitor_widget_->isShutdownComplete();
    if (udp_stopped && history_stopped && video_stopped && monitor_stopped) {
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
            requestHistoryShutdown();
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

void MainWindow::handleUdpWorkerStopped() {
    if (shutdown_started_ && udp_thread_ != nullptr) {
        udp_thread_->quit();
        requestHistoryShutdown();
    }
}

void MainWindow::handleVideoWorkerStopped() {
    video_shutdown_complete_ = true;
    completeShutdownIfReady();
}

void MainWindow::handleSystemMonitorStopped() {
    monitor_shutdown_complete_ = true;
    completeShutdownIfReady();
}

void MainWindow::completeShutdownIfReady() {
    if (shutdown_started_ && udp_shutdown_complete_ && history_shutdown_complete_ && video_shutdown_complete_ &&
        monitor_shutdown_complete_) {
        close();
    }
}

void MainWindow::handleUdpStatusChanged(utms::UdpStatus status, const QString &detail) {
    const bool listening = status != utms::UdpStatus::kStopped;
    if (!udp_listening_ && listening) {
        emit startHistorySessionRequested();
    } else if (udp_listening_ && !listening) {
        emit stopHistorySessionRequested();
    }
    udp_listening_ = listening;

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

void MainWindow::handleHistoryConfigurationLoaded(const utms::HistoryConfiguration &configuration) {
    const QSignalBlocker sampling_blocker(history_sampling_combo_box_);
    const QSignalBlocker retention_blocker(history_retention_spin_box_);
    const int sampling_index = history_sampling_combo_box_->findData(static_cast<int>(configuration.sampling_rate));
    if (sampling_index >= 0) {
        history_sampling_combo_box_->setCurrentIndex(sampling_index);
    }
    history_retention_spin_box_->setValue(configuration.retention_days);
}

void MainWindow::handleHistoryAvailabilityChanged(bool available, const QString &detail) {
    const QString color = available ? QStringLiteral("#208a4b") : QStringLiteral("#c0392b");
    updateHistoryStatusLabel(detail, color);
    history_query_widget_->setAvailable(available);
    history_query_widget_->showStatus(detail, !available);
    geofence_manager_widget_->setAvailable(available);
    if (!available) {
        geofence_manager_widget_->showStatus(detail, true);
    }
}

void MainWindow::handleHistorySessionActiveChanged(bool active, const QString &detail) {
    const QString color = active ? QStringLiteral("#208a4b") : QStringLiteral("#555555");
    updateHistoryStatusLabel(detail, color);
}

void MainWindow::handleHistoryError(const QString &message) {
    const QString detail = tr("历史记录错误：%1").arg(message);
    updateHistoryStatusLabel(detail, QStringLiteral("#c0392b"));
    history_query_widget_->showStatus(detail, true);
}

void MainWindow::handleHistorySessionsLoaded(const QVector<utms::HistorySession> &sessions) {
    history_query_widget_->setSessions(sessions);
}

void MainWindow::handleHistoryQueryCompleted(const utms::HistoryQueryResult &result) {
    history_query_widget_->applyQueryResult(result);
}

void MainWindow::handleHistoryExportCompleted(const QString &output_path, int record_count) {
    history_query_widget_->showExportCompleted(output_path, record_count);
}

void MainWindow::handleHistoryDatabaseSizeChanged(qint64 size_bytes) {
    history_query_widget_->setDatabaseSizeBytes(size_bytes);
}

void MainWindow::handleGeofencesLoaded(const QVector<utms::Geofence> &geofences) {
    map_panel_->setGeofences(geofences);
    geofence_manager_widget_->setGeofences(map_panel_->geofences());
}

void MainWindow::handleGeofenceError(const QString &message) {
    map_panel_->discardPendingGeofenceEdits();
    geofence_manager_widget_->setGeofences(map_panel_->geofences());
    geofence_manager_widget_->showStatus(tr("电子围栏错误：%1").arg(message), true);
}

void MainWindow::handleReplayModeChanged(bool replay_mode) {
    replay_mode_ = replay_mode;
    map_panel_->setReplayMode(replay_mode);
    history_query_widget_->setReplayMode(replay_mode);
    bottom_status_bar_->setReplayState(replay_mode, false);
    setWindowTitle(replay_mode ? tr("GUET-UTMS 历史回放") : tr("GUET-UTMS 实时雷达显示"));

    if (!replay_mode && latest_live_frame_.has_value()) {
        updateNonMapDisplays(latest_live_frame_.value());
    }
}

void MainWindow::handlePlaybackStateChanged(bool playing) {
    history_query_widget_->setPlaying(playing);
    bottom_status_bar_->setReplayState(replay_mode_, playing);
}

void MainWindow::handlePlaybackFrameChanged(const utms::RadarFrame &frame, int frame_index, int frame_count,
                                            const QDateTime &frame_time) {
    if (!replay_mode_) {
        return;
    }
    map_panel_->setReplayFrame(frame);
    updateNonMapDisplays(frame);
    history_query_widget_->setPlaybackPosition(frame_index, frame_count, frame_time);
}

void MainWindow::updateCurrentFrame(const utms::RadarFrame &frame) {
    map_panel_->setFrame(frame);
    latest_live_frame_ = frame;
    if (!replay_mode_) {
        updateNonMapDisplays(frame);
    }
}

void MainWindow::updateNonMapDisplays(const utms::RadarFrame &frame) {
    track_table_->replaceTracks(frame.tracks);
    statistics_widget_->updateStatistics(frame.statistics);
    bottom_status_bar_->updateStatistics(frame.statistics);
}

void MainWindow::setupUi() {
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

    auto *history_group = new QGroupBox(tr("历史记录"), system_config_widget);
    auto *history_layout = new QGridLayout(history_group);
    history_sampling_combo_box_ = new QComboBox(history_group);
    history_sampling_combo_box_->addItem(tr("1 FPS"), static_cast<int>(utms::HistorySamplingRate::kOneFps));
    history_sampling_combo_box_->addItem(tr("2 FPS"), static_cast<int>(utms::HistorySamplingRate::kTwoFps));
    history_sampling_combo_box_->addItem(tr("5 FPS"), static_cast<int>(utms::HistorySamplingRate::kFiveFps));
    history_sampling_combo_box_->addItem(tr("全帧"), static_cast<int>(utms::HistorySamplingRate::kEveryFrame));
    history_sampling_combo_box_->setCurrentIndex(1);
    history_retention_spin_box_ = new QSpinBox(history_group);
    history_retention_spin_box_->setRange(1, 30);
    history_retention_spin_box_->setSuffix(tr(" 天"));
    history_retention_spin_box_->setValue(7);
    history_status_label_ = new QLabel(tr("历史数据库初始化中"), history_group);
    history_layout->addWidget(new QLabel(tr("采样频率"), history_group), 0, 0);
    history_layout->addWidget(history_sampling_combo_box_, 0, 1);
    history_layout->addWidget(new QLabel(tr("保留期限"), history_group), 0, 2);
    history_layout->addWidget(history_retention_spin_box_, 0, 3);
    history_layout->addWidget(history_status_label_, 1, 0, 1, 4);
    history_layout->setColumnStretch(1, 1);
    history_layout->setColumnStretch(3, 1);

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

    auto *trajectory_group = new QGroupBox(tr("实时短航迹"), system_config_widget);
    auto *trajectory_layout = new QHBoxLayout(trajectory_group);
    auto *trajectory_duration_combo_box = new QComboBox(trajectory_group);
    trajectory_duration_combo_box->addItem(tr("关闭"), static_cast<int>(utms::RealtimeTrajectoryDuration::kOff));
    trajectory_duration_combo_box->addItem(tr("10 秒"),
                                           static_cast<int>(utms::RealtimeTrajectoryDuration::kTenSeconds));
    trajectory_duration_combo_box->addItem(tr("30 秒"),
                                           static_cast<int>(utms::RealtimeTrajectoryDuration::kThirtySeconds));
    trajectory_duration_combo_box->addItem(tr("1 分钟"),
                                           static_cast<int>(utms::RealtimeTrajectoryDuration::kOneMinute));
    trajectory_duration_combo_box->setCurrentIndex(2);
    auto *show_all_trajectories_check_box = new QCheckBox(tr("显示所有目标航迹"), trajectory_group);
    trajectory_layout->addWidget(new QLabel(tr("显示时长"), trajectory_group));
    trajectory_layout->addWidget(trajectory_duration_combo_box);
    trajectory_layout->addSpacing(12);
    trajectory_layout->addWidget(show_all_trajectories_check_box);
    trajectory_layout->addStretch();

    track_table_ = new utms::TrackTableWidget(data_splitter);
    track_table_->setMinimumSize(350, 200);

    system_config_layout->addWidget(udp_group);
    system_config_layout->addWidget(history_group);
    system_config_layout->addWidget(map_center_group);
    system_config_layout->addWidget(map_display_group);
    system_config_layout->addWidget(trajectory_group);
    system_config_layout->addStretch();
    configuration_tabs->addTab(system_config_widget, tr("系统配置"));

    statistics_widget_ = new utms::StatisticsWidget(configuration_tabs);
    configuration_tabs->addTab(statistics_widget_, tr("统计图表"));

    video_stream_widget_ = new utms::VideoStreamWidget(configuration_tabs);
    configuration_tabs->addTab(video_stream_widget_, tr("视频流"));

    system_monitor_widget_ = new utms::SystemMonitorWidget(configuration_tabs);
    configuration_tabs->addTab(system_monitor_widget_, tr("系统监控"));
    connect(system_monitor_widget_, &utms::SystemMonitorWidget::stopped, this, &MainWindow::handleSystemMonitorStopped);

    history_query_widget_ = new utms::HistoryQueryWidget(configuration_tabs);
    configuration_tabs->addTab(history_query_widget_, tr("历史回放"));
    connect(history_query_widget_, &utms::HistoryQueryWidget::queryRequested, this, &MainWindow::queryHistoryRequested);
    connect(history_query_widget_, &utms::HistoryQueryWidget::exportRequested, this,
            &MainWindow::exportHistoryRequested);
    connect(history_query_widget_, &utms::HistoryQueryWidget::deleteSessionRequested, this,
            &MainWindow::deleteHistorySessionRequested);
    connect(history_query_widget_, &utms::HistoryQueryWidget::deleteAllSessionsRequested, this,
            &MainWindow::deleteAllHistorySessionsRequested);
    connect(history_query_widget_, &utms::HistoryQueryWidget::refreshRequested, this,
            &MainWindow::refreshHistoryInfoRequested);

    geofence_manager_widget_ = new utms::GeofenceManagerWidget(configuration_tabs);
    configuration_tabs->addTab(geofence_manager_widget_, tr("电子围栏"));
    connect(geofence_manager_widget_, &utms::GeofenceManagerWidget::createRequested, this,
            &MainWindow::createGeofenceRequested);
    connect(geofence_manager_widget_, &utms::GeofenceManagerWidget::updateRequested, this,
            &MainWindow::updateGeofenceRequested);
    connect(geofence_manager_widget_, &utms::GeofenceManagerWidget::enabledChangeRequested, this,
            &MainWindow::setGeofenceEnabledRequested);
    connect(geofence_manager_widget_, &utms::GeofenceManagerWidget::visibilityChangeRequested, this,
            &MainWindow::setGeofenceVisibleRequested);
    connect(geofence_manager_widget_, &utms::GeofenceManagerWidget::deleteRequested, this,
            &MainWindow::deleteGeofenceRequested);
    connect(geofence_manager_widget_, &utms::GeofenceManagerWidget::locateRequested, this, [this](qint64 geofence_id) {
        if (!map_panel_->locateGeofence(geofence_id)) {
            geofence_manager_widget_->showStatus(tr("无法定位：电子围栏不存在"), true);
        }
    });
    connect(geofence_manager_widget_, &utms::GeofenceManagerWidget::mapEditRequested, this,
            [this](std::optional<qint64> geofence_id) {
                if (!map_panel_->setEditableGeofenceId(geofence_id)) {
                    geofence_manager_widget_->showStatus(tr("无法开启地图编辑：请先显示该围栏"), true);
                }
            });
    connect(map_panel_, &utms::MapPanel::geofenceEditingChanged, geofence_manager_widget_,
            &utms::GeofenceManagerWidget::setEditingGeofenceId);
    connect(map_panel_, &utms::MapPanel::geofenceEdited, this, [this](const utms::Geofence &geofence) {
        geofence_manager_widget_->applyMapEditedGeofence(geofence);
        emit updateGeofenceGeometryRequested(geofence);
    });
    connect(map_panel_, &utms::MapPanel::geofenceEditError, this, [this](const QString &message) {
        geofence_manager_widget_->showStatus(tr("地图编辑无效：%1，已恢复原围栏").arg(message), true);
    });

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
    const auto save_history_configuration = [this]() {
        const auto sampling_rate =
            static_cast<utms::HistorySamplingRate>(history_sampling_combo_box_->currentData().toInt());
        emit saveHistoryConfigurationRequested({sampling_rate, history_retention_spin_box_->value()});
    };
    connect(history_sampling_combo_box_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [save_history_configuration](int) { save_history_configuration(); });
    connect(history_retention_spin_box_, qOverload<int>(&QSpinBox::valueChanged), this,
            [save_history_configuration](int) { save_history_configuration(); });
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
    connect(trajectory_duration_combo_box, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, trajectory_duration_combo_box](int index) {
                map_panel_->setTrajectoryDuration(static_cast<utms::RealtimeTrajectoryDuration>(
                    trajectory_duration_combo_box->itemData(index).toInt()));
            });
    connect(show_all_trajectories_check_box, &QCheckBox::toggled, map_panel_, &utms::MapPanel::setShowAllTrajectories);
    connect(locate_radar_button_, &QPushButton::clicked, this, [this]() { map_panel_->locateRadar(); });
    connect(locate_position_button, &QPushButton::clicked, this,
            [this]() { map_panel_->setCenter({latitude_spin_box_->value(), longitude_spin_box_->value()}); });
    connect(track_table_, &utms::TrackTableWidget::targetSelected, this, [this](qint64 track_id) {
        if (!map_panel_->selectTarget(track_id, true)) {
            qWarning() << "MainWindow: failed to select table track" << track_id;
        }
        selectReplayTrack(track_id);
    });
    connect(map_panel_, &utms::MapPanel::targetClicked, this, [this](qint64 track_id) {
        const bool selected_row_visible = track_table_->selectTrackById(track_id);
        if (!selected_row_visible && track_table_->selectedTrackId() != std::optional<qint64>(track_id)) {
            qWarning() << "MainWindow: failed to select map track in table" << track_id;
        }
        selectReplayTrack(track_id);
    });
    connect(track_table_, &utms::TrackTableWidget::targetSelectionCleared, this, [this]() {
        map_panel_->clearSelectionForMissingTarget();
        if (replay_mode_ && playback_controller_ != nullptr) {
            playback_controller_->clearSelectedTrackId();
        }
    });
}

void MainWindow::setupPlaybackController() {
    playback_controller_ = new utms::HistoryPlaybackController(this);
    connect(history_query_widget_, &utms::HistoryQueryWidget::replayRequested, this,
            [this](const utms::HistoryQueryResult &result) {
                if (!playback_controller_->beginReplay(result)) {
                    history_query_widget_->showStatus(tr("无法进入回放：查询结果中没有可用帧"), true);
                }
            });
    connect(history_query_widget_, &utms::HistoryQueryWidget::returnLiveRequested, playback_controller_,
            &utms::HistoryPlaybackController::returnToLive);
    connect(history_query_widget_, &utms::HistoryQueryWidget::playRequested, playback_controller_,
            &utms::HistoryPlaybackController::play);
    connect(history_query_widget_, &utms::HistoryQueryWidget::pauseRequested, playback_controller_,
            &utms::HistoryPlaybackController::pause);
    connect(history_query_widget_, &utms::HistoryQueryWidget::previousFrameRequested, playback_controller_,
            &utms::HistoryPlaybackController::previousFrame);
    connect(history_query_widget_, &utms::HistoryQueryWidget::nextFrameRequested, playback_controller_,
            &utms::HistoryPlaybackController::nextFrame);
    connect(history_query_widget_, &utms::HistoryQueryWidget::seekRequested, playback_controller_,
            &utms::HistoryPlaybackController::seekTo);
    connect(history_query_widget_, &utms::HistoryQueryWidget::playbackRateRequested, this, [this](double rate) {
        if (!playback_controller_->setPlaybackRate(rate)) {
            qWarning() << "MainWindow: rejected unsupported history playback rate" << rate;
        }
    });
    connect(playback_controller_, &utms::HistoryPlaybackController::replayModeChanged, this,
            &MainWindow::handleReplayModeChanged);
    connect(playback_controller_, &utms::HistoryPlaybackController::playbackStateChanged, this,
            &MainWindow::handlePlaybackStateChanged);
    connect(playback_controller_, &utms::HistoryPlaybackController::frameChanged, this,
            &MainWindow::handlePlaybackFrameChanged);
    connect(playback_controller_, &utms::HistoryPlaybackController::dataGapSkipped, history_query_widget_,
            &utms::HistoryQueryWidget::showDataGap);
    connect(playback_controller_, &utms::HistoryPlaybackController::selectedTrajectoryChanged, map_panel_,
            &utms::MapPanel::setReplayTrajectory);
    connect(playback_controller_, &utms::HistoryPlaybackController::selectedTrajectoryCleared, map_panel_,
            &utms::MapPanel::clearReplayTrajectory);
}

void MainWindow::selectReplayTrack(qint64 track_id) {
    if (replay_mode_ && playback_controller_ != nullptr) {
        playback_controller_->setSelectedTrackId(track_id);
    }
}

void MainWindow::setupHistoryController() {
    history_thread_ = new QThread(this);
    history_controller_ = new utms::HistoryController();
    if (!history_controller_->moveToThread(history_thread_)) {
        delete history_controller_;
        history_controller_ = nullptr;
        history_shutdown_complete_ = true;
        handleHistoryAvailabilityChanged(false, tr("历史工作线程初始化失败"));
        return;
    }

    const QString database_path =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data/utms.sqlite"));
    connect(history_thread_, &QThread::started, history_controller_,
            [controller = history_controller_, database_path]() { controller->initialize(database_path); });
    connect(this, &MainWindow::startHistorySessionRequested, history_controller_,
            &utms::HistoryController::startSession);
    connect(this, &MainWindow::stopHistorySessionRequested, history_controller_, &utms::HistoryController::stopSession);
    connect(this, &MainWindow::saveHistoryConfigurationRequested, history_controller_,
            &utms::HistoryController::saveConfiguration);
    connect(this, &MainWindow::queryHistoryRequested, history_controller_, &utms::HistoryController::queryHistory);
    connect(this, &MainWindow::exportHistoryRequested, history_controller_, &utms::HistoryController::exportHistoryCsv);
    connect(this, &MainWindow::deleteHistorySessionRequested, history_controller_,
            &utms::HistoryController::deleteSession);
    connect(this, &MainWindow::deleteAllHistorySessionsRequested, history_controller_,
            &utms::HistoryController::deleteAllSessions);
    connect(this, &MainWindow::refreshHistoryInfoRequested, history_controller_,
            &utms::HistoryController::refreshHistoryInfo);
    connect(this, &MainWindow::createGeofenceRequested, history_controller_, &utms::HistoryController::createGeofence);
    connect(this, &MainWindow::updateGeofenceRequested, history_controller_, &utms::HistoryController::updateGeofence);
    connect(this, &MainWindow::updateGeofenceGeometryRequested, history_controller_,
            &utms::HistoryController::updateGeofenceGeometry);
    connect(this, &MainWindow::setGeofenceEnabledRequested, history_controller_,
            &utms::HistoryController::setGeofenceEnabled);
    connect(this, &MainWindow::setGeofenceVisibleRequested, history_controller_,
            &utms::HistoryController::setGeofenceVisible);
    connect(this, &MainWindow::deleteGeofenceRequested, history_controller_, &utms::HistoryController::deleteGeofence);
    connect(this, &MainWindow::shutdownHistoryWorkerRequested, history_controller_, &utms::HistoryController::shutdown);
    connect(history_controller_, &utms::HistoryController::configurationLoaded, this,
            &MainWindow::handleHistoryConfigurationLoaded);
    connect(history_controller_, &utms::HistoryController::availabilityChanged, this,
            &MainWindow::handleHistoryAvailabilityChanged);
    connect(history_controller_, &utms::HistoryController::sessionActiveChanged, this,
            &MainWindow::handleHistorySessionActiveChanged);
    connect(history_controller_, &utms::HistoryController::sessionsLoaded, this,
            &MainWindow::handleHistorySessionsLoaded);
    connect(history_controller_, &utms::HistoryController::queryCompleted, this,
            &MainWindow::handleHistoryQueryCompleted);
    connect(history_controller_, &utms::HistoryController::exportCompleted, this,
            &MainWindow::handleHistoryExportCompleted);
    connect(history_controller_, &utms::HistoryController::databaseSizeChanged, this,
            &MainWindow::handleHistoryDatabaseSizeChanged);
    connect(history_controller_, &utms::HistoryController::geofencesLoaded, this, &MainWindow::handleGeofencesLoaded);
    connect(history_controller_, &utms::HistoryController::geofenceErrorOccurred, this,
            &MainWindow::handleGeofenceError);
    connect(history_controller_, &utms::HistoryController::sessionDeleted, this, [this](qint64 session_id) {
        history_query_widget_->showStatus(tr("历史会话 #%1 已删除").arg(session_id), false);
    });
    connect(history_controller_, &utms::HistoryController::allSessionsDeleted, this, [this](int session_count) {
        history_query_widget_->showStatus(tr("已删除全部 %1 个历史会话").arg(session_count), false);
    });
    connect(history_controller_, &utms::HistoryController::errorOccurred, this, &MainWindow::handleHistoryError);
    connect(history_thread_, &QThread::finished, history_controller_, &QObject::deleteLater);
    connect(history_thread_, &QThread::finished, this, [this]() {
        history_shutdown_complete_ = true;
        completeShutdownIfReady();
    });

    history_thread_->start();
}

void MainWindow::setupUdpWorker() {
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
    if (history_controller_ != nullptr) {
        connect(udp_receiver_, &utms::UdpReceiver::frameReceived, history_controller_,
                &utms::HistoryController::recordAcceptedFrame);
    }
    connect(udp_receiver_, &utms::UdpReceiver::stopped, this, &MainWindow::handleUdpWorkerStopped);
    connect(udp_thread_, &QThread::finished, udp_receiver_, &QObject::deleteLater);
    connect(udp_thread_, &QThread::finished, this, [this]() {
        udp_shutdown_complete_ = true;
        completeShutdownIfReady();
    });

    udp_thread_->start();
}

void MainWindow::requestHistoryShutdown() {
    if (history_shutdown_requested_) {
        return;
    }
    history_shutdown_requested_ = true;

    if (history_thread_ != nullptr && history_thread_->isRunning() && history_controller_ != nullptr) {
        emit shutdownHistoryWorkerRequested();
    } else {
        history_shutdown_complete_ = true;
        completeShutdownIfReady();
    }
}

void MainWindow::updateHistoryStatusLabel(const QString &detail, const QString &color) {
    history_status_label_->setText(detail);
    history_status_label_->setStyleSheet(QStringLiteral("QLabel { color: %1; font-weight: 600; }").arg(color));
}

void MainWindow::setupVideoController() {
    rtsp_controller_ = new utms::RtspController(this);
    connect(video_stream_widget_, &utms::VideoStreamWidget::connectRequested, rtsp_controller_,
            &utms::RtspController::connectToStream);
    connect(video_stream_widget_, &utms::VideoStreamWidget::disconnectRequested, rtsp_controller_,
            &utms::RtspController::disconnectFromStream);
    connect(video_stream_widget_, &utms::VideoStreamWidget::detectionEnabledRequested, rtsp_controller_,
            &utms::RtspController::setDetectionEnabled);
    connect(video_stream_widget_, &utms::VideoStreamWidget::startRecordingRequested, rtsp_controller_,
            &utms::RtspController::startRecording);
    connect(video_stream_widget_, &utms::VideoStreamWidget::stopRecordingRequested, rtsp_controller_,
            &utms::RtspController::stopRecording);
    connect(video_stream_widget_, &utms::VideoStreamWidget::openRecordingDirectoryRequested, rtsp_controller_,
            &utms::RtspController::openRecordingDirectory);
    connect(rtsp_controller_, &utms::RtspController::stateChanged, video_stream_widget_,
            &utms::VideoStreamWidget::setConnectionState);
    connect(rtsp_controller_, &utms::RtspController::detectionStateChanged, video_stream_widget_,
            &utms::VideoStreamWidget::setDetectionState);
    connect(rtsp_controller_, &utms::RtspController::recordingStateChanged, video_stream_widget_,
            &utms::VideoStreamWidget::setRecordingState);
    connect(rtsp_controller_, &utms::RtspController::recordingDurationChanged, video_stream_widget_,
            &utms::VideoStreamWidget::setRecordingDuration);
    connect(rtsp_controller_, &utms::RtspController::frameReady, video_stream_widget_,
            &utms::VideoStreamWidget::setFrame);
    connect(rtsp_controller_, &utms::RtspController::stopped, this, &MainWindow::handleVideoWorkerStopped);
}
