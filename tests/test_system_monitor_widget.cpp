#include <atomic>
#include <memory>

#include <QApplication>
#include <QThread>
#include <QtTest>

#include "core/SystemMetricsProvider.h"
#include "ui/SystemMonitorWidget.h"

class FakeSystemMetricsProvider final : public utms::SystemMetricsProvider {
public:
    void reset() override
    {
        ++reset_count_;
    }

    utms::SystemMetricsSnapshot sample() override
    {
        ++sample_count_;
        if (sample_delay_ms_ > 0) {
            QThread::msleep(static_cast<unsigned long>(sample_delay_ms_));
        }
        return snapshot_;
    }

    utms::SystemMetricsSnapshot snapshot_;
    std::atomic<int> reset_count_ = 0;
    std::atomic<int> sample_count_ = 0;
    int sample_delay_ms_ = 0;
};

class SystemMonitorWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void defaultsToMonitoringDisabled();
    void samplesEverySecondOnlyWhileMonitoring();
    void stopClearsStaleMetricsImmediately();
    void metricFailureDoesNotHideSuccessfulMetrics();
    void lateSnapshotDoesNotRepopulateStoppedMetrics();
    void shutdownDoesNotBlockOnInFlightSample();
};

std::unique_ptr<FakeSystemMetricsProvider> successfulProvider()
{
    auto provider = std::make_unique<FakeSystemMetricsProvider>();
    provider->snapshot_.host_cpu_percent.value = 37.5;
    provider->snapshot_.host_memory.value =
        utms::HostMemoryUsage{8ULL * 1024 * 1024 * 1024, 16ULL * 1024 * 1024 * 1024, 50.0};
    provider->snapshot_.process_cpu_percent.value = 12.5;
    provider->snapshot_.process_memory_bytes.value = 256ULL * 1024 * 1024;
    return provider;
}

void SystemMonitorWidgetTest::defaultsToMonitoringDisabled()
{
    utms::SystemMonitorWidget widget(successfulProvider());

    QVERIFY(!widget.isMonitoring());
    QCOMPARE(widget.statusText(), QStringLiteral("监控未开启"));
    QCOMPARE(widget.toggleButtonText(), QStringLiteral("开启监控"));
    QCOMPARE(widget.hostCpuText(), QStringLiteral("--"));
    QCOMPARE(widget.hostMemoryText(), QStringLiteral("--"));
    QCOMPARE(widget.processCpuText(), QStringLiteral("--"));
    QCOMPARE(widget.processMemoryText(), QStringLiteral("--"));

    widget.shutdown();
    QTRY_VERIFY(widget.isShutdownComplete());
}

void SystemMonitorWidgetTest::samplesEverySecondOnlyWhileMonitoring()
{
    auto provider = successfulProvider();
    auto *provider_observer = provider.get();
    utms::SystemMonitorWidget widget(std::move(provider));

    widget.startMonitoring();
    QTRY_COMPARE(provider_observer->reset_count_.load(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(provider_observer->sample_count_.load() >= 1, 1'500);

    widget.stopMonitoring();
    const int stopped_sample_count = provider_observer->sample_count_.load();
    QTest::qWait(1'100);
    QCOMPARE(provider_observer->sample_count_.load(), stopped_sample_count);

    widget.shutdown();
    QTRY_VERIFY(widget.isShutdownComplete());
}

void SystemMonitorWidgetTest::stopClearsStaleMetricsImmediately()
{
    utms::SystemMonitorWidget widget(successfulProvider());
    widget.startMonitoring();
    widget.refreshNow();

    QTRY_COMPARE(widget.hostCpuText(), QStringLiteral("37.5%"));
    QCOMPARE(widget.hostMemoryText(), QStringLiteral("8.0 GB / 16.0 GB（50.0%）"));
    QCOMPARE(widget.processCpuText(), QStringLiteral("12.5%"));
    QCOMPARE(widget.processMemoryText(), QStringLiteral("256.0 MB"));

    widget.stopMonitoring();
    QVERIFY(!widget.isMonitoring());
    QCOMPARE(widget.statusText(), QStringLiteral("监控已停止"));
    QCOMPARE(widget.toggleButtonText(), QStringLiteral("开启监控"));
    QCOMPARE(widget.hostCpuText(), QStringLiteral("--"));
    QCOMPARE(widget.hostMemoryText(), QStringLiteral("--"));
    QCOMPARE(widget.processCpuText(), QStringLiteral("--"));
    QCOMPARE(widget.processMemoryText(), QStringLiteral("--"));

    widget.shutdown();
    QTRY_VERIFY(widget.isShutdownComplete());
}

void SystemMonitorWidgetTest::metricFailureDoesNotHideSuccessfulMetrics()
{
    auto provider = successfulProvider();
    provider->snapshot_.host_memory = {std::nullopt, QStringLiteral("物理内存不可用")};
    provider->snapshot_.process_memory_bytes = {std::nullopt, QStringLiteral("进程内存不可用")};
    utms::SystemMonitorWidget widget(std::move(provider));

    widget.startMonitoring();
    widget.refreshNow();

    QTRY_COMPARE(widget.hostCpuText(), QStringLiteral("37.5%"));
    QCOMPARE(widget.hostMemoryText(), QStringLiteral("读取失败：物理内存不可用"));
    QCOMPARE(widget.processCpuText(), QStringLiteral("12.5%"));
    QCOMPARE(widget.processMemoryText(), QStringLiteral("读取失败：进程内存不可用"));

    widget.shutdown();
    QTRY_VERIFY(widget.isShutdownComplete());
}

void SystemMonitorWidgetTest::lateSnapshotDoesNotRepopulateStoppedMetrics()
{
    auto provider = successfulProvider();
    auto *provider_observer = provider.get();
    provider->sample_delay_ms_ = 150;
    utms::SystemMonitorWidget widget(std::move(provider));

    widget.startMonitoring();
    widget.refreshNow();
    QTRY_VERIFY(provider_observer->sample_count_.load() >= 1);
    widget.stopMonitoring();

    QTest::qWait(250);
    QCOMPARE(widget.statusText(), QStringLiteral("监控已停止"));
    QCOMPARE(widget.hostCpuText(), QStringLiteral("--"));
    QCOMPARE(widget.hostMemoryText(), QStringLiteral("--"));
    QCOMPARE(widget.processCpuText(), QStringLiteral("--"));
    QCOMPARE(widget.processMemoryText(), QStringLiteral("--"));

    widget.shutdown();
    QTRY_VERIFY(widget.isShutdownComplete());
}

void SystemMonitorWidgetTest::shutdownDoesNotBlockOnInFlightSample()
{
    auto provider = successfulProvider();
    auto *provider_observer = provider.get();
    provider->sample_delay_ms_ = 300;
    utms::SystemMonitorWidget widget(std::move(provider));

    widget.startMonitoring();
    QTRY_VERIFY(provider_observer->sample_count_.load() >= 1);

    widget.shutdown();
    QVERIFY(!widget.isShutdownComplete());
    QTRY_VERIFY(widget.isShutdownComplete());
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("minimal"));
    QApplication application(argc, argv);
    SystemMonitorWidgetTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_system_monitor_widget.moc"
