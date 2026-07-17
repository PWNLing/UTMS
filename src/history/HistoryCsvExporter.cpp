#include "history/HistoryCsvExporter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>

namespace utms {
namespace {

void setError(QString *error_message, const QString &message) {
    if (error_message != nullptr) {
        *error_message = message;
    }
}

QString exporterText(const char *source_text) { return QCoreApplication::translate("HistoryCsvExporter", source_text); }

QString csvField(const QString &value) {
    if (!value.contains(QLatin1Char(',')) && !value.contains(QLatin1Char('"')) && !value.contains(QLatin1Char('\n')) &&
        !value.contains(QLatin1Char('\r'))) {
        return value;
    }

    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

QString optionalDoubleCsv(const std::optional<double> &value) {
    return value.has_value() ? QString::number(value.value(), 'g', 15) : QString();
}

QString optionalIntegerCsv(const std::optional<qint64> &value) {
    return value.has_value() ? QString::number(value.value()) : QString();
}

} // namespace

std::optional<int> HistoryCsvExporter::exportToFile(const HistoryQueryResult &result, const QString &output_path,
                                                    QString *error_message) {
    if (output_path.isEmpty()) {
        setError(error_message, exporterText(QT_TRANSLATE_NOOP("HistoryCsvExporter", "CSV 导出路径为空")));
        return std::nullopt;
    }

    const QFileInfo output_info(output_path);
    QDir output_directory = output_info.dir();
    if (!output_directory.exists() && !output_directory.mkpath(QStringLiteral("."))) {
        setError(error_message, exporterText(QT_TRANSLATE_NOOP("HistoryCsvExporter", "无法创建 CSV 导出目录：%1"))
                                    .arg(output_directory.absolutePath()));
        return std::nullopt;
    }

    QSaveFile output_file(output_path);
    if (!output_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setError(error_message, exporterText(QT_TRANSLATE_NOOP("HistoryCsvExporter", "无法创建 CSV 文件 %1：%2"))
                                    .arg(output_path, output_file.errorString()));
        return std::nullopt;
    }

    const QByteArray header("session_id,frame_time,received_at,sequence,track_id,"
                            "type,latitude,longitude,velocity_mps,distance_m\n");
    if (output_file.write(header) != header.size()) {
        setError(error_message, exporterText(QT_TRANSLATE_NOOP("HistoryCsvExporter", "写入 CSV 文件 %1 失败：%2"))
                                    .arg(output_path, output_file.errorString()));
        output_file.cancelWriting();
        return std::nullopt;
    }

    int record_count = 0;
    for (const HistoryFrameRecord &frame : result.frames) {
        for (const TrackData &track : frame.tracks) {
            const QStringList fields = {
                QString::number(frame.session_id),
                frame.frame_time.toUTC().toString(Qt::ISODateWithMs),
                frame.received_at.toUTC().toString(Qt::ISODateWithMs),
                optionalIntegerCsv(frame.sequence),
                QString::number(track.track_id),
                targetTypeDisplayName(track.type),
                QString::number(track.position.latitude, 'f', 8),
                QString::number(track.position.longitude, 'f', 8),
                optionalDoubleCsv(track.velocity_mps),
                optionalDoubleCsv(track.distance_m),
            };
            QStringList escaped_fields;
            escaped_fields.reserve(fields.size());
            for (const QString &field : fields) {
                escaped_fields.append(csvField(field));
            }
            QByteArray row = escaped_fields.join(QLatin1Char(',')).toUtf8();
            row.append('\n');
            if (output_file.write(row) != row.size()) {
                setError(error_message,
                         exporterText(QT_TRANSLATE_NOOP("HistoryCsvExporter", "写入 CSV 文件 %1 失败：%2"))
                             .arg(output_path, output_file.errorString()));
                output_file.cancelWriting();
                return std::nullopt;
            }
            ++record_count;
        }
    }

    if (!output_file.commit()) {
        setError(error_message, exporterText(QT_TRANSLATE_NOOP("HistoryCsvExporter", "提交 CSV 文件 %1 失败：%2"))
                                    .arg(output_path, output_file.errorString()));
        return std::nullopt;
    }
    return record_count;
}

} // namespace utms
