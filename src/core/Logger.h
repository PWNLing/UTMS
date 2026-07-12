#pragma once

#include <QtGlobal>

#include <QString>

namespace utms {

class Logger {
public:
    static constexpr qint64 kDefaultMaxFileSizeBytes = 10 * 1024 * 1024;
    static constexpr int kDefaultMaxFileCount = 5;

    static bool install(const QString &log_directory, qint64 max_file_size_bytes = kDefaultMaxFileSizeBytes,
                        int max_file_count = kDefaultMaxFileCount);
    static void shutdown();
    static QString logFilePath();
};

} // namespace utms
