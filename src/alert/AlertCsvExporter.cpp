#include "alert/AlertCsvExporter.h"

#include <QCoreApplication>
#include <QSaveFile>
#include <QStringConverter>
#include <QTextStream>

namespace utms {
namespace {

void setError(QString *error_message, const QString &message) {
    if (error_message != nullptr) {
        *error_message = message;
    }
}

QString csvField(const QString &value) {
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

QString optionalMeasurement(const std::optional<double> &value) {
    return value.has_value() ? QString::number(value.value(), 'f', 3) : QString();
}

} // namespace

std::optional<int> AlertCsvExporter::exportToFile(const AlertQueryResult &result, const QString &output_path,
                                                  QString *error_message) {
    if (output_path.trimmed().isEmpty()) {
        setError(error_message, QCoreApplication::translate("AlertCsvExporter", "告警 CSV 输出路径不能为空"));
        return std::nullopt;
    }

    QSaveFile output_file(output_path);
    if (!output_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setError(error_message, QCoreApplication::translate("AlertCsvExporter", "无法创建告警 CSV %1：%2")
                                    .arg(output_path, output_file.errorString()));
        return std::nullopt;
    }

    QTextStream stream(&output_file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << QChar::ByteOrderMark;
    stream << QStringLiteral(
        "发生时间,等级,规则,围栏,航迹 ID,类别,纬度,经度,速度 m/s,距离 m,描述,"
        "确认状态,确认时间,确认人,处理备注\n");
    for (const TargetAlert &alert : result.alerts) {
        stream << csvField(alert.occurred_at.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")))
               << QLatin1Char(',') << csvField(alertSeverityDisplayName(alert.severity)) << QLatin1Char(',')
               << csvField(alert.rule_name) << QLatin1Char(',') << csvField(alert.geofence_name) << QLatin1Char(',')
               << alert.track_id << QLatin1Char(',') << csvField(targetTypeDisplayName(alert.target_type))
               << QLatin1Char(',') << QString::number(alert.position.latitude, 'f', 7) << QLatin1Char(',')
               << QString::number(alert.position.longitude, 'f', 7) << QLatin1Char(',')
               << optionalMeasurement(alert.velocity_mps) << QLatin1Char(',') << optionalMeasurement(alert.distance_m)
               << QLatin1Char(',') << csvField(alert.description) << QLatin1Char(',')
               << csvField(alert.acknowledged ? QCoreApplication::translate("AlertCsvExporter", "已确认")
                                              : QCoreApplication::translate("AlertCsvExporter", "未确认"))
               << QLatin1Char(',')
               << csvField(alert.acknowledged_at.has_value()
                               ? alert.acknowledged_at->toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                               : QString())
               << QLatin1Char(',') << csvField(alert.acknowledged_by) << QLatin1Char(',')
               << csvField(alert.handling_note) << QLatin1Char('\n');
    }

    if (stream.status() != QTextStream::Ok || !output_file.commit()) {
        setError(error_message, QCoreApplication::translate("AlertCsvExporter", "写入告警 CSV %1 失败：%2")
                                    .arg(output_path, output_file.errorString()));
        return std::nullopt;
    }
    return result.alerts.size();
}

} // namespace utms
