#include "media/VideoRecordingFileNamer.h"

#include <QDir>
#include <QFileInfo>

namespace utms {

QString nextVideoRecordingPath(const QString &directory_path, const QDateTime &timestamp)
{
    const QDir directory(directory_path);
    const QString base_name = QStringLiteral("UTMS_%1").arg(timestamp.toString(QStringLiteral("yyyyMMdd_HHmmss")));
    QString candidate_path = directory.filePath(base_name + QStringLiteral(".mp4"));
    for (quint64 suffix = 1; QFileInfo::exists(candidate_path); ++suffix) {
        candidate_path =
            directory.filePath(QStringLiteral("%1_%2.mp4").arg(base_name).arg(suffix, 2, 10, QLatin1Char('0')));
    }
    return candidate_path;
}

} // namespace utms
