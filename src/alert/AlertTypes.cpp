#include "alert/AlertTypes.h"

#include <algorithm>

#include <QCoreApplication>

namespace utms {

QString alertRuleTypeDisplayName(AlertRuleType type) {
    switch (type) {
    case AlertRuleType::kStableEntry:
        return QCoreApplication::translate("AlertTypes", "稳定进入");
    case AlertRuleType::kStableExit:
        return QCoreApplication::translate("AlertTypes", "稳定离开");
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
    if (rule.confirmation_ms < 0 || rule.confirmation_ms > 60'000) {
        return QCoreApplication::translate("AlertTypes", "确认时间必须为 0–60 秒");
    }
    return {};
}

} // namespace utms
