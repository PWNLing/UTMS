#include "core/SystemMetricsSampler.h"

#include <QTimer>

namespace utms {
namespace {

constexpr int kRefreshIntervalMs = 1'000;

} // namespace

SystemMetricsSampler::SystemMetricsSampler(std::unique_ptr<SystemMetricsProvider> provider, QObject *parent)
    : QObject(parent), provider_(std::move(provider)), refresh_timer_(new QTimer(this))
{
    Q_ASSERT(provider_ != nullptr);
    refresh_timer_->setInterval(kRefreshIntervalMs);
    connect(refresh_timer_, &QTimer::timeout, this, &SystemMetricsSampler::collectSnapshot);
}

void SystemMetricsSampler::startSampling(quint64 session_id)
{
    session_id_ = session_id;
    provider_->reset();
    sampling_ = true;
    refresh_timer_->start();
}

void SystemMetricsSampler::stopSampling()
{
    sampling_ = false;
    refresh_timer_->stop();
}

void SystemMetricsSampler::sampleNow(quint64 session_id)
{
    if (!sampling_ || session_id != session_id_) {
        return;
    }
    collectSnapshot();
}

void SystemMetricsSampler::collectSnapshot()
{
    if (!sampling_) {
        return;
    }
    emit snapshotReady(session_id_, provider_->sample());
}

} // namespace utms
