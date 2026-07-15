#include "core/SystemMetricsProvider.h"

#include <algorithm>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Psapi.h>
#endif

namespace utms {
namespace {

template <typename T> MetricResult<T> successfulMetric(T value)
{
    return {std::move(value), QString()};
}

template <typename T> MetricResult<T> failedMetric(const QString &error)
{
    return {std::nullopt, error};
}

#ifdef Q_OS_WIN
quint64 fileTimeTicks100ns(const FILETIME &time)
{
    ULARGE_INTEGER value{};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
}

QString windowsError(const QString &operation)
{
    return QStringLiteral("%1（Windows 错误 %2）").arg(operation).arg(GetLastError());
}
#endif

} // namespace

NativeSystemMetricsProvider::NativeSystemMetricsProvider()
{
#ifdef Q_OS_WIN
    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);
    logical_processor_count_ = std::max(1, static_cast<int>(system_info.dwNumberOfProcessors));
#endif
}

void NativeSystemMetricsProvider::reset()
{
    host_cpu_baseline_valid_ = false;
    process_cpu_baseline_valid_ = false;
    process_cpu_timer_.invalidate();

#ifdef Q_OS_WIN
    FILETIME idle_time{};
    FILETIME kernel_time{};
    FILETIME user_time{};
    if (GetSystemTimes(&idle_time, &kernel_time, &user_time) != 0) {
        previous_host_cpu_times_100ns_ = {fileTimeTicks100ns(idle_time), fileTimeTicks100ns(kernel_time),
                                          fileTimeTicks100ns(user_time)};
        host_cpu_baseline_valid_ = true;
    }

    FILETIME creation_time{};
    FILETIME exit_time{};
    FILETIME process_kernel_time{};
    FILETIME process_user_time{};
    if (GetProcessTimes(GetCurrentProcess(), &creation_time, &exit_time, &process_kernel_time, &process_user_time) !=
        0) {
        previous_process_cpu_ticks_100ns_ =
            fileTimeTicks100ns(process_kernel_time) + fileTimeTicks100ns(process_user_time);
        process_cpu_baseline_valid_ = true;
        process_cpu_timer_.start();
    }
#endif
}

SystemMetricsSnapshot NativeSystemMetricsProvider::sample()
{
    SystemMetricsSnapshot snapshot;
    snapshot.host_cpu_percent = sampleHostCpu();
    snapshot.host_memory = sampleHostMemory();
    snapshot.process_cpu_percent = sampleProcessCpu();
    snapshot.process_memory_bytes = sampleProcessMemory();
    return snapshot;
}

MetricResult<double> NativeSystemMetricsProvider::sampleHostCpu()
{
#ifdef Q_OS_WIN
    FILETIME idle_time{};
    FILETIME kernel_time{};
    FILETIME user_time{};
    if (GetSystemTimes(&idle_time, &kernel_time, &user_time) == 0) {
        return failedMetric<double>(windowsError(QStringLiteral("读取本机 CPU 使用率失败")));
    }

    const HostCpuTimes100ns current_times_100ns = {fileTimeTicks100ns(idle_time), fileTimeTicks100ns(kernel_time),
                                                   fileTimeTicks100ns(user_time)};
    if (!host_cpu_baseline_valid_) {
        previous_host_cpu_times_100ns_ = current_times_100ns;
        host_cpu_baseline_valid_ = true;
        return failedMetric<double>(QStringLiteral("本机 CPU 正在建立采样基准"));
    }

    const quint64 idle_delta_100ns =
        current_times_100ns.idle_ticks_100ns - previous_host_cpu_times_100ns_.idle_ticks_100ns;
    const quint64 kernel_delta_100ns =
        current_times_100ns.kernel_ticks_100ns - previous_host_cpu_times_100ns_.kernel_ticks_100ns;
    const quint64 user_delta_100ns =
        current_times_100ns.user_ticks_100ns - previous_host_cpu_times_100ns_.user_ticks_100ns;
    previous_host_cpu_times_100ns_ = current_times_100ns;

    // Windows 的 kernel 时间包含 idle 时间，因此总忙碌时间需从 kernel + user 中扣除 idle。
    const quint64 total_delta_100ns = kernel_delta_100ns + user_delta_100ns;
    if (total_delta_100ns == 0 || idle_delta_100ns > total_delta_100ns) {
        return failedMetric<double>(QStringLiteral("本机 CPU 采样间隔无效"));
    }
    const double used_percent =
        100.0 * static_cast<double>(total_delta_100ns - idle_delta_100ns) / static_cast<double>(total_delta_100ns);
    return successfulMetric(std::clamp(used_percent, 0.0, 100.0));
#else
    return failedMetric<double>(QStringLiteral("当前平台不支持本机 CPU 采样"));
#endif
}

MetricResult<HostMemoryUsage> NativeSystemMetricsProvider::sampleHostMemory() const
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memory_status{};
    memory_status.dwLength = sizeof(memory_status);
    if (GlobalMemoryStatusEx(&memory_status) == 0) {
        return failedMetric<HostMemoryUsage>(windowsError(QStringLiteral("读取物理内存失败")));
    }

    HostMemoryUsage usage;
    usage.total_bytes = memory_status.ullTotalPhys;
    usage.used_bytes = memory_status.ullTotalPhys - memory_status.ullAvailPhys;
    usage.used_percent = memory_status.dwMemoryLoad;
    return successfulMetric(usage);
#else
    return failedMetric<HostMemoryUsage>(QStringLiteral("当前平台不支持物理内存采样"));
#endif
}

MetricResult<double> NativeSystemMetricsProvider::sampleProcessCpu()
{
#ifdef Q_OS_WIN
    FILETIME creation_time{};
    FILETIME exit_time{};
    FILETIME kernel_time{};
    FILETIME user_time{};
    if (GetProcessTimes(GetCurrentProcess(), &creation_time, &exit_time, &kernel_time, &user_time) == 0) {
        return failedMetric<double>(windowsError(QStringLiteral("读取 UTMS 进程 CPU 使用率失败")));
    }

    const quint64 current_process_cpu_ticks_100ns = fileTimeTicks100ns(kernel_time) + fileTimeTicks100ns(user_time);
    if (!process_cpu_baseline_valid_ || !process_cpu_timer_.isValid()) {
        previous_process_cpu_ticks_100ns_ = current_process_cpu_ticks_100ns;
        process_cpu_baseline_valid_ = true;
        process_cpu_timer_.start();
        return failedMetric<double>(QStringLiteral("UTMS 进程 CPU 正在建立采样基准"));
    }

    const qint64 elapsed_nanoseconds = process_cpu_timer_.nsecsElapsed();
    const quint64 process_delta_100ns = current_process_cpu_ticks_100ns - previous_process_cpu_ticks_100ns_;
    previous_process_cpu_ticks_100ns_ = current_process_cpu_ticks_100ns;
    process_cpu_timer_.restart();
    if (elapsed_nanoseconds <= 0) {
        return failedMetric<double>(QStringLiteral("UTMS 进程 CPU 采样间隔无效"));
    }

    constexpr double kHundredNanosecondsPerSecond = 10'000'000.0;
    const double elapsed_seconds = static_cast<double>(elapsed_nanoseconds) / 1'000'000'000.0;
    const double cpu_capacity_100ns = elapsed_seconds * kHundredNanosecondsPerSecond * logical_processor_count_;
    // 进程 CPU 时间按逻辑处理器总容量归一化，使结果与整机百分比同为 0～100%。
    const double used_percent = 100.0 * static_cast<double>(process_delta_100ns) / cpu_capacity_100ns;
    return successfulMetric(std::clamp(used_percent, 0.0, 100.0));
#else
    return failedMetric<double>(QStringLiteral("当前平台不支持 UTMS 进程 CPU 采样"));
#endif
}

MetricResult<quint64> NativeSystemMetricsProvider::sampleProcessMemory() const
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                             sizeof(counters)) == 0) {
        return failedMetric<quint64>(windowsError(QStringLiteral("读取 UTMS 进程内存失败")));
    }
    return successfulMetric(static_cast<quint64>(counters.WorkingSetSize));
#else
    return failedMetric<quint64>(QStringLiteral("当前平台不支持 UTMS 进程内存采样"));
#endif
}

} // namespace utms
