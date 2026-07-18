#pragma once

#include <QObject>

#include "alert/AlertEngine.h"

namespace utms {

class AlertWorker : public QObject {
    Q_OBJECT

  public:
    explicit AlertWorker(QObject *parent = nullptr);

  public slots:
    void setGeofences(const QVector<utms::Geofence> &geofences);
    void setRules(const QVector<utms::AlertRule> &rules);
    void evaluateAcceptedFrame(const utms::RadarFrame &frame);
    void clearState();
    void shutdown();

  signals:
    void alertTriggered(const utms::TargetAlert &alert);
    void errorOccurred(const QString &message);
    void stopped();

  private:
    AlertEngine engine_;
    bool shutting_down_ = false;
};

} // namespace utms
