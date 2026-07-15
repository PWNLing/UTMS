#pragma once

#include <QDateTime>
#include <QString>

namespace utms {

QString nextVideoRecordingPath(const QString &directory_path,
                               const QDateTime &timestamp = QDateTime::currentDateTime());

} // namespace utms
