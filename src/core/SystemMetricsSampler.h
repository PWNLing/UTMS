#pragma once

#include <memory>

#include <QObject>
#include <QtTypes>

#include "core/SystemMetricsProvider.h"

class QTimer;

namespace utms {

class SystemMetricsSampler : public QObject {
    Q_OBJECT

public:
    explicit SystemMetricsSampler(std::unique_ptr<SystemMetricsProvider> provider, QObject *parent = nullptr);

public slots:
    void startSampling(quint64 session_id);
    void stopSampling();
    void sampleNow(quint64 session_id);

signals:
    void snapshotReady(quint64 session_id, const utms::SystemMetricsSnapshot &snapshot);

private:
    void collectSnapshot();

    std::unique_ptr<SystemMetricsProvider> provider_;
    QTimer *refresh_timer_ = nullptr;
    quint64 session_id_ = 0;
    bool sampling_ = false;
};

} // namespace utms
