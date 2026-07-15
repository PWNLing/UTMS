#pragma once

#include <optional>

#include <QElapsedTimer>
#include <QMetaType>
#include <QString>
#include <QtTypes>

namespace utms {

template <typename T> struct MetricResult {
    std::optional<T> value;
    QString error;

    bool hasValue() const
    {
        return value.has_value();
    }
};

struct HostMemoryUsage {
    quint64 used_bytes = 0;
    quint64 total_bytes = 0;
    double used_percent = 0.0;
};

struct SystemMetricsSnapshot {
    MetricResult<double> host_cpu_percent;
    MetricResult<HostMemoryUsage> host_memory;
    MetricResult<double> process_cpu_percent;
    MetricResult<quint64> process_memory_bytes;
};

class SystemMetricsProvider {
public:
    virtual ~SystemMetricsProvider() = default;

    virtual void reset() = 0;
    virtual SystemMetricsSnapshot sample() = 0;
};

class NativeSystemMetricsProvider final : public SystemMetricsProvider {
public:
    NativeSystemMetricsProvider();

    void reset() override;
    SystemMetricsSnapshot sample() override;

private:
    struct HostCpuTimes100ns {
        quint64 idle_ticks_100ns = 0;
        quint64 kernel_ticks_100ns = 0;
        quint64 user_ticks_100ns = 0;
    };

    MetricResult<double> sampleHostCpu();
    MetricResult<HostMemoryUsage> sampleHostMemory() const;
    MetricResult<double> sampleProcessCpu();
    MetricResult<quint64> sampleProcessMemory() const;

    HostCpuTimes100ns previous_host_cpu_times_100ns_;
    quint64 previous_process_cpu_ticks_100ns_ = 0;
    int logical_processor_count_ = 1;
    bool host_cpu_baseline_valid_ = false;
    bool process_cpu_baseline_valid_ = false;
    QElapsedTimer process_cpu_timer_;
};

} // namespace utms

Q_DECLARE_METATYPE(utms::SystemMetricsSnapshot)
