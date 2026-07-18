#include "alert/AlertTypes.h"

#include <algorithm>
#include <cmath>

#include <QCoreApplication>

namespace utms {

QString alertRuleTypeDisplayName(AlertRuleType type) {
    switch (type) {
    case AlertRuleType::kStableEntry:
        return QCoreApplication::translate("AlertTypes", "稳定进入");
    case AlertRuleType::kStableExit:
        return QCoreApplication::translate("AlertTypes", "稳定离开");
    case AlertRuleType::kDwellTimeout:
        return QCoreApplication::translate("AlertTypes", "围栏内停留超时");
    case AlertRuleType::kGeofenceSpeeding:
        return QCoreApplication::translate("AlertTypes", "围栏内超速");
    }
    return QCoreApplication::translate("AlertTypes", "未知规则");
}

QString alertSeverityDisplayName(AlertSeverity severity) {
    switch (severity) {
    case AlertSeverity::kInfo:
        return QCoreApplication::translate("AlertTypes", "提示");
    case AlertSeverity::kWarning:
        return QCoreApplication::translate("AlertTypes", "警告");
    case AlertSeverity::kSevere:
        return QCoreApplication::translate("AlertTypes", "严重");
    }
    return QCoreApplication::translate("AlertTypes", "未知等级");
}

QString validateAlertRule(const AlertRule &rule) {
    if (rule.name.trimmed().isEmpty()) {
        return QCoreApplication::translate("AlertTypes", "规则名称不能为空");
    }
    if (rule.geofence_id <= 0) {
        return QCoreApplication::translate("AlertTypes", "规则必须关联电子围栏");
    }
    if (rule.target_types.isEmpty()) {
        return QCoreApplication::translate("AlertTypes", "规则至少需要选择一个目标类别");
    }
    for (TargetType type : rule.target_types) {
        if (std::find(kTargetTypes.cbegin(), kTargetTypes.cend(), type) == kTargetTypes.cend()) {
            return QCoreApplication::translate("AlertTypes", "规则包含无效目标类别");
        }
    }
    switch (rule.type) {
    case AlertRuleType::kStableEntry:
    case AlertRuleType::kStableExit:
        break;
    case AlertRuleType::kDwellTimeout:
        if (rule.dwell_threshold_ms < 5'000 || rule.dwell_threshold_ms > 86'400'000) {
            return QCoreApplication::translate("AlertTypes", "停留阈值必须为 5 秒–24 小时");
        }
        break;
    case AlertRuleType::kGeofenceSpeeding:
        if (!std::isfinite(rule.speed_threshold_mps) || rule.speed_threshold_mps <= 0.0) {
            return QCoreApplication::translate("AlertTypes", "超速阈值必须为大于 0 的有限数值（m/s）");
        }
        break;
    default:
        return QCoreApplication::translate("AlertTypes", "规则类型无效");
    }
    if (rule.confirmation_ms < 0 || rule.confirmation_ms > 60'000) {
        return QCoreApplication::translate("AlertTypes", "确认时间必须为 0–60 秒");
    }
    if (rule.cooldown_ms < 0 || rule.cooldown_ms > 86'400'000) {
        return QCoreApplication::translate("AlertTypes", "冷却时间必须为 0 秒–24 小时");
    }
    return {};
}

} // namespace utms
