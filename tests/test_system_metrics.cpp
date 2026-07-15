#include <cmath>

#include <QtTest>

#include "core/SystemMetricsProvider.h"

class SystemMetricsProviderTest : public QObject {
    Q_OBJECT

private slots:
    void nativeProviderReturnsHostAndProcessMetrics();
};

void SystemMetricsProviderTest::nativeProviderReturnsHostAndProcessMetrics()
{
    utms::NativeSystemMetricsProvider provider;
    provider.reset();
    QTest::qWait(50);

    const utms::SystemMetricsSnapshot snapshot = provider.sample();

    QVERIFY2(snapshot.host_cpu_percent.hasValue(), qPrintable(snapshot.host_cpu_percent.error));
    QVERIFY(std::isfinite(*snapshot.host_cpu_percent.value));
    QVERIFY(*snapshot.host_cpu_percent.value >= 0.0);
    QVERIFY(*snapshot.host_cpu_percent.value <= 100.0);

    QVERIFY2(snapshot.host_memory.hasValue(), qPrintable(snapshot.host_memory.error));
    QVERIFY(snapshot.host_memory.value->total_bytes > 0);
    QVERIFY(snapshot.host_memory.value->used_bytes <= snapshot.host_memory.value->total_bytes);
    QVERIFY(snapshot.host_memory.value->used_percent >= 0.0);
    QVERIFY(snapshot.host_memory.value->used_percent <= 100.0);

    QVERIFY2(snapshot.process_cpu_percent.hasValue(), qPrintable(snapshot.process_cpu_percent.error));
    QVERIFY(std::isfinite(*snapshot.process_cpu_percent.value));
    QVERIFY(*snapshot.process_cpu_percent.value >= 0.0);
    QVERIFY(*snapshot.process_cpu_percent.value <= 100.0);

    QVERIFY2(snapshot.process_memory_bytes.hasValue(), qPrintable(snapshot.process_memory_bytes.error));
    QVERIFY(*snapshot.process_memory_bytes.value > 0);
}

QTEST_APPLESS_MAIN(SystemMetricsProviderTest)

#include "test_system_metrics.moc"
