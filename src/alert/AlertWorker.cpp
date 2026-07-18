#include "alert/AlertWorker.h"

#include <exception>

#include <QDebug>
#include <QThread>

namespace utms {

AlertWorker::AlertWorker(QObject *parent) : QObject(parent) {
    qRegisterMetaType<AlertRule>();
    qRegisterMetaType<TargetAlert>();
    qRegisterMetaType<QVector<AlertRule>>();
    qInfo() << "AlertWorker: alert engine started";
}

void AlertWorker::setGeofences(const QVector<Geofence> &geofences) {
    if (!shutting_down_) {
        engine_.setGeofences(geofences);
    }
}

void AlertWorker::setRules(const QVector<AlertRule> &rules) {
    if (shutting_down_) {
        return;
    }
    QVector<AlertRule> valid_rules;
    valid_rules.reserve(rules.size());
    for (const AlertRule &rule : rules) {
        const QString validation_error = validateAlertRule(rule);
        if (!validation_error.isEmpty()) {
            emit errorOccurred(tr("告警规则 %1 无效：%2").arg(rule.id).arg(validation_error));
            continue;
        }
        valid_rules.append(rule);
    }
    engine_.setRules(valid_rules);
}

void AlertWorker::evaluateAcceptedFrame(const RadarFrame &frame) {
    if (shutting_down_) {
        return;
    }
    try {
        const QVector<TargetAlert> alerts = engine_.evaluateFrame(frame);
        for (const TargetAlert &alert : alerts) {
            qInfo() << "AlertWorker: created target alert" << alert.rule_id << alert.track_id
                    << static_cast<int>(alert.severity);
            emit alertTriggered(alert);
        }
    } catch (const std::exception &error) {
        const QString detail = tr("告警计算失败：%1").arg(QString::fromLocal8Bit(error.what()));
        qWarning() << "AlertWorker:" << detail;
        emit errorOccurred(detail);
    } catch (...) {
        const QString detail = tr("告警计算失败：未知错误");
        qWarning() << "AlertWorker:" << detail;
        emit errorOccurred(detail);
    }
}

void AlertWorker::clearState() { engine_.clearEvaluationState(); }

void AlertWorker::shutdown() {
    if (shutting_down_) {
        return;
    }
    shutting_down_ = true;
    engine_.clearEvaluationState();
    qInfo() << "AlertWorker: alert engine stopped";
    emit stopped();
    QThread::currentThread()->quit();
}

} // namespace utms
