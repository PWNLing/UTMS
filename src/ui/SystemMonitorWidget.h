#pragma once

#include <memory>

#include <QHash>
#include <QString>
#include <QWidget>

#include "core/SystemMetricsProvider.h"

class QLabel;
class QPushButton;
class QThread;

namespace utms {

class SystemMetricsSampler;

class SystemMonitorWidget : public QWidget {
    Q_OBJECT

public:
    explicit SystemMonitorWidget(QWidget *parent = nullptr);
    explicit SystemMonitorWidget(std::unique_ptr<SystemMetricsProvider> provider, QWidget *parent = nullptr);
    ~SystemMonitorWidget() override;

    bool isMonitoring() const;
    bool isShutdownComplete() const;
    QString statusText() const;
    QString toggleButtonText() const;
    QString hostCpuText() const;
    QString hostMemoryText() const;
    QString processCpuText() const;
    QString processMemoryText() const;

public slots:
    void startMonitoring();
    void stopMonitoring();
    void refreshNow();
    void shutdown();

signals:
    void stopped();
    void startSamplingRequested(quint64 session_id);
    void stopSamplingRequested();
    void sampleNowRequested(quint64 session_id);

private slots:
    void applySnapshot(quint64 session_id, const utms::SystemMetricsSnapshot &snapshot);
    void handleSamplerThreadFinished();

private:
    enum class MetricKind { kHostCpu, kHostMemory, kProcessCpu, kProcessMemory };

    void setupUi();
    void setupSamplerThread(std::unique_ptr<SystemMetricsProvider> provider);
    void clearMetrics();
    QString percentageText(const MetricResult<double> &metric, MetricKind metric_kind, const QString &metric_name);
    QString hostMemoryUsageText(const MetricResult<HostMemoryUsage> &metric);
    QString processMemoryUsageText(const MetricResult<quint64> &metric);
    QString failureText(MetricKind metric_kind, const QString &metric_name, const QString &error);
    void clearMetricFailure(MetricKind metric_kind);

    QThread *sampler_thread_ = nullptr;
    SystemMetricsSampler *sampler_ = nullptr;
    QPushButton *toggle_button_ = nullptr;
    QLabel *status_label_ = nullptr;
    QLabel *host_cpu_value_label_ = nullptr;
    QLabel *host_memory_value_label_ = nullptr;
    QLabel *process_cpu_value_label_ = nullptr;
    QLabel *process_memory_value_label_ = nullptr;
    QHash<MetricKind, QString> logged_errors_;
    quint64 session_id_ = 0;
    bool monitoring_ = false;
    bool shutdown_started_ = false;
    bool shutdown_complete_ = false;
};

} // namespace utms
