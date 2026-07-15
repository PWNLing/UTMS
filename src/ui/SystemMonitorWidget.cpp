#include "ui/SystemMonitorWidget.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>

#include "core/SystemMetricsSampler.h"

namespace utms {
namespace {

constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;
constexpr double kBytesPerGigabyte = 1024.0 * 1024.0 * 1024.0;

QLabel *createMetricValueLabel(QWidget *parent)
{
    auto *label = new QLabel(QStringLiteral("--"), parent);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral("QLabel { font-size: 15px; font-weight: 600; color: #2c3e50; }"));
    return label;
}

} // namespace

SystemMonitorWidget::SystemMonitorWidget(QWidget *parent)
    : SystemMonitorWidget(std::make_unique<NativeSystemMetricsProvider>(), parent)
{
}

SystemMonitorWidget::SystemMonitorWidget(std::unique_ptr<SystemMetricsProvider> provider, QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    setupSamplerThread(std::move(provider));
}

SystemMonitorWidget::~SystemMonitorWidget()
{
    if (!shutdown_complete_ && sampler_thread_ != nullptr && sampler_thread_->isRunning()) {
        // 主窗口会异步等待 stopped()；其他所有者提前销毁时则解除父子关系，
        // 避免阻塞 GUI 线程或由 QWidget 析构仍在运行的 QThread。
        sampler_thread_->setParent(nullptr);
        connect(sampler_thread_, &QThread::finished, sampler_thread_, &QObject::deleteLater);
    }
    shutdown();
}

bool SystemMonitorWidget::isMonitoring() const
{
    return monitoring_;
}

bool SystemMonitorWidget::isShutdownComplete() const
{
    return shutdown_complete_;
}

QString SystemMonitorWidget::statusText() const
{
    return status_label_->text();
}

QString SystemMonitorWidget::toggleButtonText() const
{
    return toggle_button_->text();
}

QString SystemMonitorWidget::hostCpuText() const
{
    return host_cpu_value_label_->text();
}

QString SystemMonitorWidget::hostMemoryText() const
{
    return host_memory_value_label_->text();
}

QString SystemMonitorWidget::processCpuText() const
{
    return process_cpu_value_label_->text();
}

QString SystemMonitorWidget::processMemoryText() const
{
    return process_memory_value_label_->text();
}

void SystemMonitorWidget::startMonitoring()
{
    if (monitoring_ || shutdown_started_ || sampler_ == nullptr) {
        return;
    }

    ++session_id_;
    logged_errors_.clear();
    clearMetrics();
    monitoring_ = true;
    status_label_->setText(tr("监控已开启"));
    status_label_->setStyleSheet(QStringLiteral("QLabel { color: #208a4b; font-weight: 600; }"));
    toggle_button_->setText(tr("停止监控"));
    emit startSamplingRequested(session_id_);
    qInfo() << "SystemMonitorWidget: system monitoring started";
}

void SystemMonitorWidget::stopMonitoring()
{
    if (!monitoring_) {
        return;
    }

    monitoring_ = false;
    emit stopSamplingRequested();
    logged_errors_.clear();
    clearMetrics();
    status_label_->setText(tr("监控已停止"));
    status_label_->setStyleSheet(QStringLiteral("QLabel { color: #7f8c8d; font-weight: 600; }"));
    toggle_button_->setText(tr("开启监控"));
    qInfo() << "SystemMonitorWidget: system monitoring stopped";
}

void SystemMonitorWidget::refreshNow()
{
    if (!monitoring_ || sampler_ == nullptr) {
        return;
    }
    emit sampleNowRequested(session_id_);
}

void SystemMonitorWidget::shutdown()
{
    if (shutdown_started_) {
        return;
    }

    shutdown_started_ = true;
    stopMonitoring();
    toggle_button_->setEnabled(false);
    if (sampler_thread_ != nullptr && sampler_thread_->isRunning()) {
        sampler_thread_->quit();
        return;
    }

    shutdown_complete_ = true;
    emit stopped();
}

void SystemMonitorWidget::applySnapshot(quint64 session_id, const SystemMetricsSnapshot &snapshot)
{
    if (!monitoring_ || session_id != session_id_) {
        return;
    }
    host_cpu_value_label_->setText(percentageText(snapshot.host_cpu_percent, MetricKind::kHostCpu, tr("本机 CPU")));
    host_memory_value_label_->setText(hostMemoryUsageText(snapshot.host_memory));
    process_cpu_value_label_->setText(
        percentageText(snapshot.process_cpu_percent, MetricKind::kProcessCpu, tr("UTMS 进程 CPU")));
    process_memory_value_label_->setText(processMemoryUsageText(snapshot.process_memory_bytes));
}

void SystemMonitorWidget::handleSamplerThreadFinished()
{
    sampler_ = nullptr;
    if (shutdown_complete_) {
        return;
    }
    shutdown_complete_ = true;
    if (shutdown_started_) {
        emit stopped();
    }
}

void SystemMonitorWidget::setupUi()
{
    auto *main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(12, 12, 12, 12);
    main_layout->setSpacing(12);

    auto *control_layout = new QHBoxLayout();
    status_label_ = new QLabel(tr("监控未开启"), this);
    status_label_->setStyleSheet(QStringLiteral("QLabel { color: #7f8c8d; font-weight: 600; }"));
    toggle_button_ = new QPushButton(tr("开启监控"), this);
    toggle_button_->setMinimumWidth(100);
    control_layout->addWidget(status_label_, 1);
    control_layout->addWidget(toggle_button_);
    main_layout->addLayout(control_layout);

    auto *host_group = new QGroupBox(tr("本机整体"), this);
    auto *host_layout = new QFormLayout(host_group);
    host_cpu_value_label_ = createMetricValueLabel(host_group);
    host_memory_value_label_ = createMetricValueLabel(host_group);
    host_layout->addRow(tr("CPU 使用率"), host_cpu_value_label_);
    host_layout->addRow(tr("物理内存 已用 / 总量 / 使用率"), host_memory_value_label_);

    auto *process_group = new QGroupBox(tr("UTMS 进程"), this);
    auto *process_layout = new QFormLayout(process_group);
    process_cpu_value_label_ = createMetricValueLabel(process_group);
    process_memory_value_label_ = createMetricValueLabel(process_group);
    process_layout->addRow(tr("CPU 使用率"), process_cpu_value_label_);
    process_layout->addRow(tr("内存占用"), process_memory_value_label_);

    main_layout->addWidget(host_group);
    main_layout->addWidget(process_group);
    main_layout->addStretch();

    connect(toggle_button_, &QPushButton::clicked, this, [this]() {
        if (monitoring_) {
            stopMonitoring();
        } else {
            startMonitoring();
        }
    });
}

void SystemMonitorWidget::setupSamplerThread(std::unique_ptr<SystemMetricsProvider> provider)
{
    Q_ASSERT(provider != nullptr);
    qRegisterMetaType<utms::SystemMetricsSnapshot>();

    sampler_thread_ = new QThread(this);
    sampler_ = new SystemMetricsSampler(std::move(provider));
    if (!sampler_->moveToThread(sampler_thread_)) {
        delete sampler_;
        sampler_ = nullptr;
        toggle_button_->setEnabled(false);
        status_label_->setText(tr("监控初始化失败"));
        status_label_->setStyleSheet(QStringLiteral("QLabel { color: #c0392b; font-weight: 600; }"));
        qCritical() << "SystemMonitorWidget: failed to move metrics sampler to worker thread";
        return;
    }

    connect(this, &SystemMonitorWidget::startSamplingRequested, sampler_, &SystemMetricsSampler::startSampling);
    connect(this, &SystemMonitorWidget::stopSamplingRequested, sampler_, &SystemMetricsSampler::stopSampling);
    connect(this, &SystemMonitorWidget::sampleNowRequested, sampler_, &SystemMetricsSampler::sampleNow);
    connect(sampler_, &SystemMetricsSampler::snapshotReady, this, &SystemMonitorWidget::applySnapshot);
    connect(sampler_thread_, &QThread::finished, sampler_, &QObject::deleteLater);
    connect(sampler_thread_, &QThread::finished, this, &SystemMonitorWidget::handleSamplerThreadFinished);
    sampler_thread_->start();
}

void SystemMonitorWidget::clearMetrics()
{
    host_cpu_value_label_->setText(QStringLiteral("--"));
    host_memory_value_label_->setText(QStringLiteral("--"));
    process_cpu_value_label_->setText(QStringLiteral("--"));
    process_memory_value_label_->setText(QStringLiteral("--"));
}

QString SystemMonitorWidget::percentageText(const MetricResult<double> &metric, MetricKind metric_kind,
                                            const QString &metric_name)
{
    if (!metric.hasValue()) {
        return failureText(metric_kind, metric_name, metric.error);
    }
    clearMetricFailure(metric_kind);
    return tr("%1%").arg(*metric.value, 0, 'f', 1);
}

QString SystemMonitorWidget::hostMemoryUsageText(const MetricResult<HostMemoryUsage> &metric)
{
    const QString metric_name = tr("本机物理内存");
    if (!metric.hasValue()) {
        return failureText(MetricKind::kHostMemory, metric_name, metric.error);
    }
    clearMetricFailure(MetricKind::kHostMemory);
    return tr("%1 GB / %2 GB（%3%）")
        .arg(static_cast<double>(metric.value->used_bytes) / kBytesPerGigabyte, 0, 'f', 1)
        .arg(static_cast<double>(metric.value->total_bytes) / kBytesPerGigabyte, 0, 'f', 1)
        .arg(metric.value->used_percent, 0, 'f', 1);
}

QString SystemMonitorWidget::processMemoryUsageText(const MetricResult<quint64> &metric)
{
    const QString metric_name = tr("UTMS 进程内存");
    if (!metric.hasValue()) {
        return failureText(MetricKind::kProcessMemory, metric_name, metric.error);
    }
    clearMetricFailure(MetricKind::kProcessMemory);
    return tr("%1 MB").arg(static_cast<double>(*metric.value) / kBytesPerMegabyte, 0, 'f', 1);
}

QString SystemMonitorWidget::failureText(MetricKind metric_kind, const QString &metric_name, const QString &error)
{
    if (logged_errors_.value(metric_kind) != error) {
        logged_errors_.insert(metric_kind, error);
        qWarning().noquote() << "SystemMonitorWidget:" << metric_name << error;
    }
    return tr("读取失败：%1").arg(error);
}

void SystemMonitorWidget::clearMetricFailure(MetricKind metric_kind)
{
    logged_errors_.remove(metric_kind);
}

} // namespace utms
